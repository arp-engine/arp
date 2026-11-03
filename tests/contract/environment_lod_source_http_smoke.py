"""Exact version-pinned Plant/Rock prefix acquisition through engine HTTP."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/fixtures"))
from source_file_http_smoke import run

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="arp-environment-lod-source-") as folder:
        subprocess.run([sys.argv[1], "--emit", folder], check=True, timeout=15)
        expected = {f"/{kind}/{file}.glb": count for kind in ("plants", "rocks")
                    for file, count in (("close", 80), ("distant", 40), ("shared", 40))}
        raise SystemExit(run(sys.argv[1], folder, expected))
