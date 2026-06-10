import struct
import socket
import time
import math
import random
import argparse
import threading
import json
import sys
from dataclasses import dataclass, field
from typing import List, Optional, Dict

PXI_HEADER_FMT = '<HBBIQBBH'
PXI_HEADER_SIZE = struct.calcsize(PXI_HEADER_FMT)
MAX_PAYLOAD_SAMPLES = 256
PXI_PACKET_SIZE = PXI_HEADER_SIZE + MAX_PAYLOAD_SAMPLES * 4

MAGIC = 0xCA5C
VERSION = 0x01

HYDROPHONE = 1
ACCELEROMETER = 2

LOC_SPIRAL_CASE_INLET = 1
LOC_AFTER_GUIDE_VANE = 2
LOC_DRAFT_TUBE = 3
LOC_RUNNER_INLET = 4
LOC_RUNNER_OUTLET = 5
LOC_BLADE_CHANNEL = 6

CAV_NONE = 0
CAV_INCIPIENT = 1
CAV_CRITICAL = 2
CAV_DEVELOPED = 3

CAV_STAGE_NAMES = {0: 'none', 1: 'incipient', 2: 'critical', 3: 'developed'}

SENSOR_LAYOUT = [
    (1,  HYDROPHONE,    LOC_SPIRAL_CASE_INLET, 0, 0),
    (2,  HYDROPHONE,    LOC_AFTER_GUIDE_VANE,  0, 0),
    (3,  HYDROPHONE,    LOC_DRAFT_TUBE,        0, 0),
    (4,  HYDROPHONE,    LOC_RUNNER_INLET,      1, 1),
    (5,  HYDROPHONE,    LOC_RUNNER_INLET,      1, 2),
    (6,  HYDROPHONE,    LOC_RUNNER_INLET,      1, 3),
    (7,  HYDROPHONE,    LOC_BLADE_CHANNEL,     2, 1),
    (8,  HYDROPHONE,    LOC_BLADE_CHANNEL,     2, 2),
    (9,  HYDROPHONE,    LOC_BLADE_CHANNEL,     2, 3),
    (10, HYDROPHONE,    LOC_RUNNER_OUTLET,     3, 1),
    (11, HYDROPHONE,    LOC_RUNNER_OUTLET,     3, 2),
    (12, HYDROPHONE,    LOC_RUNNER_OUTLET,     3, 3),
    (13, ACCELEROMETER, LOC_SPIRAL_CASE_INLET, 0, 0),
    (14, ACCELEROMETER, LOC_AFTER_GUIDE_VANE,  0, 0),
    (15, ACCELEROMETER, LOC_DRAFT_TUBE,        0, 0),
    (16, ACCELEROMETER, LOC_RUNNER_INLET,      1, 1),
    (17, ACCELEROMETER, LOC_RUNNER_INLET,      1, 2),
    (18, ACCELEROMETER, LOC_BLADE_CHANNEL,     2, 1),
    (19, ACCELEROMETER, LOC_BLADE_CHANNEL,     2, 2),
    (20, ACCELEROMETER, LOC_DRAFT_TUBE,        4, 1),
]


@dataclass
class SensorDesc:
    sensor_id: int
    sensor_type: int
    location: int
    blade_id: int
    zone_id: int


SENSORS_PER_TURBINE = [
    SensorDesc(sid, stype, loc, bid, zid)
    for sid, stype, loc, bid, zid in SENSOR_LAYOUT
]


@dataclass
class BladeState:
    stage: int = CAV_NONE
    intensity: float = 0.0
    target_stage: int = CAV_NONE
    transition_speed: float = 0.005
    forced: bool = False
    forced_stage: int = CAV_NONE
    forced_intensity: float = 0.0


@dataclass
class TurbineState:
    turbine_id: int = 0
    blades: Dict[int, BladeState] = field(default_factory=dict)

    def __post_init__(self):
        for b in range(1, 14):
            self.blades[b] = BladeState(transition_speed=random.uniform(0.001, 0.005))


