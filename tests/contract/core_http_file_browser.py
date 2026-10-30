"""Run actual Wasm FileService/Fetch against loopback in an owned browser session."""
import argparse
import functools
import json
import pathlib
import subprocess
import sys
import tempfile
import threading

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests/fixtures"))
from browser_contract_runner import (BrowserContractRunner, create_browser_http_server,
                                    retire_browser_process, wait_for_browser_result)
from http_file_fixture_server import HttpFileFixtureHandler


def run(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    ready = threading.Event()
    report = {}

    class Handler(HttpFileFixtureHandler):
        def end_headers(self):
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
            super().end_headers()

        def do_POST(self):
            if self.path != "/result":
                self.send_error(404)
                return
            size = int(self.headers.get("Content-Length", "0"))
            if size <= 0 or size > 64 * 1024:
                self.send_error(413)
                return
            report.update(json.loads(self.rfile.read(size)))
            self.send_response(200)
            self.send_header("Content-Length", "2")
            self.end_headers()
            self.wfile.write(b"ok")
            ready.set()

        def handle(self):
            try:
                super().handle()
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass # Expected peer teardown in the real cancellation fixtures.

    server = create_browser_http_server(functools.partial(Handler, directory=str(args.directory.resolve())))
    server.request_lines = []
    server.redirect_followed = False
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    profile = tempfile.TemporaryDirectory(prefix="arp-fetch-io-")
    browser = None
    try:
        runner = BrowserContractRunner(args.browser, args.directory, "", output, 45,
                                       disable_background_timer_throttling=True)
        url = f"http://127.0.0.1:{server.server_port}/arp_core_http_file_fetch_smoke.html"
        command = runner.browser_command(profile.name, url)
        with (output / "browser.log").open("w", encoding="utf-8") as log:
            browser = subprocess.Popen(command, stdout=log, stderr=log,
                creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
            if not wait_for_browser_result(ready, browser, profile.name, 45,
                                           crash_output=output / "crashes"):
                raise RuntimeError("IO-FETCH-BROWSER-TIMEOUT")
        assert report.get("success") is True, report
        assert not server.redirect_followed, "IO-FETCH-REDIRECT-WAS-FOLLOWED"
        assert report.get("cycles") == 20 and report.get("warmupCycles") == 20
        assert report.get("coreLiveBytesDelta") == 0 and report.get("coreLiveAllocationsDelta") == 0
        assert report.get("pendingFetches") == 0
        assert report.get("lateSettled") == 1 and report.get("lateAborted") == 1
        assert report.get("lateBodyCancelled") == 1
        assert any("%E8%B7%AF%E5%BE%84/a%20%23%25.bin" in line for line in server.request_lines)
        print("PASS real browser Fetch: byte/range/version/failure/cancel/remount/late-result and 20 ordinary cycles")
    finally:
        if browser:
            retire_browser_process(browser, profile)
        else:
            profile.cleanup()
        server.shutdown()
        server.server_close()
        worker.join()
        (output / "report.yaml").write_text(yaml.safe_dump({**report,
            "redirectFollowed": server.redirect_followed, "requests": server.request_lines},
            allow_unicode=True, sort_keys=False), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser", required=True)
    parser.add_argument("--directory", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
