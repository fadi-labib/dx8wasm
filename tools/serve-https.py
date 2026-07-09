#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# dx8wasm dev server. WASM+threads needs a *secure context* (SharedArrayBuffer)
# and cross-origin isolation. This serves over TLS (self-signed) and sends
# COOP/COEP/CORP so the page is crossOriginIsolated, with HTTP Range so large
# asset bundles stream in segments. Tolerates client disconnects.
#
# Usage:  serve-https.py [DIR] [PORT]     (DIR default ./dist, PORT default 8443)
# Cert:   put cert.pem/key.pem next to this script, or generate:
#   openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 \
#     -nodes -subj "/CN=localhost" -addext "subjectAltName=IP:<your-ip>,DNS:localhost"
#
# SPDX-License-Identifier: MIT
import http.server, ssl, os, sys
from functools import partial

HERE = os.path.dirname(os.path.abspath(__file__))
DIR  = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.getcwd(), "dist")
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8443
_DISCONNECT = (BrokenPipeError, ConnectionResetError, ConnectionAbortedError,
               TimeoutError, ssl.SSLError, ssl.SSLEOFError)

class H(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        super().end_headers()

    def send_head(self):
        rng = self.headers.get("Range")
        if not rng or not rng.startswith("bytes="):
            return super().send_head()
        path = self.translate_path(self.path)
        if not os.path.isfile(path):
            return super().send_head()
        size = os.path.getsize(path)
        s, _, e = rng[6:].partition("-")
        start = int(s) if s else 0
        end = min(int(e) if e else size - 1, size - 1)
        if start > end:
            self.send_error(416); return None
        f = open(path, "rb"); f.seek(start)
        self.send_response(206)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
        self._range = (start, end)
        return f

    def copyfile(self, src, dst):
        try:
            if hasattr(self, "_range"):
                start, end = self._range; remaining = end - start + 1
                while remaining > 0:
                    chunk = src.read(min(65536, remaining))
                    if not chunk: break
                    dst.write(chunk); remaining -= len(chunk)
            else:
                super().copyfile(src, dst)
        except _DISCONNECT:
            pass
        finally:
            self.__dict__.pop("_range", None)

    def log_message(self, *a):
        pass

class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    def handle_error(self, request, client_address):
        if isinstance(sys.exc_info()[1], _DISCONNECT):
            return
        super().handle_error(request, client_address)

if __name__ == "__main__":
    cert = os.path.join(HERE, "cert.pem"); key = os.path.join(HERE, "key.pem")
    if not (os.path.exists(cert) and os.path.exists(key)):
        sys.exit(f"missing cert.pem/key.pem next to {__file__} — see header for openssl cmd")
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    httpd = Server(("0.0.0.0", PORT), partial(H, directory=DIR))
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
    print(f"dx8wasm: serving {DIR}\n  https://0.0.0.0:{PORT}  (COOP/COEP + Range, disconnect-tolerant)")
    httpd.serve_forever()
