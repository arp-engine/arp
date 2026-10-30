"""Serve the exact generated package set; the HTTP child receives only a URL."""
import argparse
import functools
import http.server
from pathlib import Path
import subprocess
import sys
import threading

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from browser_contract_runner import create_browser_http_server


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.observed.append(self.path)
        super().do_GET()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--http", action="store_true")
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()
    if not args.http:
        subprocess.run([str(args.executable.resolve()), "--memory", str(args.fixture.resolve()),
                        *(["--quick"] if args.quick else [])],
                       check=True, timeout=180)
        return
    expected = []
    for mode in ("native", "emulated"):
        publication = f"BindlessFixture.{mode}.bindless.yaml"
        envelope = yaml.safe_load((args.fixture / publication).read_text(encoding="utf-8"))
        payload = yaml.safe_load((args.fixture / envelope["payload"]).read_text(encoding="utf-8"))
        expected.append({"/" + name for name in
                         [publication, envelope["payload"], *payload["artifacts"].keys()]})
    server = create_browser_http_server(functools.partial(Handler, directory=str(args.fixture)))
    server.observed = []
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        subprocess.run([str(args.executable.resolve()), "--http",
                        f"http://127.0.0.1:{server.server_port}/"], check=True, timeout=180)
        index = 0
        for _ in range(40):
            for paths in expected:
                observed = server.observed[index:index + len(paths)]
                assert len(observed) == len(paths) and set(observed) == paths, observed
                index += len(paths)
        assert index == len(server.observed), server.observed[index:]
        print(f"Bindless package HTTP: {index} real GETs; exactly one per declared file per cycle")
    finally:
        server.shutdown()
        server.server_close()
        worker.join()


if __name__ == "__main__":
    main()
