"""BindlessCross/1 typed Slang helper generator.

This is an offline compiler component. It emits Slang, never handwritten WGSL
or MSL, from a closed operation schema. Callers compile the returned source
through the ordinary Slang target and reflection pipeline. No runtime capability
is implied by successful generation. Invalid indices return a fault bit and do
not execute a resource operation; consumers must latch that bit for readback.
"""

from dataclasses import dataclass
import hashlib
import re
from slang_resource_switch import SlangResourceSwitch
from buffer_vector_record import BufferVectorRecord


class BindlessContractError(ValueError):
    pass


def reject(reason):
    raise BindlessContractError("RHI-BINDLESS-" + reason)


def integer(value, name, minimum=0):
    if type(value) is not int or not minimum <= value <= 0xFFFFFFFF:
        reject("INVALID-" + name.upper())
    return value


def identifier(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", value):
        reject("INVALID-IDENTIFIER")
    return value


@dataclass(frozen=True)
class GeneratedBindless:
    source: str
    reflection: dict


_KINDS = {
    "SampledTexture": "Texture2D<float4>",
    "Sampler": "SamplerState",
    "StorageTexture": "RWTexture2D<float>",
    "UniformBuffer": "ConstantBuffer<ArpBindlessUniform>",
    "ReadOnlyStorageBuffer": "StructuredBuffer<uint>",
    "StorageBuffer": "RWStructuredBuffer<uint>",
}
_OPS = {
    "SampledTexture": {"load", "query", "sampleLevel", "sampleGrad"},
    "Sampler": set(),
    "StorageTexture": {"load", "store", "query"},
    "UniformBuffer": {"load"},
    "ReadOnlyStorageBuffer": {"load", "query"},
    "StorageBuffer": {"load", "store", "query", "atomicAdd", "atomicOr"},
}
_STAGES = {"vertex": 1, "fragment": 2, "compute": 4}


class BindlessPushConstantContract:
    """Explicit AtomicFullBlockPush/1 compiler intent; runtime admission is separate."""
    @staticmethod
    def normalize(document):
        if "pushConstants" not in document:
            return None
        raw = document["pushConstants"]
        if (document.get("pipelineInterfaceContractVersion") != 1
                or not isinstance(raw, dict)
                or set(raw) != {"contractVersion", "sizeBytes", "stages",
                                "webGpuGroup", "webGpuBinding"}):
            reject("PUSH-SCHEMA")
        if type(raw["contractVersion"]) is not int or raw["contractVersion"] != 1:
            reject("PUSH-VERSION")
        # AtomicFullBlockPush/1's L2 ceiling, not a native-device capability.
        if type(raw["sizeBytes"]) is not int or not 0 < raw["sizeBytes"] <= 256 or raw["sizeBytes"] % 4:
            reject("PUSH-SIZE")
        stages = raw["stages"]
        if (not isinstance(stages, list) or not stages
                or any(not isinstance(s, str) or s not in _STAGES for s in stages)
                or len(set(stages)) != len(stages)
                or ("compute" in stages and len(stages) != 1)):
            reject("PUSH-STAGES")
        for field in ("webGpuGroup", "webGpuBinding"):
            integer(raw[field], "push-" + field)
        return {k: v for k, v in raw.items() if k != "stages"} | {
            "stageMask": sum(_STAGES[s] for s in stages)}

    @staticmethod
    def verify_targets(compiler, intent, paths, logical, binary_facts, mode):
        blocks = binary_facts.push_constant_layouts()
        if len(blocks) != 1 or blocks[0]["occupiedExtentBytes"] != intent["sizeBytes"]:
            reject("PUSH-BINARY-SIZE")
        contract = dict(webgpuGroup=None, webgpuBinding=None,
                        metalBufferIndex=compiler.get_metal_push_constant_buffer_index(paths["metal"]))
        if mode == "Emulated":
            contract.update(webgpuGroup=intent["webGpuGroup"], webgpuBinding=intent["webGpuBinding"])
            compiler.set_wgsl_push_constant_binding(paths["wgsl"], contract)
            for binding in compiler.get_webgpu_binding_space_lowering(logical, paths["wgsl"]):
                if binding.get("active") and binding["group"] == contract["webgpuGroup"]:
                    reject("PUSH-WEBGPU-GROUP-COLLISION")
            # Reuse the ordinary compiler's complete reserved-group check;
            # its setter intentionally permits absent blocks for legacy tools.
            try:
                compiler.validate_wgsl_reserved_group(paths["wgsl"], contract)
            except SystemExit:
                reject("PUSH-WEBGPU-DECLARATION")
        return contract


class OrdinaryOpaqueBindingContract:
    """Explicit scalar descriptor intent; target declarations grant no execution."""
    dimensions = {"Texture2D": (1, False), "Texture2DArray": (1, True),
                  "Texture3D": (2, False), "TextureCube": (3, False)}

    @classmethod
    def normalize(cls, document):
        if "ordinaryOpaqueBindings" not in document:
            return []
        raw = document["ordinaryOpaqueBindings"]
        if (document.get("pipelineInterfaceContractVersion") != 1 or not isinstance(raw, list)
                or not raw or len(raw) > document["maxBindings"]):
            reject("PIPELINE-OPAQUE-SCHEMA")
        output, keys, names = [], set(), set()
        for item in raw:
            if not isinstance(item, dict):
                reject("PIPELINE-OPAQUE-SCHEMA")
            kind = item.get("type")
            fields = {"name", "set", "binding", "type", "stages"}
            fields |= ({"textureViewDimension", "depthTexture", "sampledTextureFilterable"}
                       if kind == "SampledTexture" else {"samplerFiltering"})
            if kind not in ("SampledTexture", "Sampler") or set(item) != fields:
                reject("PIPELINE-OPAQUE-SCHEMA")
            name = item["name"]
            if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name):
                reject("PIPELINE-OPAQUE-NAME")
            if any(type(item[k]) is not int or not 0 <= item[k] <= 0xffffffff for k in ("set", "binding")):
                reject("PIPELINE-OPAQUE-LOCATION")
            key = (item["set"], item["binding"])
            stages = item["stages"]
            if (key[0] == document["set"] or key in keys or name in names
                    or not isinstance(stages, list) or not stages
                    or any(not isinstance(stage, str) or stage not in _STAGES for stage in stages)
                    or len(set(stages)) != len(stages)):
                reject("PIPELINE-OPAQUE-IDENTITY")
            keys.add(key); names.add(name)
            normalized = {k: v for k, v in item.items() if k != "stages"}
            normalized.update(arrayCount=1, stageMask=sum(_STAGES[stage] for stage in stages))
            if kind == "SampledTexture":
                if (not isinstance(item["textureViewDimension"], str)
                        or item["textureViewDimension"] not in cls.dimensions
                        or type(item["depthTexture"]) is not bool
                        or type(item["sampledTextureFilterable"]) is not bool
                        or (item["depthTexture"] and (item["textureViewDimension"] == "Texture3D"
                                                      or item["sampledTextureFilterable"]))):
                    reject("PIPELINE-OPAQUE-IMAGE-SHAPE")
            elif item["samplerFiltering"] not in ("Filtering", "NonFiltering", "Comparison"):
                reject("PIPELINE-OPAQUE-SAMPLER")
            output.append(normalized)
        return output

    @classmethod
    def verify_spirv(cls, binding, facts):
        if facts.get("descriptorCount") != 1:
            reject("PIPELINE-OPAQUE-BINARY")
        if binding["type"] == "Sampler":
            if facts.get("kind") != "Sampler":
                reject("PIPELINE-OPAQUE-BINARY")
            return
        dimension, arrayed = cls.dimensions[binding["textureViewDimension"]]
        if (facts.get("kind") != "Image" or facts.get("dimension") != dimension
                or facts.get("arrayed") != arrayed or facts.get("multisampled")
                or facts.get("sampled") != 1 or facts.get("imageFormat") != 0
                or facts.get("scalarKind") != "Float" or facts.get("scalarWidth") != 32
                or (facts.get("depth") != 1 if binding["depthTexture"] else facts.get("depth") not in (0, 2))):
            reject("PIPELINE-OPAQUE-BINARY")


