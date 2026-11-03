"""Bounded HTTP wire fixture shared by native and actual-browser transport checks."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import time

TOKEN = "transfer-012345678901234567890123456789"
BODY = '{"message":"雪"}'.encode("utf-8")


class AuthenticatedFixtureHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def end_headers(self):
        if self.headers.get("Origin") == self.server.allowed_origin:
            self.send_header("Access-Control-Allow-Origin", self.server.allowed_origin)
        super().end_headers()

    def do_OPTIONS(self):
        assert self.headers.get("Origin") == self.server.allowed_origin
        assert self.headers.get("Access-Control-Request-Method") == "POST"
        requested = {item.strip().lower() for item in
                     self.headers.get("Access-Control-Request-Headers", "").split(",")}
        assert requested == {"authorization", "content-type"}
        self.server.preflights += 1
        self.send_response(204)
        self.send_header("Access-Control-Allow-Methods", "POST")
        self.send_header("Access-Control-Allow-Headers", "Authorization, Content-Type")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self):
        self.server.posts += 1
        size = int(self.headers.get("Content-Length", "-1"))
        assert 0 < size <= 1024
        assert self.headers.get("Content-Type") == "application/json"
        assert self.headers.get("Cookie") is None and self.headers.get("Range") is None
        assert self.rfile.read(size) == BODY
        if self.path == "/redirected":
            self.server.redirect_followed = True
        authorized = self.headers.get("Authorization") == "Bearer " + TOKEN
        status, body = (200, BODY) if authorized else (401, b"denied")
        if self.path == "/slow":
            time.sleep(0.4)
        if self.path == "/redirect":
            status, body = 302, b""
        elif self.path == "/oversize":
            body = b"x" * 4096
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", "8" if self.path == "/truncated" else str(len(body)))
        self.send_header("Connection", "close")
        if self.path == "/redirect":
            self.send_header("Location", "/redirected")
        self.end_headers()
        try:
            self.wfile.write(b"abc" if self.path == "/truncated" else body)
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        self.close_connection = True


def create_authenticated_server(server_factory=None):
    server = (server_factory(AuthenticatedFixtureHandler) if server_factory else
              ThreadingHTTPServer(("127.0.0.1", 0), AuthenticatedFixtureHandler))
    server.allowed_origin = ""
    server.preflights = server.posts = 0
    server.redirect_followed = False
    return server