class CavitationInjectionServer:
    def __init__(self, turbines: Dict[int, TurbineState], http_port=8091):
        self.turbines = turbines
        self.http_port = http_port
        self.running = False
        self.server_socket = None

    def _send_json(self, conn, status, body):
        status_text = {200: 'OK', 400: 'Bad Request', 404: 'Not Found'}
        resp = (
            f"HTTP/1.1 {status} {status_text.get(status, 'OK')}\r\n"
            f"Content-Type: application/json\r\n"
            f"Access-Control-Allow-Origin: *\r\n"
            f"Content-Length: {len(body.encode())}\r\n"
            f"\r\n"
            f"{body}"
        )
        conn.sendall(resp.encode())

    def _handle_client(self, conn):
        try:
            request = conn.recv(8192).decode('utf-8', errors='replace')
            if not request:
                conn.close()
                return

            lines = request.split('\r\n')
            first_line = lines[0]
            method_path = first_line.split(' ')

            if len(method_path) < 2:
                conn.close()
                return

            method = method_path[0]
            path = method_path[1]

            if method == 'OPTIONS':
                self._send_json(conn, 200, '')
                return

            body_start = request.find('\r\n\r\n')
            req_body = request[body_start + 4:] if body_start >= 0 else ''

            if method == 'POST' and '/inject' in path:
                self._handle_inject(conn, req_body)
            elif method == 'DELETE' and '/inject' in path:
                self._handle_clear_inject(conn, path)
            elif method == 'GET' and '/status' in path:
                self._handle_status(conn)
            elif method == 'GET' and '/health' in path:
                self._send_json(conn, 200, '{"status":"ok"}')
            else:
                self._send_json(conn, 404, '{"error":"not found"}')

        except Exception as e:
            try:
                self._send_json(conn, 500, f'{{"error":"{str(e)}"}}')
            except:
                pass
        finally:
            conn.close()

    def _handle_inject(self, conn, body):
        try:
            cmd = json.loads(body)
        except json.JSONDecodeError:
            self._send_json(conn, 400, '{"error":"invalid JSON"}')
            return

        turbine_id = cmd.get('turbine_id')
        blade_id = cmd.get('blade_id')
        stage = cmd.get('stage')
        intensity = cmd.get('intensity', 0.8)

        if stage is not None:
            stage_map = {'none': CAV_NONE, 'incipient': CAV_INCIPIENT,
                         'critical': CAV_CRITICAL, 'developed': CAV_DEVELOPED}
            if isinstance(stage, str):
                stage = stage_map.get(stage.lower(), CAV_NONE)

        affected = []

        if turbine_id is not None and turbine_id in self.turbines:
            t = self.turbines[turbine_id]
            if blade_id is not None and blade_id in t.blades:
                b = t.blades[blade_id]
                b.forced = True
                b.forced_stage = stage if stage is not None else CAV_DEVELOPED
                b.forced_intensity = intensity
                affected.append({'turbine': turbine_id, 'blade': blade_id,
                                 'stage': CAV_STAGE_NAMES.get(b.forced_stage, 'unknown'),
                                 'intensity': b.forced_intensity})
            elif blade_id is None or blade_id == 0:
                for bid, b in t.blades.items():
                    b.forced = True
                    b.forced_stage = stage if stage is not None else CAV_DEVELOPED
                    b.forced_intensity = intensity
                    affected.append({'turbine': turbine_id, 'blade': bid,
                                     'stage': CAV_STAGE_NAMES.get(b.forced_stage, 'unknown'),
                                     'intensity': b.forced_intensity})
        elif turbine_id is None or turbine_id == 0:
            for tid, t in self.turbines.items():
                for bid, b in t.blades.items():
                    b.forced = True
                    b.forced_stage = stage if stage is not None else CAV_DEVELOPED
                    b.forced_intensity = intensity
                    affected.append({'turbine': tid, 'blade': bid,
                                     'stage': CAV_STAGE_NAMES.get(b.forced_stage, 'unknown'),
                                     'intensity': b.forced_intensity})

        self._send_json(conn, 200, json.dumps({
            'injected': True,
            'affected_count': len(affected),
            'affected': affected[:20]
        }, ensure_ascii=False))

    def _handle_clear_inject(self, conn, path):
        for t in self.turbines.values():
            for b in t.blades.values():
                b.forced = False
                b.forced_stage = CAV_NONE
                b.forced_intensity = 0.0
        self._send_json(conn, 200, '{"cleared":true}')

    def _handle_status(self, conn):
        result = {}
        for tid, t in self.turbines.items():
            blades = {}
            for bid, b in t.blades.items():
                blades[str(bid)] = {
                    'stage': CAV_STAGE_NAMES.get(b.stage, 'unknown'),
                    'intensity': round(b.intensity, 3),
                    'forced': b.forced,
                    'forced_stage': CAV_STAGE_NAMES.get(b.forced_stage, 'unknown') if b.forced else None
                }
            result[str(tid)] = {'blades': blades}
        self._send_json(conn, 200, json.dumps(result, ensure_ascii=False))

    def start(self):
        self.running = True
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind(('0.0.0.0', self.http_port))
        self.server_socket.listen(10)
        self.server_socket.settimeout(1.0)

        print(f"[PXI Inject] HTTP control on port {self.http_port}")

        while self.running:
            try:
                c, _ = self.server_socket.accept()
                threading.Thread(target=self._handle_client, args=(c,), daemon=True).start()
            except socket.timeout:
                continue
            except OSError:
                break

    def stop(self):
        self.running = False
        if self.server_socket:
            self.server_socket.close()


