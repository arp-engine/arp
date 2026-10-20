"""Sealed Traditional PBR reference and hostile readback evidence."""
import argparse
import copy
import hashlib
import pathlib
import shutil
import sys
import tempfile
import unittest

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from pbr_implicit_sampling_reference import (
    canonical_inputs, load_pbr_reference_contract, validate_pbr_reference_result,
)


class PbrReferenceTests(unittest.TestCase):
    def evidence(self):
        contract = load_pbr_reference_contract(RUNNER)
        inputs = canonical_inputs()
        reference = dict(contract, success=True, realImplicitDerivatives=True,
                         actualDiscardValidated=True, distinctFiltersValidated=True,
                         publicExecutionGranted=False, nativeErrors=[],
                         cleanRootReceipts=2, cleanPixelReceipts=2,
                         values=inputs["expected"], pixels=inputs["pixels"])
        messages = []
        for kind, values in (("VALUES", reference["values"]), ("PIXELS", reference["pixels"])):
            message = "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PBR_PARITY_" + kind + " fault0 " + " ".join(map(str, values))
            messages.extend([dict(message=message), dict(message=message)])
        return contract, dict(pbrImplicitSamplingReference=reference, progress=messages)

    def test_real_cross_target_reference_and_canonical_input(self):
        contract, result = self.evidence()
        self.assertEqual(contract["contractVersion"], 2)
        self.assertTrue(validate_pbr_reference_result(result, contract))

    def test_missing_extra_or_changed_root_receipts_refuse(self):
        contract, result = self.evidence()
        for mutation in (lambda r: r["progress"].pop(),
                         lambda r: r["progress"].append(r["progress"][0]),
                         lambda r: r["progress"][0].update(message=r["progress"][0]["message"] + " 0"),
                         lambda r: r["progress"][0].update(message=r["progress"][0]["message"].replace("fault0", "fault1"))):
            altered = copy.deepcopy(result)
            mutation(altered)
            self.assertFalse(validate_pbr_reference_result(altered, contract))

    def test_wrong_values_pixels_hashes_and_capability_refuse(self):
        contract, result = self.evidence()
        for field, value in (("shaderSha256", "0"*64), ("inputSha256", "0"*64),
                             ("nativeErrors", ["validation"]), ("publicExecutionGranted", True),
                             ("realImplicitDerivatives", False), ("actualDiscardValidated", False),
                             ("distinctFiltersValidated", False), ("success", False)):
            altered = copy.deepcopy(result)
            altered["pbrImplicitSamplingReference"][field] = value
            self.assertFalse(validate_pbr_reference_result(altered, contract), field)
        for lane, value in ((0, 9), (25, result["pbrImplicitSamplingReference"]["values"][24]), (127, float("nan"))):
            altered = copy.deepcopy(result)
            altered["pbrImplicitSamplingReference"]["values"][lane] = value
            self.assertFalse(validate_pbr_reference_result(altered, contract))

    def test_matching_but_incorrect_gpu_outputs_still_refuse(self):
        contract, result = self.evidence()
        for kind, lane, value in (("values", 25, result["pbrImplicitSamplingReference"]["values"][24]),
                                  ("pixels", 4, 64), ("pixels", 7, 0)):
            altered = copy.deepcopy(result)
            altered["pbrImplicitSamplingReference"][kind][lane] = value
            prefix = "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PBR_PARITY_" + ("VALUES" if kind == "values" else "PIXELS") + " fault0 "
            for receipt in altered["progress"]:
                if receipt["message"].startswith(prefix):
                    receipt["message"] = prefix + " ".join(map(str, altered["pbrImplicitSamplingReference"][kind]))
            self.assertFalse(validate_pbr_reference_result(altered, contract))

    def test_mutated_input_and_resealed_lod_shader_refuse(self):
        for mutation in ("input", "lod", "discard", "suppression", "child-source"):
            with tempfile.TemporaryDirectory() as directory:
                destination = pathlib.Path(directory)
                folder = destination / "bindless-implicit-reference/ordinary_pbr_material"
                shutil.copytree(RUNNER / "bindless-implicit-reference/ordinary_pbr_material", folder)
                if mutation != "input":
                    publication_path = folder / "ordinary_pbr_material.graphics.module.yaml"
                    publication = yaml.safe_load(publication_path.read_text(encoding="utf-8"))
                    if mutation == "child-source":
                        entry = publication["entries"][1]
                        child_path = folder / entry["publication"]["path"]
                        child = yaml.safe_load(child_path.read_text(encoding="utf-8"))
                        source = child_path.parent / child["files"]["source"]["path"]
                        source.write_text(source.read_text(encoding="utf-8") + "\n// different source\n", encoding="utf-8")
                        child["files"]["source"]["sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
                        child_path.write_text(yaml.safe_dump(child), encoding="utf-8")
                        entry["publication"]["sha256"] = hashlib.sha256(child_path.read_bytes()).hexdigest()
                    else:
                        wgsl = folder / publication["files"]["wgsl"]["path"]
                        if mutation == "lod":
                            from compile_shaders import lower_wgsl_implicit_texture_sampling
                            lower_wgsl_implicit_texture_sampling(wgsl)
                        else:
                            code = wgsl.read_text(encoding="utf-8")
                            code = (code.replace("discard;", "return vec4<f32>(0.0);") if mutation == "discard"
                                    else "diagnostic(off, derivative_uniformity);\n" + code)
                            wgsl.write_text(code, encoding="utf-8")
                        publication["files"]["wgsl"]["sha256"] = hashlib.sha256(wgsl.read_bytes()).hexdigest()
                    publication_path.write_text(yaml.safe_dump(publication), encoding="utf-8")
                else:
                    (folder / "inputs.json").write_text("{}", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "IMPLICIT-REFERENCE|PBR-REFERENCE-INPUT"):
                    load_pbr_reference_contract(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", required=True, type=pathlib.Path)
    args, rest = parser.parse_known_args()
    RUNNER = args.runner
    unittest.main(argv=[sys.argv[0]] + rest)
