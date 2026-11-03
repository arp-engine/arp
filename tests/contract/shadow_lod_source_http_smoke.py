"""Actual staged v1/v2 ShadowLOD file reads against emitted formal fixtures."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/fixtures"))
from source_file_http_smoke import run

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="arp-shadow-source-") as folder:
        subprocess.run([sys.argv[1], "--emit", folder], check=True, timeout=15)
        expected = {"/source.glb": 80, "/lod/one.glb": 80, "/lod/two.glb": 80,
                    "/lod/cert.yaml": 80, "/lod/order.yaml": 40,
                    "/lod/manifest1.yaml": 40, "/lod/manifest2.yaml": 40}
        raise SystemExit(run(sys.argv[1], folder, expected))
