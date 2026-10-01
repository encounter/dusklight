"""Run Borealis's real HTTP and WebSocket suites against an asset-free peer."""
import base64
import contextlib
import hashlib
import http.server
import os
import struct
import ssl
import subprocess
import sys
import tempfile
import threading


class Peer(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    body = b"Borealis Nix transport fixture\n" * 32
    etag = '"nix-transport-fixture-v1"'

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

    def download(self, head=False):
        body = self.body
        status = 200
        content_range = None
        requested = self.headers.get("Range")
        if requested and self.headers.get("If-Range", self.etag) == self.etag:
            start = int(requested.removeprefix("bytes=").split("-", 1)[0])
            if start >= len(body):
                status = 416
                content_range = f"bytes */{len(body)}"
                body = b""
            else:
                status = 206
                content_range = f"bytes {start}-{len(body) - 1}/{len(body)}"
                body = body[start:]
        self.send_response(status)
        self.send_header("ETag", self.etag)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(len(body)))
        if content_range:
            self.send_header("Content-Range", content_range)
        self.end_headers()
        if not head:
            self.wfile.write(body)

    def do_HEAD(self):
        self.download(head=True)

    def do_GET(self):
        if self.path == "/download":
            self.download()
            return
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


with contextlib.ExitStack() as stack:
    server = stack.enter_context(http.server.ThreadingHTTPServer(("127.0.0.1", 0), Peer))
    env = dict(os.environ)
    scheme = "ws"
    if len(sys.argv) > 2:
        directory = stack.enter_context(tempfile.TemporaryDirectory())
        certificate, key = f"{directory}/certificate.pem", f"{directory}/key.pem"
        subprocess.run([env.get("OPENSSL", "openssl"), "req", "-x509", "-newkey", "rsa:2048",
                        "-nodes", "-keyout", key, "-out", certificate, "-days", "1",
                        "-subj", "/CN=localhost", "-addext",
                        "subjectAltName=DNS:localhost,IP:127.0.0.1"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        # Trust only this ephemeral fixture CA in the child curl processes.
        env["SSL_CERT_FILE"] = certificate
        scheme = "wss"
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = f"https://127.0.0.1:{server.server_port}/download"
    env.update(BOREALIS_WS_TEST_URL=f"{scheme}://127.0.0.1:{server.server_port}",
               BOREALIS_HTTP_RESUME_TEST_URL=url, BOREALIS_HTTP_METHOD_TEST_URL=url)
    result = subprocess.run([sys.argv[1]], env=env, timeout=90)
    if result.returncode == 0 and len(sys.argv) > 2:
        result = subprocess.run([sys.argv[2], "--gtest_filter=HttpTest.Live*"], env=env, timeout=90)
    server.shutdown()
    sys.exit(result.returncode)
