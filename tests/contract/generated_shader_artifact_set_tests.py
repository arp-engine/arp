"""Run real compiler output after relocation, or through the local HTTP service."""
import argparse
import functools
import http.server
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from browser_contract_runner import create_browser_http_server
import yaml


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.observed.append(self.path)
        super().do_GET()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--fixture", type=pathlib.Path, required=True)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--http", action="store_true")
    args = parser.parse_args()
    publication = "ordinary_graphics_storage_read.artifact-set.yaml"
    names = [publication]
    manifest = yaml.safe_load((args.fixture / publication).read_text(encoding="utf-8"))
    names += [file["path"] for file in manifest["files"].values() if "path" in file]
    with tempfile.TemporaryDirectory(prefix="arp-generated-set-") as folder:
        root = pathlib.Path(folder)
        for name in names:
            assert pathlib.Path(name).name == name
            shutil.copy2(args.fixture / name, root / name)
        extra = ["--quick"] if args.quick else []
        executable = str(pathlib.Path(args.executable).resolve())
        if not args.http:
            subprocess.run([executable, "--memory", folder, *extra], check=True, timeout=150)
            return
        server = create_browser_http_server(functools.partial(Handler, directory=folder))
        server.observed = []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            subprocess.run([executable, "--http", f"http://127.0.0.1:{server.server_port}/", *extra], check=True, timeout=150)
            cycles = 1 if args.quick else 40
            assert len(server.observed) == cycles * len(names), server.observed
            assert all(server.observed.count("/" + name) == cycles for name in names)
            assert server.observed[::len(names)] == ["/" + publication] * cycles
            print(f"Real HTTP: {cycles} cycles, {len(server.observed)} GETs PASS")
        finally:
            server.shutdown()
            server.server_close()
            worker.join()


if __name__ == "__main__":
    main()
