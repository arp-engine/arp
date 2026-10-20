"""Real Slang outputs and hostile publication metadata for the native loader."""
import argparse
import copy
import hashlib
import pathlib
import shutil
import subprocess
import tempfile

import yaml


class ArtifactContractFixture:
    def __init__(self, executable, publication, arguments=()):
        self.executable = executable
        self.publication = publication
        self.calls = 0
        self.arguments = tuple(arguments)

    def verify(self, expected="valid", publication_edit=None, reflection_edit=None, bytes_edit=None):
        self.calls += 1
        with tempfile.TemporaryDirectory(prefix="arp-single-entry-") as temporary:
            folder = pathlib.Path(temporary) / "artifact"
            shutil.copytree(self.publication.parent, folder)
            path = folder / self.publication.name
            node = yaml.safe_load(path.read_text())
            if reflection_edit:
                role = node["files"]["reflection"]
                reflection_path = folder / role["path"]
                reflection = yaml.safe_load(reflection_path.read_text())
                reflection_edit(reflection)
                reflection_path.write_text(yaml.safe_dump(reflection, sort_keys=False))
                role["sha256"] = hashlib.sha256(reflection_path.read_bytes()).hexdigest()
            if bytes_edit:
                bytes_edit(folder, node)
            if publication_edit:
                publication_edit(node)
            path.write_text(yaml.safe_dump(node, sort_keys=False))
            result = subprocess.run([self.executable, str(path), *self.arguments, expected], capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f"{expected}: {result.stdout}\n{result.stderr}")


