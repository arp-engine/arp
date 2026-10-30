#include "shader/shader_reflection_cache.h"

#include "core/io/file_io.h"
#include "core/memory/cpu_allocator.h"
#include "core/text/text_decoder.h"

#include <charconv>
#include <fkYAML/node.hpp>

namespace arp::shader {

namespace {

using YamlNode = fkyaml::node;

// Look up a key in a mapping node; returns nullptr if the node is not a
// mapping or the key is absent. Mirrors the helper in prefab_asset_resources.cpp.
const YamlNode* findYamlValue(const YamlNode& mapping, const char* key)
{
    if (!mapping.is_mapping()) {
        return nullptr;
    }
    const auto& values = mapping.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = values.find(YamlNode(key));
    return found == values.end() ? nullptr : &found->second;
}

std::pmr::string yamlString(const YamlNode& node, const char* key)
{
    const auto* child = findYamlValue(node, key);
    if (child == nullptr || !child->is_string()) {
        return {};
    }
    const auto& value = child->get_value_ref<const YamlNode::string_type&>();
    return core::pmr::String(value.data(), value.size(), &core::defaultCpuMemoryResource());
}

uint32_t yamlUInt(const YamlNode& node, const char* key, uint32_t fallback = 0)
{
    const auto* child = findYamlValue(node, key);
    if (child == nullptr || !child->is_integer()) {
        return fallback;
    }
    return static_cast<uint32_t>(child->get_value<YamlNode::integer_type>());
}

bool yamlBool(const YamlNode& node, const char* key, bool fallback = false)
{
    const auto* child = findYamlValue(node, key);
    if (child == nullptr || !child->is_boolean()) {
        return fallback;
    }
    return child->get_value<bool>();
}

ShaderStage stageFromString(const std::pmr::string& value)
{
    if (value == "Fragment") {
        return ShaderStage::Fragment;
    }
    if (value == "Compute") {
        return ShaderStage::Compute;
    }
    if (value == "TessellationControl") {
        return ShaderStage::TessellationControl;
    }
    if (value == "TessellationEvaluation") {
        return ShaderStage::TessellationEvaluation;
    }
    return ShaderStage::Vertex;
}

VertexAttributeFormat vertexFormatFromString(const std::pmr::string& value)
{
    if (value == "Float") {
        return VertexAttributeFormat::Float;
    }
    if (value == "Float2") {
        return VertexAttributeFormat::Float2;
    }
    if (value == "Float3") {
        return VertexAttributeFormat::Float3;
    }
    if (value == "Float4") {
        return VertexAttributeFormat::Float4;
    }
    return VertexAttributeFormat::Unknown;
}

BindingType bindingTypeFromString(const std::pmr::string& value)
{
    if (value == "StorageBuffer") {
        return BindingType::StorageBuffer;
    }
    if (value == "ReadOnlyStorageBuffer") {
        return BindingType::ReadOnlyStorageBuffer;
    }
    if (value == "SampledTexture") {
        return BindingType::SampledTexture;
    }
    if (value == "StorageTexture") {
        return BindingType::StorageTexture;
    }
    if (value == "Sampler") {
        return BindingType::Sampler;
    }
    if (value == "InputAttachment") {
        return BindingType::InputAttachment;
    }
    return BindingType::UniformBuffer;
}

std::pmr::vector<ShaderEntryPoint> parseEntryPoints(const YamlNode& root)
{
    core::pmr::Vector<ShaderEntryPoint> entryPoints{&core::defaultCpuMemoryResource()};
    const auto* seq = findYamlValue(root, "entryPoints");
    if (seq == nullptr || !seq->is_sequence()) {
        return entryPoints;
    }
    for (const auto& item : seq->get_value_ref<const YamlNode::sequence_type&>()) {
        auto name = yamlString(item, "name");
        const auto stage = yamlString(item, "stage");
        if (!name.empty()) {
            entryPoints.push_back({
                std::move(name),
                stageFromString(stage),
                yamlUInt(item, "computeWorkgroupSizeX"),
                yamlUInt(item, "computeWorkgroupSizeY"),
                yamlUInt(item, "computeWorkgroupSizeZ"),
                yamlBool(item, "writesRenderTargetArrayIndex"),
            });
        }
    }
    return entryPoints;
}

std::pmr::vector<ShaderVertexAttribute> parseVertexAttributes(const YamlNode& root)
{
    core::pmr::Vector<ShaderVertexAttribute> attributes{&core::defaultCpuMemoryResource()};
    const auto* seq = findYamlValue(root, "vertexAttributes");
    if (seq == nullptr || !seq->is_sequence()) {
        return attributes;
    }
    for (const auto& item : seq->get_value_ref<const YamlNode::sequence_type&>()) {
        auto name = yamlString(item, "name");
        auto semantic = yamlString(item, "semantic");
        const auto format = yamlString(item, "format");
        if (!name.empty() && !semantic.empty()) {
            attributes.push_back(ShaderVertexAttribute{
                std::move(name),
                std::move(semantic),
                vertexFormatFromString(format),
                yamlUInt(item, "location"),
            });
        }
    }
    return attributes;
}

std::pmr::vector<ShaderBinding> parseBindings(const YamlNode& root)
{
    core::pmr::Vector<ShaderBinding> bindings{&core::defaultCpuMemoryResource()};
    const auto* seq = findYamlValue(root, "bindings");
    if (seq == nullptr || !seq->is_sequence()) {
        return bindings;
    }
    for (const auto& item : seq->get_value_ref<const YamlNode::sequence_type&>()) {
        auto name = yamlString(item, "name");
        const auto type = yamlString(item, "type");
        if (!name.empty()) {
            bindings.push_back(ShaderBinding{
                std::move(name),
                bindingTypeFromString(type),
                yamlUInt(item, "set"),
                yamlUInt(item, "binding"),
                yamlUInt(item, "attachmentIndex", kInvalidSubpassAttachmentIndex),
                yamlBool(item, "depthTexture"),
                yamlUInt(item, "arrayCount", 1u),
                yamlUInt(item, "stageMask"),
            });
        }
    }
    return bindings;
}

std::pmr::vector<ShaderWebGpuBindingSpaceBinding> parseWebGpuBindingSpace(const YamlNode& root)
{
    core::pmr::Vector<ShaderWebGpuBindingSpaceBinding> bindings{&core::defaultCpuMemoryResource()};
    const auto* seq = findYamlValue(root, "webGpuBindingSpace");
    if (seq == nullptr || !seq->is_sequence()) {
        return bindings;
    }
    for (const auto& item : seq->get_value_ref<const YamlNode::sequence_type&>()) {
        auto name = yamlString(item, "name");
        if (name.empty()) {
            continue;
        }
        bindings.push_back(ShaderWebGpuBindingSpaceBinding{
            std::move(name),
            yamlUInt(item, "set"),
            yamlUInt(item, "binding"),
            yamlUInt(item, "group"),
            yamlUInt(item, "webGpuBinding"),
            yamlBool(item, "active"),
            bindingTypeFromString(yamlString(item, "physicalType")),
            yamlString(item, "loadSemantics"),
            yamlBool(item, "requiresSampler"),
            yamlBool(item, "depthTexture"),
            yamlUInt(item, "arrayElement", 0u),
            yamlUInt(item, "capacity", 1u),
        });
    }
    return bindings;
}

std::pmr::vector<std::uint32_t> yamlUIntSequence(const YamlNode& node, const char* key)
{
    core::pmr::Vector<std::uint32_t> values{&core::defaultCpuMemoryResource()};
    const auto* child = findYamlValue(node, key);
    if (child == nullptr || !child->is_sequence()) {
        return values;
    }
    for (const auto& value : child->get_value_ref<const YamlNode::sequence_type&>()) {
        if (!value.is_integer()) {
            values.clear();
            return values;
        }
        values.push_back(static_cast<std::uint32_t>(value.get_value<YamlNode::integer_type>()));
    }
    return values;
}

std::pmr::vector<ShaderMetalBindingSpaceBinding> parseMetalBindingSpace(const YamlNode& root)
{
    core::pmr::Vector<ShaderMetalBindingSpaceBinding> bindings{&core::defaultCpuMemoryResource()};
    const auto* seq = findYamlValue(root, "metalBindingSpace");
    if (seq == nullptr || !seq->is_sequence()) {
        return bindings;
    }
    for (const auto& item : seq->get_value_ref<const YamlNode::sequence_type&>()) {
        auto name = yamlString(item, "name");
        if (name.empty()) {
            continue;
        }
        bindings.push_back(ShaderMetalBindingSpaceBinding{
            std::move(name),
            yamlUInt(item, "set"),
            yamlUInt(item, "binding"),
            yamlUInt(item, "metalSlot", UINT32_MAX),
            yamlBool(item, "active"),
            bindingTypeFromString(yamlString(item, "physicalType")),
        });
    }
    return bindings;
}

std::pmr::vector<ShaderMetalArgumentBufferBinding> parseMetalArgumentBuffer(const YamlNode& root)
{
    core::pmr::Vector<ShaderMetalArgumentBufferBinding> bindings{&core::defaultCpuMemoryResource()};
    const auto* seq = findYamlValue(root, "metalArgumentBuffer");
    if (seq == nullptr || !seq->is_sequence()) {
        return bindings;
    }
    for (const auto& item : seq->get_value_ref<const YamlNode::sequence_type&>()) {
        auto name = yamlString(item, "name");
        if (name.empty()) {
            continue;
        }
        bindings.push_back(ShaderMetalArgumentBufferBinding{
            std::move(name),
            yamlUInt(item, "set"),
            yamlUInt(item, "binding"),
            yamlUInt(item, "argumentBufferSlot", UINT32_MAX),
            yamlUInt(item, "argumentIndex", UINT32_MAX),
            yamlUInt(item, "capacity"),
            yamlBool(item, "active"),
            bindingTypeFromString(yamlString(item, "physicalType")),
        });
    }
    return bindings;
}

std::pmr::vector<ShaderMetalFramebufferFetchBinding> parseMetalFramebufferFetch(
    const YamlNode& root)
{
    core::pmr::Vector<ShaderMetalFramebufferFetchBinding> bindings{
        &core::defaultCpuMemoryResource()};
    const auto* seq = findYamlValue(root, "metalFramebufferFetch");
    if (seq == nullptr || !seq->is_sequence())
        return bindings;
    for (const auto& item : seq->get_value_ref<const YamlNode::sequence_type&>()) {
        auto name = yamlString(item, "name");
        if (name.empty())
            continue;
        bindings.push_back(ShaderMetalFramebufferFetchBinding{
            std::move(name),
            yamlUInt(item, "set"),
            yamlUInt(item, "binding"),
            yamlUInt(item, "logicalAttachmentIndex", UINT32_MAX),
            yamlUInt(item, "metalColorSlot", UINT32_MAX),
            yamlBool(item, "depth"),
            yamlBool(item, "active"),
        });
    }
    return bindings;
}

struct PushConstantMembersParseResult {
    core::pmr::Vector<ShaderPushConstantMember> members{&core::defaultCpuMemoryResource()};
    bool success = true;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

bool parseHexUInt32(const std::pmr::string& text, std::uint32_t& value)
{
    std::string_view encoded(text.data(), text.size());
    if (encoded.size() >= 2u && encoded[0] == '0' && (encoded[1] == 'x' || encoded[1] == 'X')) {
        encoded.remove_prefix(2u);
    }
    if (encoded.empty()) {
        return false;
    }
    const auto parsed = std::from_chars(encoded.data(), encoded.data() + encoded.size(), value, 16);
    return parsed.ec == std::errc{} && parsed.ptr == encoded.data() + encoded.size();
}

// Parse the pushConstantMembers sequence emitted by shader_bindings_codegen.py.
// These are the authoritative member offsets/sizes/matrix stride from
// SPIRV-Reflect; contract tests cross-check them against the .spv payload.
PushConstantMembersParseResult parsePushConstantMembers(const YamlNode& root)
{
    PushConstantMembersParseResult result;
    const auto* seq = findYamlValue(root, "pushConstantMembers");
    if (seq == nullptr || !seq->is_sequence()) {
        return result;
    }
    for (const auto& item : seq->get_value_ref<const YamlNode::sequence_type&>()) {
        ShaderPushConstantMember m;
        m.name = yamlString(item, "name");
        m.scalarType = yamlString(item, "scalarType");
        m.scalarWidth = yamlUInt(item, "scalarWidth");
        m.vecSize = yamlUInt(item, "vecSize");
        m.matrixRows = yamlUInt(item, "matrixRows");
        m.matrixCols = yamlUInt(item, "matrixCols");
        m.matrixStride = yamlUInt(item, "matrixStride");
        m.rowMajor = yamlBool(item, "rowMajor");
        m.arrayDims = yamlUIntSequence(item, "arrayDims");
        m.arrayStride = yamlUInt(item, "arrayStride");
        m.offset = yamlUInt(item, "offset");
        m.size = yamlUInt(item, "size");
        // YAML resolves the compiler's unquoted 0x... flags as an integer.
        // Retain the documented legacy quoted hex encoding as well; invalid
        // present values must not silently become zero.
        if (const auto* decoration = findYamlValue(item, "decorations")) {
            bool valid = false;
            if (decoration->is_integer()) {
                const auto value = decoration->get_value<YamlNode::integer_type>();
                valid = value >= 0 && std::uint64_t(value) <= UINT32_MAX;
                if (valid)
                    m.decorations = static_cast<std::uint32_t>(value);
            } else if (decoration->is_string())
                valid = parseHexUInt32(yamlString(item, "decorations"), m.decorations);
            if (!valid) {
                result.success = false;
                result.diagnostics = "pushConstantMembers contains an invalid decorations value";
                return result;
            }
        }
        result.members.push_back(std::move(m));
    }
    return result;
}

} // namespace

ShaderReflectionCacheLoadResult ShaderReflectionCache::loadFromYaml(const std::pmr::string& yaml)
{
    ShaderReflectionCacheLoadResult result;
    if (yaml.empty()) {
        result.diagnostics = "reflection yaml is empty";
        return result;
    }
    core::TextDecodeDesc desc;
    desc.bom = core::TextBomPolicy::Strip;
    desc.nul = core::TextNulPolicy::Reject;
    desc.maxOutputBytes = yaml.size();
    const auto decoded = core::TextDecoder::decode(yaml, desc);
    if (decoded.status == core::TextDecodeStatus::AllocationFailed)
        throw std::bad_alloc();
    if (decoded.status != core::TextDecodeStatus::Success) {
        result.diagnostics = decoded.diagnosticId;
        return result;
    }

    YamlNode root;
    try {
        root =
            YamlNode::deserialize(decoded.text.data(), decoded.text.data() + decoded.text.size());
    } catch (const std::exception& err) {
        result.diagnostics = "reflection yaml parse failed: ";
        result.diagnostics += core::pmr::String(err.what(), &core::defaultCpuMemoryResource());
        return result;
    }

    result.module.moduleName = yamlString(root, "moduleName");
    result.module.sourcePath = yamlString(root, "sourcePath");
    result.module.reflection.reflectionContractVersion =
        yamlString(root, "reflectionContractVersion");
    result.module.reflection.sourceSha256 = yamlString(root, "sourceSha256");
    result.module.reflection.bindlessContractVersion = yamlString(root, "bindlessContractVersion");
    result.module.reflection.bindlessExecution = yamlString(root, "bindlessExecution");
    result.module.reflection.bindlessLoweringSha256 = yamlString(root, "bindlessLoweringSha256");
    // Target paths live under the nested ``targets`` mapping (spirv/metal/wgsl).
    // The old JSON regex loader matched these by first-occurrence substring
    // scan, which silently worked but was a latent bug; structured YAML access
    // requires going through targets.* explicitly.
    if (const auto* targets = findYamlValue(root, "targets")) {
        result.module.spirvPath = yamlString(*targets, "spirv");
        result.module.metalPath = yamlString(*targets, "metal");
        result.module.wgslPath = yamlString(*targets, "wgsl");
    }
    result.module.reflection.entryPoints = parseEntryPoints(root);
    result.module.reflection.bindings = parseBindings(root);
    result.module.reflection.webGpuBindingSpaceContractVersion =
        yamlString(root, "webGpuBindingSpaceContractVersion");
    result.module.reflection.webGpuBindingSpaceStatus =
        yamlString(root, "webGpuBindingSpaceStatus");
    result.module.reflection.webGpuBindingSpaceUnavailableReason =
        yamlString(root, "webGpuBindingSpaceUnavailableReason");
    result.module.reflection.webGpuBindingSpaceGroupCount =
        yamlUInt(root, "webGpuBindingSpaceGroupCount");
    result.module.reflection.webGpuBindingSpace = parseWebGpuBindingSpace(root);
    result.module.reflection.metalBindingSpaceContractVersion =
        yamlString(root, "metalBindingSpaceContractVersion");
    result.module.reflection.metalBindingSpaceStatus = yamlString(root, "metalBindingSpaceStatus");
    result.module.reflection.metalBindingSpaceUnavailableReason =
        yamlString(root, "metalBindingSpaceUnavailableReason");
    result.module.reflection.metalBindingSpace = parseMetalBindingSpace(root);
    result.module.reflection.metalArgumentBufferContractVersion =
        yamlString(root, "metalArgumentBufferContractVersion");
    result.module.reflection.metalArgumentBufferStatus =
        yamlString(root, "metalArgumentBufferStatus");
    result.module.reflection.metalArgumentBufferUnavailableReason =
        yamlString(root, "metalArgumentBufferUnavailableReason");
    result.module.reflection.metalArgumentBuffer = parseMetalArgumentBuffer(root);
    result.module.reflection.metalFramebufferFetchContractVersion =
        yamlString(root, "metalFramebufferFetchContractVersion");
    result.module.reflection.metalFramebufferFetchStatus =
        yamlString(root, "metalFramebufferFetchStatus");
    result.module.reflection.metalFramebufferFetchUnavailableReason =
        yamlString(root, "metalFramebufferFetchUnavailableReason");
    result.module.reflection.metalFramebufferFetch = parseMetalFramebufferFetch(root);
    result.module.reflection.pushConstantSizeBytes = yamlUInt(root, "pushConstantSizeBytes");
    result.module.reflection.vertexAttributes = parseVertexAttributes(root);

    // Track A P5: consume enriched reflection metadata.
    result.module.reflection.pushConstantContractVersion =
        yamlString(root, "pushConstantContractVersion");
    result.module.reflection.pushConstantStages = yamlString(root, "pushConstantStages");
    if (const auto* wgpuBinding = findYamlValue(root, "pushConstantWebGpuBinding")) {
        result.module.reflection.pushConstantWebGpuBindingGroup =
            yamlUInt(*wgpuBinding, "group", UINT32_MAX);
        result.module.reflection.pushConstantWebGpuBindingIndex =
            yamlUInt(*wgpuBinding, "binding", UINT32_MAX);
    }
    result.module.reflection.pushConstantMetalBufferIndex =
        yamlUInt(root, "pushConstantMetalBufferIndex", UINT32_MAX);
    result.module.reflection.computeWorkgroupSizeX = yamlUInt(root, "computeWorkgroupSizeX");
    result.module.reflection.computeWorkgroupSizeY = yamlUInt(root, "computeWorkgroupSizeY");
    result.module.reflection.computeWorkgroupSizeZ = yamlUInt(root, "computeWorkgroupSizeZ");
    result.module.reflection.tessellationControlPointCount =
        yamlUInt(root, "tessellationControlPointCount");
    result.module.reflection.metalGpuCountedIndirectContractVersion =
        yamlString(root, "metalGpuCountedIndirectContractVersion");
    result.module.reflection.metalGpuCountedIndirectStatus =
        yamlString(root, "metalGpuCountedIndirectStatus");
    result.module.reflection.metalGpuCountedIndirectCountBufferIndex =
        yamlUInt(root, "metalGpuCountedIndirectCountBufferIndex", UINT32_MAX);
    result.module.reflection.metalGpuCountedIndirectUsesDrawId =
        yamlBool(root, "metalGpuCountedIndirectUsesDrawId");
    result.module.reflection.metalGpuCountedIndirectMasksOutOfRange =
        yamlBool(root, "metalGpuCountedIndirectMasksOutOfRange");
    result.module.reflection.metalTessellationContractVersion =
        yamlString(root, "metalTessellationContractVersion");
    result.module.reflection.metalTessellationStatus = yamlString(root, "metalTessellationStatus");
    result.module.reflection.metalTessellationVertexEntryPoint =
        yamlString(root, "metalTessellationVertexEntryPoint");
    result.module.reflection.metalTessellationFactorBufferIndex =
        yamlUInt(root, "metalTessellationFactorBufferIndex", UINT32_MAX);
    result.module.reflection.metalTessellationControlPointCount =
        yamlUInt(root, "metalTessellationControlPointCount");
    result.module.reflection.metalTessellationPatchType =
        yamlUInt(root, "metalTessellationPatchType", UINT32_MAX);
    result.module.reflection.metalTessellationFactorFormat =
        yamlUInt(root, "metalTessellationFactorFormat", UINT32_MAX);
    result.module.reflection.metalTessellationFactorBytesPerPatch =
        yamlUInt(root, "metalTessellationFactorBytesPerPatch");
    result.module.reflection.metalTessellationFactorInstanceStrideBytes =
        yamlUInt(root, "metalTessellationFactorInstanceStrideBytes");

    // Authoritative member-level layout from SPIRV-Reflect (codegen).
    auto pushConstantMembers = parsePushConstantMembers(root);
    if (!pushConstantMembers.success) {
        result.diagnostics = std::move(pushConstantMembers.diagnostics);
        return result;
    }
    result.module.reflection.pushConstantMembers = std::move(pushConstantMembers.members);

    if (result.module.moduleName.empty()) {
        result.diagnostics = "moduleName is missing";
        return result;
    }
    if (result.module.reflection.entryPoints.empty()) {
        result.diagnostics = "entryPoints are missing";
        return result;
    }

    result.success = true;
    return result;
}

ShaderReflectionCacheLoadResult ShaderReflectionCache::loadFromFile(const std::pmr::string& path)
{
    const auto file = core::readTextFile(std::filesystem::u8path(path.begin(), path.end()));
    if (!file.success) {
        ShaderReflectionCacheLoadResult result;
        result.diagnostics = "reflection file could not be opened: ";
        result.diagnostics += path;
        return result;
    }

    return loadFromYaml(file.text);
}

} // namespace arp::shader
