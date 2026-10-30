"""Serve the pthread Wasm editor with required cross-origin isolation headers."""
import argparse
import functools
import http.server
import os
import threading
from pathlib import Path
from editor_web_bridge import EditorWebBridge


class EditorWebServer(http.server.ThreadingHTTPServer):
    """Bound all HTTP bodies/worker stacks, including requests awaiting a page."""
    request_queue_size = 8

    def __init__(self, *args, **kwargs):
        self.slots = threading.BoundedSemaphore(4)
        super().__init__(*args, **kwargs)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            try:
                request.settimeout(1)
                request.sendall(b'HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
            finally:
                self.shutdown_request(request)
            return
        try:
            super().process_request(request, address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()


class EditorWebHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if not self.bridge_request():
            super().do_GET()

    def do_POST(self):
        if not self.bridge_request():
            self.send_error(404)

    def bridge_request(self):
        bridge = getattr(self.server, 'editor_bridge', None)
        if bridge:
            return bridge.handle(self)
        if self.path.startswith(('/api/', '/bridge/')) or self.path == '/mcp':
            self.send_error(503, 'Editor CLI bridge is disabled; use --remote')
            return True
        return False

    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', default='build/editor-webgl')
    parser.add_argument('--port', type=int, default=8093)
    parser.add_argument('--remote', action='store_true', help='Enable authenticated CLI bridge')
    args = parser.parse_args()
    directory = Path(args.directory).resolve()
    if not (directory/'arp_editor.html').is_file():
        parser.error('Editor bundle missing: build with python scripts/build.py editor-webgl')
    server = EditorWebServer(('127.0.0.1', args.port),
        functools.partial(EditorWebHandler, directory=str(directory)))
    server.editor_bridge = EditorWebBridge(os.environ.get('ARP_EDITOR_REMOTE_TOKEN', '')) if args.remote else None
    print(f'ARP Editor: http://127.0.0.1:{server.server_port}/arp_editor.html', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
