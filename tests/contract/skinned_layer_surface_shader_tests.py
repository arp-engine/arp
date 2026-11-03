"""Verify the emitted shader ABI against the native actor surface contract."""
from pathlib import Path
import hashlib
import sys
import yaml

engine, generated = map(Path, sys.argv[1:3])
sys.path.insert(0, str(engine / "scripts"))
from spirv_layout import SpirvLayoutFacts

def require(value, message):
    if not value:
        raise RuntimeError(message)

stem = generated / "skinned_layer_surface"
for extension in ("slang", "spv", "metal", "wgsl", "reflection.yaml", "artifact-set.yaml"):
    require(stem.with_suffix("." + extension).is_file(), "Missing shader artifact: " + extension)
publication = yaml.safe_load(stem.with_suffix(".artifact-set.yaml").read_text(encoding="utf-8"))
require(publication["type"] == "GeneratedShaderArtifactSet" and publication["matrixLayout"] == "row-major",
        "Unexpected shader publication contract")
require(set(publication["files"]) == {"source", "spirv", "metal", "wgsl", "reflection"},
        "Incomplete target publication")
for role, record in publication["files"].items():
    path = generated / record["path"]
    require(path.resolve().parent == generated.resolve()
            and hashlib.sha256(path.read_bytes()).hexdigest() == record["sha256"],
            "Shader publication hash/path differs: " + role)
require((engine / "shaders/renderer/skinned_layer_surface.slang").read_bytes() == stem.with_suffix(".slang").read_bytes(),
        "Generated shader source differs from current engine source")
reflection = yaml.safe_load(stem.with_suffix(".reflection.yaml").read_text(encoding="utf-8"))
require(reflection["sourceSha256"] == hashlib.sha256(stem.with_suffix(".slang").read_bytes()).hexdigest(), "Stale source/reflection identity")
require(reflection["pushConstantSizeBytes"] == 128, "Native push extent differs")
members = {m["name"]: m for m in reflection["pushConstantMembers"]}
require({name: member["offset"] for name, member in members.items()} == {
    "viewProjection": 0, "tintAndAlpha": 64, "surfaceParameters": 80,
    "lightDirectionAndAmbient": 96, "lightColorAndIntensity": 112}, "Native member offsets differ")
matrix = members["viewProjection"]
require(matrix["matrixStride"] == 16 and matrix["rowMajor"] and matrix["size"] == 64,
        "Native row-major camera matrix ABI differs")
require({(b["set"], b["binding"], b["type"], b["stageMask"]) for b in reflection["bindings"]} == {
    (0, 0, "UniformBuffer", 1), (1, 0, "SampledTexture", 2), (1, 1, "Sampler", 2)}, "Descriptor ABI differs")
facts = SpirvLayoutFacts(stem.with_suffix(".spv"))
layouts = facts.buffer_layouts()
require(set(layouts) == {(0, 0)}, "Palette descriptor missing or extra buffer present")
palette = layouts[(0, 0)]
require(palette["staticOccupiedExtentBytes"] == 16432 and not palette["hasRuntimeArray"], "Native palette UBO extent differs")
block = palette["blockTypeId"]
require([facts.member_decoration(block, i, 35) for i in range(7)] ==
        [0, 16384, 16400, 16416, 16420, 16424, 16428], "Native palette member offsets differ")
array_wrapper = facts.definition(block, 30)[1][1]
wrapper = facts.definition(array_wrapper, 30)[1]
require(len(wrapper) == 2 and facts.member_decoration(array_wrapper, 0, 35) == 0,
        "Palette array wrapper has unexpected members or offset")
require(facts.matrix_stride(array_wrapper, 0) == 16,
        "Palette matrices do not share the native row-major byte contract")
array_type = wrapper[1]
require(facts.decoration(array_type, 6) == 64 and facts.array_length(facts.definition(array_type, 28)[1][2]) == 256,
        "Native palette matrix count/stride differs")
require([b["occupiedExtentBytes"] for b in facts.push_constant_layouts()] == [128], "Binary push extent differs from reflection")
require([(a["location"], a["format"]) for a in reflection["vertexAttributes"]] ==
        [(0, "Float3"), (1, "Float3"), (2, "Float2"), (3, "Float4"), (4, "Float4")], "Native two-stream vertex layout differs")
for target in ("webGpuBindingSpace", "metalBindingSpace"):
    require(reflection[target + "Status"] == "Available" and all(b["active"] for b in reflection[target]), target + " unavailable")
require(reflection["pushConstantWebGpuBinding"]["group"] not in {b["group"] for b in reflection["webGpuBindingSpace"]}, "WGSL push slot collision")
if len(sys.argv) == 4:
    evidence = {
        "type": "arp_skinned_layer_surface_shader_verification", "contractVersion": 1, "status": "passed",
        "shader_abi_verified": True, "gpu_verified": False, "engine_root": str(engine.resolve()),
        "engine_sources": {name: "sha256:" + hashlib.sha256((engine / name).read_bytes()).hexdigest()
            for name in ("shaders/renderer/skinned_layer_surface.slang", "shaders/core/skinning_geometry.slang",
                         "src/renderer/mesh/gpu_skinning_contract.h",
                         "scripts/compile_shaders.py", "scripts/spirv_layout.py",
                         "tests/contract/skinned_layer_surface_shader_tests.py")},
        "artifact_root": str(generated.resolve()),
        "artifacts": {record["path"]: "sha256:" + record["sha256"] for record in publication["files"].values()},
        "push_bytes": 128, "palette_bytes": 16432, "max_joints": 256,
        "validation": "compiled binary layout, generated publication and target binding contracts; no GPU execution",
    }
    Path(sys.argv[3]).write_text(yaml.safe_dump(evidence, sort_keys=False), encoding="utf-8")
print("Actor surface shader ABI: SPIR-V buffer/matrix facts, native push offsets, descriptors, vertex inputs and MSL/WGSL bindings passed")
