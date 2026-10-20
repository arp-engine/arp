"""Retain actual compiler-owned producer packages for native cold tests."""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
from bindless_codegen import (compile_bindless_package, zero_base_indirect_producer_schema,
    zero_base_indirect_consumer_schema, _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--slangc", required=True)
    parser.add_argument("--spirv-reflect", required=True)
    args = parser.parse_args()
    for schema, entries in ((zero_base_indirect_producer_schema(), ["arpZeroBaseIndirectMain"]),
            (zero_base_indirect_consumer_schema(), _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES)):
        for mode in ("Native", "Emulated"):
            compile_bindless_package(schema, None, entries, mode, args.output, args.slangc, args.spirv_reflect)