def generate_cavitation_signal(t: float, sensor: SensorDesc,
                                cav_stage: int, cav_intensity: float) -> float:
    base_freq = 25.0
    if sensor.sensor_type == HYDROPHONE:
        amplitude = 0.1
        signal = amplitude * math.sin(2 * math.pi * base_freq * t)
        signal += 0.02 * math.sin(2 * math.pi * 50 * t)
        signal += 0.01 * math.sin(2 * math.pi * 100 * t)
        noise = random.gauss(0, 0.005)

        if cav_stage >= CAV_INCIPIENT and sensor.blade_id > 0:
            cav_freq = 20e3 + 10e3 * cav_intensity
            cav_amp = 0.05 * cav_intensity
            signal += cav_amp * math.sin(2 * math.pi * cav_freq * t)
            signal += cav_amp * 0.3 * random.gauss(0, 1)

        if cav_stage >= CAV_CRITICAL and sensor.blade_id > 0:
            burst_prob = 0.1 * cav_intensity
            if random.random() < burst_prob:
                signal += 0.5 * cav_intensity * random.gauss(0, 1)

            mid_freq = 8e3 + 5e3 * cav_intensity
            signal += 0.15 * cav_intensity * math.sin(2 * math.pi * mid_freq * t)

        if cav_stage >= CAV_DEVELOPED and sensor.blade_id > 0:
            signal += 0.3 * cav_intensity * random.gauss(0, 1)
            sub_freq = 2.0 + 3.0 * cav_intensity
            signal += 0.2 * cav_intensity * math.sin(2 * math.pi * sub_freq * t)
            high_freq = 40e3 + 20e3 * cav_intensity
            signal += 0.08 * cav_intensity * math.sin(2 * math.pi * high_freq * t)

        return signal + noise

    else:
        amplitude = 0.5
        signal = amplitude * math.sin(2 * math.pi * base_freq * t)
        signal += 0.1 * math.sin(2 * math.pi * 75 * t)
        signal += 0.05 * math.sin(2 * math.pi * 150 * t)
        noise = random.gauss(0, 0.05)

        if cav_stage >= CAV_INCIPIENT and sensor.blade_id > 0:
            vib_freq = 200 + 300 * cav_intensity
            signal += 0.8 * cav_intensity * math.sin(2 * math.pi * vib_freq * t)

        if cav_stage >= CAV_CRITICAL and sensor.blade_id > 0:
            vib_freq = 500 + 500 * cav_intensity
            signal += 2.0 * cav_intensity * math.sin(2 * math.pi * vib_freq * t)
            if random.random() < 0.05 * cav_intensity:
                signal += 3.0 * cav_intensity * random.gauss(0, 1)

        if cav_stage >= CAV_DEVELOPED and sensor.blade_id > 0:
            signal += 5.0 * cav_intensity * random.gauss(0, 1)
            low_freq = 5.0 + 10.0 * cav_intensity
            signal += 1.0 * cav_intensity * math.sin(2 * math.pi * low_freq * t)

        return signal + noise


def build_packet(turbine_id: int, sensor: SensorDesc,
                  samples: List[float], seq: int) -> bytes:
    header = struct.pack(
        PXI_HEADER_FMT,
        MAGIC,
        VERSION,
        turbine_id,
        seq,
        int(time.time() * 1000),
        sensor.sensor_type,
        sensor.sensor_id,
        len(samples)
    )
    payload = struct.pack(f'<{len(samples)}f', *samples)
    return header + payload


def update_blade_state(blade: BladeState):
    if blade.forced:
        blade.target_stage = blade.forced_stage
        if blade.stage != blade.forced_stage:
            blade.intensity += blade.transition_speed * 3
            if blade.intensity >= 1.0:
                blade.stage = blade.forced_stage
                blade.intensity = blade.forced_intensity
        else:
            blade.intensity = blade.forced_intensity
        return

    r = random.random()
    if blade.stage == CAV_NONE and r < 0.0005:
        blade.target_stage = CAV_INCIPIENT
    elif blade.stage == CAV_INCIPIENT and r < 0.001:
        blade.target_stage = CAV_CRITICAL
    elif blade.stage == CAV_INCIPIENT and r < 0.005:
        blade.target_stage = CAV_NONE
    elif blade.stage == CAV_CRITICAL and r < 0.0005:
        blade.target_stage = CAV_DEVELOPED
    elif blade.stage == CAV_CRITICAL and r < 0.003:
        blade.target_stage = CAV_INCIPIENT
    elif blade.stage == CAV_DEVELOPED and r < 0.002:
        blade.target_stage = CAV_CRITICAL

    if blade.stage != blade.target_stage:
        blade.intensity += blade.transition_speed
        if blade.intensity >= 1.0:
            blade.stage = blade.target_stage
            blade.intensity = 0.0
    else:
        if blade.stage == CAV_NONE:
            blade.intensity = max(0, blade.intensity - 0.01)


