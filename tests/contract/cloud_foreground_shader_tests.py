"""CloudForeground/1 actual selected-entry ABI, independent of target language."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from spirv_layout import SpirvLayoutFacts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, required=True)
    root = parser.parse_args().directory
    for stage in ("Vertex", "Fragment"):
        path = root / stage / "cloud_foreground"
        facts = SpirvLayoutFacts(path.with_suffix(".spv"))
        assert not facts.push_constant_layouts()
        layouts = facts.buffer_layouts()
        if stage == "Fragment":
            assert set(layouts) == {(0, 0)}, layouts
            assert layouts[(0, 0)]["staticOccupiedExtentBytes"] == 16, layouts
        else:
            assert not layouts, layouts
        for extension in (".metal", ".wgsl", ".reflection.yaml"):
            assert path.with_suffix(extension).stat().st_size > 0
    print("CloudForeground/1: vertex-ID geometry, fragment uniform16, no push constants, three targets")


if __name__ == "__main__":
    main()
