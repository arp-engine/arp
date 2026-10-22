"""CLI routing tests with browser launch intercepted; no native execution proof."""
import contextlib
import importlib.util
import io
import itertools
import os
import pathlib
import tempfile
import unittest
from unittest import mock
from urllib.parse import parse_qs, urlparse

DEFAULT_DRIVER = pathlib.Path(__file__).with_name("bindless_webgpu_rhi_smoke.py")
DRIVER = pathlib.Path(os.environ.get("ARP_BINDLESS_DRIVER_CANDIDATE", DEFAULT_DRIVER))
spec = importlib.util.spec_from_file_location("finite_driver_under_test", DRIVER)
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


class BrowserLaunchIntercepted(Exception):
    pass


class FiniteLifecycleDriverTest(unittest.TestCase):
    selectors = (("--general-claim-terminal", "cancel"), ("--general-recording-terminal", "cancel"),
                 ("--general-submission-terminal", "cancel"), ("--general-physical-loss", "queue-uncertain"),
                 ("--general-map-terminal", "cancel"), ("--general-post-suppression-failure", "commit"))

    def test_scene_deadline_deducts_setup_from_original_host_budget(self):
        with tempfile.TemporaryDirectory() as directory:
            args = [str(DRIVER), "--browser", "unit-fixture-no-browser",
                    "--runner", "unit-fixture-no-runner", "--output", directory,
                    "--original-bindless-root-preparation", "--original-root-scene-family", "terrain",
                    "--timeout-seconds", "60"]
            handlers = []
            bodies = []
            server = mock.Mock(server_port=12345)

            def create_server(handler):
                handlers.append(handler.func)
                return server

            def intercept(*unused, **kwargs):
                handler = object.__new__(handlers[0])
                handler.path = "/progress-contract.js"
                handler.wfile = io.BytesIO()
                handler.send_response = mock.Mock()
                handler.send_header = mock.Mock()
                handler.end_headers = mock.Mock()
                handler.do_GET()
                bodies.append(handler.wfile.getvalue().decode())
                raise BrowserLaunchIntercepted()

            with mock.patch.object(driver.sys, "argv", args), \
                    mock.patch.object(driver, "read_bindless_publication", return_value=dict(shaderPackageIdentity="unit-fixture-only")), \
                    mock.patch.object(driver, "create_browser_http_server", side_effect=create_server), \
                    mock.patch.object(driver.threading, "Thread"), \
                    mock.patch.object(driver.time, "monotonic", side_effect=[100.0, 107.25]), \
                    mock.patch.object(driver.subprocess, "Popen", side_effect=intercept), \
                    self.assertRaises(BrowserLaunchIntercepted):
                driver.main()
            self.assertIn("window.BindlessSceneCompletionDeadline=performance.now()+52750.0;", bodies[0])
            server.shutdown.assert_called_once()
            server.server_close.assert_called_once()

    def base(self, output):
        return [str(DRIVER), "--browser", "unit-fixture-no-browser", "--runner", "unit-fixture-no-runner",
                "--output", str(output), "--general-resource-projection-scenario", "--general-finite-lifecycle"]

    def refuses(self, options, base_mutation=None):
        with tempfile.TemporaryDirectory() as directory:
            arguments = self.base(pathlib.Path(directory))
            if base_mutation:
                arguments = base_mutation(arguments)
            with mock.patch.object(driver.sys, "argv", arguments + options), \
                    mock.patch.object(driver, "read_bindless_publication", side_effect=AssertionError("forbidden I/O")), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
                driver.main()
            self.assertEqual(result.exception.code, 2)

    def test_exactly_one_lifecycle_selector_and_no_numerical_mode(self):
        self.refuses([])
        for left, right in itertools.combinations(self.selectors, 2):
            with self.subTest(left=left, right=right):
                self.refuses(list(left + right))
        for selector in self.selectors:
            with self.subTest(selector=selector):
                self.refuses(list(selector) + ["--general-finite-execution"])

    def test_post_suppression_requires_lifecycle_and_its_own_resource_projection(self):
        for missing in ("--general-finite-lifecycle", "--general-resource-projection-scenario"):
            with self.subTest(missing=missing):
                self.refuses(["--general-post-suppression-failure", "commit"],
                             lambda args: [arg for arg in args if arg != missing])
        self.refuses(["--general-post-suppression-failure", "unknown"])
        self.refuses(["--general-post-suppression-failure", "publication", "--general-content-plan-dynamic", "--general-source-whole"])

    def test_all_twelve_post_suppression_queries_route_without_another_terminal(self):
        for failure, buffer_type, interval in itertools.product(("commit", "publication"),
                ("uniform", "readonly-storage"), ("static", "dynamic", "whole")):
            with tempfile.TemporaryDirectory() as directory:
                output = pathlib.Path(directory)
                args = self.base(output) + ["--general-post-suppression-failure", failure,
                                           "--general-content-buffer-type", buffer_type]
                if interval == "dynamic": args.append("--general-content-plan-dynamic")
                if interval == "whole": args.append("--general-source-whole")
                captured = []
                def intercept(command, **unused):
                    captured.append(command)
                    raise BrowserLaunchIntercepted()
                server = mock.Mock(server_port=12345)
                with mock.patch.object(driver.sys, "argv", args), \
                        mock.patch.object(driver, "read_bindless_publication", return_value=dict(shaderPackageIdentity="unit-fixture-only")), \
                        mock.patch.object(driver.http.server, "ThreadingHTTPServer", return_value=server), \
                        mock.patch.object(driver.threading, "Thread"), \
                        mock.patch.object(driver.subprocess, "Popen", side_effect=intercept), \
                        self.assertRaises(BrowserLaunchIntercepted):
                    driver.main()
                self.assertEqual(len(captured), 1)
                query = parse_qs(urlparse(captured[0][-1]).query, keep_blank_values=True)
                self.assertEqual(query["general-post-suppression-failure"], [failure])
                self.assertEqual(query["general-content-buffer-type"], [buffer_type])
                self.assertIn("general-finite-lifecycle", query)
                self.assertEqual("general-content-plan-dynamic" in query, interval == "dynamic")
                self.assertEqual("general-source-whole" in query, interval == "whole")
                for selector, unused in self.selectors[:-1]:
                    self.assertNotIn(selector.removeprefix("--"), query)
                self.assertIn("--disable-background-timer-throttling", captured[0])
                server.shutdown.assert_called_once()
                server.server_close.assert_called_once()


    def assert_graphics_stage_routes(self,payload=False,cold=False,command=False,callback=False):
        with tempfile.TemporaryDirectory() as directory:
            args=[str(DRIVER),"--browser","unit-fixture-no-browser","--runner","unit-fixture-no-runner",
                  "--output",directory,"--general-finite-graphics-content-root","--general-graphics-shader-content",
                  "--general-graphics-native-custody","--general-graphics-dependency-freeze"]
            if payload:args.append("--general-graphics-native-payload")
            if cold:args.append("--general-graphics-native-cold")
            if command:args.append("--general-graphics-native-command")
            if callback:args.append("--general-graphics-native-callback")
            predecessors=["--general-finite-graphics-content-root","--general-graphics-shader-content","--general-graphics-native-custody"]
            if payload:predecessors.append("--general-graphics-dependency-freeze")
            if cold:predecessors.append("--general-graphics-native-payload")
            if command:predecessors.append("--general-graphics-native-cold")
            if callback:predecessors.append("--general-graphics-native-command")
            for missing in predecessors:
                with mock.patch.object(driver.sys,"argv",[a for a in args if a!=missing]), \
                        mock.patch.object(driver,"read_bindless_publication",side_effect=AssertionError("forbidden I/O")), \
                        contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
                    driver.main()
                self.assertEqual(result.exception.code,2)
            captured=[]
            def intercept(command,**unused):
                captured.append(command);raise BrowserLaunchIntercepted()
            server=mock.Mock(server_port=12345)
            with mock.patch.object(driver.sys,"argv",args), \
                    mock.patch.object(driver,"read_bindless_publication",return_value=dict(shaderPackageIdentity="unit-fixture-only")), \
                    mock.patch.object(driver.http.server,"ThreadingHTTPServer",return_value=server), \
                    mock.patch.object(driver.threading,"Thread"), \
                    mock.patch.object(driver.subprocess,"Popen",side_effect=intercept), self.assertRaises(BrowserLaunchIntercepted):
                driver.main()
            query=parse_qs(urlparse(captured[0][-1]).query,keep_blank_values=True)
            for option in args[-(8 if callback else 7 if command else 6 if cold else 5 if payload else 4):]:self.assertIn(option.removeprefix("--"),query)
            self.assertNotIn("general-finite-lifecycle",query)
            server.shutdown.assert_called_once();server.server_close.assert_called_once()

    def test_graphics_freeze8_requires_its_independent_predecessors_and_routes_exact_query(self):
        self.assert_graphics_stage_routes()

    def test_graphics_payload8_requires_freeze8_and_routes_without_compute_authority(self):
        self.assert_graphics_stage_routes(True)

    def test_graphics_cold8_requires_payload8_and_routes_without_compute_authority(self):
        self.assert_graphics_stage_routes(True,True)

    def test_graphics_command8_requires_cold8_and_routes_without_compute_authority(self):
        self.assert_graphics_stage_routes(True,True,True)

    def test_graphics_callback8_requires_command8_and_routes_without_execution_authority(self):
        self.assert_graphics_stage_routes(True,True,True,True)

    def test_baseline_wasm_policy_is_explicit_and_only_for_root_correctness(self):
        self.refuses(["--wasm-baseline-only"])
        for baseline in (False, True):
            with tempfile.TemporaryDirectory() as directory:
                args = [str(DRIVER), "--browser", "unit-fixture-no-browser", "--runner", "unit-fixture-no-runner",
                        "--output", directory, "--original-bindless-root-preparation"]
                if baseline:
                    args.append("--wasm-baseline-only")
                commands = []
                def intercept(command, **unused):
                    commands.append(command)
                    raise BrowserLaunchIntercepted()
                with mock.patch.object(driver.sys, "argv", args), \
                        mock.patch.object(driver, "read_bindless_publication", return_value=dict(shaderPackageIdentity="unit-fixture-only")), \
                        mock.patch.object(driver.http.server, "ThreadingHTTPServer", return_value=mock.Mock(server_port=12345)), \
                        mock.patch.object(driver.threading, "Thread"), \
                        mock.patch.object(driver.subprocess, "Popen", side_effect=intercept), self.assertRaises(BrowserLaunchIntercepted):
                    driver.main()
                self.assertEqual("--js-flags=--liftoff-only" in commands[0], baseline)

    def test_sampled_execution_selector_requires_original_root_and_routes_independently(self):
        self.refuses(["--original-root-sampled-execution-only"])
        with tempfile.TemporaryDirectory() as directory:
            args = [str(DRIVER), "--browser", "unit-fixture-no-browser", "--runner", "unit-fixture-no-runner",
                    "--output", directory, "--original-bindless-root-preparation",
                    "--original-root-sampled-execution-only", "--wasm-baseline-only"]
            commands = []
            def intercept(command, **unused):
                commands.append(command)
                raise BrowserLaunchIntercepted()
            server = mock.Mock(server_port=12345)
            with mock.patch.object(driver.sys, "argv", args), \
                    mock.patch.object(driver, "read_bindless_publication", return_value=dict(shaderPackageIdentity="unit-fixture-only")), \
                    mock.patch.object(driver.http.server, "ThreadingHTTPServer", return_value=server), \
                    mock.patch.object(driver.threading, "Thread"), \
                    mock.patch.object(driver.subprocess, "Popen", side_effect=intercept), self.assertRaises(BrowserLaunchIntercepted):
                driver.main()
            query = parse_qs(urlparse(commands[0][-1]).query, keep_blank_values=True)
            self.assertIn("original-root-sampled-execution-only", query)
            self.assertNotIn("general-finite-execution", query)
            self.assertNotIn("general-finite-lifecycle", query)
            self.assertEqual(urlparse(commands[0][-1]).path, "/bindless_webgpu_original_root_runner.html")
            server.shutdown.assert_called_once()
            server.server_close.assert_called_once()

    def test_geometry_execution_selector_requires_original_root_and_routes_independently(self):
        self.refuses(["--original-root-geometry-execution-only"])
        self.refuses(["--original-bindless-root-preparation", "--original-root-geometry-execution-only", "--original-root-sampled-execution-only"])
        self.refuses(["--original-bindless-root-preparation", "--original-root-geometry-execution-only", "--original-root-wocc-ibl"])
        with tempfile.TemporaryDirectory() as directory:
            args = [str(DRIVER), "--browser", "unit-fixture-no-browser", "--runner", "unit-fixture-no-runner",
                    "--output", directory, "--original-bindless-root-preparation",
                    "--original-root-geometry-execution-only", "--wasm-baseline-only"]
            commands = []
            def intercept(command, **unused):
                commands.append(command)
                raise BrowserLaunchIntercepted()
            server = mock.Mock(server_port=12345)
            with mock.patch.object(driver.sys, "argv", args), \
                    mock.patch.object(driver, "read_bindless_publication", return_value=dict(shaderPackageIdentity="unit-fixture-only")), \
                    mock.patch.object(driver.http.server, "ThreadingHTTPServer", return_value=server), \
                    mock.patch.object(driver.threading, "Thread"), \
                    mock.patch.object(driver.subprocess, "Popen", side_effect=intercept), self.assertRaises(BrowserLaunchIntercepted):
                driver.main()
            query = parse_qs(urlparse(commands[0][-1]).query, keep_blank_values=True)
            self.assertIn("original-root-geometry-execution-only", query)
            self.assertNotIn("general-finite-execution", query)
            self.assertNotIn("general-finite-lifecycle", query)
            self.assertEqual(urlparse(commands[0][-1]).path, "/bindless_webgpu_original_root_runner.html")
            server.shutdown.assert_called_once()
            server.server_close.assert_called_once()

    def test_implicit_parity_requires_an_original_sampled_or_array_cohort(self):
        self.refuses(["--original-root-implicit-parity"])
        self.refuses(["--original-bindless-root-preparation", "--original-root-implicit-parity"])

    def test_implicit_parity_loads_sealed_reference_before_launch(self):
        for domain in ("array", "sampled"):
            with self.subTest(domain=domain), tempfile.TemporaryDirectory() as directory:
                args = [str(DRIVER), "--browser", "unit-fixture-no-browser", "--runner", "unit-fixture-no-runner",
                        "--output", directory, "--original-bindless-root-preparation", "--original-root-implicit-parity",
                        "--original-root-" + domain + "-execution-only", "--wasm-baseline-only"]
                commands = []
                def intercept(command, **unused):
                    commands.append(command)
                    raise BrowserLaunchIntercepted()
                server = mock.Mock(server_port=12345)
                with mock.patch.object(driver.sys, "argv", args), \
                        mock.patch.object(driver, "read_bindless_publication", return_value=dict(shaderPackageIdentity="unit-fixture-only")), \
                        mock.patch.object(driver, "load_reference_contract", return_value={}) as load, \
                        mock.patch.object(driver.http.server, "ThreadingHTTPServer", return_value=server), \
                        mock.patch.object(driver.threading, "Thread"), \
                        mock.patch.object(driver.subprocess, "Popen", side_effect=intercept), self.assertRaises(BrowserLaunchIntercepted):
                    driver.main()
                load.assert_called_once_with(pathlib.Path("unit-fixture-no-runner"), domain == "array")
                query = parse_qs(urlparse(commands[0][-1]).query, keep_blank_values=True)
                self.assertIn("original-root-implicit-parity", query)
                self.assertIn("original-root-" + domain + "-execution-only", query)

    def test_scoped_execution_selectors_require_original_root_and_route_independently(self):
        for domain in ("array", "composed", "storage"):
            selector="--original-root-"+domain+"-execution-only"
            self.refuses([selector])
            self.refuses(["--original-bindless-root-preparation", selector,
                          "--original-root-sampled-execution-only"])
            with tempfile.TemporaryDirectory() as directory:
                args = [str(DRIVER), "--browser", "unit-fixture-no-browser", "--runner", "unit-fixture-no-runner",
                        "--output", directory, "--original-bindless-root-preparation",
                        selector, "--wasm-baseline-only"]
                commands = []
                def intercept(command, **unused):
                    commands.append(command)
                    raise BrowserLaunchIntercepted()
                server = mock.Mock(server_port=12345)
                with mock.patch.object(driver.sys, "argv", args), \
                        mock.patch.object(driver, "read_bindless_publication", return_value=dict(shaderPackageIdentity="unit-fixture-only")), \
                        mock.patch.object(driver.http.server, "ThreadingHTTPServer", return_value=server), \
                        mock.patch.object(driver.threading, "Thread"), \
                        mock.patch.object(driver.subprocess, "Popen", side_effect=intercept), self.assertRaises(BrowserLaunchIntercepted):
                    driver.main()
                query = parse_qs(urlparse(commands[0][-1]).query, keep_blank_values=True)
                self.assertIn(selector[2:], query)
                self.assertNotIn("original-root-sampled-execution-only", query)
                self.assertNotIn("general-finite-execution", query)
                self.assertEqual(urlparse(commands[0][-1]).path, "/bindless_webgpu_original_root_runner.html")
                server.shutdown.assert_called_once()
                server.server_close.assert_called_once()

if __name__ == "__main__":
    unittest.main()
