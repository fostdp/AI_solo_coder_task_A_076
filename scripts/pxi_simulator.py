import struct
import socket
import time
import math
import random
import argparse
import threading
from dataclasses import dataclass
from typing import List

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


@dataclass
class SensorDesc:
    sensor_id: int
    sensor_type: int
    location: int
    blade_id: int
    zone_id: int


SENSORS_PER_TURBINE = [
    SensorDesc(1,  HYDROPHONE,    LOC_SPIRAL_CASE_INLET, 0, 0),
    SensorDesc(2,  HYDROPHONE,    LOC_AFTER_GUIDE_VANE,  0, 0),
    SensorDesc(3,  HYDROPHONE,    LOC_DRAFT_TUBE,        0, 0),
    SensorDesc(4,  HYDROPHONE,    LOC_RUNNER_INLET,      1, 1),
    SensorDesc(5,  HYDROPHONE,    LOC_RUNNER_INLET,      1, 2),
    SensorDesc(6,  HYDROPHONE,    LOC_RUNNER_INLET,      1, 3),
    SensorDesc(7,  HYDROPHONE,    LOC_BLADE_CHANNEL,     2, 1),
    SensorDesc(8,  HYDROPHONE,    LOC_BLADE_CHANNEL,     2, 2),
    SensorDesc(9,  HYDROPHONE,    LOC_BLADE_CHANNEL,     2, 3),
    SensorDesc(10, HYDROPHONE,    LOC_RUNNER_OUTLET,     3, 1),
    SensorDesc(11, HYDROPHONE,    LOC_RUNNER_OUTLET,     3, 2),
    SensorDesc(12, HYDROPHONE,    LOC_RUNNER_OUTLET,     3, 3),
    SensorDesc(13, ACCELEROMETER, LOC_SPIRAL_CASE_INLET, 0, 0),
    SensorDesc(14, ACCELEROMETER, LOC_AFTER_GUIDE_VANE,  0, 0),
    SensorDesc(15, ACCELEROMETER, LOC_DRAFT_TUBE,        0, 0),
    SensorDesc(16, ACCELEROMETER, LOC_RUNNER_INLET,      1, 1),
    SensorDesc(17, ACCELEROMETER, LOC_RUNNER_INLET,      1, 2),
    SensorDesc(18, ACCELEROMETER, LOC_BLADE_CHANNEL,     2, 1),
    SensorDesc(19, ACCELEROMETER, LOC_BLADE_CHANNEL,     2, 2),
    SensorDesc(20, ACCELEROMETER, LOC_DRAFT_TUBE,        4, 1),
]


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

        if cav_stage >= CAV_DEVELOPED and sensor.blade_id > 0:
            signal += 0.3 * cav_intensity * random.gauss(0, 1)
            sub_freq = 2.0 + 3.0 * cav_intensity
            signal += 0.2 * cav_intensity * math.sin(2 * math.pi * sub_freq * t)

        return signal + noise

    else:
        amplitude = 0.5
        signal = amplitude * math.sin(2 * math.pi * base_freq * t)
        signal += 0.1 * math.sin(2 * math.pi * 75 * t)
        signal += 0.05 * math.sin(2 * math.pi * 150 * t)
        noise = random.gauss(0, 0.05)

        if cav_stage >= CAV_CRITICAL and sensor.blade_id > 0:
            vib_freq = 500 + 500 * cav_intensity
            signal += 2.0 * cav_intensity * math.sin(2 * math.pi * vib_freq * t)

        if cav_stage >= CAV_DEVELOPED and sensor.blade_id > 0:
            signal += 5.0 * cav_intensity * random.gauss(0, 1)

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


class CavitationProfile:
    def __init__(self):
        self.blade_states = {}
        for blade in range(1, 14):
            self.blade_states[blade] = {
                'stage': CAV_NONE,
                'intensity': 0.0,
                'target_stage': CAV_NONE,
                'transition_speed': random.uniform(0.001, 0.005)
            }

    def update(self):
        for blade_id in self.blade_states:
            state = self.blade_states[blade_id]
            r = random.random()
            if state['stage'] == CAV_NONE and r < 0.0005:
                state['target_stage'] = CAV_INCIPIENT
            elif state['stage'] == CAV_INCIPIENT and r < 0.001:
                state['target_stage'] = CAV_CRITICAL
            elif state['stage'] == CAV_INCIPIENT and r < 0.005:
                state['target_stage'] = CAV_NONE
            elif state['stage'] == CAV_CRITICAL and r < 0.0005:
                state['target_stage'] = CAV_DEVELOPED
            elif state['stage'] == CAV_CRITICAL and r < 0.003:
                state['target_stage'] = CAV_INCIPIENT
            elif state['stage'] == CAV_DEVELOPED and r < 0.002:
                state['target_stage'] = CAV_CRITICAL

            if state['stage'] != state['target_stage']:
                state['intensity'] += state['transition_speed']
                if state['intensity'] >= 1.0:
                    state['stage'] = state['target_stage']
                    state['intensity'] = 0.0
            else:
                if state['stage'] == CAV_NONE:
                    state['intensity'] = max(0, state['intensity'] - 0.01)

    def get_stage_and_intensity(self, blade_id: int):
        state = self.blade_states.get(blade_id, {'stage': CAV_NONE, 'intensity': 0.0})
        return state['stage'], state['intensity']


def simulate_turbine(sock: socket.socket, target_ip: str, target_port: int,
                      turbine_id: int, sample_rate: int = 1000):
    profile = CavitationProfile()
    seq = 0
    t_offset = turbine_id * 100.0
    dt = 1.0 / sample_rate

    batch_interval = 0.001
    samples_per_packet = min(MAX_PAYLOAD_SAMPLES, sample_rate // 10)

    print(f"[Turbine {turbine_id}] Starting simulation -> {target_ip}:{target_port}")

    while True:
        profile.update()

        for sensor in SENSORS_PER_TURBINE:
            samples = []
            cav_stage = CAV_NONE
            cav_intensity = 0.0

            if sensor.blade_id > 0:
                cav_stage, cav_intensity = profile.get_stage_and_intensity(sensor.blade_id)

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

        time.sleep(batch_interval)


def main():
    parser = argparse.ArgumentParser(description='PXI UDP Data Simulator for Cavitation Monitoring')
    parser.add_argument('--target-ip', default='127.0.0.1', help='Target IP address')
    parser.add_argument('--target-port', type=int, default=9200, help='Target UDP port')
    parser.add_argument('--turbines', type=int, default=6, help='Number of turbines to simulate')
    parser.add_argument('--sample-rate', type=int, default=1000, help='Sample rate in Hz')
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 8 * 1024 * 1024)

    threads = []
    for tid in range(1, args.turbines + 1):
        t = threading.Thread(target=simulate_turbine,
                              args=(sock, args.target_ip, args.target_port, tid, args.sample_rate),
                              daemon=True)
        t.start()
        threads.append(t)
        time.sleep(0.1)

    print(f"PXI Simulator running: {args.turbines} turbines, {args.sample_rate} Hz")
    print(f"Sending to {args.target_ip}:{args.target_port}")
    print("Press Ctrl+C to stop")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping simulator...")
        sock.close()


if __name__ == '__main__':
    main()
