"""Exercise real 2D graph acquisition against authored files."""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/fixtures"))
from source_file_http_smoke import run

if __name__ == "__main__":
    expected = {f"/{name}": 40 for name in (
        "atlas.yaml", "atlas_b.yaml", "atlas.png", "tileset.yaml", "tilemap.yaml")}
    expected["/atlas.png"] = 80
    raise SystemExit(run(sys.argv[1], ROOT / "tests/fixtures/scene_2d", expected))
