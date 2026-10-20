"""Rehashed original byte/map/type custody, using the existing artifact harness."""
import argparse
import hashlib
import pathlib
import shutil
import struct
import subprocess
import tempfile

import yaml
from single_entry_shader_artifact_test import ArtifactContractFixture


def edit_role(role_name, transform):
    def edit(folder, publication):
        role = publication["files"][role_name]
        path = folder / role["path"]
        original = path.read_bytes()
        changed = transform(original)
        assert changed != original
        path.write_bytes(changed)
        role["sha256"] = hashlib.sha256(changed).hexdigest()
        if role_name == "spirv" and "bufferDeclarationLayout" in publication:
            publication["bufferDeclarationLayout"]["spirvSha256"] = role["sha256"]
    return edit


def replace(old, new):
    def transform(data):
        assert old in data
        return data.replace(old, new, 1)
    return transform


def patch_instruction(opcode, operand, value, predicate):
    def transform(data):
        words = list(struct.unpack("<%dI" % (len(data) // 4), data))
        offset = 5
        while offset < len(words):
            count = words[offset] >> 16
            if words[offset] & 65535 == opcode and predicate(words[offset + 1:offset + count]):
                words[offset + 1 + operand] = value
                return struct.pack("<%dI" % len(words), *words)
            offset += count
        raise AssertionError("No authored binary mutation target")
    return transform


def child_edit(stage, edit):
    def mutate(folder, publication):
        reference = publication["entries"][stage]["publication"]
        path = folder / reference["path"]
        child = yaml.safe_load(path.read_text())
        edit(path.parent, child)
        path.write_text(yaml.safe_dump(child, sort_keys=False))
        reference["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
    return mutate


def deleted(executable, path, module=False, probe=False):
    with tempfile.TemporaryDirectory(prefix="arp-target-memory-deleted-") as temporary:
        folder = pathlib.Path(temporary) / "artifact"
        shutil.copytree(path.parent, folder)
        cmd = [executable, "--module-deleted" if module else "--entry-deleted", str(folder / path.name)]
        if probe:
            cmd.append("probe")
        subprocess.run(cmd, check=True, capture_output=True, text=True)
        assert not folder.exists()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--push", type=pathlib.Path, required=True)
    parser.add_argument("--unavailable", type=pathlib.Path, required=True)
    parser.add_argument("--storage-module", type=pathlib.Path, required=True)
    parser.add_argument("--push-module", type=pathlib.Path, required=True)
    parser.add_argument("--uniform-module", type=pathlib.Path, required=True)
    args = parser.parse_args()
    probe = ArtifactContractFixture(args.executable, args.probe)
    probe.verify()
    probe.verify("MEMORY-MISMATCH", bytes_edit=edit_role("wgsl", replace(b"Scalar_0 : u32", b"Scalar_0 : i32")))
    probe.verify("MEMORY-MISMATCH", bytes_edit=edit_role("wgsl", replace(b"marker_0 : u32", b"marker_0 : i32")))
    probe.verify("MEMORY-MISMATCH", bytes_edit=edit_role("wgsl", replace(b"i32(3)", b"i32(4)")))
    probe.verify("TARGET-COVERAGE", bytes_edit=edit_role("wgsl", lambda b: b + b"\n@group(0) @binding(12) var<uniform> Extra:u32;\n"))
    probe.verify("TARGET-COVERAGE", bytes_edit=edit_role("wgsl", replace(b"@binding(0) @group(0) var<uniform> Scalar_0 : u32;", b"")))
    probe.verify("PHYSICAL-DECLARATION", reflection_edit=lambda n: n["webGpuBindingSpace"][0].update(webGpuBinding=12))
    probe.verify("ACCESS", bytes_edit=edit_role("wgsl", replace(b"storage, read_write", b"storage, read")))
    probe.verify("MEMORY-MISMATCH", bytes_edit=edit_role("spirv", patch_instruction(21, 2, 1, lambda v: v[1:] == [32, 0])))
    probe.verify("lookup:TYPE-UNSUPPORTED", bytes_edit=edit_role("wgsl", replace(b"Scalar_0 : u32", b"Scalar_0 : UnknownType")))
    def legacy(n):
        n.pop("bufferDeclarationLayout")
        n["contractVersion"] = 1
    probe.verify("lookup:LEGACY", publication_edit=legacy)
    probe.verify("FILE-READ-FAILED", bytes_edit=lambda f, n: (f / n["files"]["wgsl"]["path"]).unlink())
    deleted(args.executable, args.probe, probe=True)

    push = ArtifactContractFixture(args.executable, args.push)
    push.verify()
    push.verify("MEMORY-MISMATCH", bytes_edit=edit_role("wgsl", replace(b"positionScaleBiasDepth_0 : vec4<f32>", b"positionScaleBiasDepth_0 : vec4<u32>")))
    push.verify("PUSH-BINARY-SCALAR", reflection_edit=lambda n: n["pushConstantMembers"][0].update(scalarType="uint32"))
    push.verify("PUSH-BINARY-SIZE", bytes_edit=edit_role("spirv", patch_instruction(72, 3, 32, lambda v: v[1:] == [1, 35, 16])))
    push.verify("PHYSICAL-DECLARATION", reflection_edit=lambda n: n["pushConstantWebGpuBinding"].update(binding=12))
    push.verify("TARGET-COVERAGE", bytes_edit=edit_role("wgsl", lambda b: b + b"\n@group(0) @binding(12) var<uniform> Extra:u32;\n"))
    deleted(args.executable, args.push)

    unavailable = ArtifactContractFixture(args.executable, args.unavailable)
    node = yaml.safe_load(args.unavailable.read_text())
    assert node["contractVersion"] == 4
    unavailable.verify("lookup:" + node["files"]["wgsl"]["diagnosticId"])

    storage = ArtifactContractFixture(args.executable, args.storage_module)
    storage.verify()
    storage.verify("STAGE-TARGET-TYPE", bytes_edit=edit_role("wgsl", replace(b"positionScaleAndBias_0 : vec4<f32>", b"positionScaleAndBias_0 : vec4<u32>")))
    storage.verify("TARGET-COVERAGE", bytes_edit=edit_role("wgsl", lambda b: b + b"\n@group(0) @binding(12) var<uniform> Extra:u32;\n"))
    storage.verify("PHYSICAL-DECLARATION", reflection_edit=lambda n: n["webGpuBindingSpace"][0].update(webGpuBinding=12))
    storage.verify("MEMORY-MISMATCH", bytes_edit=child_edit(1, edit_role("wgsl", replace(b"color_0 : vec4<f32>", b"color_0 : vec4<u32>"))))
    storage.verify("STAGE-BINARY-TYPE", bytes_edit=edit_role("spirv", patch_instruction(22, 1, 16, lambda v: v[1]==32)))
    def legacy_child(folder, node):
        legacy(node)
    storage.verify("lookup:LEGACY", bytes_edit=child_edit(0, legacy_child))
    storage.verify("lookup:TYPE-UNSUPPORTED", bytes_edit=child_edit(1, edit_role("wgsl", replace(b"color_0 : vec4<f32>", b"color_0 : UnknownType"))))
    storage.verify("MEMORY-MISMATCH", bytes_edit=child_edit(1, edit_role("wgsl", replace(b"color_0 : vec4<f32>", b"@size(32) color_0 : vec4<f32>"))))
    deleted(args.executable, args.storage_module, module=True)

    module_push = ArtifactContractFixture(args.executable, args.push_module)
    module_push.verify()
    module_push.verify("PUSH-STAGE-TARGET", bytes_edit=edit_role("wgsl", replace(b"color_0 : vec4<f32>", b"color_0 : vec4<u32>")))
    module_push.verify("MEMORY-MISMATCH", bytes_edit=child_edit(0, edit_role("wgsl", replace(b"color_0 : vec4<f32>", b"color_0 : vec4<u32>"))))
    deleted(args.executable, args.push_module, module=True)
    uniform = ArtifactContractFixture(args.executable, args.uniform_module)
    uniform.verify()
    uniform.verify("valid-padded-stage", bytes_edit=child_edit(1, edit_role("wgsl", replace(b"color_0 : vec4<f32>", b"@size(32) color_0 : vec4<f32>"))))
    print("OriginalTargetMemory/1:", sum(f.calls for f in (probe, push, unavailable, storage, module_push, uniform)), "rehashed cases; deletion4; no length/native authority")


if __name__ == "__main__":
    main()