def verify_webgpu_raw_metadata(fixture, original):
    """Shared rehashed raw-map cases for both original artifact loaders."""
    table = original["webGpuBindingSpace"]
    active = next((i for i, m in enumerate(table) if m["active"]), None)
    inactive = next((i for i, m in enumerate(table) if not m["active"]), None)
    if original["webGpuBindingSpaceStatus"] == "Unavailable":
        fixture.verify("WEBGPU-UNAVAILABLE-MAP", reflection_edit=lambda n: n.update(webGpuBindingSpaceGroupCount=1))
        fixture.verify("WEBGPU-UNAVAILABLE-MAP", reflection_edit=lambda n: n.update(pushConstantWebGpuBinding={"group": 0, "binding": 0}))
        return
    fixture.verify("WEBGPU-PHYSICAL-CONTRACT", reflection_edit=lambda n: n.update(webGpuBindingSpaceContractVersion="WebGpuBindingSpace/4"))
    fixture.verify("WEBGPU-GROUP-EXTENT", reflection_edit=lambda n: n.update(webGpuBindingSpaceGroupCount=original["webGpuBindingSpaceGroupCount"] + 1))
    for field in ("webGpuBindingSpaceGroupCount", "pushConstantWebGpuBinding"):
        fixture.verify("YAML-CONTRACT-FIELD-INVALID", reflection_edit=lambda n, f=field: n.pop(f))
    if active is not None:
        for field in ("group", "webGpuBinding", "capacity", "arrayElement"):
            fixture.verify("YAML-CONTRACT-FIELD-INVALID", reflection_edit=lambda n, f=field: n["webGpuBindingSpace"][active].pop(f))
            for invalid in (-1, 2 ** 32, True, None, 1.5, "0"):
                fixture.verify("YAML-CONTRACT-FIELD-INVALID", reflection_edit=lambda n, f=field, v=invalid:
                    n["webGpuBindingSpace"][active].update({f: v}))
        fixture.verify("WEBGPU-PHYSICAL-CARDINALITY", reflection_edit=lambda n: n["webGpuBindingSpace"][active].update(capacity=0))
        fixture.verify("WEBGPU-PHYSICAL-CARDINALITY", reflection_edit=lambda n: n["webGpuBindingSpace"][active].update(arrayElement=1))
        fixture.verify("WEBGPU-PHYSICAL-LOCATION", reflection_edit=lambda n:
            n["webGpuBindingSpace"][active].update(group=original["webGpuBindingSpaceGroupCount"]))
        fixture.verify("WEBGPU-PHYSICAL-LOCATION", reflection_edit=lambda n: n["webGpuBindingSpace"][active].update(webGpuBinding=2 ** 32 - 1))
        fixture.verify("WEBGPU-LOGICAL-MISMATCH", reflection_edit=lambda n:
            n["webGpuBindingSpace"][active].update(capacity=table[active]["capacity"] + 1))
        fixture.verify("WEBGPU-LOGICAL-MISMATCH", reflection_edit=lambda n: n["webGpuBindingSpace"][active].update(name="Unrelated"))
        fixture.verify("WEBGPU-LOGICAL-MISMATCH", reflection_edit=lambda n: n["webGpuBindingSpace"][active].update(set=2 ** 32 - 1))
        fixture.verify("WEBGPU-LOGICAL-TYPE", reflection_edit=lambda n: n["webGpuBindingSpace"][active].update(
            physicalType="Sampler" if table[active]["physicalType"] != "Sampler" else "UniformBuffer"))
        fixture.verify("WEBGPU-LOGICAL-COVERAGE", reflection_edit=lambda n: n["webGpuBindingSpace"].pop(active))
        # Replace an entry, preserving table length so the declared binding budget
        # does not obscure the duplicate relation being tested.
        if len(table) > 1:
            other = next(i for i in range(len(table)) if i != active)
            fixture.verify("WEBGPU-LOGICAL-DUPLICATE", reflection_edit=lambda n:
                n["webGpuBindingSpace"].__setitem__(other, copy.deepcopy(n["webGpuBindingSpace"][active])))
        other_active = next((i for i, m in enumerate(table) if m["active"] and i != active), None)
        if other_active is not None:
            fixture.verify("WEBGPU-PHYSICAL-DUPLICATE", reflection_edit=lambda n: n["webGpuBindingSpace"][other_active].update(
                group=table[active]["group"], webGpuBinding=table[active]["webGpuBinding"]))
    if inactive is not None:
        for field in ("group", "webGpuBinding", "capacity", "arrayElement", "physicalType"):
            fixture.verify("WEBGPU-INACTIVE-PAYLOAD", reflection_edit=lambda n, f=field:
                n["webGpuBindingSpace"][inactive].update({f: "UniformBuffer" if f == "physicalType" else 0}))
    push = original["pushConstantWebGpuBinding"]
    if push is not None:
        for field in ("group", "binding"):
            fixture.verify("WEBGPU-PUSH-MAPPING", reflection_edit=lambda n, f=field: n["pushConstantWebGpuBinding"].pop(f))
            for invalid in (-1, True, "0", 2 ** 32):
                fixture.verify("YAML-CONTRACT-FIELD-INVALID", reflection_edit=lambda n, f=field, v=invalid:
                    n["pushConstantWebGpuBinding"].update({f: v}))
        fixture.verify("WEBGPU-PUSH-MAPPING", reflection_edit=lambda n: n.update(pushConstantWebGpuBinding=None))
        if active is not None:
            fixture.verify("WEBGPU-PUSH-COLLISION", reflection_edit=lambda n: n.update(pushConstantWebGpuBinding={
                "group": table[active]["group"], "binding": table[active]["webGpuBinding"]}))
    else:
        fixture.verify("WEBGPU-PUSH-MAPPING", reflection_edit=lambda n: n.update(pushConstantWebGpuBinding={"group": 0, "binding": 0}))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--publication", action="append", required=True)
    args = parser.parse_args()
    fixtures = [ArtifactContractFixture(args.executable, pathlib.Path(path)) for path in args.publication]
    for fixture in fixtures:
        fixture.verify()
        metadata = yaml.safe_load(fixture.publication.read_text())
        unavailable = metadata["contractVersion"] in (2, 4)
        assert metadata["contractVersion"] in (3, 4)
        def legacy(n):
            n.pop("bufferDeclarationLayout")
            n["contractVersion"] = 2 if unavailable else 1
        fixture.verify("legacy-valid", legacy)
        fixture.verify("PUBLICATION-CONTRACT", lambda n: n.pop("bufferDeclarationLayout"))
        fixture.verify("PUBLICATION-CONTRACT", lambda n: n.update(contractVersion=2 if unavailable else 1))
        fixture.verify("BUFFER-LAYOUT-CONTRACT", lambda n: n["bufferDeclarationLayout"].update(contractVersion=99))
        fixture.verify("BUFFER-LAYOUT-CONTRACT", lambda n: n["bufferDeclarationLayout"].update(status="Available"))
        fixture.verify("BUFFER-LAYOUT-SPIRV-HASH", lambda n: n["bufferDeclarationLayout"].update(spirvSha256="0" * 64))
        def rehash_invalid_binary(folder, node):
            role = node["files"]["spirv"]
            path = folder / role["path"]
            data = bytearray(path.read_bytes())
            data[:4] = b'\0\0\0\0'
            path.write_bytes(data)
            role["sha256"] = hashlib.sha256(data).hexdigest()
            node["bufferDeclarationLayout"]["spirvSha256"] = role["sha256"]
        fixture.verify("SHADER-SPIRV-BINARY", bytes_edit=rehash_invalid_binary)
        layout_records = metadata["bufferDeclarationLayout"]["bindings"]
        if layout_records:
            for field in ("set", "binding", "spirvId", "blockTypeId", "storageClass"):
                fixture.verify("BUFFER-LAYOUT-BINARY-MISMATCH", lambda n, f=field:
                    n["bufferDeclarationLayout"]["bindings"][0].update({f: layout_records[0][f] + 1}))
            for field in ("descriptorCountHex", "staticOccupiedExtentBytesHex", "runtimeArrayOffsetBytesHex",
                          "runtimeArrayStrideBytesHex", "runtimeElementOccupiedExtentBytesHex"):
                fixture.verify("BUFFER-LAYOUT-BINARY-MISMATCH", lambda n, f=field:
                    n["bufferDeclarationLayout"]["bindings"][0].update({f: f"{int(layout_records[0][f], 16) + 1:016x}"}))
            fixture.verify("BUFFER-LAYOUT-BINARY-MISMATCH", lambda n:
                n["bufferDeclarationLayout"]["bindings"][0].update(hasRuntimeArray=not layout_records[0]["hasRuntimeArray"]))
            fixture.verify("BUFFER-LAYOUT-RECORD", lambda n:
                n["bufferDeclarationLayout"]["bindings"][0].update(nativeMinimumBindingBytesHex="0000000000000004"))
            fixture.verify("BUFFER-LAYOUT-COVERAGE", lambda n: n["bufferDeclarationLayout"].update(bindings=[]))
            fixture.verify("BUFFER-LAYOUT-COVERAGE", lambda n:
                n["bufferDeclarationLayout"]["bindings"].append(copy.deepcopy(n["bufferDeclarationLayout"]["bindings"][0])))
            for field in layout_records[0]:
                fixture.verify("BUFFER-LAYOUT-RECORD", lambda n, f=field: n["bufferDeclarationLayout"]["bindings"][0].pop(f))
        for field, value in (("contractVersion", 99), ("type", "BindlessShaderArtifact"), ("matrixLayout", "column-major")):
            fixture.verify("PUBLICATION-CONTRACT", lambda n, f=field, v=value: n.update({f: v}))
        fixture.verify("STAGE-INVALID", lambda n: n.update(stage="Geometry"))
        fixture.verify("REFLECTION-AMBIGUOUS", lambda n: n.update(entry="missingMain"))
        fixture.verify("REFLECTION-AMBIGUOUS", reflection_edit=lambda n: n["entryPoints"].append(copy.deepcopy(n["entryPoints"][0])))
        fixture.verify("YAML-CONTRACT-FIELD-INVALID", reflection_edit=lambda n: n["entryPoints"][0].pop("writesRenderTargetArrayIndex"))
        fixture.verify("FILE-ROLES", lambda n: n["files"].pop("wgsl"))
        fixture.verify("FILE-ROLE-PATH", lambda n: n["files"]["source"].update(path="../bad.slang"))
        fixture.verify("FILE-ROLE-PATH", lambda n: n["files"]["metal"].update(path=n["files"]["source"]["path"]))
        fixture.verify("FILE-HASH", lambda n: n["files"]["metal"].update(sha256="0" * 64))
        fixture.verify("SOURCE-HASH", reflection_edit=lambda n: n.update(sourceSha256="0" * 64))
        fixture.verify("REFLECTION-FILE-MISMATCH", reflection_edit=lambda n: n["targets"].update(metal="other.metal"))
        fixture.verify("REFLECTION-CONTRACT", reflection_edit=lambda n: n.update(reflectionContractVersion="ShaderReflection/0"))
        fixture.verify("VERTEX-INPUT-CONTRACT", reflection_edit=lambda n: n.update(vertexInputContractVersion="LegacySourceVertexInput/0"))
        fixture.verify("YAML-CONTRACT-FIELD-INVALID", reflection_edit=lambda n: n.pop("vertexInputContractVersion"))
        fixture.verify("FILE-HASH", bytes_edit=lambda folder, n: (folder / n["files"]["spirv"]["path"]).write_bytes(b"changed"))
        if unavailable:
            for field, value in (("status", "Available"), ("contract", "WgslBufferArrayDiagnostic/0"), ("diagnosticId", "Unknown"), ("path", "stale.wgsl")):
                fixture.verify("TARGET-DIAGNOSTIC-CONTRACT", lambda n, f=field, v=value: n["files"]["wgsl"].update({f: v}))
            fixture.verify("TARGET-DIAGNOSTIC-REFLECTION", reflection_edit=lambda n: n.update(webGpuBindingSpaceStatus="Available"))
            fixture.verify("TARGET-DIAGNOSTIC-REFLECTION", reflection_edit=lambda n: n.update(webGpuBindingSpaceUnavailableReason="Unknown"))
            fixture.verify("TARGET-DIAGNOSTIC-REFLECTION", reflection_edit=lambda n: n["targets"].update(wgsl="stale.wgsl"))
            if metadata["files"]["wgsl"]["contract"] == "WgslGraphicsAtomicDiagnostic/1":
                fixture.verify("GRAPHICS-WRITE-PROOF-BINDING", reflection_edit=lambda n: [b.update(arrayCount=2) for b in n["bindings"]])
                fixture.verify("GRAPHICS-WRITE-PROOF-BINDING", reflection_edit=lambda n: n["bindings"][0].update(binding=8))
            else:
                fixture.verify("TARGET-DIAGNOSTIC-REFLECTION", reflection_edit=lambda n: [b.update(arrayCount=1) for b in n["bindings"]])
            # A stray stale file cannot grant target availability.
            fixture.verify(bytes_edit=lambda folder, n: (folder / "stale.wgsl").write_text("stale"))
        else:
            fixture.verify("PUBLICATION-CONTRACT", lambda n: n.update(contractVersion=2))
            fixture.verify("FILE-READ-FAILED", bytes_edit=lambda folder, n: (folder / n["files"]["wgsl"]["path"]).unlink())
        fixture.verify("BINDLESS-UNAUTHORIZED", reflection_edit=lambda n: n.update(
            bindlessContractVersion="BindlessCross/1", bindlessExecution="Native", bindlessLoweringSha256="0" * 64))
        original = yaml.safe_load((fixture.publication.parent / yaml.safe_load(fixture.publication.read_text())["files"]["reflection"]["path"]).read_text())
        verify_webgpu_raw_metadata(fixture, original)
        if not unavailable and any(m["active"] for m in original["webGpuBindingSpace"]):
            index = next(i for i, m in enumerate(original["webGpuBindingSpace"]) if m["active"])
            fixture.verify("legacy-valid", legacy, reflection_edit=lambda n: n["webGpuBindingSpace"][index].pop("capacity"))
        if original["vertexAttributes"]:
            fixture.verify("VERTEX-INPUT-FORMAT", reflection_edit=lambda n: n["vertexAttributes"][0].update(format="Unknown"))
            fixture.verify("VERTEX-INPUT-DUPLICATE", reflection_edit=lambda n: n["vertexAttributes"].append(copy.deepcopy(n["vertexAttributes"][0])))
            fixture.verify("VERTEX-INPUT-LOCATION", reflection_edit=lambda n: n["vertexAttributes"][0].update(location=0xffffffff))
            fixture.verify("YAML-CONTRACT-FIELD-INVALID", reflection_edit=lambda n: n["vertexAttributes"][0].pop("location"))
        for table in ("metalBindingSpace", "metalArgumentBuffer", "webGpuBindingSpace"):
            active = next((i for i, m in enumerate(original[table]) if m["active"]), None)
            if active is None:
                continue
            fixture.verify("PHYSICAL-TYPE", reflection_edit=lambda n, t=table, i=active: n[t][i].update(physicalType="Unknown"))
            fixture.verify("YAML-CONTRACT-FIELD-INVALID", reflection_edit=lambda n, t=table, i=active: n[t][i].pop("physicalType"))
            fixture.verify("YAML-CONTRACT-FIELD-INVALID", reflection_edit=lambda n, t=table, i=active: n[t][i].pop("active"))
        if original["bindings"]:
            fixture.verify("BINDING-TYPE", reflection_edit=lambda n: n["bindings"][0].update(type="Unknown"))
            fixture.verify("BINDING-SEMANTICS", reflection_edit=lambda n: n["bindings"][0].update(stageMask=0))
            fixture.verify("BINDING-SEMANTICS", reflection_edit=lambda n: n["bindings"][0].update(arrayCount=0))
            fixture.verify("YAML-CONTRACT-FIELD-INVALID", reflection_edit=lambda n: n["bindings"][0].pop("arrayCount"))
            fixture.verify("BINDING-DUPLICATE", reflection_edit=lambda n: n["bindings"].append(copy.deepcopy(n["bindings"][0])))
    print(f"SingleEntryShaderArtifact: {sum(f.calls for f in fixtures)} VS/FS/Compute real artifact cases; identity/budget/immutability negative contracts pass")


if __name__ == "__main__":
    main()
