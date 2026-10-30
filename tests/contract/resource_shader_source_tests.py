"""Finite copies of real generated artifacts, acquired through memory or HTTP."""
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


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.observed.append(self.path)
        super().do_GET()


def prepare(root, graphics, webgl, depth=None):
    shutil.copytree(graphics / "Vertex", root / "single")
    shutil.copytree(graphics, root / "graphics")
    shutil.copytree(webgl, root / "webgl")
    if depth:
        shutil.copytree(depth, root / "depth")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--graphics", type=pathlib.Path, required=True)
    parser.add_argument("--webgl", type=pathlib.Path, required=True)
    parser.add_argument("--depth", type=pathlib.Path)
    parser.add_argument("--http", action="store_true")
    args = parser.parse_args()
    executable = str(pathlib.Path(args.executable).resolve())
    with tempfile.TemporaryDirectory(prefix="arp-shader-source-") as folder:
        root = pathlib.Path(folder)
        prepare(root, args.graphics, args.webgl, args.depth)
        if not args.http:
            subprocess.run([executable, "--memory", folder], check=True, timeout=150)
            return 0
        server = create_browser_http_server(functools.partial(Handler, directory=folder))
        server.observed = []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            # The executing child receives only a URL, never the fixture root.
            subprocess.run([executable, "--http", f"http://127.0.0.1:{server.server_port}/"],
                           check=True, timeout=150)
            publications = ["/single/ordinary_graphics_storage_read.vertexMain.entry.yaml",
                            "/graphics/ordinary_graphics_storage_read.graphics.module.yaml",
                            "/webgl/webgl1_graphics.webgl-program.yaml"]
            if args.depth:
                publications.append("/depth/original-depth-table.expansion.yaml")
            starts = [path for path in server.observed if path in publications]
            assert starts == [publications[i % len(publications)] for i in range(20)] * 2, starts
            assert server.observed[0] == publications[0]
            assert any(path.endswith(".spv") for path in server.observed)
            assert any(path.endswith(".metal") for path in server.observed)
            assert any(path.endswith(".wgsl") for path in server.observed)
            assert any(path.endswith(".glsl-es-100") for path in server.observed)
            assert any(path.startswith("/graphics/Vertex/") for path in server.observed)
            assert any(path.startswith("/graphics/Fragment/") for path in server.observed)
            print(f"Real shader HTTP: 40 publication-first cycles, {len(server.observed)} requests PASS")
        finally:
            server.shutdown()
            server.server_close()
            worker.join()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
