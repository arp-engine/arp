"""Deletion uses shared original artifact fixture; new input never reopens files."""
import argparse
import pathlib
from original_runtime_length_test import delete_and_hot


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--fixture-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    for variant in ("structured", "rwstructured", "byteaddress", "rwbyteaddress"):
        delete_and_hot(args.executable, args.fixture_dir / variant / ("runtime_length_"+variant+".computeMain.entry.yaml"))
    delete_and_hot(args.executable, args.fixture_dir / "graphics/runtime_length_graphics.graphics.module.yaml")
    print("OriginalAvailableTargetRuntimeLengthInput/1: deleted5 caller-drop owner/quota/policy retained")


if __name__ == "__main__":
    main()
