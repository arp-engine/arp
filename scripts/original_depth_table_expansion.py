"""OriginalDepthTableScalarExpansion/1: closed, Slang-only offline lowering.

This publication deliberately has no ordinary pipeline or runtime reader. Its
logical table remains capacity3; scalar physical reflection is not L2 authority.
"""
from dataclasses import dataclass
import hashlib
import pathlib
import subprocess

import yaml

import compile_shaders as compiler
from slang_resource_switch import SlangResourceSwitch
from spirv_layout import SpirvLayoutFacts


class OriginalTableExpansionError(ValueError):
    pass


def reject(reason):
    raise OriginalTableExpansionError("RHI-ORIGINAL-TABLE-EXPANSION-" + reason)


def digest(data):
    return hashlib.sha256(data).hexdigest()


@dataclass(frozen=True)
class GeneratedOriginalTable:
    source: str
    metadata: dict


class OriginalDepthTableExpansion:
    VERSION = 1
    PROFILES = {
        "Compute": ("a5c13f383abb647fc2b64202a7f9ad96a0beed5961089a5445fc97d6495ceacd", 4,
            [("computeMain", "Compute")], "Inputs[thread.x % 3].Load(int3(0, 0, 0))",
            "arpOriginalInputsLoad(thread.x % 3, int3(0, 0, 0))"),
        "Graphics": ("e7efff11f4e28fc1e6294a5ed3c1673837e9ee9756dae6628ff3d3baab0fa4e8", 3,
            [("vertexMain", "Vertex"), ("fragmentMain", "Fragment")],
            "Inputs[element].Load(int3(0, 0, 0))", "arpOriginalInputsLoad(element, int3(0, 0, 0))"),
        "GraphicsGeometry": ("06755dad27f07d0446f8631b802213b3da3c51f6866c06ffd1c80f8bcdd061b5", 3,
            [("vertexMain", "Vertex"), ("fragmentMain", "Fragment")],
            "Inputs[element].Load(int3(0, 0, 0))", "arpOriginalInputsLoad(element, int3(0, 0, 0))"),
    }
    FIELDS = {"type", "contractVersion", "profile", "sourceSha256", "capacity",
        "maxPhysicalBindings", "maxSwitchCases", "maxArtifactBytes"}

    @classmethod
    def generate(cls, schema, original):
        if not isinstance(schema, dict) or set(schema) != cls.FIELDS:
            reject("SCHEMA")
        if schema["type"] != "OriginalDepthTableScalarExpansion" or type(schema["contractVersion"]) is not int or schema["contractVersion"] != cls.VERSION:
            reject("VERSION")
        profile = schema["profile"]
        if not isinstance(profile, str) or profile not in cls.PROFILES:
            reject("PROFILE")
        expected_hash, bindings, entries, expression, replacement = cls.PROFILES[profile]
        if not isinstance(original, bytes) or schema["sourceSha256"] != expected_hash or digest(original) != expected_hash:
            reject("SOURCE-IDENTITY")
        for field in ("capacity", "maxPhysicalBindings", "maxSwitchCases", "maxArtifactBytes"):
            if type(schema[field]) is not int or not 1 <= schema[field] <= 0xFFFFFFFF:
                reject("BUDGET")
        if schema["capacity"] != 3:
            reject("CAPACITY")
        if schema["maxPhysicalBindings"] < bindings or schema["maxSwitchCases"] < 3:
            reject("BUDGET")
        source = original.decode("utf-8")
        declaration = "[[vk::binding(0, 0)]] ARP_SAMPLED_DEPTH(Inputs)[3];"
        depth_type = "#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name"
        if any(source.count(site) != 1 for site in (declaration, expression, depth_type)):
            reject("PROGRAM-SHAPE")
        switch = SlangResourceSwitch(3, schema["maxSwitchCases"])
        names = [switch.resource_name("Inputs", i) for i in range(3)]
        physical = "\n".join(f"[[vk::binding({i}, 0)]] ARP_SAMPLED_DEPTH({name});"
            for i, name in enumerate(names))
        helper = ["float arpOriginalInputsLoad(uint index, int3 location)", "{"]
        helper += switch.emit("index", lambda i: [f"return {names[i]}.Load(location);"])
        helper += ["    return 0;", "}"]
        # The explicit L2 depth annotation is lowered in Slang itself. No
        # generated WGSL/MSL type or operation edits are used or permitted.
        source = source.replace(depth_type, "#define ARP_SAMPLED_DEPTH(name) DepthTexture2D name")
        source = source.replace(declaration, physical + "\n\n" + "\n".join(helper))
        source = source.replace(expression, replacement)
        metadata = dict(type="OriginalDepthTableScalarExpansion", contractVersion=cls.VERSION,
            schema=dict(schema), matrixLayout="row-major", executionAvailable=False,
            originalSourceSha256=expected_hash, generatedSourceSha256=digest(source.encode()),
            logicalTable=dict(name="Inputs", set=0, binding=0, capacity=3,
                type="SampledTexture", depthTexture=True, dimension="D2",
                stage="Compute" if profile == "Compute" else "Fragment"),
            elementMap=[dict(arrayElement=i, name=name, set=0, binding=i) for i, name in enumerate(names)],
            physicalBindingCount=bindings, switchCases=3,
            # Frozen publication/1 scope, not the current RHI Backend enum.
            # WebGL/ES100 requires a separate approved schema/package contract.
            backendDecisions={name: dict(mode="DiagnosticFailure", executionAvailable=False,
                diagnostic="RHI-ORIGINAL-TYPED-INPUT-TABLE-PROFILE-UNAVAILABLE")
                for name in ("Null", "Vulkan", "Metal", "WebGPU")})
        return GeneratedOriginalTable(source, metadata)

    @classmethod
    def verify_entry(cls, generated, entry, stage, reflection):
        mask = {"Vertex": 1, "Fragment": 2, "Compute": 4}[stage]
        actual = reflection.get("bindings")
        expected = [] if stage == "Vertex" else [dict(name=m["name"], type="SampledTexture",
            set=0, binding=m["binding"], arrayCount=1, stageMask=mask, depthTexture=True)
            for m in generated.metadata["elementMap"]]
        if stage == "Compute":
            expected.append(dict(name="Output", type="StorageBuffer", set=1, binding=0,
                arrayCount=1, stageMask=4))
        if yaml.safe_dump(actual, sort_keys=True) != yaml.safe_dump(expected, sort_keys=True) or reflection.get("sourceSha256") != generated.metadata["generatedSourceSha256"]:
            reject("REFLECTION")
        entries = reflection.get("entryPoints", [])
        if len(entries) != 1 or entries[0].get("name") != entry or entries[0].get("stage") != stage:
            reject("ENTRY")
        # Vertex layout is part of the trusted complete program, not inferred
        # from a supplied pipeline or from the scalar resource interface.
        expected_vertex = []
        if stage == "Vertex" and generated.metadata["schema"]["profile"] == "GraphicsGeometry":
            expected_vertex = [(0, "Float2"), (1, "Float2"), (2, "Float4")]
        attributes = reflection.get("vertexAttributes", [])
        if [(a.get("location"), a.get("format")) for a in attributes] != expected_vertex:
            reject("VERTEX-ABI")
        if stage == "Compute" and any(entries[0].get("computeWorkgroupSize" + axis) != value
                for axis, value in (("X", 5), ("Y", 1), ("Z", 1))):
            reject("WORKGROUP")
        active_map = [m for m in reflection.get("webGpuBindingSpace", []) if m.get("active")]
        if len(active_map) != len(expected) or reflection.get("webGpuBindingSpaceStatus") != "Available":
            reject("PHYSICAL-MAP")
        for logical, physical in zip(expected, active_map):
            if (any(physical.get(key) != logical[key] for key in ("name", "set", "binding"))
                    or physical.get("capacity") != 1 or physical.get("arrayElement") != 0
                    or physical.get("group") != logical["set"] or physical.get("webGpuBinding") != logical["binding"]
                    or physical.get("physicalType") != logical["type"]
                    or physical.get("depthTexture", False) != logical.get("depthTexture", False)):
                reject("PHYSICAL-MAP")

    @classmethod
    def verify_targets(cls, generated, stage, paths, reflection):
        logical = compiler.get_shader_bindings(generated.source)
        # Reuse the binary parser, not a resource-name or output-text inference,
        # for the exact three non-array, non-MS, sampled 2D depth resource types.
        facts = SpirvLayoutFacts(paths["spirv"])
        images = []
        for variable in facts.variables:
            if variable[2] != 0:  # SPIR-V UniformConstant storage class.
                continue
            _, pointer = facts.definition(variable[0], opcode=32, count=3)
            _, image = facts.definition(pointer[2], opcode=25, count=8)
            # OpTypeImage: result, sampled type, Dim, Depth, Arrayed, MS,
            # Sampled, Image Format. Dim2D=1; Depth=1 is explicit depth.
            if tuple(image[2:]) != (1, 1, 0, 0, 1, 0): reject("SPIRV-DEPTH-ABI")
            images.append((facts.decoration(variable[1], 34), facts.decoration(variable[1], 33)))
        if sorted(images) != ([] if stage == "Vertex" else [(0, 0), (0, 1), (0, 2)]):
            reject("SPIRV-ELEMENT-MAP")
        wgsl = paths["wgsl"].read_text()
        if "binding_array" in wgsl or (stage != "Vertex" and wgsl.count("texture_depth_2d") != 3):
            reject("TARGET-DEPTH-ABI")
        webgpu = compiler.get_webgpu_binding_space_lowering(logical, str(paths["wgsl"]))
        if yaml.safe_dump(webgpu, sort_keys=True) != yaml.safe_dump(reflection["webGpuBindingSpace"], sort_keys=True): reject("WGSL-REFLECTION-MAP")
        metal = compiler.get_metal_binding_space_lowering(logical, str(paths["metal"]))
        if yaml.safe_dump(metal, sort_keys=True) != yaml.safe_dump(reflection["metalBindingSpace"], sort_keys=True): reject("METAL-REFLECTION-MAP")


