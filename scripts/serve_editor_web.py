"""Serve the pthread Wasm editor with required cross-origin isolation headers."""
import argparse
import functools
import http.server
from pathlib import Path


class EditorWebHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', default='build/editor-webgl')
    parser.add_argument('--port', type=int, default=8093)
    args = parser.parse_args()
    directory = Path(args.directory).resolve()
    if not (directory/'arp_editor.html').is_file():
        parser.error('Editor bundle missing: build with python scripts/build.py editor-webgl')
    server = http.server.ThreadingHTTPServer(('127.0.0.1', args.port),
        functools.partial(EditorWebHandler, directory=str(directory)))
    print(f'ARP Editor: http://127.0.0.1:{server.server_port}/arp_editor.html', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
