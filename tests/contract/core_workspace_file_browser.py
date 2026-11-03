"""Actual browser FileService against the production native workspace host.

The isolated proxy only injects lost/corrupt responses after real host IO; it
does not parse assets or implement any production filesystem/backend behavior.
"""
import argparse
import functools
import hashlib
import http.client
from http.server import SimpleHTTPRequestHandler
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
from urllib.parse import urlencode

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from browser_contract_runner import (BrowserContractRunner, create_browser_http_server,
                                    retire_browser_process, wait_for_browser_result)

sys.path.insert(0, str(ROOT / "tests/fixtures"))
from workspace_fault_proxy import WorkspaceFaultProxy

READ_TOKEN = "read-01234567890123456789012345678901"
WRITE_TOKEN = "write-0123456789012345678901234567890"
ORIGINAL = b"\xef\xbb\xbf# saved\r\n"


def run(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report, ready = {}, threading.Event()

    class Files(SimpleHTTPRequestHandler):
        def log_message(self, *_): pass
        def handle(self):
            try: super().handle()
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass
        def end_headers(self):
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
            super().end_headers()
        def do_POST(self):
            if self.path != "/result": self.send_error(404); return
            size = int(self.headers.get("Content-Length", "0"))
            if not 0 < size <= 65536: self.send_error(413); return
            report.update(json.loads(self.rfile.read(size)))
            self.send_response(200); self.send_header("Content-Length", "0"); self.end_headers()
            ready.set()

    files = create_browser_http_server(functools.partial(Files, directory=str(args.directory.resolve())))
    proxy = WorkspaceFaultProxy()
    counts, replace_wires, attempts = proxy.counts, proxy.replace_wires, proxy.attempts
    workers = [threading.Thread(target=s.serve_forever, daemon=True) for s in (files, proxy)]
    profile = tempfile.TemporaryDirectory(prefix="arp-workspace-browser-")
    browser = host = None
    with tempfile.TemporaryDirectory(prefix="arp-workspace-actual-root-") as temporary:
        root = Path(temporary).resolve(strict=True)
        assert root.parent == Path(tempfile.gettempdir()).resolve(strict=True)
        for i in range(21):
            folder = root / f"cycle-{i}"; folder.mkdir()
            for name in ("rules.arp", "project.yaml", "scene.yaml", "changed.arp"):
                (folder / name).write_bytes(ORIGINAL)
        try:
            origin = f"http://127.0.0.1:{files.server_port}"
            host = subprocess.Popen([str(args.host.resolve(strict=True)), "--root", str(root),
                "--id", "fixture", "--origin", origin, "--write", "--port", "0"],
                cwd=root, env=dict(os.environ, ARP_WORKSPACE_READ_TOKEN=READ_TOKEN,
                                  ARP_WORKSPACE_WRITE_TOKEN=WRITE_TOKEN),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
            line = host.stdout.readline().strip()
            match = re.fullmatch(r"ARP workspace: http://127\.0\.0\.1:([0-9]+)/workspace/v2 id=fixture session=([a-z0-9-]+)", line)
            assert match, line
            proxy.upstream_port = int(match.group(1))
            for worker in workers: worker.start()
            runner = BrowserContractRunner(args.browser, args.directory, "", output, 120,
                                           disable_background_timer_throttling=True)
            url = origin + "/arp_core_workspace_file_fetch_smoke.html?" + urlencode({"origin": f"http://127.0.0.1:{proxy.server_port}"})
            with (output / "browser.log").open("w", encoding="utf-8") as log:
                browser = subprocess.Popen(runner.browser_command(profile.name, url), stdout=log, stderr=log,
                    creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
                if not wait_for_browser_result(ready, browser, profile.name, 120, crash_output=output / "crashes"):
                    raise RuntimeError("WORKSPACE-FETCH-BROWSER-TIMEOUT")
            assert report.get("success") is True, report
            assert report.get("cycles") == 20 and report.get("warmupCycles") == 1
            assert report.get("coreLiveBytesDelta") == 0 and report.get("coreLiveAllocationsDelta") == 0
            assert report.get("pendingFetches") == 0
            assert len(replace_wires) == 5 * 21 and counts["dropped"] >= 21 and counts["corrupted"] == 42
            assert counts["preflight"] > 0
            connection = http.client.HTTPConnection("127.0.0.1", proxy.upstream_port, timeout=10)
            try:
                connection.request("POST", "/workspace/v2", body=json.dumps({"type": "arp.workspace.request",
                    "contractVersion": 2, "workspace": "fixture", "operation": "info"}),
                    headers={"Content-Type": "application/json", "Authorization": "Bearer " + READ_TOKEN})
                response = connection.getresponse()
                assert response.status == 200
                statistics = json.loads(response.read(131072))
            finally: connection.close()
            assert statistics["saveSubmissions"] == 5 * 21 and statistics["confirmedCommits"] == 4 * 21
            report.update(logicalSaves=len(replace_wires), duplicateWireAttempts=counts["replace"] - len(replace_wires),
                maxWireAttemptsObserved=max(attempts.values()), serverSaveSubmissions=statistics["saveSubmissions"],
                serverConfirmedCommits=statistics["confirmedCommits"], duplicateWireIdentityVerified=True)
            for i in range(21):
                folder = root / f"cycle-{i}"
                assert (folder / "new.arp").read_bytes() == b"# updated\n"
                assert (folder / "lost.arp").read_bytes() == b"# lost response\n"
                assert (folder / "rules.arp").read_bytes() == ORIGINAL
                assert not (folder / "denied.arp").exists() and not (folder / "cancelled.arp").exists()
            print("PASS actual browser FileService + native host/disk: scopes, budgets, conditional saves, lost responses, receipt/release and ordinary20")
        finally:
            if browser: retire_browser_process(browser, profile)
            else: profile.cleanup()
            if host and host.poll() is None:
                host.terminate(); host.communicate(timeout=10)
            for server, worker in zip((files, proxy), workers):
                if worker.is_alive(): server.shutdown(); worker.join()
                server.server_close()
            report.update(counts=counts, fixtureHostStoppedByParent=True,
                          nativeHostLifecycleClaim=False, proxyPurpose="fault injection after actual production host IO")
            (output / "report.yaml").write_text(yaml.safe_dump(report, sort_keys=False), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser", required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())


if __name__ == "__main__": main()
