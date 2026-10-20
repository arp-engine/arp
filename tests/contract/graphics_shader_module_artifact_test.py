"""Real Slang combined roles, selected-entry provenance and hostile YAML."""
import argparse
import hashlib
import pathlib

import yaml
from single_entry_shader_artifact_test import ArtifactContractFixture, verify_webgpu_raw_metadata


def edit_child_source(folder, node):
    reference = node["entries"][1]["publication"]
    path = folder / reference["path"]
    child = yaml.safe_load(path.read_text())
    source = path.parent / child["files"]["source"]["path"]
    source.write_bytes(source.read_bytes() + b"\n// different sealed source\n")
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    child["files"]["source"]["sha256"] = digest
    reflection_path = path.parent / child["files"]["reflection"]["path"]
    reflection = yaml.safe_load(reflection_path.read_text())
    reflection["sourceSha256"] = digest
    reflection_path.write_text(yaml.safe_dump(reflection, sort_keys=False))
    child["files"]["reflection"]["sha256"] = hashlib.sha256(reflection_path.read_bytes()).hexdigest()
    path.write_text(yaml.safe_dump(child, sort_keys=False))
    reference["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--publication", required=True)
    args = parser.parse_args()
    fixture = ArtifactContractFixture(args.executable, pathlib.Path(args.publication))
    fixture.verify()
    publication = yaml.safe_load(fixture.publication.read_text())
    original = yaml.safe_load((fixture.publication.parent / publication["files"]["reflection"]["path"]).read_text())
    verify_webgpu_raw_metadata(fixture, original)
    for field, value in (("type", "SingleEntryShaderArtifact"), ("contractVersion", 99), ("matrixLayout", "column-major")):
        fixture.verify("PUBLICATION-CONTRACT", lambda n, f=field, v=value: n.update({f: v}))
    fixture.verify("RUNTIME-LENGTH-REQUEST-REQUIRED", lambda n: n.update(contractVersion=2))
    fixture.verify("ENTRY-COVERAGE", lambda n: n["entries"].pop())
    fixture.verify("ENTRY-COVERAGE", lambda n: n["entries"].append(n["entries"][0].copy()))
    fixture.verify("ENTRY-STAGE", lambda n: n["entries"][0].update(stage="Compute"))
    fixture.verify("ENTRY-STAGE", lambda n: n["entries"].reverse())
    fixture.verify("REFLECTION-ENTRY", lambda n: n["entries"][0].update(entry="missingMain"))
    fixture.verify("ENTRY-COVERAGE", reflection_edit=lambda n: n["entryPoints"].pop())
    fixture.verify("REFLECTION-ENTRY", reflection_edit=lambda n: n["entryPoints"][0].update(stage="Compute"))
    for path in ("../Vertex/x.entry.yaml", "/tmp/x.entry.yaml", "Vertex//x.entry.yaml", "Vertex/./x.entry.yaml",
                 "Vertex/x.txt", "Vertex/../../x.entry.yaml", "Vertex\\x.entry.yaml", "a/" + "x" * 300 + ".entry.yaml"):
        fixture.verify("REFERENCE-PATH", lambda n, p=path: n["entries"][0]["publication"].update(path=p))
    fixture.verify("REFERENCE-HASH", lambda n: n["entries"][0]["publication"].update(sha256="0" * 64))
    fixture.verify("REFERENCE-ALIAS", lambda n: n["entries"][1].update(publication=n["entries"][0]["publication"].copy()))
    fixture.verify("REFERENCE-ENTRY", lambda n: n["entries"][0].update(publication=n["entries"][1]["publication"].copy()))
    fixture.verify("REFERENCE-SOURCE", bytes_edit=edit_child_source)
    fixture.verify("FILE-READ-FAILED", bytes_edit=lambda folder, n: (folder / n["entries"][0]["publication"]["path"]).unlink())
    fixture.verify("FILE-HASH", bytes_edit=lambda folder, n: (folder / "Vertex" / "ordinary_graphics_storage_read.wgsl").write_text("changed"))
    fixture.verify("FILE-ROLES", lambda n: n["files"].pop("metal"))
    fixture.verify("FILE-ROLE-PATH", lambda n: n["files"]["wgsl"].update(path="../x.wgsl"))
    fixture.verify("FILE-HASH", lambda n: n["files"]["wgsl"].update(sha256="0" * 64))
    fixture.verify("SOURCE-HASH", reflection_edit=lambda n: n.update(sourceSha256="0" * 64))
    fixture.verify("REFLECTION-FILE", reflection_edit=lambda n: n["targets"].update(wgsl="different.wgsl"))
    fixture.verify("REFLECTION-CONTRACT", reflection_edit=lambda n: n.update(reflectionContractVersion="ShaderReflection/0"))
    fixture.verify("BINDING-TYPE", reflection_edit=lambda n: n["bindings"][0].update(type="Unknown"))
    fixture.verify("BINDING-SEMANTICS", reflection_edit=lambda n: n["bindings"][0].update(stageMask=4))
    fixture.verify("BINDING-DUPLICATE", reflection_edit=lambda n: n["bindings"].append(n["bindings"][0].copy()))
    fixture.verify("PHYSICAL-TYPE", reflection_edit=lambda n: n["webGpuBindingSpace"][0].update(physicalType="Unknown"))
    fixture.verify("BINDLESS-UNAUTHORIZED", reflection_edit=lambda n: n.update(
        bindlessContractVersion="BindlessCross/1", bindlessExecution="Native", bindlessLoweringSha256="0" * 64))
    print(f"GraphicsShaderModuleArtifact/1: {fixture.calls} real combined publication cases pass; identity/provenance only")


if __name__ == "__main__":
    main()
