import socket
import struct
import argparse
import threading
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
import time
import json
import sys


class IEC61850Simulator:
    MMS_PORT = 102
    TPKT_HEADER_SIZE = 4
    COTP_CR_SIZE = 7

    def __init__(self, listen_port=102):
        self.listen_port = listen_port
        self.reports = []
        self.reports_lock = threading.Lock()
        self.running = False
        self.server_socket = None

    def _build_association_response(self):
        return bytes([
            0x03, 0x00, 0x00, 0x23,
            0x02, 0xF0, 0x80,
            0x01, 0x00,
            0x00, 0x01,
            0x00, 0x1A,
            0x02, 0x01, 0x00,
            0x04, 0x01, 0x00,
            0xA0, 0x10,
            0x02, 0x01, 0x01,
            0x04, 0x01, 0x00,
            0xA2, 0x08,
            0x02, 0x01, 0x00,
            0x04, 0x01, 0x00,
            0x30, 0x00,
        ])

    def _parse_mms_message(self, data):
        results = []
        try:
            if len(data) < 20:
                return results

            xml_start = data.find(b'<?xml')
            if xml_start == -1:
                xml_start = data.find(b'<')
            if xml_start == -1:
                return results

            xml_data = data[xml_start:]
            try:
                root = ET.fromstring(xml_data)
            except ET.ParseError:
                try:
                    end = xml_data.rfind(b'>')
                    if end > 0:
                        root = ET.fromstring(xml_data[:end+1])
                    else:
                        return results
                except ET.ParseError:
                    return results

            ns = {'mms': 'urn:iec:61850:8.1:MMS'}

            for elem in root.iter():
                tag = elem.tag.split('}')[-1] if '}' in elem.tag else elem.tag
                if tag in ('InformationReport', 'AlarmSummary', 'CavitationStatus',
                           'FatigueDamage', 'VariableAccessSpecification'):
                    info = {
                        'element': tag,
                        'timestamp': datetime.now(timezone.utc).isoformat(),
                        'attributes': dict(elem.attrib),
                        'children': []
                    }
                    for child in elem:
                        child_tag = child.tag.split('}')[-1] if '}' in child.tag else child.tag
                        info['children'].append({
                            'tag': child_tag,
                            'text': child.text,
                            'attrib': dict(child.attrib)
                        })
                    results.append(info)

        except Exception as e:
            pass

        return results

    def _handle_client(self, conn, addr):
        print(f"[IEC 61850] Client connected from {addr[0]}:{addr[1]}")

        try:
            init_data = conn.recv(4096)
            if not init_data:
                return

            conn.sendall(self._build_association_response())

            while self.running:
                try:
                    conn.settimeout(1.0)
                    data = conn.recv(65536)
                    if not data:
                        break

                    parsed = self._parse_mms_message(data)
                    if parsed:
                        with self.reports_lock:
                            for report in parsed:
                                self.reports.append(report)
                                print(f"[IEC 61850] Received: {report['element']}")
                                for child in report.get('children', []):
                                    print(f"  {child['tag']}: {child['text']}")

                except socket.timeout:
                    continue
                except ConnectionResetError:
                    break

        except Exception as e:
            print(f"[IEC 61850] Client error: {e}")
        finally:
            conn.close()
            print(f"[IEC 61850] Client disconnected {addr[0]}:{addr[1]}")

    def start(self):
        self.running = True
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind(('0.0.0.0', self.listen_port))
        self.server_socket.listen(5)
        self.server_socket.settimeout(1.0)

        print(f"[IEC 61850] Simulator listening on port {self.listen_port}")

        while self.running:
            try:
                conn, addr = self.server_socket.accept()
                t = threading.Thread(target=self._handle_client, args=(conn, addr), daemon=True)
                t.start()
            except socket.timeout:
                continue
            except OSError:
                break

    def stop(self):
        self.running = False
        if self.server_socket:
            self.server_socket.close()

    def get_reports(self):
        with self.reports_lock:
            return list(self.reports)

    def get_report_count(self):
        with self.reports_lock:
            return len(self.reports)


class IEC61850HTTPServer:
    def __init__(self, simulator, http_port=8090):
        self.simulator = simulator
        self.http_port = http_port
        self.server_socket = None
        self.running = False

    def _handle_http(self, conn):
        try:
            request = conn.recv(4096).decode('utf-8', errors='replace')
            if not request:
                conn.close()
                return

            first_line = request.split('\r\n')[0]

            if 'GET /reports' in first_line:
                reports = self.simulator.get_reports()
                body = json.dumps({
                    'total': len(reports),
                    'reports': reports[-100:]
                }, ensure_ascii=False)
                self._send_json(conn, 200, body)

            elif 'GET /stats' in first_line:
                body = json.dumps({
                    'total_reports': self.simulator.get_report_count(),
                    'listening_port': self.simulator.listen_port,
                    'status': 'running' if self.simulator.running else 'stopped'
                })
                self._send_json(conn, 200, body)

            elif 'GET /health' in first_line:
                self._send_json(conn, 200, '{"status":"ok"}')

            elif 'DELETE /reports' in first_line:
                with self.simulator.reports_lock:
                    self.simulator.reports.clear()
                self._send_json(conn, 200, '{"cleared":true}')

            else:
                self._send_json(conn, 404, '{"error":"not found"}')

        except Exception as e:
            try:
                self._send_json(conn, 500, f'{{"error":"{str(e)}"}}')
            except:
                pass
        finally:
            conn.close()

    def _send_json(self, conn, status, body):
        status_text = {200: 'OK', 404: 'Not Found', 500: 'Internal Server Error'}
        response = (
            f"HTTP/1.1 {status} {status_text.get(status, 'OK')}\r\n"
            f"Content-Type: application/json\r\n"
            f"Access-Control-Allow-Origin: *\r\n"
            f"Content-Length: {len(body.encode())}\r\n"
            f"\r\n"
            f"{body}"
        )
        conn.sendall(response.encode())

    def start(self):
        self.running = True
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind(('0.0.0.0', self.http_port))
        self.server_socket.listen(10)
        self.server_socket.settimeout(1.0)

        print(f"[IEC 61850 HTTP] Status server on port {self.http_port}")

        while self.running:
            try:
                conn, _ = self.server_socket.accept()
                threading.Thread(target=self._handle_http, args=(conn,), daemon=True).start()
            except socket.timeout:
                continue
            except OSError:
                break

    def stop(self):
        self.running = False
        if self.server_socket:
            self.server_socket.close()


def main():
    parser = argparse.ArgumentParser(description='IEC 61850 MMS Simulator')
    parser.add_argument('--listen-port', type=int, default=102, help='MMS listen port')
    parser.add_argument('--http-port', type=int, default=8090, help='HTTP status port')
    args = parser.parse_args()

    sim = IEC61850Simulator(listen_port=args.listen_port)
    http = IEC61850HTTPServer(sim, http_port=args.http_port)

    mms_thread = threading.Thread(target=sim.start, daemon=True)
    http_thread = threading.Thread(target=http.start, daemon=True)
    mms_thread.start()
    http_thread.start()

    print(f"IEC 61850 Simulator running (MMS:{args.listen_port}, HTTP:{args.http_port})")
    print("Press Ctrl+C to stop")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping...")
        sim.stop()
        http.stop()


if __name__ == '__main__':
    main()
