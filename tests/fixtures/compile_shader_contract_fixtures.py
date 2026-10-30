"""Compile the explicit legacy test catalog through the production compiler."""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import compile_shaders


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--slangc", required=True)
    parser.add_argument("--spirv-reflect", required=True)
    args = parser.parse_args()
    compile_shaders.SHADER_INCLUDE_DIR = str(ROOT / "tests/fixtures")
    compile_shaders.SHADER_SOURCES = [
        ("RhiPushContract", "rhi_push_contract", "rhi_push_contract.slang", ["vsMain", "fsMain", "csMain"]),
        ("RhiPipelineContract", "rhi_pipeline_contract", "rhi_pipeline_contract.slang", ["vsMain", "fsMain", "csMain"]),
        ("NullHiddenFrame", "null_hidden_frame_smoke", "null_hidden_frame_smoke.slang", ["vsMain", "fsMain"]),
    ]
    # Explicit authored push profiles, using the same target lowering and
    # validation policies as the production compiler catalog.
    compile_shaders.WGSL_PUSH_CONSTANT_SHADERS.update({"rhi_push_contract", "rhi_pipeline_contract", "null_hidden_frame_smoke"})
    for module in compile_shaders.SHADER_SOURCES:
        sys.argv = [compile_shaders.__file__, "--module", module[1], "--out-dir", args.output,
                    "--slangc", args.slangc, "--spirv-reflect", args.spirv_reflect]
        compile_shaders.main()


if __name__ == "__main__":
    main()