def compile_expansion(schema, original, out_dir, slangc, spirv_reflect):
    output = pathlib.Path(out_dir).resolve(); output.mkdir(parents=True, exist_ok=True)
    publication = output / "original-depth-table.expansion.yaml"
    publication.unlink(missing_ok=True)
    generated = OriginalDepthTableExpansion.generate(schema, original)
    original_path = output / "original.slang"; original_path.write_bytes(original)
    source_path = output / "expanded.slang"; source_path.write_bytes(generated.source.encode())
    logical = compiler.get_shader_bindings(generated.source)
    entries = []
    for entry, stage in OriginalDepthTableExpansion.PROFILES[schema["profile"]][2]:
        paths = {role: output / (entry + "." + suffix) for role, suffix in
            (("spirv", "spv"), ("metal", "metal"), ("wgsl", "wgsl"), ("reflection", "reflection.yaml"))}
        for target in ("spirv", "metal", "wgsl"):
            compiler.run_slangc(str(slangc), str(source_path), target,
                "spirv_1_5" if target == "spirv" else "sm_6_0", [entry], str(paths[target]), include_dirs=[])
        masks = compiler._bindings_codegen.reflect_descriptor_stage_masks(
            str(spirv_reflect), str(paths["spirv"]), allow_empty=True)
        compiler.write_shader_reflection(str(source_path), "OriginalDepthTableScalarExpansion",
            str(paths["spirv"]), str(paths["metal"]), str(paths["wgsl"]), str(paths["reflection"]),
            None, spirv_reflect_exe=str(spirv_reflect), descriptor_stage_masks=masks,
            entry_names=[entry], metal_binding_space=compiler.get_metal_binding_space_lowering(logical, str(paths["metal"])),
            metal_argument_buffer=compiler.get_metal_argument_buffer_lowering(logical, str(paths["metal"])), single_entry=True)
        reflection = yaml.safe_load(paths["reflection"].read_text())
        OriginalDepthTableExpansion.verify_entry(generated, entry, stage, reflection)
        OriginalDepthTableExpansion.verify_targets(generated, stage, paths, reflection)
        entries.append(dict(entry=entry, stage=stage,
            files={role: dict(path=path.name, sha256=digest(path.read_bytes())) for role, path in paths.items()}))
    version = subprocess.run([str(slangc), "-version"], check=True, capture_output=True, text=True)
    node = dict(generated.metadata, compilerVersion=(version.stdout + version.stderr).strip(),
        originalSource=dict(path=original_path.name, sha256=digest(original)),
        generatedSource=dict(path=source_path.name, sha256=digest(source_path.read_bytes())), entries=entries)
    artifacts = [original_path, source_path] + [output / f["path"] for e in entries for f in e["files"].values()]
    size = sum(p.stat().st_size for p in artifacts)
    node["artifactBytes"] = size
    if size > schema["maxArtifactBytes"]:
        reject("ARTIFACT-BUDGET")
    # Validate the entire staged publication before its final atomic rename.
    # Neither compilation nor validation failure can retain a success ticket.
    staged = output / "original-depth-table.expansion.pending.yaml"
    try:
        staged.write_bytes(yaml.safe_dump(node, sort_keys=False).encode())
        read_expansion(staged)
        staged.replace(publication)
    finally:
        staged.unlink(missing_ok=True)
    return publication


