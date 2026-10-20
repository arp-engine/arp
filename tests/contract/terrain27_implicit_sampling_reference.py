"""Independent Traditional Terrain27 evidence, never an execution capability."""
import argparse
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests/tools"))
from bindless_graphics_readback_oracles import Terrain27Oracle
from implicit_sampling_reference import load_sealed_reference, validate_numeric_reference_result


def canonical_inputs():
    oracle = Terrain27Oracle()
    return {"type": "Terrain27ImplicitReferenceInputs", "contractVersion": 1,
            "ordinary": [list(oracle.ordinary_buffers[size]) for size in (96, 16)],
            "textureMips": [[list(b"".join(layer[mip] for layer in image)) for mip in range(2)]
                            for image in oracle.texture_layers],
            "textureLayers": [len(image) for image in oracle.texture_layers],
            "expected": oracle.expected, "pixels": [64, 128, 191, 255] * 4}


def input_bytes():
    return (json.dumps(canonical_inputs(), sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def load_terrain27_reference_contract(runner):
    name = "ordinary_terrain27"
    publication = load_sealed_reference(runner, name, implicit_samples=27)
    path = pathlib.Path(runner) / "bindless-implicit-reference" / name / "inputs.json"
    if path.read_bytes() != input_bytes():
        raise ValueError("TERRAIN27-REFERENCE-INPUT")
    return {"type": "BindlessTerrain27ImplicitSamplingReference", "contractVersion": 1,
            "shaderSha256": publication["files"]["wgsl"]["sha256"],
            "inputSha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def validate_terrain27_reference_result(result, contract):
    inputs = canonical_inputs()
    return validate_numeric_reference_result(result, contract,
        key="terrain27ImplicitSamplingReference",
        prefix="BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_TERRAIN27_PARITY_",
        expected=inputs["expected"], pixels=inputs["pixels"], tolerance=Terrain27Oracle.tolerance,
        required_flags=("realImplicitDerivatives", "mipZeroSubstitutionRejected", "arrayLayersValidated"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--write-inputs", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.write_inputs.parent.mkdir(parents=True, exist_ok=True)
    args.write_inputs.write_bytes(input_bytes())
