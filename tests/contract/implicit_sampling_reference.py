"""Sealed ordinary derivative oracle; never an RHI execution capability."""
import hashlib
import math
import pathlib
import re

import yaml
from spirv_layout import SpirvLayoutFacts


def numeric_values_match(values, expected, tolerance):
    return (isinstance(values, list) and len(values) == len(expected)
            and all(type(value) in (int, float) and math.isfinite(value)
                    and abs(value - wanted) <= tolerance * (1 + abs(wanted))
                    for value, wanted in zip(values, expected)))


def validate_numeric_reference_result(result, contract, *, key, prefix, expected,
                                      pixels, tolerance, required_flags):
    """Join independent GPU output to CPU values and actual public Root receipts."""
    reference = result.get(key, {})
    if not isinstance(reference, dict) or not isinstance(result.get("progress"), list):
        return False
    values, actual_pixels = reference.get("values"), reference.get("pixels")
    if not (all(reference.get(k) == v for k, v in contract.items())
            and all(reference.get(k) is True for k in ("success", *required_flags))
            and reference.get("publicExecutionGranted") is False and reference.get("nativeErrors") == []
            and reference.get("cleanRootReceipts") == 2 and reference.get("cleanPixelReceipts") == 2
            and numeric_values_match(values, expected, tolerance)
            and isinstance(actual_pixels, list) and len(actual_pixels) == len(pixels)
            and all(type(byte) is int and 0 <= byte <= 255 for byte in actual_pixels)
            and all(abs(value - wanted) <= 1 for value, wanted in zip(actual_pixels, pixels))):
        return False
    messages = [item.get("message", "") for item in result["progress"] if isinstance(item, dict)]
    for kind, actual in (("VALUES", values), ("PIXELS", actual_pixels)):
        start = prefix + kind + " fault0 "
        receipts = [message[len(start):] for message in messages if message.startswith(start)]
        if len(receipts) != 2:
            return False
        try:
            for receipt in receipts:
                if kind == "VALUES":
                    if not numeric_values_match(list(map(float, receipt.split())), actual, tolerance):
                        return False
                elif list(map(int, receipt.split())) != actual:
                    return False
        except (TypeError, ValueError, OverflowError):
            return False
    return True


def load_reference_contract(runner, array):
    name = "ordinary_implicit_array" if array else "ordinary_implicit_2d"
    publication = load_sealed_reference(runner, name)
    return {"type": "BindlessImplicitSamplingReference", "contractVersion": 1,
            "dimension": "Texture2DArray" if array else "Texture2D",
            "shaderSha256": publication["files"]["wgsl"]["sha256"]}


def load_sealed_reference(runner, name, implicit_samples=2, require_discard=False):
    """Authenticate combined and child compiler roles before accepting an oracle."""
    folder = pathlib.Path(runner) / "bindless-implicit-reference" / name
    publication = yaml.safe_load((folder / (name + ".graphics.module.yaml")).read_text(encoding="utf-8"))

    def roles(base, node):
        if node["matrixLayout"] != "row-major":
            raise ValueError("IMPLICIT-REFERENCE-MATRIX")
        paths = {}
        for role in ("source", "spirv", "metal", "wgsl", "reflection"):
            record = node["files"][role]
            path = base / record["path"]
            if path.parent != base or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != record["sha256"]:
                raise ValueError("IMPLICIT-REFERENCE-FILE-HASH")
            paths[role] = path
        return paths

    if publication["type"] != "GraphicsShaderModuleArtifact" or publication["contractVersion"] != 1:
        raise ValueError("IMPLICIT-REFERENCE-PUBLICATION")
    paths = roles(folder, publication)
    if [entry["stage"] for entry in publication["entries"]] != ["Vertex", "Fragment"]:
        raise ValueError("IMPLICIT-REFERENCE-STAGES")
    for entry in publication["entries"]:
        reference = entry["publication"]
        child_path = folder / reference["path"]
        if child_path.parent != folder / entry["stage"] or hashlib.sha256(child_path.read_bytes()).hexdigest() != reference["sha256"]:
            raise ValueError("IMPLICIT-REFERENCE-CHILD-HASH")
        child = yaml.safe_load(child_path.read_text(encoding="utf-8"))
        if child["type"] != "SingleEntryShaderArtifact" or child["contractVersion"] != 3 or child["entry"] != entry["entry"] or child["stage"] != entry["stage"]:
            raise ValueError("IMPLICIT-REFERENCE-CHILD")
        child_roles = roles(child_path.parent, child)
        if child_roles["source"].read_bytes() != paths["source"].read_bytes():
            raise ValueError("IMPLICIT-REFERENCE-CHILD-SOURCE")
        if entry["stage"] == "Fragment":
            verify_derivatives(child_roles, implicit_samples, require_discard)
    verify_derivatives(paths, implicit_samples, require_discard)
    return publication


def verify_derivatives(paths, implicit_samples=2, require_discard=False):
    wgsl = paths["wgsl"].read_text(encoding="utf-8")
    implicit = re.findall(r"\btextureSample\s*\(", wgsl)
    if (len(implicit) != implicit_samples or re.search(r"textureSampleGrad|diagnostic\s*\(\s*off", wgsl)
            or require_discard and re.search(r"\btextureSampleLevel\s*\(", wgsl)):
        raise ValueError("IMPLICIT-REFERENCE-DERIVATIVES")
    # SPIR-V core OpImageSampleImplicitLod=87. The independent target agrees
    # with preserved WGSL; explicit SampleLevel controls remain separate.
    instructions = SpirvLayoutFacts(paths["spirv"]).instructions
    if sum(opcode == 87 for _, opcode, _ in instructions) != implicit_samples:
        raise ValueError("IMPLICIT-REFERENCE-SPIRV-DERIVATIVES")
    # OpKill=252 is the compiler's authored fragment discard, not an alpha
    # predicate merely transported to a numeric output.
    if require_discard and (not re.search(r"\bdiscard\s*;", wgsl)
                            or not any(opcode == 252 for _, opcode, _ in instructions)):
        raise ValueError("IMPLICIT-REFERENCE-DISCARD")


def validate_reference_result(result, contract):
    reference = result.get("implicitSamplingReference", {})
    if not isinstance(reference, dict) or not isinstance(result.get("progress"), list):
        return False
    pixels = reference.get("pixels")
    prefix = "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_" + ("ARRAY" if contract["dimension"] == "Texture2DArray" else "SAMPLED") + "_PIXELS fault0 rgba "
    messages = [item.get("message", "") for item in result.get("progress", []) if isinstance(item, dict)]
    matching = [message for message in messages if message.startswith(prefix)]
    return (all(reference.get(key) == value for key, value in contract.items())
        and reference.get("success") is True and reference.get("realImplicitDerivatives") is True
        and reference.get("mipZeroSubstitutionRejected") is True
        and reference.get("publicExecutionGranted") is False and reference.get("nativeErrors") == []
        and reference.get("cleanRootReceipts") == 2 and isinstance(pixels, list) and len(pixels) == 16
        and all(type(byte) is int and 0 <= byte <= 255 for byte in pixels)
        and len(matching) == 2 and all(message == prefix + " ".join(map(str, pixels)) for message in matching)
        and pixels[2] != 32 + 3 * 4 + 1
        and (contract["dimension"] != "Texture2DArray" or pixels[10] != 32 + 16 + 3 * 4 + 1))