def zero_base_indirect_producer_schema():
    return dict(type="BindlessShaderVariant", contractVersion=1,
        name="ArpZeroBaseIndirectProducer", set=0, maxBindings=8,
        maxSwitchCases=5, snapshotBytes=4096, snapshotMetadataBinding=2,
        faultLatchBinding=3, indirectInstanceBaseContractVersion=1,
        zeroBaseIndirectProducerContractVersion=1, tables=[
            dict(name="LogicalArgs", tableId=1, kind="ReadOnlyStorageBuffer", capacity=1,
                binding=4, stages=["compute"], operations=["load", "query"]),
            dict(name="PhysicalArgs", tableId=2, kind="StorageBuffer", capacity=1,
                binding=5, stages=["compute"], operations=["store"]),
            dict(name="InstanceMappings", tableId=3, kind="StorageBuffer", capacity=1,
                binding=6, stages=["compute"], operations=["store"]),
            dict(name="ProducerParams", tableId=4, kind="UniformBuffer", capacity=1,
                binding=7, stages=["compute"], operations=["load"])])


def zero_base_indirect_consumer_schema():
    """ClosedImageMappingConsumer/1; explicit first supported specialization."""
    capacity = 3
    metadata_bytes = (4 + 4 + capacity) * 4
    return dict(type="BindlessShaderVariant", contractVersion=1,
        name="ArpZeroBaseIndirectConsumer", set=0, maxBindings=8,
        maxSwitchCases=capacity, snapshotBytes=metadata_bytes + 2 * 4, snapshotMetadataBinding=1,
        faultLatchBinding=2, indirectInstanceBaseContractVersion=1,
        zeroBaseIndirectConsumerContractVersion=1,
        indirectInstanceConsumer=dict(binding=7, recordIndex=0, instanceCapacity=capacity),
        tables=[dict(name="Images", tableId=1, kind="SampledTexture", capacity=capacity,
            binding=3, stages=["fragment"], operations=["load"])])


_ZERO_BASE_INDIRECT_CONSUMER_ENTRIES = ["arpZeroBaseIndirectConsumerVertex", "arpZeroBaseIndirectConsumerFragment"]
_ZERO_BASE_INDIRECT_CONSUMER_BODY = """struct ArpZeroBaseIndirectConsumerVertexOutput {
    float4 position : SV_Position;
    nointerpolation uint logicalInstance : TEXCOORD0;
    nointerpolation uint instanceFault : TEXCOORD1;
};
[shader("vertex")]
ArpZeroBaseIndirectConsumerVertexOutput arpZeroBaseIndirectConsumerVertex(
    float2 position : POSITION, uint instanceId : SV_InstanceID) {
    ArpZeroBaseIndirectConsumerVertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    uint logicalInstance;
    bool valid = arpBindlessLoadIndirectInstance(instanceId, logicalInstance);
    output.logicalInstance = logicalInstance;
    output.instanceFault = valid ? 0u : 1u;
    return output;
}
[shader("fragment")]
float4 arpZeroBaseIndirectConsumerFragment(ArpZeroBaseIndirectConsumerVertexOutput input) : SV_Target0 {
    if (input.instanceFault != 0u) {
        arpBindlessReportFault();
        return float4(1.0, 0.0, 1.0, 1.0);
    }
    bool fault;
    return Images_load(input.logicalInstance, int3(0, 0, 0), fault);
}
"""


_ZERO_BASE_INDIRECT_ENTRY = "arpZeroBaseIndirectMain"
_ZERO_BASE_INDIRECT_BODY = """[shader("compute")]
[numthreads(1, 1, 1)]
void arpZeroBaseIndirectMain() {
    bool fault;
    uint4 parameters = ProducerParams_load(0u, fault);
    uint wordCount = LogicalArgs_query(0u, fault);
    ArpBindlessIndirectCommand physical;
    physical.indexCount = 0u; physical.instanceCount = 0u;
    physical.firstIndex = 0u; physical.vertexOffset = 0; physical.firstInstance = 0u;
    uint4 mapping = uint4(0u);
    if (fault || parameters.z != 0u || parameters.w != 1u
        || parameters.x > wordCount || wordCount - parameters.x < 5u) {
        arpBindlessReportFault();
    } else {
        uint first = parameters.x;
        uint indexCount = LogicalArgs_load(0u, first, fault);
        uint instanceCount = LogicalArgs_load(0u, first + 1u, fault);
        uint firstIndex = LogicalArgs_load(0u, first + 2u, fault);
        int vertexOffset = asint(LogicalArgs_load(0u, first + 3u, fault));
        uint firstInstance = LogicalArgs_load(0u, first + 4u, fault);
        arpBindlessEncodeIndirectInstanceBase(indexCount, instanceCount, firstIndex,
            vertexOffset, firstInstance, parameters.y, physical, mapping);
    }
    PhysicalArgs_store(0u, 0u, physical.indexCount, fault);
    PhysicalArgs_store(0u, 1u, physical.instanceCount, fault);
    PhysicalArgs_store(0u, 2u, physical.firstIndex, fault);
    PhysicalArgs_store(0u, 3u, asuint(physical.vertexOffset), fault);
    PhysicalArgs_store(0u, 4u, 0u, fault);
    InstanceMappings_store(0u, 0u, mapping.x, fault);
    InstanceMappings_store(0u, 1u, mapping.y, fault);
    InstanceMappings_store(0u, 2u, mapping.z, fault);
    InstanceMappings_store(0u, 3u, mapping.w, fault);
}
"""


