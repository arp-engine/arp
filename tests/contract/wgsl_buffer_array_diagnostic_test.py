"""Actual Slang errors/emissions, unavailable target provenance and loader gates."""
import argparse
import hashlib
import pathlib
import re
import subprocess
import sys
import tempfile
import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.compile_shaders import (get_shader_bindings, run_slangc,
                                    validate_wgsl_buffer_array_diagnostic_artifact)
from scripts.shader_bindings_codegen import (apply_descriptor_stage_masks,
    reflect_buffer_array_descriptors, reflect_descriptor_stage_masks)
from single_entry_shader_artifact_test import ArtifactContractFixture

parser = argparse.ArgumentParser()
parser.add_argument("--slangc", required=True)
parser.add_argument("--spirv-reflect", required=True)
parser.add_argument("--loader", required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="arp-wgsl-array-policy-") as temporary:
    folder = pathlib.Path(temporary)
    command = [sys.executable, str(ROOT / "scripts/compile_shaders.py"),
               "--fixture", "ordinary_graphics_storage_array", "--single-entry", "fragmentMain",
               "--slangc", args.slangc, "--spirv-reflect", args.spirv_reflect, "--out-dir", str(folder)]
    wgsl = folder / "ordinary_graphics_storage_array.wgsl"
    publication = folder / "ordinary_graphics_storage_array.fragmentMain.entry.yaml"
    wgsl.write_text("stale target")
    for _ in range(2):
        result = subprocess.run(command + ["--wgsl-buffer-array-diagnostic"], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        assert not wgsl.exists()
        node = yaml.safe_load(publication.read_text())
        assert node["contractVersion"] == 4
        diagnostic = node["files"]["wgsl"]
        emitted = diagnostic["contract"] == "WgslBufferArrayDiagnostic/2"
        assert diagnostic == dict(status="DiagnosticFailure",
            contract="WgslBufferArrayDiagnostic/2" if emitted else "WgslBufferArrayDiagnostic/1",
            diagnosticId="SHADER-WGSL-BUFFER-ARRAY-ABI-UNAVAILABLE" if emitted else "SHADER-WGSL-BUFFER-ARRAY-UNSUPPORTED")
        assert ("WgslBufferArrayDiagnostic/2" if emitted else "E56105") in result.stdout
        for role, file in node["files"].items():
            if role != "wgsl":
                assert hashlib.sha256((folder / file["path"]).read_bytes()).hexdigest() == file["sha256"]
        assert reflect_buffer_array_descriptors(args.spirv_reflect, str(folder / "ordinary_graphics_storage_array.spv")) == {(0, 3): 2, (0, 8): 2}
        fixture = ArtifactContractFixture(args.loader, publication)
        fixture.verify()
        fixture.verify("TARGET-DIAGNOSTIC-CONTRACT", publication_edit=lambda n:
            n["files"]["wgsl"].update(contract="WgslBufferArrayDiagnostic/1" if emitted else "WgslBufferArrayDiagnostic/2"))
        fixture.verify("TARGET-DIAGNOSTIC-REFLECTION", reflection_edit=lambda n:
            n.update(webGpuBindingSpaceUnavailableReason="Unknown"))
        wgsl.write_text("stale target")
    # Inspect the actual successful target before its deterministic deletion.
    # Older Slang's exact E56105 case has no emitted bytes to classify.
    probe = folder / "buffer-array-probe.wgsl"
    source = folder / "ordinary_graphics_storage_array.slang"
    if run_slangc(args.slangc, str(source), "wgsl", "sm_6_0", ["fragmentMain"], str(probe),
                  diagnostic_failure_code="E56105"):
        bindings = get_shader_bindings(source.read_text())
        spv = str(folder / "ordinary_graphics_storage_array.spv")
        apply_descriptor_stage_masks(bindings, reflect_descriptor_stage_masks(args.spirv_reflect, spv),
                                     spv, allow_inactive=True)
        arrays = reflect_buffer_array_descriptors(args.spirv_reflect, spv)
        original = probe.read_text()
        validate_wgsl_buffer_array_diagnostic_artifact(bindings, arrays, str(probe))
        for changed in (re.sub(r",\s*(?:i32\(2\)|2)>;", ", 3>;", original),
                        original.replace("var Shared_0 :", "var Shared_0 : u32; //"),
                        original.replace("var Shared_0", "var Foreign_0"),
                        original + "\n@group(0) @binding(8) var Shared_7 : binding_array<array<u32>, 2>;",
                        "// " + original.replace("\n", "\n// ")):
            assert changed != original
            probe.write_text(changed)
            try:
                validate_wgsl_buffer_array_diagnostic_artifact(bindings, arrays, str(probe))
            except RuntimeError as error:
                assert "EMISSION-MISMATCH" in str(error)
            else:
                raise AssertionError("malformed native array emission accepted")
        probe.write_text(original)
        changed_arrays = dict(arrays); changed_arrays[(0, 3)] = 3
        try:
            validate_wgsl_buffer_array_diagnostic_artifact(bindings, changed_arrays, str(probe))
        except RuntimeError as error:
            assert "POLICY-MISMATCH" in str(error)
        else:
            raise AssertionError("changed authoritative capacity accepted")
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode and not publication.exists()
    assert "E56105" in result.stderr or "unsupported WGSL resource declaration" in result.stderr
    result = subprocess.run([arg if arg != "ordinary_graphics_storage_array" else "ordinary_graphics_storage" for arg in command]
                            + ["--wgsl-buffer-array-diagnostic"], capture_output=True, text=True)
    assert result.returncode and "SHADER-WGSL-BUFFER-ARRAY-POLICY-MISMATCH" in result.stderr
    # An unrelated actual compiler error cannot be accepted as E56105.
    bad = folder / "invalid.slang"
    bad.write_text('[shader("fragment")] float4 fragmentMain() : SV_Target0 { return unknown_symbol; }')
    try:
        run_slangc(args.slangc, str(bad), "wgsl", "sm_6_0", ["fragmentMain"], str(folder / "invalid.wgsl"),
                   diagnostic_failure_code="E56105")
    except SystemExit as error:
        assert error.code
    else:
        raise AssertionError("unrelated compiler failure published")
print("WgslBufferArrayDiagnostic/1,2: real target errors/emissions, exact SPIR-V cardinality, loader, stale cleanup and unrelated failure gates pass")
