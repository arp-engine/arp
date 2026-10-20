"""Rehashed original operation/role custody using shared artifact mutation fixtures."""
import argparse
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

import yaml
from single_entry_shader_artifact_test import ArtifactContractFixture
from original_target_memory_test import edit_role, replace, patch_instruction, child_edit

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from spirv_layout import SpirvLayoutFacts


def erase_lengths(binding=None):
    """Valid OpUndef preserves each result identity; no length operation remains."""
    def edit(folder, publication):
        role = publication["files"]["spirv"]
        facts = SpirvLayoutFacts(folder / role["path"])
        roots = {var[1] for var in facts.variables if binding is None or facts.decorations.get((var[1], 33)) == (binding,)}
        def transform(data):
            output = bytearray(data[:20])
            for _, opcode, operands in facts.instructions:
                if opcode == 68 and operands[2] in roots:
                    opcode, operands = 45, operands[:2]  # OpUndef result type/id
                output.extend(struct.pack("<%dI" % (len(operands)+1), ((len(operands)+1)<<16)|opcode, *operands))
            return bytes(output)
        edit_role("spirv", transform)(folder, publication)
    return edit


def no_op(folder, publication):
    erase_lengths()(folder, publication)
    edit_role("wgsl", replace(b"arrayLength(&Input_0)", b"u32(0)"))(folder, publication)


def delete_and_hot(executable, publication):
    with tempfile.TemporaryDirectory(prefix="arp-runtime-length-deleted-") as temporary:
        folder = pathlib.Path(temporary) / "artifact"
        shutil.copytree(publication.parent, folder)
        subprocess.run([executable, str(folder / publication.name), "deleted"], check=True, capture_output=True, text=True)
        assert not folder.exists()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--fixture-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    fixtures = []
    for variant in ("structured", "rwstructured", "byteaddress", "rwbyteaddress"):
        publication = args.fixture_dir / variant / ("runtime_length_"+variant+".computeMain.entry.yaml")
        fixture = ArtifactContractFixture(args.executable, publication)
        fixtures.append(fixture)
        fixture.verify()
        fixture.verify("TARGET-DIAGNOSTIC-CONTRACT", publication_edit=lambda n: n["files"]["metal"].update(contract="MetalRuntimeLengthDiagnostic/2"))
        fixture.verify("TARGET-DIAGNOSTIC-CONTRACT", publication_edit=lambda n: n["files"]["metal"].update(diagnosticId="SHADER-METAL-OTHER"))
        fixture.verify("TARGET-DIAGNOSTIC-CONTRACT", publication_edit=lambda n: n["files"]["metal"].update(path="dummy.metal"))
        fixture.verify("FILE-ROLES", publication_edit=lambda n: n["files"].pop("metal"))
        fixture.verify("YAML-CONTRACT-FIELD-INVALID", publication_edit=lambda n: n.update(contractVersion=3))
        for field, value in (("metalBindingSpaceStatus", "Available"), ("metalBindingSpaceContractVersion", "MetalBindingSpace/1"),
                             ("metalBindingSpaceUnavailableReason", "invented"), ("pushConstantMetalBufferIndex", 0),
                             ("metalGpuCountedIndirectStatus", "Available"), ("metalTessellationControlPointCount", 3)):
            fixture.verify("METAL-LENGTH-DIAGNOSTIC-REFLECTION", reflection_edit=lambda n, f=field, v=value: n.update({f:v}))
        fixture.verify("METAL-LENGTH-DIAGNOSTIC-REFLECTION", reflection_edit=lambda n: n["targets"].update(metal="dummy.metal"))
        fixture.verify("METAL-LENGTH-DIAGNOSTIC-REFLECTION", reflection_edit=lambda n: n["metalArgumentBuffer"].append({"active": False}))
        fixture.verify("TARGET-COVERAGE", bytes_edit=edit_role("wgsl", replace(b"arrayLength(&Input_0)", b"arrayLength(&Output_0)")))
        fixture.verify("TARGET-COVERAGE", bytes_edit=edit_role("wgsl", replace(b"arrayLength(&Input_0)", b"u32(0)")))
        fixture.verify("TARGET-COVERAGE", bytes_edit=edit_role("wgsl", replace(b"arrayLength(&Input_0)", b"arrayLength(&Input_0)+arrayLength(&Input_0)")))
        fixture.verify("TARGET-OPERATION", bytes_edit=edit_role("wgsl", replace(b"arrayLength(&Input_0)", b"arrayLength(&Input_0.member)")))
        fixture.verify("SHADOW", bytes_edit=edit_role("wgsl", replace(b"fn computeMain()", b"fn computeMain(Input_0:u32)")))
        fixture.verify("FINAL-MEMBER", bytes_edit=edit_role("spirv", patch_instruction(68, 3, 1, lambda _: True)))
        fixture.verify("TARGET-COVERAGE", bytes_edit=erase_lengths())
        fixture.verify("OPERATION-REQUIRED", bytes_edit=no_op)
        fixture.verify("PHYSICAL-DECLARATION", reflection_edit=lambda n: n["webGpuBindingSpace"][0].update(webGpuBinding=12))
        fixture.verify("MEMORY-MISMATCH", bytes_edit=edit_role("wgsl", replace(b"array<u32>", b"array<i32>")))
        fixture.verify("FILE-READ-FAILED", bytes_edit=lambda f,n: (f / n["files"]["wgsl"]["path"]).unlink())
        delete_and_hot(args.executable, publication)
    publication = args.fixture_dir / "graphics/runtime_length_graphics.graphics.module.yaml"
    fixture = ArtifactContractFixture(args.executable, publication)
    fixtures.append(fixture)
    fixture.verify()
    fixture.verify("TARGET-DIAGNOSTIC-CONTRACT", publication_edit=lambda n: n["files"]["metal"].update(contract="MetalRuntimeLengthDiagnostic/0"))
    fixture.verify("YAML-CONTRACT-FIELD-INVALID", publication_edit=lambda n: n.update(contractVersion=1))
    fixture.verify("METAL-LENGTH-DIAGNOSTIC-REFLECTION", reflection_edit=lambda n: n["targets"].update(metal="fake.metal"))
    for stage in (0,1):
        fixture.verify("TARGET-DIAGNOSTIC-CONTRACT", bytes_edit=child_edit(stage, lambda f,n:n["files"]["metal"].update(diagnosticId="OTHER")))
        fixture.verify("TARGET-COVERAGE", bytes_edit=child_edit(stage, edit_role("wgsl", replace(b"arrayLength(&Shared_0)", b"u32(0)"))))
        fixture.verify("FINAL-MEMBER", bytes_edit=child_edit(stage, edit_role("spirv", patch_instruction(68,3,1,lambda _:True))))
    fixture.verify("TARGET-COVERAGE", bytes_edit=edit_role("wgsl", replace(b"arrayLength(&FragmentOnly_0)", b"u32(0)")))
    # Prove union coverage independently of per-scope correspondence and nonempty role.
    def erase_fragment_only(folder, node):
        erase_lengths(1)(folder, node)
        edit_role("wgsl", replace(b"arrayLength(&FragmentOnly_0)",b"u32(0)"))(folder,node)
    fixture.verify("CHILD-COVERAGE", bytes_edit=erase_fragment_only)
    fixture.verify("COMBINED-COVERAGE", bytes_edit=child_edit(1,erase_fragment_only))
    delete_and_hot(args.executable, publication)
    print("OriginalRuntimeLengthCustody/1:",sum(f.calls for f in fixtures),"rehashed cases; deletion5; no native length authority")


if __name__ == "__main__":
    main()
