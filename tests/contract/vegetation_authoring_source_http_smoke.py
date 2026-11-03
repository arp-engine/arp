"""Read vegetation authoring metadata and strict GLB via real engine HTTP."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/fixtures"))
from source_file_http_smoke import run

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="arp-vegetation-source-") as folder:
        subprocess.run([sys.argv[1], "--emit", folder], check=True, timeout=15)
        raise SystemExit(run(sys.argv[1], folder, {"/authoring.yaml": 40, "/plant.glb": 40}))
