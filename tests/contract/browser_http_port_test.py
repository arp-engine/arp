"""Browser HTTP selection regression; no GPU execution authority."""
import http.server
import pathlib
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
import browser_contract_runner as runner


class BrowserHttpPortTest(unittest.TestCase):
    def test_unsafe_ephemeral_ports_are_held_and_closed_before_return(self):
        blocked = [mock.Mock(server_port=port) for port in (6667, 6669, 10080)]
        accepted = mock.Mock(server_port=49152)

        def allocate(*args):
            for server in blocked:
                server.server_close.assert_not_called()
            return (blocked + [accepted])[factory.call_count - 1]

        with mock.patch.object(runner.http.server, "ThreadingHTTPServer", side_effect=allocate) as factory:
            self.assertIs(runner.create_browser_http_server(object()), accepted)
        self.assertEqual(factory.call_count, 4)
        for server in blocked:
            server.server_close.assert_called_once()
        accepted.server_close.assert_not_called()

    def test_bind_failure_releases_previously_rejected_sockets(self):
        blocked = mock.Mock(server_port=6667)
        error = OSError("bind refused")
        with mock.patch.object(runner.http.server, "ThreadingHTTPServer", side_effect=[blocked, error]):
            with self.assertRaisesRegex(OSError, "bind refused"):
                runner.create_browser_http_server(object())
        blocked.server_close.assert_called_once()

    def test_selection_is_bounded_if_allocator_repeats_a_blocked_port(self):
        blocked = mock.Mock(server_port=6667)
        with mock.patch.object(runner.http.server, "ThreadingHTTPServer", return_value=blocked) as factory:
            with self.assertRaisesRegex(RuntimeError, "BROWSER-CONTRACT-HTTP-PORT-CAPACITY"):
                runner.create_browser_http_server(object())
        self.assertEqual(factory.call_count, len(runner._FETCH_BAD_PORTS) + 1)
        self.assertEqual(blocked.server_close.call_count, factory.call_count)

    def test_real_socket_is_listening_on_loopback_and_usable_port(self):
        server = runner.create_browser_http_server(http.server.SimpleHTTPRequestHandler)
        try:
            self.assertEqual(server.server_address[0], "127.0.0.1")
            self.assertNotIn(server.server_port, runner._FETCH_BAD_PORTS)
            self.assertGreater(server.server_port, 0)
            self.assertGreaterEqual(server.socket.fileno(), 0)
        finally:
            server.server_close()


if __name__ == "__main__":
    unittest.main()
