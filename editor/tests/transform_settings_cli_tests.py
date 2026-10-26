"""Check Interpreter transform CLI syntax and exact wire tokens, without Scene mocks."""
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import unittest

CLI = Path(sys.argv.pop(1)).resolve()
COMMAND = "scene.transform.settings.set"
VALID = ["rotate", "local", "individual", "false", "0.5", "0.25", "0.125"]


class TransformSettingsCliTests(unittest.TestCase):
    def invoke(self, *arguments, url="http://127.0.0.1:0"):
        environment = os.environ.copy()
        environment["ARP_EDITOR_REMOTE_TOKEN"] = "01234567890123456789012345678901"
        return subprocess.run(
            [str(CLI), "--url", url, *arguments], env=environment,
            capture_output=True, text=True, encoding="utf-8", timeout=15)

    def local_error(self, arguments, message):
        result = self.invoke(*arguments)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(message, result.stderr)
        self.assertNotIn("HTTP transport", result.stderr)

    def test_help_and_exact_arity(self):
        result = self.invoke("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("scene.transform.settings.get", result.stdout)
        self.assertIn("<rotateStepRadians>", result.stdout)
        self.assertIn("scene.selection.transform <move|rotate|scale>", result.stdout)
        self.local_error(["scene.transform.settings.get", "extra"], "takes no arguments")
        for count in range(7):
            with self.subTest(count=count):
                self.local_error([COMMAND, *VALID[:count]], "requires all seven")
        self.local_error([COMMAND, *VALID, "extra"], "too many command arguments")

    def test_enums_and_boolean_are_strict(self):
        for index, values, message in (
                (0, ["Rotate", "unknown", 'move","x":1'], "tool must"),
                (1, ["World", "", "screen"], "space must"),
                (2, ["Center", "none", 'active"}'], "pivot must"),
                (3, ["True", "0", 'false,"x":1'], "snapEnabled must")):
            for value in values:
                with self.subTest(index=index, value=value):
                    args = VALID.copy()
                    args[index] = value
                    self.local_error([COMMAND, *args], message)

    def test_complete_positive_json_syntax_and_budget_before_network(self):
        bad = ["", "0", "0.00e+15", "-0", "-1", "+1", "nan", "NaN", "inf",
               "1e309junk", " 1", "1 ", "1\n", "0x10", ".5", "1.", "01",
               "1e", "1e+", "1e-", "1..2", "1e2e3", "１", '1,"tool":"select"',
               "1" * 65]
        for index in (4, 5, 6):
            for value in bad:
                with self.subTest(index=index, value=value):
                    args = VALID.copy()
                    args[index] = value
                    self.local_error([COMMAND, *args], "steps require positive JSON")

    def test_selection_transform_arity_enums_axis_and_signed_syntax(self):
        command = "scene.selection.transform"
        valid = ["move", "world", "center", "0", "-0.125"]
        for count in range(5):
            self.local_error([command, *valid[:count]], "requires tool, space, pivot, axis and amount")
        self.local_error([command, *valid, "extra"], "requires tool, space, pivot, axis and amount")
        for index, values, message in (
                (0, ["select", "Move", 'move","axis":1'], "tool must"),
                (1, ["World", "screen"], "space must"),
                (2, ["Center", "none"], "pivot must"),
                (3, ["-1", "3", "0.0", "01", '0,"amount":1'], "axis must"),
                (4, ["", "-", "+1", "nan", "inf", "-inf", "--1", "-01", "-.5",
                     "-1.", "1e-", " 1", "1\n", "0x10", '1,"axis":2', "1" * 65],
                 "amount requires a signed JSON")):
            for value in values:
                with self.subTest(index=index, value=value):
                    args = valid.copy()
                    args[index] = value
                    self.local_error([command, *args], message)

    def test_wire_tokens_remain_exact_and_no_fields_are_inferred(self):
        # This HTTP receiver verifies request construction only; the native
        # Editor smoke separately proves range, Busy and Scene isolation.
        requests = []

        class Receiver(BaseHTTPRequestHandler):
            def do_POST(self):
                requests.append(self.rfile.read(int(self.headers["Content-Length"])))
                self.send_response(200)
                self.send_header("Content-Length", "2")
                self.end_headers()
                self.wfile.write(b"{}")

            def log_message(self, *args):
                pass

        with HTTPServer(("127.0.0.1", 0), Receiver) as server:
            thread = threading.Thread(target=server.serve_forever)
            thread.start()
            try:
                url = "http://127.0.0.1:" + str(server.server_port)
                for token in ("1", "0.5", "1E+2", "1e-2", "5e-324",
                              "0.10000000000000002", "1.7976931348623157e308",
                              "0." + "0" * 61 + "1"):
                    with self.subTest(token=token):
                        result = self.invoke(COMMAND, *VALID[:4], token, token, token, url=url)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        body = requests[-1]
                        settings = json.loads(body)["settings"]
                        self.assertEqual(set(settings), {
                            "tool", "space", "pivot", "snapEnabled", "moveStep",
                            "rotateStepRadians", "scaleStep"})
                        self.assertEqual(settings["tool"], "rotate")
                        self.assertFalse(settings["snapEnabled"])
                        for field in ("moveStep", "rotateStepRadians", "scaleStep"):
                            self.assertIn(('"' + field + '":' + token).encode("ascii"), body)
                for index, token in enumerate(("0", "-0", "0.00e+15", "-1", "-1.25", "-1E-2",
                                               "-5e-324", "0.10000000000000002",
                                               "-0." + "0" * 60 + "1")):
                    tool = ("move", "rotate", "scale")[index % 3]
                    pivot = ("active", "center", "individual")[index % 3]
                    axis = str(index % 3)
                    with self.subTest(selection_amount=token):
                        result = self.invoke("scene.selection.transform", tool, "local", pivot,
                                             axis, token, url=url)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertEqual(json.loads(requests[-1]), {
                            "schemaVersion": 1, "command": "scene.selection.transform",
                            "tool": tool, "space": "local", "pivot": pivot,
                            "axis": int(axis), "amount": float(token)})
                        self.assertIn(('"amount":' + token).encode("ascii"), requests[-1])
                result = self.invoke("scene.transform.settings.get", url=url)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(json.loads(requests[-1]), {
                    "schemaVersion": 1, "command": "scene.transform.settings.get"})
            finally:
                server.shutdown()
                thread.join(timeout=5)


if __name__ == "__main__":
    unittest.main()
