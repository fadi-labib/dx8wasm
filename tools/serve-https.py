#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# dx8wasm dev server. WASM+threads needs a *secure context* (SharedArrayBuffer)
# and cross-origin isolation. This serves over TLS (self-signed) and sends
# COOP/COEP/CORP so the page is crossOriginIsolated, with HTTP Range so large
# asset bundles stream in segments. Tolerates client disconnects.
#
# It is a DEV server: it binds 0.0.0.0 so a phone or second machine on the LAN can
# reach it, and like SimpleHTTPRequestHandler it lists directories with no
# index.html. Do not point it at the internet.
#
# Usage:  serve-https.py [DIR] [PORT]     (DIR default ./dist, PORT default 8443)
# Cert:   put cert.pem/key.pem next to this script, or generate:
#   openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 \
#     -nodes -subj "/CN=localhost" -addext "subjectAltName=IP:<your-ip>,DNS:localhost"
import http.server, ssl, os, sys
from functools import partial

HERE = os.path.dirname(os.path.abspath(__file__))
DIR  = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.getcwd(), "dist")
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8443
_DISCONNECT = (BrokenPipeError, ConnectionResetError, ConnectionAbortedError,
               TimeoutError, ssl.SSLError, ssl.SSLEOFError)

def parse_range(rng, size):
    """'bytes=a-b' -> inclusive (start, end) clamped to a size-byte file, or None when the
    header is malformed or unsatisfiable (-> 416). 'bytes=-N' is a SUFFIX range: the last N
    bytes (the old code read it as 0-N). Non-numeric input is None, not a traceback."""
    if not rng or not rng.startswith("bytes=") or size <= 0:
        return None
    s, sep, e = rng[6:].partition("-")
    if not sep:
        return None
    try:
        if s == "":
            n = int(e)
            if n <= 0:
                return None
            return (max(size - n, 0), size - 1)
        start = int(s)
        end = int(e) if e else size - 1
    except ValueError:
        return None
    end = min(end, size - 1)
    if start < 0 or start > end:
        return None
    return (start, end)

class H(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        super().end_headers()

    def send_head(self):
        # Per request. A HEAD with Range sets it but never reaches copyfile(), and the handler
        # instance lives across keep-alive requests, so it used to leak into the next GET and
        # truncate its body.
        self._range = None
        rng = self.headers.get("Range")
        if not rng:
            return super().send_head()
        path = self.translate_path(self.path)
        if not os.path.isfile(path):
            return super().send_head()
        size = os.path.getsize(path)
        r = parse_range(rng, size)
        if r is None:
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{size}")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return None
        start, end = r
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
            if getattr(self, "_range", None):
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
            self._range = None

    def log_message(self, *a):
        pass

class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    def handle_error(self, request, client_address):
        if isinstance(sys.exc_info()[1], _DISCONNECT):
            return
        super().handle_error(request, client_address)

def _selftest():
    """Range-header parsing, the one piece of logic here that is not stdlib. Run: serve-https.py --selftest"""
    size = 1000
    cases = {
        "bytes=0-99":     (0, 99),
        "bytes=500-":     (500, 999),      # open-ended: to the end
        "bytes=-100":     (900, 999),      # SUFFIX range: the LAST 100 bytes, not 0-100
        "bytes=0-5000":   (0, 999),        # end clamped to the file
        "bytes=999-999":  (999, 999),
        "bytes=1000-":    None,            # start past the end -> 416
        "bytes=5-2":      None,            # inverted -> 416
        "bytes=abc":      None,            # garbage -> 416, not a traceback
        "bytes=-0":       None,            # zero-length suffix -> 416
        "bytes=-5000":    (0, 999),        # suffix longer than the file: whole file
    }
    bad = 0
    for hdr, want in cases.items():
        got = parse_range(hdr, size)
        if got != want:
            print(f"FAIL parse_range({hdr!r}, {size}) = {got!r}, expected {want!r}"); bad += 1
    if bad: sys.exit(1)
    print(f"OK — serve-https range parser: {len(cases)} cases")

if __name__ == "__main__":
    if "--selftest" in sys.argv:
        _selftest(); sys.exit(0)
    cert = os.path.join(HERE, "cert.pem"); key = os.path.join(HERE, "key.pem")
    if not (os.path.exists(cert) and os.path.exists(key)):
        sys.exit(f"missing cert.pem/key.pem next to {__file__} — see header for openssl cmd")
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    httpd = Server(("0.0.0.0", PORT), partial(H, directory=DIR))
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
    print(f"dx8wasm: serving {DIR}\n  https://0.0.0.0:{PORT}  (COOP/COEP + Range, disconnect-tolerant)")
    httpd.serve_forever()
