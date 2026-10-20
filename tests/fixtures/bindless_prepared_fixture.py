"""Deterministic shared Native/Emulated fixture for prepared L2 bindings."""
import argparse
import pathlib
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from bindless_codegen import compile_bindless_package


class PreparedBindingFixture:
    def __init__(self):
        self.schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_variant.yaml").read_text())
        inputs = [table for table in self.schema["tables"] if table["name"] == "Inputs"]
        if len(inputs) != 1 or inputs[0]["kind"] != "ReadOnlyStorageBuffer":
            raise ValueError("Prepared fixture requires exactly one named Inputs storage table")
        inputs[0]["stages"] = ["vertex", "compute"]
        self.entries = ["computeMain", "fragmentMain", "vertexMain"]

    def write_source(self, output):
        output = pathlib.Path(output)
        output.mkdir(parents=True, exist_ok=True)
        source = output / "graphics_fixture.slang"
        source.write_text("".join((ROOT / "tests/fixtures" / name).read_text(encoding="utf-8")
            for name in ("bindless_access.slang", "bindless_vertex_buffer.slang")), encoding="utf-8")
        return source

    def compile(self, mode, output, slangc, reflect):
        return compile_bindless_package(self.schema, self.write_source(output), self.entries,
            mode, output, slangc, reflect)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--slangc", required=True)
    parser.add_argument("--spirv-reflect", required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    fixture = PreparedBindingFixture()
    for mode in ("Native", "Emulated"):
        print(fixture.compile(mode, args.output, args.slangc, args.spirv_reflect))