def simulate_turbine(sock: socket.socket, target_ip: str, target_port: int,
                      turbine_state: TurbineState, sample_rate: int = 1000,
                      interval_ms: int = 1):
    turbine_id = turbine_state.turbine_id
    seq = 0
    t_offset = turbine_id * 100.0
    dt = 1.0 / sample_rate

    samples_per_packet = min(MAX_PAYLOAD_SAMPLES, sample_rate // 10)
    sleep_interval = interval_ms / 1000.0

    print(f"[Turbine {turbine_id}] Starting -> {target_ip}:{target_port} "
          f"({len(SENSORS_PER_TURBINE)} sensors, {interval_ms}ms interval)")

    while True:
        for blade in turbine_state.blades.values():
            update_blade_state(blade)

        for sensor in SENSORS_PER_TURBINE:
            cav_stage = CAV_NONE
            cav_intensity = 0.0

            if sensor.blade_id > 0:
                blade = turbine_state.blades.get(sensor.blade_id)
                if blade:
                    cav_stage = blade.stage
                    cav_intensity = blade.intensity

            samples = []
            for i in range(samples_per_packet):
                t = t_offset + seq * dt + i * dt
                sample = generate_cavitation_signal(t, sensor, cav_stage, cav_intensity)
                samples.append(sample)

            packet = build_packet(turbine_id, sensor, samples, seq)
            try:
                sock.sendto(packet, (target_ip, target_port))
            except Exception as e:
                print(f"[Turbine {turbine_id}] Send error: {e}")
                return

            seq += samples_per_packet
            t_offset += samples_per_packet * dt

        time.sleep(sleep_interval)


def main():
    parser = argparse.ArgumentParser(
        description='PXI UDP Data Simulator for Cavitation Monitoring',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Default: 6 turbines, 20 sensors each, 1ms interval
  python pxi_simulator.py --target-ip cavitation-monitor --target-port 9000

  # Inject critical cavitation on turbine 3 blade 7
  curl -X POST http://localhost:8091/inject \\
    -H 'Content-Type: application/json' \\
    -d '{"turbine_id":3,"blade_id":7,"stage":"critical","intensity":0.8}'

  # Inject developed cavitation on all blades of turbine 1
  curl -X POST http://localhost:8091/inject \\
    -d '{"turbine_id":1,"stage":"developed","intensity":0.9}'

  # Clear all injections (return to natural evolution)
  curl -X DELETE http://localhost:8091/inject

  # Query current status
  curl http://localhost:8091/status
""")
    parser.add_argument('--target-ip', default='127.0.0.1', help='Target IP address')
    parser.add_argument('--target-port', type=int, default=9000, help='Target UDP port')
    parser.add_argument('--turbines', type=int, default=6, help='Number of turbines to simulate')
    parser.add_argument('--sample-rate', type=int, default=1000, help='Sample rate in Hz')
    parser.add_argument('--interval-ms', type=int, default=1, help='Packet interval in milliseconds')
    parser.add_argument('--inject-port', type=int, default=8091, help='HTTP injection control port')
    args = parser.parse_args()

    turbines = {}
    for tid in range(1, args.turbines + 1):
        turbines[tid] = TurbineState(turbine_id=tid)

    inject_server = CavitationInjectionServer(turbines, http_port=args.inject_port)
    inject_thread = threading.Thread(target=inject_server.start, daemon=True)
    inject_thread.start()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 32 * 1024 * 1024)

    threads = []
    for tid in range(1, args.turbines + 1):
        t = threading.Thread(
            target=simulate_turbine,
            args=(sock, args.target_ip, args.target_port,
                  turbines[tid], args.sample_rate, args.interval_ms),
            daemon=True
        )
        t.start()
        threads.append(t)
        time.sleep(0.1)

    print(f"PXI Simulator running:")
    print(f"  Turbines:       {args.turbines} x 20 sensors = {args.turbines * 20} channels")
    print(f"  Sample rate:    {args.sample_rate} Hz")
    print(f"  Packet interval: {args.interval_ms} ms")
    print(f"  Target:         {args.target_ip}:{args.target_port}")
    print(f"  Inject control: http://0.0.0.0:{args.inject_port}")
    print("Press Ctrl+C to stop")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping simulator...")
        inject_server.stop()
        sock.close()


if __name__ == '__main__':
    main()
