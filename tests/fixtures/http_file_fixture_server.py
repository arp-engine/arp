"""Bounded deterministic loopback HTTP fixtures, shared by native and Fetch tests."""
import http.server
import time
import urllib.parse


class HttpFileFixtureHandler(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def parse_request(self):
        if hasattr(self.server, "request_lines") and len(self.server.request_lines) < 512:
            self.server.request_lines.append(self.raw_requestline.decode("ascii", "backslashreplace").strip())
        return super().parse_request()

    def do_GET(self):
        if not self.path.startswith("/fixture/"):
            return super().do_GET()
        name = urllib.parse.unquote(self.path.removeprefix("/fixture/"))
        body = b"abcdef"
        if name == "missing":
            self.send_error(404)
            return
        if name == "forbidden":
            self.send_error(403)
            return
        if name == "redirect":
            self.send_response(302)
            self.send_header("Location", "/fixture/redirect-target")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if name == "redirect-target":
            self.server.redirect_followed = True
        if name in ("slow-headers", "late-result"):
            time.sleep(1.0)
        status = 200
        response_range = None
        if self.headers.get("Range") and name not in ("ignore-range", "changed"):
            unit, interval = self.headers["Range"].split("=", 1)
            assert unit == "bytes"
            first, last = map(int, interval.split("-"))
            if first >= len(body):
                self.send_error(416)
                return
            last = min(last, len(body) - 1)
            body = body[first:last + 1]
            status = 206
            response_range = f"bytes {first}-{last}/6"
        if name == "bad-range":
            response_range = "bytes 1-3/6"
        if name == "changed":
            body = b"abcxef"
        if name == "binary":
            body = bytes((0, 255, 10, 13, 0, 128))
        if name == "host":
            body = b"dns-ok" if self.headers.get("Host", "").startswith("localhost:") else b"bad-host"
        if name == "empty":
            body = b""
        if name == "text":
            body = b"\xef\xbb\xbfhello\r\n"
        self.send_response(status)
        if name == "header-budget":
            self.send_header("X-Filler", "a" * 8192)
        if name == "encoded":
            self.send_header("Content-Encoding", "gzip")
        if response_range:
            self.send_header("Content-Range", response_range)
        if name != "unknown-length":
            length = len(body) + 3 if name == "truncated" else len(body)
            self.send_header("Content-Length", str(length))
        if name == "duplicate-length":
            self.send_header("Content-Length", "6")
        self.send_header("Connection", "close")
        self.end_headers()
        if name == "slow-body":
            time.sleep(1.0)
        try:
            self.wfile.write(body)
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        self.close_connection = True
