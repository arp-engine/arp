"""Test-only wire faults around the real native workspace protocol and disk IO."""
import hashlib
import http.client
from http.server import BaseHTTPRequestHandler
import json
import socket
import threading

from browser_contract_runner import create_browser_http_server


class WorkspaceFaultProxy:
    def __init__(self):
        self.upstream_port = None
        self.counts = dict(replace=0, dropped=0, corrupted=0, preflight=0, blockedForget=0)
        self.bad_receipts = set()
        self.replace_wires, self.attempts, self.paths = {}, {}, {}
        self.lock = threading.Lock()
        self.drop_suffixes = {'/lost.arp'}
        self.bad_suffixes = {'/bad.arp'}
        self.corrupt_read_suffixes = {'/bad.arp'}
        self.block_forget_suffixes = set()
        owner = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'

            def log_message(self, *_):
                pass

            def handle(self):
                try:
                    super().handle()
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass

            def do_OPTIONS(self):
                self.forward(True)

            def do_POST(self):
                self.forward(False)

            def forward(self, preflight):
                assert self.path == '/workspace/v2'
                size = int(self.headers.get('Content-Length', '0'))
                assert 0 <= size <= 131072
                body = self.rfile.read(size)
                request = {} if preflight else json.loads(body)
                headers = {key: self.headers[key] for key in ('Authorization', 'Content-Type', 'Origin',
                    'Access-Control-Request-Method', 'Access-Control-Request-Headers') if key in self.headers}
                assert 'Cookie' not in self.headers
                operation, path, identity = request.get('operation'), request.get('path', ''), request.get('id')
                with owner.lock:
                    if operation == 'replace':
                        owner.counts['replace'] += 1
                        digest = hashlib.sha256(body).digest()
                        assert identity not in owner.replace_wires or owner.replace_wires[identity] == digest
                        owner.replace_wires[identity] = digest
                        owner.attempts[identity] = owner.attempts.get(identity, 0) + 1
                        owner.paths[identity] = path
                    block = operation == 'forget' and any(owner.paths.get(identity, '').endswith(suffix)
                        for suffix in owner.block_forget_suffixes)
                    if block:
                        owner.counts['blockedForget'] += 1
                if block:
                    # This fault precedes forwarding: the real server's journal
                    # remains retained. Never report a fabricated release result.
                    self.send_response(503)
                    if self.headers.get('Origin'):
                        self.send_header('Access-Control-Allow-Origin', self.headers['Origin'])
                    self.send_header('Content-Length', '0')
                    self.end_headers()
                    return
                connection = http.client.HTTPConnection('127.0.0.1', owner.upstream_port, timeout=10)
                try:
                    connection.request('OPTIONS' if preflight else 'POST', self.path, body=body, headers=headers)
                    response = connection.getresponse()
                    raw = response.read(131073)
                    assert len(raw) <= 131072
                    response_headers = {key: value for key, value in response.getheaders()
                        if key.lower() in {'content-type', 'access-control-allow-origin',
                            'access-control-allow-methods', 'access-control-allow-headers', 'cache-control'}}
                    with owner.lock:
                        if preflight:
                            owner.counts['preflight'] += 1
                        drop = operation == 'replace' and any(path.endswith(suffix) for suffix in owner.drop_suffixes)
                        if operation == 'replace' and any(path.endswith(suffix) for suffix in owner.bad_suffixes):
                            owner.bad_receipts.add(identity)
                        corrupt = ((operation == 'read' and any(path.endswith(suffix)
                            for suffix in owner.corrupt_read_suffixes)) or
                            (operation == 'receipt' and identity in owner.bad_receipts))
                        if drop:
                            assert json.loads(raw)['commit'] == 'committed'
                            owner.counts['dropped'] += 1
                        if corrupt:
                            decoded = json.loads(raw)
                            if operation == 'read':
                                decoded['data'] = '!!!!'
                            else:
                                decoded['requestVersion']['digest'] = 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA='
                            raw = json.dumps(decoded, separators=(',', ':')).encode()
                            owner.counts['corrupted'] += 1
                    if drop:
                        self.close_connection = True
                        self.connection.shutdown(socket.SHUT_RDWR)
                        return
                    self.send_response(response.status)
                    for key, value in response_headers.items():
                        self.send_header(key, value)
                    self.send_header('Content-Length', str(len(raw)))
                    self.end_headers()
                    self.wfile.write(raw)
                    self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass
                finally:
                    connection.close()

        self.server = create_browser_http_server(Handler)

    @property
    def server_port(self):
        return self.server.server_port

    def serve_forever(self):
        self.server.serve_forever()

    def shutdown(self):
        self.server.shutdown()

    def server_close(self):
        self.server.server_close()
