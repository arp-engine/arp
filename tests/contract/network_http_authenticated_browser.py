"""Actual browser CORS and WASM transfer checks in an owned session, with no GPU."""
import argparse
import functools
from http.server import SimpleHTTPRequestHandler
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from urllib.parse import urlencode

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests/fixtures"))
from browser_contract_runner import (BrowserContractRunner, create_browser_http_server,
                                    retire_browser_process, wait_for_browser_result)
from http_authenticated_fixture import create_authenticated_server


def run(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report, ready = {}, threading.Event()

    class Handler(SimpleHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def end_headers(self):
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
            super().end_headers()

        def do_POST(self):
            if self.path != "/result":
                self.send_error(404)
                return
            size = int(self.headers.get("Content-Length", "0"))
            if not 0 < size <= 64 * 1024:
                self.send_error(413)
                return
            report.update(json.loads(self.rfile.read(size)))
            self.send_response(200)
            self.send_header("Content-Length", "0")
            self.end_headers()
            ready.set()

    files = create_browser_http_server(functools.partial(Handler, directory=str(args.directory.resolve())))
    service = create_authenticated_server(create_browser_http_server)
    service.allowed_origin = f"http://127.0.0.1:{files.server_port}"
    workers = [threading.Thread(target=s.serve_forever, daemon=True) for s in (files, service)]
    for worker in workers:
        worker.start()
    profile = tempfile.TemporaryDirectory(prefix="arp-bearer-transfer-")
    browser = None
    try:
        origin = f"http://127.0.0.1:{service.server_port}"
        url = service.allowed_origin + "/arp_network_http_authenticated_fetch_smoke.html?" + urlencode({"origin": origin})
        runner = BrowserContractRunner(args.browser, args.directory, "", output, 45,
                                       disable_background_timer_throttling=True)
        with (output / "browser.log").open("w", encoding="utf-8") as log:
            browser = subprocess.Popen(runner.browser_command(profile.name, url), stdout=log, stderr=log,
                                       creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
            if not wait_for_browser_result(ready, browser, profile.name, 45,
                                           crash_output=output / "crashes"):
                raise RuntimeError("AUTHENTICATED-FETCH-BROWSER-TIMEOUT")
        assert report.get("success") is True, report
        assert report.get("cycles") == 20 and report.get("coreLiveBytesDelta") == 0
        assert report.get("coreLiveAllocationsDelta") == 0 and report.get("pendingFetches") == 0
        assert not service.redirect_followed and service.preflights > 0
        assert 26 <= service.posts <= 27, service.posts
        print("PASS real browser: cross-origin preflight, exact target, Bearer JSON POST and ordinary 20 cycles")
    finally:
        if browser:
            retire_browser_process(browser, profile)
        else:
            profile.cleanup()
        for server in (files, service):
            server.shutdown()
            server.server_close()
        for worker in workers:
            worker.join()
        report.update(preflights=service.preflights, posts=service.posts,
                      redirectFollowed=service.redirect_followed)
        (output / "report.yaml").write_text(yaml.safe_dump(report, sort_keys=False), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser", required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