def read_expansion(path):
    path = pathlib.Path(path)
    try:
        node = yaml.safe_load(path.read_text())
        if not isinstance(node, dict): reject("PUBLICATION")
        def read_file(f, expected_path):
            if not isinstance(f, dict) or set(f) != {"path", "sha256"} or f["path"] != expected_path:
                reject("FILE-PATH")
            data = (path.parent / expected_path).read_bytes()
            if digest(data) != f["sha256"]: reject("FILE-HASH")
            return data
        original = read_file(node["originalSource"], "original.slang")
        generated = OriginalDepthTableExpansion.generate(node["schema"], original)
        if read_file(node["generatedSource"], "expanded.slang") != generated.source.encode(): reject("PROGRAM")
        extra = {"compilerVersion", "originalSource", "generatedSource", "entries", "artifactBytes"}
        if set(node) != set(generated.metadata) | extra or yaml.safe_dump(
                {k: node[k] for k in generated.metadata}, sort_keys=True) != yaml.safe_dump(generated.metadata, sort_keys=True):
            reject("PUBLICATION")
        if not isinstance(node["compilerVersion"], str) or not node["compilerVersion"]: reject("COMPILER-IDENTITY")
        expected_entries = OriginalDepthTableExpansion.PROFILES[node["schema"]["profile"]][2]
        if len(node["entries"]) != len(expected_entries): reject("ENTRY")
        total = len(original) + len(generated.source.encode())
        for e, (entry, stage) in zip(node["entries"], expected_entries):
            if set(e) != {"entry", "stage", "files"} or e["entry"] != entry or e["stage"] != stage or set(e["files"]) != {"spirv", "metal", "wgsl", "reflection"}:
                reject("ENTRY")
            for role, suffix in (("spirv", "spv"), ("metal", "metal"), ("wgsl", "wgsl"), ("reflection", "reflection.yaml")):
                data = read_file(e["files"][role], entry + "." + suffix); total += len(data)
                if role == "reflection":
                    OriginalDepthTableExpansion.verify_entry(generated, entry, stage, yaml.safe_load(data))
            paths = {role: path.parent / f["path"] for role, f in e["files"].items()}
            OriginalDepthTableExpansion.verify_targets(generated, stage, paths, yaml.safe_load(data))
        if type(node["artifactBytes"]) is not int or node["artifactBytes"] != total or total > node["schema"]["maxArtifactBytes"]:
            reject("ARTIFACT-BUDGET")
        return node
    except OriginalTableExpansionError:
        raise
    except (OSError, KeyError, TypeError, ValueError, RuntimeError, yaml.YAMLError) as error:
        reject("PUBLICATION-INVALID: " + str(error))


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=pathlib.Path, required=True)
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    parser.add_argument("--slangc", required=True)
    parser.add_argument("--spirv-reflect", required=True)
    parser.add_argument("--cpp-identity", type=pathlib.Path)
    args = parser.parse_args()
    if args.cpp_identity:
        args.cpp_identity.unlink(missing_ok=True)
    publication = compile_expansion(yaml.safe_load(args.schema.read_text()), args.source.read_bytes(),
        args.out_dir, args.slangc, args.spirv_reflect)
    if args.cpp_identity:
        # Only freshly compiled approved programs issue this build provenance.
        # This is deliberately not a reader of arbitrary authored manifests.
        node = read_expansion(publication)
        prefix = node["schema"]["profile"].lower()
        namespace = 'arp::shader::compiler_contract'
        if node["schema"]["profile"] == "GraphicsGeometry":
            namespace += '::graphicsgeometry'
        lines = ['#pragma once', 'namespace ' + namespace + ' {',
            'inline constexpr char ' + prefix + 'OriginalSha256[] = "' + node['originalSourceSha256'] + '";',
            'inline constexpr char ' + prefix + 'CompilerVersionSha256[] = "' + digest(node['compilerVersion'].encode()) + '";',
            'inline constexpr char ' + prefix + 'GeneratedSha256[] = "' + node['generatedSourceSha256'] + '";']
        for entry in node['entries']:
            for role, file in entry['files'].items():
                lines.append('inline constexpr char ' + entry['entry'] + role + 'Sha256[] = "' + file['sha256'] + '";')
        lines.append('}')
        args.cpp_identity.parent.mkdir(parents=True, exist_ok=True)
        args.cpp_identity.write_text('\n'.join(lines) + '\n', encoding='ascii')
    print(publication)
