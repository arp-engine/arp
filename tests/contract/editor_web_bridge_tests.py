"""Fast transport contracts: no mock is used as proof of Wasm execution."""
import functools
import http.client
import http.server
import json
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from editor_web_bridge import BridgeError, EditorWebBridge
from serve_editor_web import EditorWebHandler


class BridgeContracts(unittest.TestCase):
    def setUp(self):
        self.bridge = EditorWebBridge('test-token-' + 'a' * 32)

    def test_original_editor_error_is_not_hidden_by_bridge(self):
        node = shutil.which('node')
        if node is None:
            self.skipTest('Node.js is required for the browser adapter message fixture')
        root = Path(__file__).resolve().parents[2]
        subprocess.run([node, str(root / 'tests/fixtures/editor_web_cli_diagnostic_tests.js'),
                        str(root / 'editor/web/cli_bridge.js')], check=True, timeout=10)

    def test_exclusive_and_stale_session(self):
        session = self.bridge.connect()['session']
        with self.assertRaises(BridgeError):
            self.bridge.connect()
        with self.assertRaises(BridgeError):
            self.bridge.poll('wrong')
        self.bridge.disconnect(session)
        self.assertNotEqual(self.bridge.connect()['session'], session)

    def test_cancel_before_delivery(self):
        self.bridge.timeout_seconds = .01
        session = self.bridge.connect()['session']
        status, value = self.bridge.submit('POST', '/api/v1/commands', '{}')
        self.assertEqual(status, 504)
        self.assertEqual(json.loads(value)['error'], 'cancelled_before_delivery')
        self.assertIsNone(self.bridge.poll(session))

    def test_delivered_once_unknown_and_late_result(self):
        self.bridge.timeout_seconds = .05
        session = self.bridge.connect()['session']
        replies = []
        worker = threading.Thread(target=lambda: replies.append(
            self.bridge.submit('POST', '/mcp', '{}', 'application/json', 'unsupported-version')))
        worker.start()
        with self.bridge.condition:
            self.bridge.condition.wait_for(lambda: self.bridge.pending is not None, 1)
        request = self.bridge.poll(session)
        self.assertEqual(request['accept'], 'application/json')
        self.assertEqual(request['protocolVersion'], 'unsupported-version')
        self.assertIsNone(self.bridge.poll(session))
        worker.join(1)
        self.assertEqual(replies[0][0], 504)
        self.assertEqual(self.bridge.last_result()['state'], 'outcome_unknown')
        with self.assertRaises(BridgeError):
            self.bridge.submit('GET', '/api/v1/discovery', '')
        self.bridge.complete(session, dict(id=request['id'], status=200, body='{"ok":true}'))
        self.assertEqual(self.bridge.last_result()['state'], 'completed')
        self.assertIsNone(self.bridge.pending)
        with self.assertRaises(BridgeError):
            self.bridge.complete(session, dict(id=request['id'], status=200, body='{}'))

    def test_loopback_http_auth_origin_limits(self):
        class Handler(EditorWebHandler):
            def log_message(self, *unused):
                pass
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0),
            functools.partial(Handler, directory=str(Path(__file__).parent)))
        server.editor_bridge = self.bridge
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        def send(path, headers, body='{}', method='POST'):
            client = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=2)
            client.request(method, path, body, headers)
            response = client.getresponse()
            status, data = response.status, response.read()
            client.close()
            return status, data
        try:
            self.assertEqual(send('/api/v1/commands', {})[0], 401)
            auth = {'Authorization': 'Bearer ' + self.bridge.token, 'Content-Type': 'application/json'}
            self.assertEqual(send('/api/v1/commands', auth)[0], 503)
            self.assertEqual(send('/bridge/connect', {})[0], 403)
            browser = {'X-ARP-Editor-Bridge': '1', 'Sec-Fetch-Site': 'same-origin',
                       'Origin': f'http://127.0.0.1:{server.server_port}'}
            self.assertEqual(send('/bridge/connect', dict(browser, Origin='http://evil.invalid'))[0], 403)
            self.assertEqual(send('/bridge/connect', browser)[0], 200)
            self.assertEqual(send('/bridge/connect', browser)[0], 409)
            self.assertEqual(send('/api/v1/commands', dict(auth, Origin=browser['Origin']))[0], 403)
            self.assertEqual(send('/api/v1/commands', dict(auth, Host='evil.invalid'))[0], 403)
            self.assertEqual(send('/api/v1/commands', dict(auth, **{'Content-Length': str(self.bridge.max_bytes+1)}), '')[0], 413)
            self.assertEqual(send('/api/v1/bridge/result', auth, '', 'GET')[0], 200)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)


if __name__ == '__main__':
    unittest.main()
