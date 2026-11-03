"""Use the C++ producer fixture and actual engine HTTP to load full GLB assets."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/fixtures"))
from source_file_http_smoke import run

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="arp-mesh-glb-source-") as folder:
        subprocess.run([sys.argv[1], "--emit", str(pathlib.Path(folder) / "model.glb")],
                       check=True, timeout=15)
        raise SystemExit(run(sys.argv[1], folder, {"/model.glb": 40}))
