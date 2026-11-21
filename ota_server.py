import sys
import os
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOST = "0.0.0.0"
PORT = 8080
KERNEL_PATH = sys.argv[1] if len(sys.argv) > 1 else "KERNEL.BIN"


class OTAHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[OTA-SERVER] {self.address_string()} - " + (fmt % args))

    def do_GET(self):
        if self.path not in ("/", "/kernel.bin", "/KERNEL.BIN"):
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Not found\n")
            return

        if not os.path.isfile(KERNEL_PATH):
            self.send_response(500)
            self.end_headers()
            self.wfile.write(b"KERNEL.BIN missing\n")
            return

        with open(KERNEL_PATH, "rb") as f:
            data = f.read()

        crc = zlib.crc32(data) & 0xFFFFFFFF

        # Endpoint de metadados: /kernel.meta -> "<tamanho> <crc32_hex>"
        if self.path == "/":
            body = f"OK KERNEL.BIN size={len(data)} crc32=0x{crc:08x}\n".encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-Kernel-Size", str(len(data)))
        self.send_header("X-Kernel-CRC32", f"{crc:08x}")
        self.end_headers()
        self.wfile.write(data)


def main():
    if not os.path.isfile(KERNEL_PATH):
        print(f"[AVISO] '{KERNEL_PATH}' nao encontrado ainda; ele sera lido quando houver requisicao.")
    else:
        with open(KERNEL_PATH, "rb") as f:
            d = f.read()
        print(f"[OTA-SERVER] Arquivo: {KERNEL_PATH} ({len(d)} bytes, CRC32=0x{zlib.crc32(d) & 0xFFFFFFFF:08x})")

    server = ThreadingHTTPServer((HOST, PORT), OTAHandler)
    print(f"[OTA-SERVER] Rodando em http://{HOST}:{PORT}/KERNEL.BIN (Ctrl+C para parar)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[OTA-SERVER] Encerrado.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()

