"""Run Borealis's real WebSocket backend suite against an asset-free loopback peer."""
import base64
import hashlib
import http.server
import os
import struct
import subprocess
import sys
import threading


class Peer(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def frame(self, opcode, data=b"", final=True):
        header = bytes([(0x80 if final else 0) | opcode])
        size = len(data)
        if size < 126:
            header += bytes([size])
        elif size < 65536:
            header += bytes([126]) + struct.pack("!H", size)
        else:
            header += bytes([127]) + struct.pack("!Q", size)
        self.wfile.write(header + data)
        self.wfile.flush()

    def do_GET(self):
        if self.path == "/reject":
            self.send_response(401)
            self.send_header("X-Borealis-Test", "rejected")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        key = self.headers["Sec-WebSocket-Key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
        accept = base64.b64encode(hashlib.sha1(key.encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        if self.path == "/fragment":
            self.frame(1, b"frag", final=False)
            self.frame(0, b"mented")
        elif self.path == "/server-close":
            self.frame(8, struct.pack("!H", 1000) + b"server close")
        try:
            while True:
                header = self.rfile.read(2)
                if len(header) != 2:
                    break
                opcode = header[0] & 15
                size = header[1] & 127
                if size == 126:
                    size = struct.unpack("!H", self.rfile.read(2))[0]
                elif size == 127:
                    size = struct.unpack("!Q", self.rfile.read(8))[0]
                mask = self.rfile.read(4) if header[1] & 128 else b""
                payload = self.rfile.read(size)
                if mask:
                    payload = bytes(value ^ mask[i % 4] for i, value in enumerate(payload))
                if opcode == 8:
                    if self.path != "/server-close":
                        self.frame(8, payload)
                    break
                if opcode == 9:
                    self.frame(10, payload)
                elif opcode in (1, 2):
                    self.frame(opcode, payload)
        except (ConnectionError, OSError):
            pass
        self.close_connection = True


with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Peer) as server:
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    env = dict(os.environ, BOREALIS_WS_TEST_URL=f"ws://127.0.0.1:{server.server_port}")
    result = subprocess.run([sys.argv[1]], env=env, timeout=90)
    server.shutdown()
    sys.exit(result.returncode)
