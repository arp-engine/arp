"""Test evidence for five-role Traditional/Root parity, never an execution grant."""
import argparse
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests/tools"))
from bindless_graphics_readback_oracles import PbrMaterialOracle
from implicit_sampling_reference import load_sealed_reference, numeric_values_match, validate_numeric_reference_result


def canonical_inputs():
    oracle = PbrMaterialOracle()
    return {"type": "PbrImplicitReferenceInputs", "contractVersion": 2,
            "ordinary": [list(oracle.ordinary_buffers[size]) for size in (288, 16)],
            "textureMips": [[list(mip) for mip in image] for image in oracle.texture_mips],
            "expected": oracle.expected, "pixels": oracle.color_pixels}


def input_bytes():
    return (json.dumps(canonical_inputs(), sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def load_pbr_reference_contract(runner):
    name = "ordinary_pbr_material"
    publication = load_sealed_reference(runner, name, implicit_samples=12, require_discard=True)
    path = pathlib.Path(runner) / "bindless-implicit-reference" / name / "inputs.json"
    if path.read_bytes() != input_bytes():
        raise ValueError("PBR-REFERENCE-INPUT")
    return {"type": "BindlessPbrImplicitSamplingReference", "contractVersion": 2,
            "shaderSha256": publication["files"]["wgsl"]["sha256"],
            "inputSha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def close_values(values, expected):
    return numeric_values_match(values, expected, PbrMaterialOracle.tolerance)


def validate_pbr_reference_result(result, contract):
    oracle = PbrMaterialOracle()
    if not validate_numeric_reference_result(result, contract,
            key="pbrImplicitSamplingReference",
            prefix="BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PBR_PARITY_",
            expected=oracle.expected, pixels=oracle.color_pixels, tolerance=oracle.tolerance,
            required_flags=("realImplicitDerivatives", "actualDiscardValidated", "distinctFiltersValidated")):
        return False
    values = result["pbrImplicitSamplingReference"]["values"]
    return all(abs(values[p*32+24] - values[p*32+25]) > .01 for p in range(4))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--write-inputs", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.write_inputs.parent.mkdir(parents=True, exist_ok=True)
    args.write_inputs.write_bytes(input_bytes())
