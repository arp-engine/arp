"""Editor-owned, bounded loopback transport; never interprets editor commands."""
import hmac
import json
import secrets
import threading
import time


class BridgeError(Exception):
    def __init__(self, status, code):
        self.status, self.code = status, code


class EditorWebBridge:
    contract_version = 1
    max_bytes = 16 * 1024 * 1024
    timeout_seconds = 30
    lease_seconds = 30

    def __init__(self, token):
        if not 32 <= len(token.encode('utf-8')) <= 256:
            raise ValueError('ARP_EDITOR_REMOTE_TOKEN must contain 32 to 256 bytes')
        self.token = token
        self.condition = threading.Condition()
        self.session = None
        self.last_poll = 0
        self.serial = 0
        self.pending = None
        self.result = None

    def _session(self, session):
        if not self.session or not hmac.compare_digest(session.encode('utf-8'), self.session.encode('utf-8')):
            raise BridgeError(409, 'bridge_session_expired')
        self.last_poll = time.monotonic()

    def connect(self):
        with self.condition:
            if self.session and time.monotonic() - self.last_poll <= self.lease_seconds:
                raise BridgeError(409, 'bridge_already_connected')
            if self.pending:
                raise BridgeError(409, 'bridge_request_unresolved')
            self.session = secrets.token_urlsafe(32)
            self.last_poll = time.monotonic()
            return {'contractVersion': 1, 'session': self.session}

    def poll(self, session):
        with self.condition:
            self._session(session)
            if not self.pending or self.pending['delivered']:
                return None
            self.pending['delivered'] = True
            return {key: self.pending[key] for key in ('id', 'method', 'path', 'body', 'accept', 'protocolVersion')}

    def complete(self, session, value):
        with self.condition:
            self._session(session)
            if (not isinstance(value, dict) or set(value) != {'id', 'status', 'body'}
                    or type(value['status']) is not int or not 100 <= value['status'] <= 599
                    or not isinstance(value['body'], str)
                    or len(value['body'].encode('utf-8')) > self.max_bytes):
                raise BridgeError(400, 'bridge_invalid_response')
            if (not self.pending or not self.pending['delivered']
                    or value['id'] != self.pending['id'] or self.pending.get('response')):
                raise BridgeError(409, 'bridge_response_not_expected')
            self.pending['response'] = (value['status'], value['body'])
            self.result = dict(value, state='completed')
            self.condition.notify_all()

    def disconnect(self, session):
        with self.condition:
            self._session(session)
            if self.pending and self.pending['delivered']:
                raise BridgeError(409, 'bridge_request_unresolved')
            self.session = None
            if self.pending:
                self.pending['response'] = (503, '{"error":"bridge_disconnected"}')
                self.condition.notify_all()

    def submit(self, method, path, body, accept='', protocol_version=''):
        with self.condition:
            if not self.session or time.monotonic() - self.last_poll > self.lease_seconds:
                raise BridgeError(503, 'bridge_not_connected')
            if self.pending:
                raise BridgeError(409, 'bridge_busy')
            self.serial += 1
            request = dict(id=str(self.serial), method=method, path=path, body=body,
                           accept=accept, protocolVersion=protocol_version,
                           delivered=False)
            self.pending = request
            self.result = dict(id=request['id'], state='pending')
            self.condition.notify_all()
            completed = self.condition.wait_for(lambda: 'response' in request,
                                                self.timeout_seconds)
            if completed:
                self.pending = None
                return request['response']
            if request['delivered']:
                # Retain the slot: do not admit another command with an unknown outcome.
                self.result = dict(id=request['id'], state='outcome_unknown')
            else:
                self.pending = None
                self.result = dict(id=request['id'], state='cancelled_before_delivery')
            return 504, json.dumps(dict(error=self.result['state'], requestId=request['id']))

    def last_result(self):
        with self.condition:
            if self.pending and self.pending.get('response'):
                self.pending = None
            return self.result or {'state': 'none'}

    def handle(self, handler):
        """Return False for static files; headers never grant CLI authentication."""
        path = handler.path
        if not (path.startswith('/bridge/') or path.startswith('/api/') or path == '/mcp'):
            return False
        try:
            expected_host = f'127.0.0.1:{handler.server.server_port}'
            if handler.headers.get('Host') != expected_host:
                raise BridgeError(403, 'bridge_host_rejected')
            browser = path.startswith('/bridge/')
            if browser:
                if (handler.headers.get('X-ARP-Editor-Bridge') != '1'
                        or handler.headers.get('Sec-Fetch-Site') != 'same-origin'
                        or (handler.command == 'POST'
                            and handler.headers.get('Origin') != 'http://' + expected_host)):
                    raise BridgeError(403, 'bridge_origin_rejected')
            else:
                authorization = handler.headers.get('Authorization', '')
                if not hmac.compare_digest(authorization.encode(), ('Bearer '+self.token).encode()):
                    raise BridgeError(401, 'unauthorized')
                if handler.headers.get('Origin'):
                    raise BridgeError(403, 'bridge_cli_origin_rejected')
            if handler.command == 'GET' and path == '/bridge/poll':
                value = self.poll(handler.headers.get('X-ARP-Editor-Session', ''))
                status, body = 200, json.dumps(value)
            elif handler.command == 'POST' and path in ('/bridge/connect', '/bridge/result', '/bridge/disconnect'):
                source = self.read_body(handler)
                if path == '/bridge/connect':
                    if source != '{}':
                        raise BridgeError(400, 'bridge_invalid_connect')
                    value = self.connect()
                else:
                    session = handler.headers.get('X-ARP-Editor-Session', '')
                    if path == '/bridge/disconnect':
                        self.disconnect(session)
                    else:
                        try:
                            self.complete(session, json.loads(source))
                        except (ValueError, TypeError):
                            raise BridgeError(400, 'bridge_invalid_response') from None
                    value = {'ok': True}
                status, body = 200, json.dumps(value)
            elif handler.command == 'GET' and path == '/api/v1/bridge/result':
                status, body = 200, json.dumps(self.last_result())
            elif ((handler.command == 'POST' and path in ('/api/v1/commands', '/mcp'))
                  or (handler.command == 'GET' and path == '/api/v1/discovery')):
                if handler.command == 'POST' and handler.headers.get('Content-Type', '').split(';')[0] != 'application/json':
                    raise BridgeError(415, 'application_json_required')
                body = self.read_body(handler) if handler.command == 'POST' else ''
                accept = handler.headers.get('Accept', '')
                protocol_version = handler.headers.get('MCP-Protocol-Version', '')
                if len(accept) > 4096 or len(protocol_version) > 4096:
                    raise BridgeError(400, 'bridge_header_too_large')
                status, body = self.submit(handler.command, path, body, accept, protocol_version)
            else:
                raise BridgeError(404, 'bridge_route_not_found')
        except BridgeError as error:
            status, body = error.status, json.dumps({'error': error.code})
        data = body.encode('utf-8')
        handler.send_response(status)
        handler.send_header('Content-Type', 'application/json')
        handler.send_header('Content-Length', str(len(data)))
        handler.end_headers()
        try:
            handler.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Terminal result remains available; never replay a command.
        return True

    @classmethod
    def read_body(cls, handler):
        try:
            length = int(handler.headers.get('Content-Length', '-1'))
        except ValueError:
            raise BridgeError(400, 'bridge_invalid_length') from None
        if length < 0 or handler.headers.get('Transfer-Encoding'):
            raise BridgeError(400, 'bridge_invalid_length')
        # JSON quoting can expand a browser response to six bytes per source byte.
        limit = cls.max_bytes * 6 + 1024 if handler.path == '/bridge/result' else cls.max_bytes
        if length > limit:
            raise BridgeError(413, 'request_too_large')
        handler.connection.settimeout(cls.timeout_seconds)
        try:
            data = handler.rfile.read(length)
            if len(data) != length:
                raise BridgeError(400, 'bridge_truncated_body')
            return data.decode('utf-8')
        except UnicodeError:
            raise BridgeError(400, 'bridge_invalid_utf8') from None
