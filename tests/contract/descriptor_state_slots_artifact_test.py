"""Verify the two logical slots through the canonical Slang publication."""
import argparse
import hashlib
import pathlib
import sys
import yaml

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
from spirv_layout import SpirvLayoutFacts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture-dir", type=pathlib.Path, required=True)
    root = parser.parse_args().fixture_dir
    publication = yaml.safe_load((root / "descriptor_state_slots.computeMain.entry.yaml").read_text())
    assert publication["type"] == "SingleEntryShaderArtifact" and publication["matrixLayout"] == "row-major"
    for role in ("source", "spirv", "metal", "wgsl", "reflection"):
        record = publication["files"][role]
        data = (root / record["path"]).read_bytes()
        assert data and hashlib.sha256(data).hexdigest() == record["sha256"]
    reflection = yaml.safe_load((root / publication["files"]["reflection"]["path"]).read_text())
    assert {(b["set"], b["binding"], b["type"], b["arrayCount"], b["stageMask"]) for b in reflection["bindings"]} == {
        (0, 0, "ReadOnlyStorageBuffer", 1, 4), (1, 0, "StorageBuffer", 1, 4)}
    assert {(b["set"], b["binding"], b["group"], b["webGpuBinding"]) for b in reflection["webGpuBindingSpace"]} == {(0, 0, 0, 0), (1, 0, 1, 0)}
    facts = SpirvLayoutFacts(root / publication["files"]["spirv"]["path"])
    assert {(facts.decoration(target, 34), facts.decoration(target, 33))
            for target, decoration in facts.decorations if decoration == 33} == {(0, 0), (1, 0)}
    print("DESCRIPTOR_STATE_SLOTS_ARTIFACT_OK Slang SPIR-V MSL WGSL reflection logical-sets2 scalar-bindings2")


if __name__ == "__main__":
    main()
