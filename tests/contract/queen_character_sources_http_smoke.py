"""Actual engine HTTP/FileService acquisition of the declared Queen corpus."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/fixtures'))
from source_file_http_smoke import run


if __name__ == '__main__':
    # One complete prewarm plus twenty measured content lifecycles.
    raise SystemExit(run(sys.argv[1], sys.argv[2],
        {'/character_manifest.yaml': 21, '/Casual.glb': 21, '/Formal.glb': 21}, timeout=240))