def generate_bindless(document, mode):
    """Generate one finite Native or Emulated variant; publication is caller-owned.

    The base helper ABI deliberately supports float4 sampled 2D textures, r32float
    read/write storage textures and uint storage
    buffers. A uniform entry is float4. Other shapes/layouts require a versioned
    extension rather than guessed conversion. TextureShape/1 additionally
    supports sampled 2DArray with an explicit integer layer. Implicit derivatives and compare
    sampling are rejected until a compiler proof is available.
    StorageImage/1 adds four-lane rgba16float WriteOnly via Slang WTexture2D;
    subsequent reads use a sampled view and a separately declared dependency.
    """
    if mode not in ("Native", "Emulated"):
        reject("EXECUTION-MODE")
    if not isinstance(document, dict) or document.get("type") != "BindlessShaderVariant":
        reject("SCHEMA-TYPE")
    if type(document.get("contractVersion")) is not int or document["contractVersion"] != 1:
        reject("CONTRACT-VERSION")
    allowed = {"type", "contractVersion", "name", "set", "maxSwitchCases", "maxBindings", "snapshotBytes", "tables", "snapshotMetadataBinding", "faultLatchBinding", "indirectInstanceBaseContractVersion", "indirectInstanceConsumer", "zeroBaseIndirectProducerContractVersion", "zeroBaseIndirectConsumerContractVersion", "textureShapeContractVersion"}
    allowed.add("vectorRecordContractVersion")
    allowed.add("sampledDepthContractVersion")
    allowed.add("storageImageContractVersion")
    allowed.add("pipelineInterfaceContractVersion")
    allowed.add("ordinaryOpaqueBindings")
    allowed.add("pushConstants")
    allowed.add("gpuVisibilityKernelContractVersion")
    allowed.add("gpuVisibilityConsumerContractVersion")
    if set(document) - allowed:
        reject("SCHEMA-FIELD")
    pipeline_version = document.get("pipelineInterfaceContractVersion", 0)
    if type(pipeline_version) is not int or pipeline_version not in (0, 1):
        reject("PIPELINE-INTERFACE-VERSION")
    push_constants = BindlessPushConstantContract.normalize(document)
    visibility_version = 0
    visibility_consumer = document.get("gpuVisibilityConsumerContractVersion", 0)
    if type(visibility_consumer) is not int or visibility_consumer not in (0, 2):
        reject("GPU-VISIBILITY-CONSUMER-VERSION")
    if "gpuVisibilityConsumerContractVersion" in document and not visibility_consumer:
        reject("GPU-VISIBILITY-CONSUMER-VERSION")
    if "gpuVisibilityKernelContractVersion" in document:
        from gpu_visibility_codegen import schema as visibility_schema
        visibility_version = integer(document["gpuVisibilityKernelContractVersion"],
                                     "gpu-visibility-kernel-version", 1)
        if visibility_version != 1 or document != visibility_schema(bool(visibility_consumer)):
            reject("GPU-VISIBILITY-KERNEL-SCHEMA")
    elif visibility_consumer:
        reject("GPU-VISIBILITY-CONSUMER-PRODUCER")
    record_version = document.get("vectorRecordContractVersion", 0)
    if type(record_version) is not int or record_version not in (0, 1):
        reject("RECORD-VERSION")
    shape_version = document.get("textureShapeContractVersion", 0)
    if type(shape_version) is not int or shape_version not in (0, 1):
        reject("TEXTURE-SHAPE-VERSION")
    depth_version = document.get("sampledDepthContractVersion", 0)
    if type(depth_version) is not int or depth_version not in (0, 1):
        reject("SAMPLED-DEPTH-VERSION")
    storage_version = document.get("storageImageContractVersion", 0)
    if type(storage_version) is not int or storage_version not in (0, 1):
        reject("STORAGE-IMAGE-VERSION")
    producer_version = 0
    if "zeroBaseIndirectProducerContractVersion" in document:
        producer_version = integer(document["zeroBaseIndirectProducerContractVersion"], "zero-base-indirect-producer-version", 1)
        if producer_version != 1:
            reject("ZERO-BASE-INDIRECT-PRODUCER-VERSION")
        if document != zero_base_indirect_producer_schema():
            reject("ZERO-BASE-INDIRECT-PRODUCER-SCHEMA")
    consumer_version = 0
    if "zeroBaseIndirectConsumerContractVersion" in document:
        consumer_version = integer(document["zeroBaseIndirectConsumerContractVersion"], "zero-base-indirect-consumer-version", 1)
        if consumer_version != 1:
            reject("ZERO-BASE-INDIRECT-CONSUMER-VERSION")
        if document != zero_base_indirect_consumer_schema():
            reject("ZERO-BASE-INDIRECT-CONSUMER-SCHEMA")
    variant = identifier(document.get("name"))
    group = integer(document.get("set"), "set")
    max_cases = integer(document.get("maxSwitchCases"), "switch-budget", 1)
    max_bindings = integer(document.get("maxBindings"), "binding-budget", 1)
    ordinary_opaque = OrdinaryOpaqueBindingContract.normalize(document)
    snapshot_bytes = integer(document.get("snapshotBytes"), "snapshot-bytes", 1)
    raw_tables = document.get("tables")
    if not isinstance(raw_tables, list) or not raw_tables:
        reject("TABLES-EMPTY")
    tables = {}
    ids = set()
    metadata_binding = integer(document.get("snapshotMetadataBinding"), "metadata-binding")
    if metadata_binding >= max_bindings:
        reject("BINDING-BUDGET")
    occupied = {metadata_binding}
    fault_binding = None
    if "faultLatchBinding" in document:
        fault_binding = integer(document["faultLatchBinding"], "fault-binding")
        if fault_binding >= max_bindings:
            reject("BINDING-BUDGET")
        if fault_binding in occupied:
            reject("PHYSICAL-BINDING-COLLISION")
        occupied.add(fault_binding)
    indirect_base_version = 0
    if "indirectInstanceBaseContractVersion" in document:
        indirect_base_version = integer(document["indirectInstanceBaseContractVersion"], "indirect-instance-base-version", 1)
        if indirect_base_version != 1:
            reject("INDIRECT-INSTANCE-BASE-VERSION")
        if fault_binding is None:
            reject("INDIRECT-INSTANCE-BASE-FAULT-LATCH-MISSING")
    consumer = None
    if "indirectInstanceConsumer" in document:
        consumer = document["indirectInstanceConsumer"]
        if not isinstance(consumer, dict) or set(consumer) != {"binding", "recordIndex", "instanceCapacity"}:
            reject("INDIRECT-INSTANCE-CONSUMER-SCHEMA")
        if indirect_base_version != 1:
            reject("INDIRECT-INSTANCE-CONSUMER-ABI")
        consumer = dict(contractVersion=1,
            binding=integer(consumer["binding"], "consumer-binding"),
            recordIndex=integer(consumer["recordIndex"], "consumer-record-index"),
            instanceCapacity=integer(consumer["instanceCapacity"], "consumer-instance-capacity", 1))
        if consumer["binding"] >= max_bindings:
            reject("BINDING-BUDGET")
        if consumer["binding"] in occupied:
            reject("PHYSICAL-BINDING-COLLISION")
        occupied.add(consumer["binding"])
    table_fields = {"name", "tableId", "kind", "capacity", "binding", "stages", "operations", "sampler", "format", "dimension", "record", "depthTexture", "access"}
    for item in raw_tables:
        if not isinstance(item, dict) or set(item) - table_fields:
            reject("TABLE-SCHEMA")
        name = identifier(item.get("name"))
        table_id = integer(item.get("tableId"), "table-id", 1)
        if name in tables or table_id in ids:
            reject("TABLE-ID-COLLISION")
        ids.add(table_id)
        kind = item.get("kind")
        if kind not in _KINDS:
            reject("RESOURCE-TYPE")
        if "depthTexture" in item:
            if (item["depthTexture"] is not True or kind != "SampledTexture"
                    or item.get("dimension") != "Texture2D"):
                reject("SAMPLED-DEPTH-DOMAIN")
            if depth_version != 1:
                reject("SAMPLED-DEPTH-VERSION")
        if "record" in item:
            if not record_version or kind not in ("UniformBuffer", "ReadOnlyStorageBuffer", "StorageBuffer"):
                reject("RECORD-DOMAIN")
            try:
                BufferVectorRecord(item["record"])
            except ValueError as error:
                raise BindlessContractError(str(error)) from error
        if "dimension" in item:
            if not shape_version:
                reject("TEXTURE-SHAPE-VERSION")
            if (kind not in ("SampledTexture", "StorageTexture")
                    or item["dimension"] not in ("Texture2D", "Texture2DArray")
                    or kind == "StorageTexture" and item["dimension"] != "Texture2D"):
                reject("TEXTURE-DIMENSION")
        count = integer(item.get("capacity"), "capacity", 1)
        first = integer(item.get("binding"), "binding")
        physical_count = count if mode == "Emulated" else 1
        if count > max_bindings or physical_count > max_bindings - len(occupied):
            reject("BINDING-BUDGET")
        if first + physical_count - 1 > 0xFFFFFFFF:
            reject("BINDING-OVERFLOW")
        if first + physical_count > max_bindings:
            reject("BINDING-BUDGET")
        bindings = set(range(first, first + physical_count))
        if occupied & bindings:
            reject("PHYSICAL-BINDING-COLLISION")
        occupied |= bindings
        stages = item.get("stages")
        if not isinstance(stages, list) or not stages or any(not isinstance(s, str) or s not in _STAGES for s in stages):
            reject("SHADER-STAGE")
        ops = item.get("operations")
        if not isinstance(ops, list) or any(not isinstance(op, str) or op not in _OPS[kind] for op in ops):
            reject("OPERATION-UNSUPPORTED")
        if len(set(ops)) != len(ops) or len(set(stages)) != len(stages):
            reject("DUPLICATE-OPERATION-OR-STAGE")
        # Atomic is one L2 operation-domain bit in BindlessCross/1. Two
        # distinct helper opcodes on one table need a new operation schema;
        # collapsing them would undercount scalar switch cases at runtime.
        if {"atomicAdd", "atomicOr"}.issubset(ops):
            reject("ATOMIC-OPERATION-DOMAIN")
        if item.get("depthTexture") and "sampleGrad" in ops:
            reject("SAMPLED-DEPTH-OPERATION")
        if "record" in item and {"atomicAdd", "atomicOr"}.intersection(ops):
            reject("RECORD-ATOMIC-UNSUPPORTED")
        if "sampleGrad" in ops and stages != ["fragment"]:
            reject("GRADIENT-STAGE")
        if kind == "StorageTexture" and item.get("format") not in (
                ("r32float", "rgba16float") if storage_version else ("r32float",)):
            reject("STORAGE-FORMAT")
        if kind != "StorageTexture" and "format" in item:
            reject("STORAGE-FORMAT")
        if "access" in item and (not storage_version or kind != "StorageTexture"):
            reject("STORAGE-IMAGE-ACCESS")
        access = item.get("access", "ReadWrite")
        if kind == "StorageTexture" and ((item["format"] == "rgba16float" and access != "WriteOnly")
                or (item["format"] == "r32float" and access != "ReadWrite")):
            reject("STORAGE-IMAGE-ACCESS")
        if kind == "StorageTexture" and access == "WriteOnly" and "load" in ops:
            reject("STORAGE-IMAGE-OPERATION")
        if "sampler" in item and not any(op in ops for op in ("sampleGrad", "sampleLevel")):
            reject("UNUSED-SAMPLER")
        tables[name] = dict(item)

    if depth_version and not any(t.get("depthTexture") for t in tables.values()):
        reject("SAMPLED-DEPTH-UNUSED")
    if storage_version and not any(t["kind"] == "StorageTexture" for t in tables.values()):
        reject("STORAGE-IMAGE-UNUSED")
    case_count = 0
    for table in tables.values():
        for op in table["operations"]:
            branches = table["capacity"]
            if op in ("sampleGrad", "sampleLevel"):
                sampler = tables.get(table.get("sampler"))
                if sampler is None or sampler["kind"] != "Sampler":
                    reject("SAMPLER-MISSING")
                if not set(table["stages"]).issubset(sampler["stages"]):
                    reject("SAMPLER-STAGE")
                branches *= sampler["capacity"]
            case_count += branches
            if case_count > max_cases:
                reject("SWITCH-BUDGET")

    metadata_words = 4
    for table in tables.values():
        table["metadataWordOffset"] = metadata_words
        metadata_words += 4 + table["capacity"]
    if metadata_words > 0xFFFFFFFF // 4:
        reject("METADATA-SIZE")
    metadata_stages = 0
    fault_stages = 0
    for table in tables.values():
        for stage in table["stages"]:
            metadata_stages |= _STAGES[stage]
            if table["operations"]:
                fault_stages |= _STAGES[stage]
    if fault_binding is not None and fault_stages == 0:
        reject("FAULT-STAGE-UNSUPPORTED")
    if consumer and not fault_stages & _STAGES["fragment"]:
        reject("INDIRECT-INSTANCE-CONSUMER-FAULT-STAGE")
    out = ["// Generated BindlessCross/1. Do not edit.",
           "struct ArpBindlessUniform { " + ("uint4" if producer_version else "float4") + " value; };",
           "// Fault=true means no valid resource result; caller must latch it."]
    if depth_version:
        out.append("#define ARP_SAMPLED_DEPTH(name) DepthTexture2D name")
        out.append("struct ArpBindlessDepthMip { uint value; };")
    if mode == "Native":
        out += ["uint arpBindlessResourceIndex(uint index) {",
                "#if defined(ARP_BINDLESS_SPIRV)",
                "    return NonUniformResourceIndex(index);",
                "#elif defined(ARP_BINDLESS_METAL)",
                "    return index;",
                "#else", '#error "Native bindless requires an explicit compiler target contract"',
                "#endif", "}"]
    out += [f"[[vk::binding({metadata_binding}, {group})]] StructuredBuffer<uint> ArpBindlessSnapshotMetadata;",
        "uint2 arpBindlessSnapshotGeneration() {",
        "    return uint2(ArpBindlessSnapshotMetadata[2], ArpBindlessSnapshotMetadata[3]);", "}",
        "bool arpBindlessMetadataValid(uint offset, uint tableId, uint capacity) {",
        f"    return ArpBindlessSnapshotMetadata[0] == 1u && ArpBindlessSnapshotMetadata[1] == {len(tables)}u",
        "        && (ArpBindlessSnapshotMetadata[2] != 0u || ArpBindlessSnapshotMetadata[3] != 0u)",
        "        && ArpBindlessSnapshotMetadata[offset] == tableId",
        "        && ArpBindlessSnapshotMetadata[offset + 2u] == capacity",
        "        && ArpBindlessSnapshotMetadata[offset + 3u] == 0u;", "}"]
    if fault_binding is not None:
        out += [f"[[vk::binding({fault_binding}, {group})]] RWStructuredBuffer<uint> ArpBindlessFaultLatch;",
                "void arpBindlessReportFault() {",
                "    uint previous;",
                "    InterlockedOr(ArpBindlessFaultLatch[0], 1u, previous);",
                "}",
                "uint arpBindlessFaultBits() {",
                "    uint previous;",
                "    InterlockedOr(ArpBindlessFaultLatch[0], 0u, previous);",
                "    return previous;",
                "}"]
    report_fault = "arpBindlessReportFault(); " if fault_binding is not None else ""
    if indirect_base_version:
        out += [
            "struct ArpBindlessIndirectCommand {",
            "    uint indexCount; uint instanceCount; uint firstIndex; int vertexOffset; uint firstInstance;",
            "};",
            "bool arpBindlessEncodeIndirectInstanceBase(uint indexCount, uint instanceCount,",
            "    uint firstIndex, int vertexOffset, uint firstInstance, uint capacity,",
            "    out ArpBindlessIndirectCommand physical, out uint4 mapping) {",
            "    physical.indexCount = indexCount; physical.instanceCount = 0u;",
            "    physical.firstIndex = firstIndex; physical.vertexOffset = vertexOffset;",
            "    physical.firstInstance = 0u; mapping = uint4(0u);",
            "    if (capacity == 0u || firstInstance > capacity || instanceCount > capacity - firstInstance) {",
            "        arpBindlessReportFault(); return false;",
            "    }",
            "    physical.instanceCount = instanceCount;",
            "    mapping = uint4(firstInstance, instanceCount, capacity, 1u); return true;",
            "}",
            "bool arpBindlessResolveIndirectInstance(uint4 mapping, uint physicalInstance, out uint logicalInstance) {",
            "    logicalInstance = 0u;",
            "    if (mapping.w != 1u || mapping.z == 0u || mapping.x > mapping.z",
            "        || mapping.y > mapping.z - mapping.x || physicalInstance >= mapping.y) return false;",
            "    logicalInstance = mapping.x + physicalInstance; return true;",
            "}",
        ]
    if consumer:
        out += [
            f"[[vk::binding({consumer['binding']}, {group})]] StructuredBuffer<uint4> ArpBindlessIndirectInstanceMappings;",
            "bool arpBindlessLoadIndirectInstance(uint physicalInstance, out uint logicalInstance) {",
            "    logicalInstance = 0u;",
            f"    uint4 mapping = ArpBindlessIndirectInstanceMappings[{consumer['recordIndex']}u];",
            f"    if (mapping.z != {consumer['instanceCapacity']}u) return false;",
            "    return arpBindlessResolveIndirectInstance(mapping, physicalInstance, logicalInstance);",
            "}",
        ]
    physical = []
    mappings = []
    for name, table in tables.items():
        kind, capacity, first = table["kind"], table["capacity"], table["binding"]
        record = BufferVectorRecord(table["record"]) if "record" in table else None
        record_type = "ArpBindlessRecord_" + name
        if record:
            out.append(record.source(record_type))
        count = capacity if mode == "Emulated" else 1
        for element in range(count):
            # Reflection count1 is the direct scalar resource ABI on every
            # backend. Keep the same guarded table/index semantics, but do not
            # emit a one-element resource array that Metal cannot bind directly.
            suffix = f"_{element}" if mode == "Emulated" else f"[{capacity}]" if capacity > 1 else ""
            storage_vector = kind == "StorageTexture" and table["format"] == "rgba16float"
            attribute = ('[[vk::image_format("rgba16f")]] ' if storage_vector
                         else '[[vk::image_format("r32f")]] ') if kind == "StorageTexture" else ""
            resource_type = ("Texture2DArray<float4>" if table.get("dimension") == "Texture2DArray" else _KINDS[kind])
            if storage_vector: resource_type = "WTexture2D<float4>"
            if record:
                resource_type = ("ConstantBuffer" if kind == "UniformBuffer" else
                                 "StructuredBuffer" if kind == "ReadOnlyStorageBuffer" else "RWStructuredBuffer") + "<" + record_type + ">"
            declaration = f"ARP_SAMPLED_DEPTH({name}_{element})" if mode == "Emulated" else f"ARP_SAMPLED_DEPTH({name}){suffix}"
            out.append(f"{attribute}[[vk::binding({first + element}, {group})]] " + (
                declaration if table.get("depthTexture") else f"{resource_type} {name}{suffix}") + ";")
            physical.append(dict(name=f"{name}_{element}" if mode == "Emulated" else name,
                                 set=group, binding=first + element, type=kind,
                                 arrayCount=1 if mode == "Emulated" else capacity,
                                 stageMask=sum(_STAGES[s] for s in table["stages"])))
            if kind == "StorageTexture":
                physical[-1]["storageTextureFormat"] = table["format"]
                physical[-1]["storageTextureAccess"] = table.get("access", "ReadWrite")
            if shape_version and kind in ("SampledTexture", "StorageTexture"):
                physical[-1]["textureViewDimension"] = table.get("dimension", "Texture2D")
            if table.get("depthTexture"):
                physical[-1]["depthTexture"] = True
            # WebGPU's non-comparison depth textureSampleLevel overload uses
            # a non-filtering sampler. Publish this explicit binding contract;
            # descriptor admission must reject a filtering resource, never
            # silently change its filter modes. Native consumes the same input.
            if kind == "Sampler" and any(t.get("depthTexture") and t.get("sampler") == name
                                         for t in tables.values()):
                physical[-1]["samplerFiltering"] = "NonFiltering"
        mapping = dict(tableId=table["tableId"], firstBinding=first, capacity=capacity,
                       operations=table["operations"], nonUniform=True, name=name,
                       metadataWordOffset=table["metadataWordOffset"])
        if any(op in ("sampleGrad", "sampleLevel") for op in table["operations"]):
            mapping["samplerTableId"] = tables[table["sampler"]]["tableId"]
        if kind in ("UniformBuffer", "ReadOnlyStorageBuffer", "StorageBuffer"):
            abi = ("Slang.uint4.uniform/1" if producer_version else "Slang.float4.uniform/1") if kind == "UniformBuffer" else "Slang.uint32.structured/1"
            stride = 16 if kind == "UniformBuffer" else 4
            mapping["bufferLayout"] = dict(identity=hashlib.sha256(abi.encode()).hexdigest()[:16],
                abi=abi, elementStrideBytes=stride, elementAlignmentBytes=stride, minBindingSizeBytes=stride)
            if record:
                mapping["bufferLayout"] = record.layout()
        mappings.append(mapping)

    def resource(name, index):
        return f"{name}_{index}" if mode == "Emulated" else (
            f"{name}[arpBindlessResourceIndex({index})]" if tables[name]["capacity"] > 1 else name)

    for name, table in tables.items():
        kind, capacity = table["kind"], table["capacity"]
        record = BufferVectorRecord(table["record"]) if "record" in table else None
        record_type = "ArpBindlessRecord_" + name
        for op in table["operations"]:
            sampled = op in ("sampleGrad", "sampleLevel")
            texture = kind in ("SampledTexture", "StorageTexture")
            array_texture = table.get("dimension") == "Texture2DArray"
            result_type = ("uint3" if array_texture else "uint2") if op == "query" and texture else (
                "uint" if op == "query" or not texture and kind != "UniformBuffer" else "float4")
            if producer_version and kind == "UniformBuffer":
                result_type = "uint4"
            if record and op == "load":
                result_type = record_type
            if kind == "StorageTexture" and op == "load":
                result_type = "float4" if table["format"] == "rgba16float" else "float"
            if table.get("depthTexture") and op in ("load", "sampleLevel"):
                result_type = "float"
            if op == "store":
                result_type = "void"
            args = ["uint index"]
            if sampled:
                args += ["uint samplerIndex", "float2 uv"]
                if array_texture:
                    args += ["uint layer"]
                args += ["ArpBindlessDepthMip mip" if table.get("depthTexture") else "float level"] if op == "sampleLevel" else ["float2 gradientX", "float2 gradientY"]
            elif op in ("load", "store", "atomicAdd", "atomicOr") and kind != "UniformBuffer":
                args += [("int4 location" if array_texture else "int3 location") if kind == "SampledTexture" else "uint2 location" if texture else "uint location"]
                if op in ("store", "atomicAdd", "atomicOr"):
                    value_type = ("float4" if table["format"] == "rgba16float" else "float") if kind == "StorageTexture" else "uint"
                    args += [(record_type if record else value_type) + " value"]
            args.append("out bool fault")
            out += [f"{result_type} {name}_{op}({', '.join(args)})", "{", "    fault = false;"]
            if table.get("depthTexture") and op == "sampleLevel":
                out.append("    uint level = mip.value;")
            zero = "return;" if result_type == "void" else f"return ({result_type})0;"
            offset = table["metadataWordOffset"]
            out.append(f"    uint activeCount = ArpBindlessSnapshotMetadata[{offset + 1}u];")
            invalid = f"!arpBindlessMetadataValid({offset}u, {table['tableId']}u, {capacity}u) || activeCount > {capacity}u || index >= activeCount"
            if sampled:
                sampler = tables[table["sampler"]]
                sampler_offset = sampler["metadataWordOffset"]
                out.append(f"    uint samplerActiveCount = ArpBindlessSnapshotMetadata[{sampler_offset + 1}u];")
                invalid += f" || !arpBindlessMetadataValid({sampler_offset}u, {sampler['tableId']}u, {sampler['capacity']}u)"
                invalid += f" || samplerActiveCount > {sampler['capacity']}u || samplerIndex >= samplerActiveCount"
            out.append(f"    if ({invalid}) {{ {report_fault}fault = true; {zero} }}")
            if kind in ("ReadOnlyStorageBuffer", "StorageBuffer"):
                out.append(f"    uint elementCount = ArpBindlessSnapshotMetadata[{offset + 4}u + index];")

            def body(ref, sampler_ref=None):
                zero = "return;" if result_type == "void" else f"return ({result_type})0;"
                if sampled:
                    tail = "level" if op == "sampleLevel" else "gradientX, gradientY"
                    method = "SampleLevel" if op == "sampleLevel" else "SampleGrad"
                    lines = []
                    if array_texture:
                        lines += ["uint width, height, layers, levels;", f"{ref}.GetDimensions(0, width, height, layers, levels);",
                                  f"if (layer >= layers) {{ {report_fault}fault = true; {zero} }}"]
                    if table.get("depthTexture"):
                        lines += ["uint width, height, levels;", f"{ref}.GetDimensions(0, width, height, levels);",
                                  f"if (level >= levels) {{ {report_fault}fault = true; {zero} }}"]
                    coordinate = "float3(uv, float(layer))" if array_texture else "uv"
                    return lines + [f"return {ref}.{method}({sampler_ref}, {coordinate}, {tail});"]
                if op == "query":
                    if texture:
                        if array_texture:
                            return ["uint width, height, layers;", f"{ref}.GetDimensions(width, height, layers);", "return uint3(width, height, layers);"]
                        return ["uint width, height;", f"{ref}.GetDimensions(width, height);", "return uint2(width, height);"]
                    return ["return elementCount;"]
                if kind == "UniformBuffer":
                    if record:
                        return [record_type + " value;"] + [f"value.{field} = {ref}.{field};" for field, _ in record.fields] + ["return value;"]
                    return [f"return {ref}.value;"]
                zero = "return;" if result_type == "void" else f"return ({result_type})0;"
                lines = []
                if kind == "SampledTexture":
                    mip = "w" if array_texture else "z"
                    dims = "width, height, layers, levels" if array_texture else "width, height, levels"
                    lines += [f"uint {dims};", f"{ref}.GetDimensions(0, {dims});",
                              f"if (location.{mip} < 0 || uint(location.{mip}) >= levels) {{ {report_fault}fault = true; {zero} }}",
                              f"{ref}.GetDimensions(uint(location.{mip}), {dims});",
                              f"if (any(location.xy < 0) || any(uint2(location.xy) >= uint2(width, height))) {{ {report_fault}fault = true; {zero} }}"]
                    if array_texture:
                        lines += [f"if (location.z < 0 || uint(location.z) >= layers) {{ {report_fault}fault = true; {zero} }}"]
                elif texture:
                    lines += ["uint width, height;", f"{ref}.GetDimensions(width, height);",
                              f"if (any(location >= uint2(width, height))) {{ {report_fault}fault = true; {zero} }}"]
                else:
                    lines += [f"if (location >= elementCount) {{ {report_fault}fault = true; {zero} }}"]
                if op == "load":
                    lines += [f"return {ref}.Load(location);" if kind == "SampledTexture" else f"return {ref}[location];"]
                elif op == "store":
                    lines += ([f"{ref}.Store(location, value);", "return;"]
                              if kind == "StorageTexture" and table.get("access") == "WriteOnly"
                              else [f"{ref}[location] = value;", "return;"])
                else:
                    method = "InterlockedAdd" if op == "atomicAdd" else "InterlockedOr"
                    lines += ["uint previous;", f"{method}({ref}[location], value, previous);", "return previous;"]
                return lines

            sampler = tables[table["sampler"]] if sampled else None
            if mode == "Native":
                valid = f"index < {capacity}u"
                if sampled:
                    valid += f" && samplerIndex < {sampler['capacity']}u"
                out.append(f"    if ({valid}) {{")
                out += ["        " + line for line in body(resource(name, "index"), resource(table["sampler"], "samplerIndex") if sampled else None)]
                out.append("    }")
            else:
                def scalar_case(element):
                    if sampled:
                        nested = SlangResourceSwitch(sampler["capacity"], document["maxSwitchCases"])
                        return nested.emit("samplerIndex", lambda sampler_index:
                            body(resource(name, element), resource(table["sampler"], sampler_index)), indent="") + ["break;"]
                    return body(resource(name, element))
                out += SlangResourceSwitch(capacity, document["maxSwitchCases"]).emit("index", scalar_case)
            out.append(f"    {report_fault}fault = true;")
            if result_type != "void":
                out.append(f"    return ({result_type})0;")
            out.append("}")
    physical.append(dict(name="ArpBindlessSnapshotMetadata", set=group, binding=metadata_binding,
        type="ReadOnlyStorageBuffer", arrayCount=1, stageMask=metadata_stages))
    if consumer:
        physical.append(dict(name="ArpBindlessIndirectInstanceMappings", set=group, binding=consumer["binding"],
            type="ReadOnlyStorageBuffer", arrayCount=1, stageMask=_STAGES["vertex"]))
    if fault_binding is not None:
        physical.append(dict(name="ArpBindlessFaultLatch", set=group, binding=fault_binding,
            type="StorageBuffer", arrayCount=1, stageMask=fault_stages))
        if snapshot_bytes < metadata_words * 4 + 2 * 4:
            reject("SNAPSHOT-BUDGET")
    elif snapshot_bytes < metadata_words * 4:
        reject("SNAPSHOT-BUDGET")
    source = "\n".join(out) + "\n"
    if producer_version:
        source += _ZERO_BASE_INDIRECT_BODY
    if consumer_version:
        source += _ZERO_BASE_INDIRECT_CONSUMER_BODY
    reflection = dict(type="BindlessShaderArtifact", contractVersion=1, name=variant,
                      activePrefixContractVersion=2,
                      faultReportingContractVersion=1 if fault_binding is not None else 0,
                      snapshotMetadata=dict(contractVersion=1, binding=metadata_binding,
                          wordCount=metadata_words, stageMask=metadata_stages),
                      validationStatus="CompilerInputRequiresTargetReflection",
                      execution=mode, set=group, switchCases=case_count if mode == "Emulated" else 0,
                      snapshotBytes=snapshot_bytes,
                      sourceSha256=hashlib.sha256(source.encode()).hexdigest(),
                      bindings=physical, tables=mappings)
    if pipeline_version:
        reflection["pipelineInterfaceContractVersion"] = pipeline_version
    if push_constants:
        reflection["pushConstants"] = push_constants
    if ordinary_opaque:
        reflection["ordinaryOpaqueBindings"] = ordinary_opaque
    if shape_version:
        reflection["textureShapeContractVersion"] = shape_version
    if depth_version:
        reflection["sampledDepthContractVersion"] = depth_version
    if storage_version:
        reflection["storageImageContractVersion"] = storage_version
    if record_version:
        reflection["vectorRecordContractVersion"] = record_version
    if fault_binding is not None:
        reflection["faultLatch"] = dict(contractVersion=1, binding=fault_binding,
            stageMask=fault_stages)
    if indirect_base_version:
        reflection["indirectInstanceBaseContractVersion"] = indirect_base_version
    if consumer:
        reflection["indirectInstanceConsumer"] = consumer
    if producer_version:
        reflection["zeroBaseIndirectProducerContractVersion"] = producer_version
    if visibility_version:
        reflection["gpuVisibilityKernelContractVersion"] = visibility_version
    if visibility_consumer:
        reflection["gpuVisibilityConsumerContractVersion"] = visibility_consumer
    if consumer_version:
        reflection["zeroBaseIndirectConsumerContractVersion"] = consumer_version
    return GeneratedBindless(source, reflection)


