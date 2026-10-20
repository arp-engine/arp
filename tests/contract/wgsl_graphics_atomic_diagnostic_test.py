"""Actual fragment atomic capability failure, selected binary proof and loader gates."""
import argparse
import hashlib
import pathlib
import subprocess
import sys
import tempfile
import yaml

from single_entry_shader_artifact_test import ArtifactContractFixture

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.compile_shaders import run_slangc


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--slangc", required=True)
    parser.add_argument("--spirv-reflect", required=True)
    parser.add_argument("--proof-tool", required=True)
    parser.add_argument("--loader", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="arp-wgsl-graphics-atomic-") as temporary:
        folder = pathlib.Path(temporary)
        command = [sys.executable, str(ROOT / "scripts/compile_shaders.py"),
                   "--fixture", "ordinary_graphics_storage_write", "--single-entry", "fragmentMain",
                   "--slangc", args.slangc, "--spirv-reflect", args.spirv_reflect, "--out-dir", str(folder)]
        policy = ["--wgsl-graphics-atomic-diagnostic", "--graphics-write-proof-tool", args.proof_tool]
        wgsl = folder / "ordinary_graphics_storage_write.wgsl"
        publication = folder / "ordinary_graphics_storage_write.fragmentMain.entry.yaml"
        wgsl.write_text("stale target")
        for _ in range(2):
            result = subprocess.run(command + policy, capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr
            assert "E36107" in result.stdout and not wgsl.exists()
            metadata = yaml.safe_load(publication.read_text())
            assert metadata["contractVersion"] == 4
            assert metadata["files"]["wgsl"] == dict(status="DiagnosticFailure", contract="WgslGraphicsAtomicDiagnostic/1",
                                                     diagnosticId="SHADER-WGSL-GRAPHICS-ATOMIC-UNSUPPORTED")
            for role, file in metadata["files"].items():
                if role != "wgsl":
                    assert hashlib.sha256((folder / file["path"]).read_bytes()).hexdigest() == file["sha256"]
            fixture = ArtifactContractFixture(args.loader, publication)
            fixture.verify()
            # Rehashing a replacement binary cannot manufacture shader proof.
            def replace_binary(folder, metadata):
                role = metadata["files"]["spirv"]
                path = folder / role["path"]
                data = bytearray(path.read_bytes())
                data[:4] = b"bad!"
                path.write_bytes(data)
                role["sha256"] = hashlib.sha256(data).hexdigest()
            fixture.verify("SHADER-SPIRV-BINARY", bytes_edit=replace_binary)
            fixture.verify("GRAPHICS-WRITE-PROOF-BINDING", reflection_edit=lambda n: n["bindings"][0].update(type="ReadOnlyStorageBuffer"))
            fixture.verify("GRAPHICS-WRITE-PROOF-BINDING", reflection_edit=lambda n: n["bindings"][0].update(set=1))
            fixture.verify("TARGET-DIAGNOSTIC-CONTRACT", publication_edit=lambda n: n["files"]["wgsl"].update(contract="WgslBufferArrayDiagnostic/1"))
            wgsl.write_text("stale target")
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode and "E36107" in result.stderr and not publication.exists()
        readonly = [arg if arg != "ordinary_graphics_storage_write" else "ordinary_graphics_storage" for arg in command]
        result = subprocess.run(readonly + policy, capture_output=True, text=True)
        assert result.returncode and "SHADER-WGSL-GRAPHICS-ATOMIC-POLICY-MISMATCH" in result.stderr
        wrong_stage = [arg if arg != "fragmentMain" else "vertexMain" for arg in command]
        result = subprocess.run(wrong_stage + policy, capture_output=True, text=True)
        assert result.returncode and "SHADER-WGSL-GRAPHICS-ATOMIC-POLICY-MISMATCH" in result.stderr
        result = subprocess.run(command + ["--wgsl-graphics-atomic-diagnostic"], capture_output=True, text=True)
        assert result.returncode and "requires --single-entry and --graphics-write-proof-tool" in result.stderr
        result = subprocess.run(command + policy + ["--wgsl-buffer-array-diagnostic"], capture_output=True, text=True)
        assert result.returncode and "mutually exclusive" in result.stderr
        bad = folder / "invalid.slang"
        bad.write_text('[shader("fragment")] float4 fragmentMain() : SV_Target0 { return unknown_symbol; }')
        try:
            run_slangc(args.slangc, str(bad), "wgsl", "sm_6_0", ["fragmentMain"], str(folder / "invalid.wgsl"),
                       diagnostic_failure_code="E36107")
        except SystemExit as error:
            assert error.code
        else:
            raise AssertionError("unrelated target failure was accepted")
    print("WgslGraphicsAtomicDiagnostic/1: actual E36107 and selected proof, sealed loader/rehash/mismatch, stale cleanup and unrelated error rejection PASS")


if __name__ == "__main__":
    main()