def compile_bindless_package(document, source_path, entries, mode, out_dir,
                             slangc, spirv_reflect, source_libraries=()):
    """Compile a finite variant through Arp's target/reflection helpers.

    Publication is the final manifest rename, after every target and stage has
    validated. This compiler artifact does not grant backend execution support.
    Native has no WGSL target; Emulated uses scalar descriptors on all targets.
    No legacy implicit-sampling or resource-array rewrite is permitted here.
    """
    import pathlib
    import subprocess
    import tempfile
    import yaml
    import compile_shaders as compiler

    # Invalidate the named publication before validation/compilation so a
    # failed replacement can never retain a success ticket for older inputs.
    variant_name = identifier(document.get("name") if isinstance(document, dict) else None)
    if mode not in ("Native", "Emulated"):
        reject("EXECUTION-MODE")
    output = pathlib.Path(out_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    prefix = variant_name + "." + mode.lower()
    manifest_path = output / (prefix + ".bindless.yaml")
    manifest_path.unlink(missing_ok=True)
    generated = generate_bindless(document, mode)
    lowering_yaml = yaml.safe_dump(generated.reflection, sort_keys=False)
    lowering_sha256 = hashlib.sha256(lowering_yaml.encode("utf-8")).hexdigest()
    if source_path is None:
        if not (generated.reflection.get("zeroBaseIndirectProducerContractVersion")
                or generated.reflection.get("zeroBaseIndirectConsumerContractVersion")):
            reject("SOURCE-MISSING")
        source_path = output / (prefix + ".generated.slang")
        authored = ""
    else:
        source_path = pathlib.Path(source_path).resolve()
        authored = source_path.read_text(encoding="utf-8")
    library_inputs = []
    library_paths = set()
    library_sources = []
    for library in source_libraries:
        library_path = pathlib.Path(library).resolve()
        if library_path in library_paths:
            reject("SOURCE-LIBRARY-DUPLICATE")
        library_paths.add(library_path)
        try:
            library_source = library_path.read_text(encoding="utf-8")
        except (OSError, UnicodeError):
            reject("SOURCE-LIBRARY-INPUT")
        library_sources.append(library_source)
        library_inputs.append(dict(name=library_path.name,
            sha256=hashlib.sha256(library_source.encode("utf-8")).hexdigest()))
    if library_sources:
        authored = "\n".join([*library_sources, authored])
    if generated.reflection.get("gpuVisibilityKernelContractVersion"):
        import gpu_visibility_codegen as visibility
        consumer = bool(generated.reflection.get("gpuVisibilityConsumerContractVersion"))
        expected_entries = visibility.CONSUMER_ENTRIES if consumer else visibility.ENTRIES
        if authored != visibility.authored_source(consumer) or tuple(entries) != expected_entries:
            reject("GPU-VISIBILITY-KERNEL-ENTRY-OWNERSHIP")
    if generated.reflection.get("zeroBaseIndirectProducerContractVersion"):
        if authored != "" or entries != [_ZERO_BASE_INDIRECT_ENTRY]:
            reject("ZERO-BASE-INDIRECT-PRODUCER-ENTRY-OWNERSHIP")
    if generated.reflection.get("zeroBaseIndirectConsumerContractVersion"):
        if authored != "" or entries != _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES:
            reject("ZERO-BASE-INDIRECT-CONSUMER-ENTRY-OWNERSHIP")
    if not entries or len(set(entries)) != len(entries):
        reject("ENTRY-POINTS")
    for entry in entries:
        identifier(entry)
    # This bounded ABI has no preprocessor dependency/expansion proof yet.
    # Token concatenation could bypass resource-identity checks; reject authored
    # directives rather than treating a textual scan as a compiler proof.
    authored_code = compiler.strip_comments_preserving_offsets(authored)
    if re.search(r"#|\bInterlocked\w*\s*\(", authored_code):
        reject("SOURCE-CONTRACT-UNSUPPORTED")
    push_intent = generated.reflection.get("pushConstants")
    if re.search(r"vk::push_constant", authored_code) and not push_intent:
        reject("SOURCE-CONTRACT-UNSUPPORTED")
    if push_intent:
        if (len(re.findall(r"\[\[vk::push_constant\]\]", authored_code)) != 1
                or compiler.get_push_constant_size_bytes(authored_code) != push_intent["sizeBytes"]):
            reject("PUSH-SOURCE-SIZE")
    # Closed generator schema only: application source accesses typed tables
    # through the emitted helpers. Direct physical accesses would invalidate
    # both the guarded-index and operation-set reflection contracts.
    reserved_names = {b["name"] for b in generated.reflection["bindings"]}
    reserved_names.update(t["name"] for t in generated.reflection["tables"])
    if reserved_names.intersection(re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", authored_code)):
        reject("DIRECT-TABLE-ACCESS-UNSUPPORTED")
    source = generated.source + "\n" + authored
    expected = {(b["set"], b["binding"]): b for b in generated.reflection["bindings"]}
    opaque = {(b["set"], b["binding"]): b for b in generated.reflection.get("ordinaryOpaqueBindings", [])}
    logical = compiler.get_shader_bindings(source)
    seen = set()
    for binding in logical:
        key = (binding["set"], binding["binding"])
        if binding["set"] != document["set"]:
            if not generated.reflection.get("pipelineInterfaceContractVersion"):
                reject("PIPELINE-INTERFACE-REQUIRED")
            # External images/samplers need target shape/usage proof, which
            # cannot be inferred from the ordinary binding-kind reflection.
            if binding["type"] not in ("UniformBuffer", "ReadOnlyStorageBuffer", "StorageBuffer"):
                if key not in opaque:
                    reject("PIPELINE-OPAQUE-MAPPING-MISSING" if opaque else "PIPELINE-ORDINARY-SHAPE-UNAVAILABLE")
                if any(binding.get(field) != opaque[key].get(field) for field in ("name", "type", "arrayCount")):
                    reject("PIPELINE-OPAQUE-MAPPING-MISMATCH")
            if mode == "Emulated" and binding["arrayCount"] != 1:
                reject("PIPELINE-ORDINARY-DESCRIPTOR-ARRAY")
        if key in seen:
            reject("PHYSICAL-BINDING-COLLISION")
        seen.add(key)
        if key in expected and any(binding.get(field) != expected[key].get(field)
                                   for field in ("name", "type", "arrayCount")):
            reject("REFLECTION-MAPPING-MISMATCH")
    if not set(expected).issubset(seen):
        reject("REFLECTION-MAPPING-MISSING")
    if not set(opaque).issubset(seen):
        reject("PIPELINE-OPAQUE-MAPPING-MISSING")
    if len(logical) + bool(push_intent) > document["maxBindings"]:
        reject("PIPELINE-BINDING-BUDGET")
    entry_facts = compiler.derive_entry_point_reflection(source, entries, str(source_path))
    entry_stage_masks = {fact["name"]: {"Vertex": 1, "Fragment": 2, "Compute": 4}[fact["stage"]]
                         for fact in entry_facts}
    version = subprocess.run([str(slangc), "-version"], check=True,
                             capture_output=True, text=True)
    artifact_files = {}
    reflected_stages = {}
    modules = []
    observed_push_stages = 0
    with tempfile.TemporaryDirectory(prefix="arp-bindless-package-") as temporary:
        temporary = pathlib.Path(temporary)
        for entry in entries:
            stem = prefix + "." + entry
            paths = {target: output / (stem + extension) for target, extension in
                     (("spirv", ".spv"), ("metal", ".metal"), ("wgsl", ".wgsl"))}
            for target in (("spirv", "metal") if mode == "Native" else ("spirv", "metal", "wgsl")):
                target_source = temporary / (stem + "." + target + ".slang")
                define = "#define ARP_BINDLESS_" + target.upper() + " 1\n" if mode == "Native" else ""
                # The random staging directory is not part of shader identity.
                # Give Slang a stable virtual source name for emitted #line and
                # debug metadata; do not rewrite target shader operations.
                line_identity = '#line 1 "' + stem + '.slang"\n'
                target_source.write_bytes((define + line_identity + source).encode("utf-8"))
                compiler.run_slangc(str(slangc), str(target_source), target,
                    "spirv_1_5" if target == "spirv" else "sm_6_0", [entry], str(paths[target]),
                    include_dirs=[], ignore_capabilities=(target == "wgsl"
                        and ("faultLatchBinding" in document
                            or any({"atomicAdd", "atomicOr"}.intersection(t["operations"])
                                   for t in document["tables"]))))
            stages = compiler._bindings_codegen.reflect_descriptor_stage_masks(
                str(spirv_reflect), str(paths["spirv"]), allow_empty=True)
            for key, mask in stages.items():
                if key not in seen:
                    reject("REFLECTION-UNDECLARED-BINDING")
                reflected_stages[key] = reflected_stages.get(key, 0) | mask
            active = [b for b in logical if stages.get((b["set"], b["binding"]), 0)]
            # Independently inspect emitted image declarations; neither source
            # spelling nor a rehashed YAML dimension proves the target ABI.
            from spirv_layout import SpirvLayoutFacts
            binary_facts = SpirvLayoutFacts(paths["spirv"])
            images = binary_facts.descriptor_image_types()
            if generated.reflection.get("pipelineInterfaceContractVersion"):
                buffer_declarations = binary_facts.buffer_layouts()
                for binding in active:
                    if binding["set"] == document["set"]:
                        continue
                    key = (binding["set"], binding["binding"])
                    if key in opaque:
                        OrdinaryOpaqueBindingContract.verify_spirv(opaque[key], images.get(key, {}))
                        continue
                    declaration = buffer_declarations.get((binding["set"], binding["binding"]), {})
                    if (declaration.get("descriptorCount") != binding["arrayCount"]
                            or declaration.get("storageClass") != (2 if binding["type"] == "UniformBuffer" else 12)):
                        reject("PIPELINE-ORDINARY-BUFFER-BINARY")
            records = {table["binding"]: table for table in document["tables"] if "record" in table}
            if records:
                buffer_facts = binary_facts.buffer_layouts()
                for table in records.values():
                    for element in range(table["capacity"] if mode == "Emulated" else 1):
                        key = (document["set"], table["binding"] + element)
                        if stages.get(key):
                            try:
                                BufferVectorRecord(table["record"]).verify_spirv(
                                    binary_facts, buffer_facts[key], table["kind"] == "UniformBuffer")
                            except (ValueError, KeyError, RuntimeError) as error:
                                reject("RECORD-TARGET-PROOF: " + str(error))
            if generated.reflection.get("gpuVisibilityConsumerContractVersion") == 2 and entry == "visibilityVertexMain":
                # Closed Consumer/2 aliases must have the very same record ABI
                # as the authenticated producer tables. This checks actual
                # binary members and strides, not source type-name equality.
                aliases = (0, 4, 6, 8)
                if set(stages) != {(2, i) for i in range(4)}:
                    reject("GPU-VISIBILITY-CONSUMER-ALIAS-BINDINGS")
                for ordinal, role in enumerate(aliases):
                    table = document["tables"][role]
                    try:
                        BufferVectorRecord(table["record"]).verify_spirv(binary_facts,
                            buffer_facts[(2, ordinal)], ordinal == 3)
                    except (ValueError, KeyError, RuntimeError) as error:
                        reject("GPU-VISIBILITY-CONSUMER-ALIAS-LAYOUT: " + str(error))
            for key, binding in expected.items():
                if not stages.get(key) or binding["type"] not in ("SampledTexture", "StorageTexture"):
                    continue
                facts = images.get(key, {})
                if (facts.get("kind") != "Image" or facts.get("descriptorCount") != binding["arrayCount"]
                        or facts.get("dimension") != 1 or (facts.get("depth") != 1 if binding.get("depthTexture") else facts.get("depth") not in (0, 2))
                        or facts.get("arrayed") != (binding.get("textureViewDimension", "Texture2D") == "Texture2DArray")
                        or facts.get("multisampled") or facts.get("scalarKind") != "Float"
                        or facts.get("scalarWidth") != 32
                        or facts.get("sampled") != (2 if binding["type"] == "StorageTexture" else 1)
                        or facts.get("imageFormat") != ((2 if binding["storageTextureFormat"] == "rgba16float" else 3)
                            if binding["type"] == "StorageTexture" else 0)):
                    reject("REFLECTION-IMAGE-SHAPE-MISMATCH")
            compiler.lower_metal_argument_buffer_artifact(str(paths["metal"]), active)
            push_contract = None
            push_members_yaml = None
            entry_push = push_intent and (entry_stage_masks[entry] & push_intent["stageMask"])
            if push_intent and not entry_push and binary_facts.push_constant_layouts():
                reject("PUSH-UNDECLARED-STAGE")
            if entry_push:
                push_contract = BindlessPushConstantContract.verify_targets(
                    compiler, push_intent, paths, logical, binary_facts, mode)
                block = compiler._bindings_codegen.reflect_push_constant_block(
                    str(spirv_reflect), str(paths["spirv"]))
                if block is None or block.size != push_intent["sizeBytes"]:
                    reject("PUSH-REFLECTED-SIZE")
                push_members_yaml = compiler._bindings_codegen.emit_push_constant_members_yaml(block)
            if mode == "Emulated":
                compiler.normalize_wgsl_struct_member_delimiters(str(paths["wgsl"]))
                compiler.lower_wgsl_depth_integer_mip_sampling(str(paths["wgsl"]),
                    [b["name"] for b in active if b.get("depthTexture")])
                # Feed the existing lowering only compiler-generated atomic
                # declarations active in this reflected entry. Other entries'
                # resources legitimately do not occur in its target artifact.
                active_names = {b["name"] for b in active}
                atomic_contract = "\n".join(line for line in generated.source.splitlines()
                    if any(re.search(r"\bInterlocked(?:Add|Or)\s*\(\s*" + re.escape(name) + r"\s*\[", line)
                           for name in active_names))
                # One backing buffer can be reset by one entry and used by
                # atomic helpers in another. The generated per-entry target
                # is authoritative; unused atomic operations may be eliminated.
                compiler.lower_wgsl_atomic_storage_buffers(
                    str(paths["wgsl"]), atomic_contract, entry_filtered=True)
                wgsl = paths["wgsl"].read_text(encoding="utf-8")
                if entry_stage_masks[entry] == _STAGES["vertex"] and (
                        re.search(r"var\s*<\s*storage\s*,\s*read_write\s*>", wgsl)
                        or re.search(r"\batomic\w*\b", wgsl)):
                    reject("WGSL-VERTEX-WRITABLE-STORAGE")
                if "binding_array" in wgsl or re.search(r"\btextureSample(?:Bias|Compare)?\s*\(", wgsl):
                    reject("WGSL-OPERATION-UNSUPPORTED")
                compiler.validate_wgsl_group0_no_collision(str(paths["wgsl"]))
            msl_space = compiler.get_metal_binding_space_lowering(logical, str(paths["metal"]))
            msl_arrays = compiler.get_metal_argument_buffer_lowering(logical, str(paths["metal"]))
            if push_contract:
                slots = {b["metalSlot"] for b in msl_space if b.get("active")
                         and b.get("physicalType") in {"UniformBuffer", "ReadOnlyStorageBuffer", "StorageBuffer"}}
                slots.update(b["argumentBufferSlot"] for b in msl_arrays if b.get("active"))
                if push_contract["metalBufferIndex"] in slots:
                    reject("PUSH-METAL-SLOT-COLLISION")
            persisted_source = output / (stem + ".slang")
            # Reflection fingerprints UTF-8 source with LF newlines. Persist the
            # exact same bytes on Windows instead of write_text CRLF expansion.
            persisted_source.write_bytes(source.encode("utf-8"))
            reflection_path = output / (stem + ".reflection.yaml")
            compiler.write_shader_reflection(str(persisted_source), document["name"],
                str(paths["spirv"]), str(paths["metal"]),
                str(paths["wgsl"]) if mode == "Emulated" else None,
                str(reflection_path), push_contract, spirv_reflect_exe=str(spirv_reflect),
                push_constant_members_yaml=push_members_yaml,
                descriptor_stage_masks=stages, entry_names=[entry],
                metal_binding_space=msl_space,
                # The namespace was actually inspected even when every table
                # is scalar or inactive in this entry. Preserve those explicit
                # inactive mappings so shared-snapshot access can prove absence
                # in both Metal namespaces; this grants no argument-buffer use.
                metal_argument_buffer=msl_arrays,
                webgpu_unavailable_reason="Native resource-array ABI is unavailable" if mode == "Native" else None,
                # Every package module above was compiled for exactly one
                # selected entry. Its vertex interface must come from that
                # SPIR-V, including authored geometry families with arbitrary
                # entry names; the legacy vertexMain source heuristic drops
                # those inputs and can copy inputs onto fragment modules.
                single_entry=True)
            reflection = yaml.safe_load(reflection_path.read_text(encoding="utf-8"))
            if push_intent and not entry_push:
                # Binary absence and the declared stage intent agree. Shared
                # authored source contains the other entries' block; it is not
                # an active push interface for this exact compiled module.
                reflection.update(pushConstantSizeBytes=0, pushConstantContractVersion=None,
                    pushConstantStages=None, pushConstantWebGpuBinding=None,
                    pushConstantMetalBufferIndex=None)
            if entry_push:
                entry_stages = {"Vertex": 1, "Fragment": 2, "Compute": 4}
                stage = entry_stages[reflection["entryPoints"][0]["stage"]]
                if (not (stage & push_intent["stageMask"])
                        or reflection["pushConstantContractVersion"] != "AtomicFullBlockPush/1"
                        or reflection["pushConstantSizeBytes"] != push_intent["sizeBytes"]):
                    reject("PUSH-REFLECTION")
                observed_push_stages |= stage
            # A sealed package is relocatable. Local build roots cannot enter
            # its content identity through reflection path metadata.
            reflection["sourcePath"] = persisted_source.name
            reflection["targets"] = {
                target: paths[target].name if target != "wgsl" or mode == "Emulated" else None
                for target in ("spirv", "metal", "wgsl")}
            # Preserve optional-extension identity in the ordinary reflection
            # too: the scalar ABI must not bypass runtime BindlessCross gates.
            reflection["bindlessContractVersion"] = "BindlessCross/1"
            reflection["bindlessExecution"] = mode
            reflection["bindlessLoweringSha256"] = lowering_sha256
            reflection_path.write_text(yaml.safe_dump(reflection, sort_keys=False), encoding="utf-8")
            # The whole executable interface, including ordinary fault/readback
            # bindings, is published for budget accounting and layout creation.
            modules.append(dict(entry=entry, reflection=reflection_path.name,
                                bindings=reflection["bindings"],
                                webGpuBindingSpace=reflection["webGpuBindingSpace"],
                                metalArgumentBuffer=reflection["metalArgumentBuffer"]))
            for path in [persisted_source, reflection_path, paths["spirv"], paths["metal"]] + (
                    [paths["wgsl"]] if mode == "Emulated" else []):
                artifact_files[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    for key, binding in expected.items():
        if reflected_stages.get(key) != binding["stageMask"]:
            reject("REFLECTION-STAGE-MISMATCH")
    for key, binding in opaque.items():
        if reflected_stages.get(key) != binding["stageMask"]:
            reject("PIPELINE-OPAQUE-STAGE-MISMATCH")
    if push_intent and observed_push_stages != push_intent["stageMask"]:
        reject("PUSH-STAGE-COVERAGE")
    manifest = dict(generated.reflection)
    manifest.update(validationStatus="TargetReflectionValidated",
                    matrixLayout="row-major", compilerVersion=(version.stdout + version.stderr).strip(),
                    authoredSourceSha256=hashlib.sha256(authored.encode()).hexdigest(),
                    compilerHelperSha256=hashlib.sha256(pathlib.Path(compiler.__file__).read_bytes()).hexdigest(),
                    generatorSha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
                    modules=modules, artifacts=artifact_files)
    manifest["loweringSha256"] = lowering_sha256
    manifest["loweringYaml"] = lowering_yaml
    if library_inputs:
        manifest["sourceCompositionContractVersion"] = 1
        manifest["sourceLibraries"] = library_inputs
    payload_path = output / (prefix + ".bindless.payload.yaml")
    payload_bytes = yaml.safe_dump(manifest, sort_keys=False).encode("utf-8")
    payload_path.write_bytes(payload_bytes)
    publication = dict(type="BindlessPackagePublication", contractVersion=1,
        payload=payload_path.name, shaderPackageIdentity=hashlib.sha256(payload_bytes).hexdigest())
    pending = output / (prefix + ".bindless.yaml.pending")
    pending.write_text(yaml.safe_dump(publication, sort_keys=False), encoding="utf-8")
    pending.replace(manifest_path)
    return manifest_path


def read_bindless_publication(path):
    """Tooling reader for the same byte-hashed YAML publication used at runtime."""
    import pathlib
    import yaml
    path = pathlib.Path(path)
    publication = yaml.safe_load(path.read_text(encoding="utf-8"))
    if publication.get("type") != "BindlessPackagePublication" or publication.get("contractVersion") != 1:
        reject("PACKAGE-PUBLICATION-VERSION")
    name = publication.get("payload", "")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", name) or name in (".", ".."):
        reject("PACKAGE-FILENAME")
    payload = (path.parent / name).read_bytes()
    if hashlib.sha256(payload).hexdigest() != publication.get("shaderPackageIdentity"):
        reject("PACKAGE-HASH-MISMATCH")
    manifest = yaml.safe_load(payload)
    manifest["shaderPackageIdentity"] = publication["shaderPackageIdentity"]
    return manifest
