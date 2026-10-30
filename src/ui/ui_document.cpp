#include "ui/ui_document.h"

#include "core/hash/stable_hash.h"
#include "core/io/file_io.h"
#include "resource/resource_source_io.h"
#include "ui/detail/ui_content_fingerprint.h"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fkYAML/Node.hpp>
#include <limits>
#include <utility>

namespace arp::ui {

namespace {

using YamlNode = fkyaml::node;

constexpr const char* kArpuiResourceType = "UiDocumentResource";
constexpr const char* kArpuiEnvelopePath = "resources/ui-document.arpres.yaml";

// Shared cold acquisition for document and authoring IR. Prepared contexts are
// authoritative; readResourceText rejects missing/foreign sources without IO.
core::TextFileReadResult readDocumentText(const std::filesystem::path& path,
    const core::pmr::String& supplied,
    bool provided,
    std::size_t budget,
    const resource::ResourceLoadContext& context,
    resource::ResourceFileOrigin origin)
{
    core::TextFileReadResult result;
    if (!budget || budget == SIZE_MAX) {
        result.diagnostics = "UI-DOCUMENT-SOURCE-BUDGET";
        return result;
    }
    if (!provided && supplied.empty()) {
        return resource::readResourceText(core::FilePath::utf8(path), context, budget, &origin);
    }
    if (supplied.size() > budget) {
        result.diagnostics = "UI-DOCUMENT-SOURCE-BUDGET";
        return result;
    }
    core::TextDecodeDesc decode;
    decode.bom = core::TextBomPolicy::Strip;
    decode.nul = core::TextNulPolicy::Reject;
    decode.maxOutputBytes = budget;
    auto decoded = core::TextDecoder::decode({supplied.data(), supplied.size()}, decode);
    result.success = decoded.status == core::TextDecodeStatus::Success;
    result.text = std::move(decoded.text);
    if (!result.success)
        result.diagnostics = decoded.diagnosticId;
    return result;
}

arp::core::pmr::String diagnosticText(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String diagnosticText(const char* prefix, const arp::core::pmr::String& value)
{
    arp::core::pmr::String result(prefix, &arp::core::defaultCpuMemoryResource());
    result += value;
    return result;
}

void appendUint(arp::core::pmr::String& out, std::uint32_t value)
{
    char buffer[16]{};
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (converted.ec == std::errc{}) {
        out.append(buffer, static_cast<std::size_t>(converted.ptr - buffer));
    }
}

void appendFloat(arp::core::pmr::String& out, float value)
{
    char buffer[32]{};
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (converted.ec == std::errc{}) {
        out.append(buffer, static_cast<std::size_t>(converted.ptr - buffer));
    }
}

void appendIndentedBlock(
    arp::core::pmr::String& out, const arp::core::pmr::String& text, const char* indent)
{
    out += indent;
    for (const char ch : text) {
        out.push_back(ch);
        if (ch == '\n') {
            out += indent;
        }
    }
    if (out.empty() || out.back() != '\n') {
        out += "\n";
    }
}

const YamlNode* findMappingValue(const YamlNode& mapping, const char* key)
{
    if (!mapping.is_mapping()) {
        return nullptr;
    }
    const auto& values = mapping.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = values.find(YamlNode(key));
    return found == values.end() ? nullptr : &found->second;
}

arp::core::pmr::String yamlString(const YamlNode& node)
{
    const auto& value = node.get_value_ref<const YamlNode::string_type&>();
    return arp::core::pmr::String(
        value.data(), value.size(), &arp::core::defaultCpuMemoryResource());
}

bool hasParent(const UiDocument& document, UiDocumentNodeId id)
{
    for (const auto& node : document.nodes()) {
        if (node.nodeId == id) {
            return true;
        }
    }
    return false;
}

UiDocumentDesc toLegacyDocumentDesc(const DocumentDesc& desc)
{
    UiDocumentDesc legacy;
    legacy.canvas = desc.canvas;
    legacy.textures = desc.textures;
    legacy.nodes.reserve(desc.nodes.size());
    for (const auto& node : desc.nodes) {
        UiDocumentNodeDesc legacyNode;
        legacyNode.nodeId = UiDocumentNodeId::fromValue(node.nodeId.value());
        legacyNode.parentNodeId = node.parentNodeId.valid()
            ? UiDocumentNodeId::fromValue(node.parentNodeId.value())
            : UiDocumentNodeId::invalid();
        if (node.nodeId.valid()) {
            legacyNode.sourceId =
                arp::core::pmr::String("node_", &arp::core::defaultCpuMemoryResource());
            appendUint(legacyNode.sourceId, static_cast<std::uint32_t>(node.nodeId.value()));
        }
        legacyNode.controlType = node.controlType;
        legacyNode.maxTextBytes = node.maxTextBytes;
        legacyNode.layout = node.layout;
        legacyNode.layoutMode = node.layoutMode;
        legacyNode.stackLayout = node.stackLayout;
        legacyNode.layoutParticipation = node.layoutParticipation;
        legacyNode.styleId = node.styleId;
        legacyNode.fontToken = node.fontToken;
        legacyNode.text = node.text;
        legacyNode.stateId = node.stateId;
        legacyNode.eventRole = node.eventRole;
        legacyNode.resourceBindings = node.resourceBindings;
        legacyNode.psdLayerBinding = node.psdLayerBinding;
        legacy.nodes.push_back(std::move(legacyNode));
    }
    return legacy;
}

bool validTextureMetadata(const UiDocumentTextureEntry& texture)
{
    if (texture.resourceId.empty() || texture.sourceLayerId.empty() || texture.atlasId.empty()
        || !texture.sourceRect.valid() || texture.sourceRect.width <= 0.0f
        || texture.sourceRect.height <= 0.0f || !std::isfinite(texture.pivot.x)
        || !std::isfinite(texture.pivot.y) || texture.alphaMode == UiTextureAlphaMode::Unknown
        || !texture.hasExplicitSrgb || texture.mipIntent == UiTextureMipIntent::Unknown
        || texture.compressionIntent == UiTextureCompressionIntent::Unknown
        || texture.samplerIntent == UiTextureSamplerIntent::Unknown) {
        return false;
    }
    if (texture.nineSlice.left < 0.0f || texture.nineSlice.top < 0.0f
        || texture.nineSlice.right < 0.0f || texture.nineSlice.bottom < 0.0f) {
        return false;
    }
    return texture.nineSlice.left + texture.nineSlice.right <= texture.sourceRect.width
        && texture.nineSlice.top + texture.nineSlice.bottom <= texture.sourceRect.height;
}

bool requireYamlString(const YamlNode& mapping,
    const char* key,
    arp::core::pmr::String& out,
    arp::core::pmr::String& diagnostics)
{
    const auto* value = findMappingValue(mapping, key);
    if (value == nullptr || !value->is_string()) {
        diagnostics = diagnosticText("arpui IR requires string field: ");
        diagnostics += key;
        return false;
    }
    out = yamlString(*value);
    return true;
}

bool optionalYamlString(const YamlNode& mapping, const char* key, arp::core::pmr::String& out)
{
    const auto* value = findMappingValue(mapping, key);
    if (value == nullptr || !value->is_string()) {
        return false;
    }
    out = yamlString(*value);
    return true;
}

bool requireYamlFloat(
    const YamlNode& mapping, const char* key, float& out, arp::core::pmr::String& diagnostics)
{
    const auto* value = findMappingValue(mapping, key);
    if (value == nullptr || (!value->is_float_number() && !value->is_integer())) {
        diagnostics = diagnosticText("arpui IR requires numeric field: ");
        diagnostics += key;
        return false;
    }
    out = value->is_float_number()
        ? static_cast<float>(value->get_value<YamlNode::float_number_type>())
        : static_cast<float>(value->get_value<YamlNode::integer_type>());
    return std::isfinite(out);
}

bool optionalYamlFloat(const YamlNode& mapping, const char* key, float& out)
{
    const auto* value = findMappingValue(mapping, key);
    if (value == nullptr || (!value->is_float_number() && !value->is_integer())) {
        return false;
    }
    out = value->is_float_number()
        ? static_cast<float>(value->get_value<YamlNode::float_number_type>())
        : static_cast<float>(value->get_value<YamlNode::integer_type>());
    return std::isfinite(out);
}

bool requireYamlU64(const YamlNode& mapping,
    const char* key,
    std::uint64_t& out,
    arp::core::pmr::String& diagnostics)
{
    const auto* value = findMappingValue(mapping, key);
    if (value == nullptr || !value->is_integer()) {
        diagnostics = diagnosticText("arpui IR requires integer field: ");
        diagnostics += key;
        return false;
    }
    const auto parsed = value->get_value<YamlNode::integer_type>();
    if (parsed < 0) {
        diagnostics = diagnosticText("arpui IR integer field must be non-negative: ");
        diagnostics += key;
        return false;
    }
    out = static_cast<std::uint64_t>(parsed);
    return true;
}

bool requireYamlId(const YamlNode& mapping,
    const char* key,
    arp::core::pmr::String& out,
    arp::core::pmr::String& diagnostics)
{
    const auto* value = findMappingValue(mapping, key);
    if (value == nullptr || (!value->is_string() && !value->is_integer())) {
        diagnostics = diagnosticText(".arpui YAML requires string or integer id field: ");
        diagnostics += key;
        return false;
    }
    if (value->is_string()) {
        out = yamlString(*value);
        if (out.empty()) {
            diagnostics = diagnosticText(".arpui YAML id field must be non-empty: ");
            diagnostics += key;
            return false;
        }
        return true;
    }
    const auto parsed = value->get_value<YamlNode::integer_type>();
    if (parsed <= 0) {
        diagnostics = diagnosticText(".arpui YAML integer id field must be positive: ");
        diagnostics += key;
        return false;
    }
    char buffer[32]{};
    const auto converted =
        std::to_chars(buffer, buffer + sizeof(buffer), static_cast<std::uint64_t>(parsed));
    if (converted.ec != std::errc{}) {
        diagnostics = diagnosticText(".arpui YAML id conversion failed: ");
        diagnostics += key;
        return false;
    }
    out = arp::core::pmr::String(buffer,
        static_cast<std::size_t>(converted.ptr - buffer),
        &arp::core::defaultCpuMemoryResource());
    return true;
}

bool optionalYamlU64(const YamlNode& mapping, const char* key, std::uint64_t& out)
{
    const auto* value = findMappingValue(mapping, key);
    if (value == nullptr || !value->is_integer()) {
        return false;
    }
    const auto parsed = value->get_value<YamlNode::integer_type>();
    if (parsed < 0) {
        return false;
    }
    out = static_cast<std::uint64_t>(parsed);
    return true;
}

bool requireMapping(const YamlNode& mapping,
    const char* key,
    const YamlNode*& out,
    arp::core::pmr::String& diagnostics)
{
    out = findMappingValue(mapping, key);
    if (out == nullptr || !out->is_mapping()) {
        diagnostics = diagnosticText("arpui IR requires mapping field: ");
        diagnostics += key;
        return false;
    }
    return true;
}

bool looksLikeJsonDocument(const arp::core::pmr::String& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    return first != arp::core::pmr::String::npos && text[first] == '{';
}

UiControlType parseControlType(const arp::core::pmr::String& value);
UiLayoutParticipation parseLayoutParticipation(const arp::core::pmr::String& value);
UiLayoutMode parseLayoutMode(const arp::core::pmr::String& value);
UiStackDirection parseStackDirection(const arp::core::pmr::String& value);
UiTextureAlphaMode parseAlphaMode(const arp::core::pmr::String& value);
UiTextureMipIntent parseMipIntent(const arp::core::pmr::String& value);
UiTextureCompressionIntent parseCompressionIntent(const arp::core::pmr::String& value);
UiTextureSamplerIntent parseSamplerIntent(const arp::core::pmr::String& value);

bool readYamlFloatRect(
    const YamlNode& mapping, LayoutRect& out, arp::core::pmr::String& diagnostics)
{
    const YamlNode* rect = nullptr;
    if (!requireMapping(mapping, "rect", rect, diagnostics)) {
        return false;
    }
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (!requireYamlFloat(*rect, "x", x, diagnostics)
        || !requireYamlFloat(*rect, "y", y, diagnostics)
        || !requireYamlFloat(*rect, "width", width, diagnostics)
        || !requireYamlFloat(*rect, "height", height, diagnostics)) {
        return false;
    }
    out = LayoutRect::fromOriginSize(x, y, width, height);
    return true;
}

bool parseArpuiYamlTexture(const YamlNode& textureNode,
    UiDocumentTextureEntry& texture,
    arp::core::pmr::String& diagnostics)
{
    if (!textureNode.is_mapping()) {
        diagnostics = diagnosticText(".arpui YAML texture entry must be a mapping");
        return false;
    }

    arp::core::pmr::String alpha(&arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String mips(&arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String compression(&arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String sampler(&arp::core::defaultCpuMemoryResource());
    if (!requireYamlString(textureNode, "resourceId", texture.resourceId, diagnostics)
        || !requireYamlString(textureNode, "layer", texture.sourceLayerId, diagnostics)
        || !requireYamlString(textureNode, "atlas", texture.atlasId, diagnostics)
        || !readYamlFloatRect(textureNode, texture.sourceRect, diagnostics)
        || !requireYamlFloat(textureNode, "pivotX", texture.pivot.x, diagnostics)
        || !requireYamlFloat(textureNode, "pivotY", texture.pivot.y, diagnostics)
        || !requireYamlFloat(textureNode, "sliceLeft", texture.nineSlice.left, diagnostics)
        || !requireYamlFloat(textureNode, "sliceTop", texture.nineSlice.top, diagnostics)
        || !requireYamlFloat(textureNode, "sliceRight", texture.nineSlice.right, diagnostics)
        || !requireYamlFloat(textureNode, "sliceBottom", texture.nineSlice.bottom, diagnostics)
        || !requireYamlString(textureNode, "alpha", alpha, diagnostics)
        || !requireYamlString(textureNode, "mips", mips, diagnostics)
        || !requireYamlString(textureNode, "compression", compression, diagnostics)
        || !requireYamlString(textureNode, "sampler", sampler, diagnostics)) {
        return false;
    }
    const auto* srgb = findMappingValue(textureNode, "srgb");
    if (srgb == nullptr || !srgb->is_boolean()) {
        diagnostics = diagnosticText(".arpui YAML texture requires boolean field: srgb");
        return false;
    }
    texture.alphaMode = parseAlphaMode(alpha);
    texture.hasExplicitSrgb = true;
    texture.srgb = srgb->get_value<bool>();
    texture.mipIntent = parseMipIntent(mips);
    texture.compressionIntent = parseCompressionIntent(compression);
    texture.samplerIntent = parseSamplerIntent(sampler);
    return true;
}

bool parseArpuiYamlLayout(
    const YamlNode& layoutNode, UiDocumentNodeDesc& node, arp::core::pmr::String& diagnostics)
{
    if (!layoutNode.is_mapping()) {
        diagnostics = diagnosticText(".arpui YAML layout must be a mapping");
        return false;
    }
    arp::core::pmr::String layoutType(&arp::core::defaultCpuMemoryResource());
    if (!requireYamlString(layoutNode, "type", layoutType, diagnostics)) {
        return false;
    }
    node.layoutMode = parseLayoutMode(layoutType);
    if (node.layoutMode == UiLayoutMode::None) {
        diagnostics = diagnosticText(".arpui YAML layout type is unsupported: ", layoutType);
        return false;
    }
    node.stackLayout.direction = parseStackDirection(layoutType);
    optionalYamlFloat(layoutNode, "spacing", node.stackLayout.spacing);
    optionalYamlFloat(layoutNode, "paddingLeft", node.stackLayout.padding.left);
    optionalYamlFloat(layoutNode, "paddingTop", node.stackLayout.padding.top);
    optionalYamlFloat(layoutNode, "paddingRight", node.stackLayout.padding.right);
    optionalYamlFloat(layoutNode, "paddingBottom", node.stackLayout.padding.bottom);
    if (node.stackLayout.spacing < 0.0f || node.stackLayout.padding.left < 0.0f
        || node.stackLayout.padding.top < 0.0f || node.stackLayout.padding.right < 0.0f
        || node.stackLayout.padding.bottom < 0.0f) {
        diagnostics =
            diagnosticText(".arpui YAML stack layout requires non-negative spacing and padding");
        return false;
    }
    return true;
}

bool parseTextInputBudget(
    const YamlNode& yaml, UiDocumentNodeDesc& node, arp::core::pmr::String& diagnostic)
{
    const auto* value = findMappingValue(yaml, "maxTextBytes");
    if (!value)
        return true;
    std::uint64_t budget = 0;
    if (node.controlType != UiControlType::TextInput
        || !requireYamlU64(yaml, "maxTextBytes", budget, diagnostic) || !budget
        || budget > std::numeric_limits<std::uint32_t>::max()) {
        diagnostic = diagnosticText(
            "UI-TEXT-BUDGET-INVALID: positive uint32 maxTextBytes requires TextInput");
        return false;
    }
    node.maxTextBytes = static_cast<std::uint32_t>(budget);
    return true;
}

bool parseArpuiYamlNode(
    const YamlNode& nodeYaml, UiDocumentNodeDesc& node, arp::core::pmr::String& diagnostics)
{
    if (!nodeYaml.is_mapping()) {
        diagnostics = diagnosticText(".arpui YAML node entry must be a mapping");
        return false;
    }
    std::uint64_t id = 0u;
    std::uint64_t parent = 0u;
    arp::core::pmr::String controlType(&arp::core::defaultCpuMemoryResource());
    if (!requireYamlU64(nodeYaml, "id", id, diagnostics)
        || !requireYamlString(nodeYaml, "type", controlType, diagnostics)
        || !requireYamlString(nodeYaml, "layer", node.psdLayerBinding.sourceLayerId, diagnostics)
        || !requireYamlString(nodeYaml, "style", node.styleId, diagnostics)
        || !requireYamlString(nodeYaml, "state", node.stateId, diagnostics)
        || !requireYamlString(nodeYaml, "event", node.eventRole, diagnostics)
        || !readYamlFloatRect(nodeYaml, node.layout, diagnostics)) {
        return false;
    }
    optionalYamlU64(nodeYaml, "parent", parent);
    node.nodeId = UiDocumentNodeId::fromValue(id);
    node.parentNodeId =
        parent == 0u ? UiDocumentNodeId::invalid() : UiDocumentNodeId::fromValue(parent);
    node.sourceId = arp::core::pmr::String("node_", &arp::core::defaultCpuMemoryResource());
    appendUint(node.sourceId, static_cast<std::uint32_t>(id));
    node.controlType = parseControlType(controlType);
    if (!parseTextInputBudget(nodeYaml, node, diagnostics))
        return false;
    optionalYamlString(nodeYaml, "text", node.text);
    optionalYamlString(nodeYaml, "fontToken", node.fontToken);

    arp::core::pmr::String layoutRole(&arp::core::defaultCpuMemoryResource());
    if (optionalYamlString(nodeYaml, "layoutRole", layoutRole)) {
        node.layoutParticipation = parseLayoutParticipation(layoutRole);
        if (node.layoutParticipation == UiLayoutParticipation::Absolute
            && layoutRole != "absolute") {
            diagnostics =
                diagnosticText(".arpui YAML node has unsupported layoutRole: ", layoutRole);
            return false;
        }
    }
    if (const auto* layout = findMappingValue(nodeYaml, "layout")) {
        if (!parseArpuiYamlLayout(*layout, node, diagnostics)) {
            return false;
        }
    }

    arp::core::pmr::String resourceRole(&arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String resourceId(&arp::core::defaultCpuMemoryResource());
    const auto hasResourceRole = optionalYamlString(nodeYaml, "resourceRole", resourceRole);
    const auto hasResourceId = optionalYamlString(nodeYaml, "resourceId", resourceId);
    if (hasResourceRole || hasResourceId) {
        UiDocumentResourceBinding binding;
        binding.role = std::move(resourceRole);
        binding.resourceId = std::move(resourceId);
        binding.required = true;
        node.resourceBindings.push_back(std::move(binding));
    }
    return true;
}

struct UiDocumentV1IdBinding {
    arp::core::pmr::String sourceId{&arp::core::defaultCpuMemoryResource()};
    UiDocumentNodeId nodeId;
};

bool sourceIdExists(const arp::core::pmr::Vector<UiDocumentV1IdBinding>& bindings,
    const arp::core::pmr::String& sourceId)
{
    return std::any_of(bindings.begin(), bindings.end(), [&](const UiDocumentV1IdBinding& binding) {
        return binding.sourceId == sourceId;
    });
}

bool parseUiDocumentV1Node(const YamlNode& nodeYaml,
    UiDocumentNodeId parentNodeId,
    std::uint64_t& nextNodeId,
    arp::core::pmr::Vector<UiDocumentV1IdBinding>& bindings,
    arp::core::pmr::Vector<UiDocumentNodeDesc>& nodes,
    arp::core::pmr::String& diagnostics)
{
    if (!nodeYaml.is_mapping()) {
        diagnostics = diagnosticText(".arpui YAML node entry must be a mapping");
        return false;
    }

    arp::core::pmr::String sourceId(&arp::core::defaultCpuMemoryResource());
    if (!requireYamlId(nodeYaml, "id", sourceId, diagnostics)) {
        return false;
    }
    if (sourceIdExists(bindings, sourceId)) {
        diagnostics = diagnosticText(".arpui YAML node id must be unique: ", sourceId);
        return false;
    }

    UiDocumentNodeDesc node;
    node.nodeId = UiDocumentNodeId::fromValue(nextNodeId++);
    node.parentNodeId = parentNodeId;
    node.sourceId = sourceId;
    bindings.push_back(UiDocumentV1IdBinding{sourceId, node.nodeId});

    arp::core::pmr::String controlType(&arp::core::defaultCpuMemoryResource());
    if (!requireYamlString(nodeYaml, "type", controlType, diagnostics)
        || !requireYamlString(nodeYaml, "layer", node.psdLayerBinding.sourceLayerId, diagnostics)
        || !requireYamlString(nodeYaml, "style", node.styleId, diagnostics)
        || !requireYamlString(nodeYaml, "state", node.stateId, diagnostics)
        || !requireYamlString(nodeYaml, "event", node.eventRole, diagnostics)
        || !readYamlFloatRect(nodeYaml, node.layout, diagnostics)) {
        return false;
    }

    node.controlType = parseControlType(controlType);
    if (!parseTextInputBudget(nodeYaml, node, diagnostics))
        return false;
    optionalYamlString(nodeYaml, "text", node.text);
    optionalYamlString(nodeYaml, "fontToken", node.fontToken);

    arp::core::pmr::String layoutRole(&arp::core::defaultCpuMemoryResource());
    if (optionalYamlString(nodeYaml, "layoutRole", layoutRole)) {
        node.layoutParticipation = parseLayoutParticipation(layoutRole);
        if (node.layoutParticipation == UiLayoutParticipation::Absolute
            && layoutRole != "absolute") {
            diagnostics =
                diagnosticText(".arpui YAML node has unsupported layoutRole: ", layoutRole);
            return false;
        }
    }
    if (const auto* layout = findMappingValue(nodeYaml, "layout")) {
        if (!parseArpuiYamlLayout(*layout, node, diagnostics)) {
            return false;
        }
    }

    arp::core::pmr::String resourceRole(&arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String resourceId(&arp::core::defaultCpuMemoryResource());
    const auto hasResourceRole = optionalYamlString(nodeYaml, "resourceRole", resourceRole);
    const auto hasResourceId = optionalYamlString(nodeYaml, "resourceId", resourceId);
    if (hasResourceRole || hasResourceId) {
        UiDocumentResourceBinding binding;
        binding.role = std::move(resourceRole);
        binding.resourceId = std::move(resourceId);
        binding.required = true;
        node.resourceBindings.push_back(std::move(binding));
    }

    const auto parsedNodeId = node.nodeId;
    nodes.push_back(std::move(node));

    if (const auto* children = findMappingValue(nodeYaml, "children")) {
        if (!children->is_sequence()) {
            diagnostics = diagnosticText(".arpui YAML node children must be a sequence");
            return false;
        }
        for (const auto& childYaml : children->get_value_ref<const YamlNode::sequence_type&>()) {
            if (!parseUiDocumentV1Node(
                    childYaml, parsedNodeId, nextNodeId, bindings, nodes, diagnostics)) {
                return false;
            }
        }
    }
    return true;
}

const UiDocumentNodeDesc* findDocumentNode(
    const arp::core::pmr::Vector<UiDocumentNodeDesc>& nodes, UiDocumentNodeId nodeId)
{
    const auto found =
        std::find_if(nodes.begin(), nodes.end(), [nodeId](const UiDocumentNodeDesc& node) {
            return node.nodeId == nodeId;
        });
    return found == nodes.end() ? nullptr : &*found;
}

void appendYamlLine(arp::core::pmr::String& out,
    const char* indent,
    const char* key,
    const arp::core::pmr::String& value)
{
    out += indent;
    out += key;
    out += ": ";
    out += value;
    out += "\n";
}

void appendYamlRect(arp::core::pmr::String& out, const char* indent, const LayoutRect& rect)
{
    out += indent;
    out += "rect: { x: ";
    appendFloat(out, rect.x);
    out += ", y: ";
    appendFloat(out, rect.y);
    out += ", width: ";
    appendFloat(out, rect.width);
    out += ", height: ";
    appendFloat(out, rect.height);
    out += " }\n";
}

void appendYamlLayout(
    arp::core::pmr::String& out, const char* indent, const UiDocumentNodeDesc& node)
{
    if (node.layoutMode != UiLayoutMode::Stack) {
        return;
    }
    out += indent;
    out += "layout: { type: ";
    out += node.stackLayout.direction == UiStackDirection::Horizontal ? "horizontalStack"
                                                                      : "verticalStack";
    out += ", paddingLeft: ";
    appendFloat(out, node.stackLayout.padding.left);
    out += ", paddingTop: ";
    appendFloat(out, node.stackLayout.padding.top);
    out += ", paddingRight: ";
    appendFloat(out, node.stackLayout.padding.right);
    out += ", paddingBottom: ";
    appendFloat(out, node.stackLayout.padding.bottom);
    out += ", spacing: ";
    appendFloat(out, node.stackLayout.spacing);
    out += " }\n";
}

void appendUiDocumentV1NodeYaml(arp::core::pmr::String& out,
    const arp::core::pmr::Vector<UiDocumentNodeDesc>& nodes,
    const UiDocumentNodeDesc& node,
    const char* indent,
    bool sequenceItem)
{
    out += indent;
    out += sequenceItem ? "- id: " : "id: ";
    if (!node.parentNodeId.valid()) {
        out += "root\n";
    } else {
        out += "node_";
        appendUint(out, static_cast<std::uint32_t>(node.nodeId.value()));
        out += "\n";
    }
    arp::core::pmr::String fieldIndent(indent, &arp::core::defaultCpuMemoryResource());
    if (sequenceItem) {
        fieldIndent += "  ";
    }
    appendYamlLine(out,
        fieldIndent.c_str(),
        "type",
        arp::core::pmr::String(
            controlTypeName(node.controlType), &arp::core::defaultCpuMemoryResource()));
    appendYamlLine(out, fieldIndent.c_str(), "layer", node.psdLayerBinding.sourceLayerId);
    appendYamlRect(out, fieldIndent.c_str(), node.layout);
    appendYamlLine(out, fieldIndent.c_str(), "style", node.styleId);
    appendYamlLine(out, fieldIndent.c_str(), "state", node.stateId);
    appendYamlLine(out, fieldIndent.c_str(), "event", node.eventRole);
    if (node.controlType == UiControlType::TextInput) {
        out += fieldIndent;
        out += "maxTextBytes: ";
        appendUint(out, node.maxTextBytes);
        out += '\n';
    }
    if (!node.text.empty()) {
        appendYamlLine(out, fieldIndent.c_str(), "text", node.text);
    }
    if (node.layoutParticipation == UiLayoutParticipation::StackItem) {
        out += fieldIndent;
        out += "layoutRole: stackItem\n";
    }
    appendYamlLayout(out, fieldIndent.c_str(), node);
    for (const auto& binding : node.resourceBindings) {
        if (!binding.role.empty()) {
            appendYamlLine(out, fieldIndent.c_str(), "resourceRole", binding.role);
        }
        if (!binding.resourceId.empty()) {
            appendYamlLine(out, fieldIndent.c_str(), "resourceId", binding.resourceId);
        }
    }

    bool wroteChildren = false;
    for (const auto& child : nodes) {
        if (child.parentNodeId != node.nodeId) {
            continue;
        }
        if (!wroteChildren) {
            out += fieldIndent;
            out += "children:\n";
            wroteChildren = true;
        }
        arp::core::pmr::String childIndent(fieldIndent);
        childIndent += "  ";
        appendUiDocumentV1NodeYaml(out, nodes, child, childIndent.c_str(), true);
    }
}

arp::core::pmr::String buildUiDocumentV1YamlFromDesc(const UiDocumentDesc& desc)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "type: ui.document\n";
    yaml += "contractVersion: ui.document/v1\n";
    yaml += "canvas:\n";
    appendYamlLine(yaml, "  ", "id", desc.canvas.canvasId);
    yaml += "  width: ";
    appendFloat(yaml, desc.canvas.width);
    yaml += "\n  height: ";
    appendFloat(yaml, desc.canvas.height);
    yaml += "\n  dpi: ";
    appendFloat(yaml, desc.canvas.designDpi);
    yaml += "\n";
    if (!desc.textures.empty()) {
        yaml += "textures:\n";
        for (const auto& texture : desc.textures) {
            yaml += "  - resourceId: ";
            yaml += texture.resourceId;
            yaml += "\n    layer: ";
            yaml += texture.sourceLayerId;
            yaml += "\n    atlas: ";
            yaml += texture.atlasId;
            yaml += "\n";
            appendYamlRect(yaml, "    ", texture.sourceRect);
            yaml += "    pivotX: ";
            appendFloat(yaml, texture.pivot.x);
            yaml += "\n    pivotY: ";
            appendFloat(yaml, texture.pivot.y);
            yaml += "\n    sliceLeft: ";
            appendFloat(yaml, texture.nineSlice.left);
            yaml += "\n    sliceTop: ";
            appendFloat(yaml, texture.nineSlice.top);
            yaml += "\n    sliceRight: ";
            appendFloat(yaml, texture.nineSlice.right);
            yaml += "\n    sliceBottom: ";
            appendFloat(yaml, texture.nineSlice.bottom);
            yaml +=
                "\n    alpha: straight\n    srgb: true\n    mips: none\n    compression: ui-lossless\n    sampler: linear-clamp\n";
        }
    } else {
        yaml += "textures: []\n";
    }
    const auto* root = findDocumentNode(desc.nodes, UiDocumentNodeId::fromValue(1u));
    if (root != nullptr) {
        yaml += "root:\n";
        appendUiDocumentV1NodeYaml(yaml, desc.nodes, *root, "  ", false);
    }
    return yaml;
}

UiArpuiRuntimeLoadResult createRuntimeFromDocument(UiDocument document,
    const UiControlFactoryRegistry* factories = nullptr,
    size_t maxWidgetCount = 0)
{
    const auto validation = UiDocumentValidator().validate(document);
    if (!validation.ok()) {
        return UiArpuiRuntimeLoadResult::failure(
            UiDiagnosticCode::RuntimeLoadFailed, validation.diagnostic().message);
    }

    if (factories) {
        for (const auto& binding : factories->bindings()) {
            const auto found = std::find_if(
                document.nodes().begin(), document.nodes().end(), [&](const auto& node) {
                    return node.sourceId == binding.sourceId;
                });
            if (found == document.nodes().end())
                return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::InvalidFactoryBinding,
                    diagnosticText("UI-FACTORY-SOURCE-ID-MISSING: ", binding.sourceId));
        }
    }
    CanvasDesc canvasDesc;
    canvasDesc.viewport =
        UiViewport::explicitPixels(document.canvas().width, document.canvas().height);
    canvasDesc.safeArea = UiSafeArea::none();
    canvasDesc.scalePolicy = UiScalePolicy::fixed(1.0f);
    canvasDesc.maxWidgetCount = maxWidgetCount ? maxWidgetCount : document.nodes().size() + 1u;
    if (canvasDesc.maxWidgetCount < document.nodes().size() + 1u)
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::EventCapacityExceeded,
            diagnosticText("UI-WIDGET-BUDGET-EXCEEDED: document exceeds the loader capacity."));
    auto runtime = arp::core::makeUnique<UiControlRuntime>(document.canvas().canvasId, canvasDesc);

    struct NodeWidgetBinding {
        UiDocumentNodeId nodeId;
        WidgetId widgetId;
    };

    arp::core::pmr::Vector<NodeWidgetBinding> bindings;
    arp::core::pmr::Vector<UiRuntimeNodeBinding> runtimeBindings;
    for (const auto& node : document.nodes()) {
        WidgetId parentWidget = runtime->rootId();
        if (node.parentNodeId.valid()) {
            const auto found = std::find_if(
                bindings.begin(), bindings.end(), [&](const NodeWidgetBinding& binding) {
                    return binding.nodeId == node.parentNodeId;
                });
            if (found == bindings.end()) {
                return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
                    diagnosticText(".arpui node parent was not loaded before child"));
            }
            parentWidget = found->widgetId;
        }

        UiControlDesc controlDesc;
        controlDesc.maxTextBytes = node.maxTextBytes;
        controlDesc.type = node.controlType;
        controlDesc.widget.name = node.text.empty()
            ? arp::core::pmr::String(
                  controlTypeName(node.controlType), &arp::core::defaultCpuMemoryResource())
            : node.text;
        controlDesc.widget.localRect = node.layout;
        controlDesc.widget.controlType = node.controlType;
        controlDesc.widget.text = node.text;
        if (node.controlType == UiControlType::Label
            || node.controlType == UiControlType::TextInput)
            controlDesc.initialValue = UiControlValue::fromText(node.text);
        controlDesc.widget.styleId = node.styleId;
        controlDesc.widget.fontToken = node.fontToken;
        controlDesc.widget.layoutMode = node.layoutMode;
        controlDesc.widget.stackLayout = node.stackLayout;
        controlDesc.widget.layoutParticipation = node.layoutParticipation;
        for (const auto& binding : node.resourceBindings) {
            if (binding.role == "texture" && !binding.resourceId.empty()) {
                const auto texture = std::find_if(document.textures().begin(),
                    document.textures().end(),
                    [&](const UiDocumentTextureEntry& entry) {
                        return entry.resourceId == binding.resourceId;
                    });
                if (texture == document.textures().end()) {
                    return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
                        diagnosticText(
                            ".arpui texture resource binding has no matching texture metadata: ",
                            binding.resourceId));
                }
                controlDesc.widget.textureResourceId = binding.resourceId;
                controlDesc.widget.textureSourceRect = texture->sourceRect;
                controlDesc.widget.hasTextureSourceRect = true;
                break;
            }
        }
        controlDesc.widget.acceptsFocus = node.controlType == UiControlType::Button
            || node.controlType == UiControlType::TextInput
            || node.controlType == UiControlType::NumericInput;
        const auto* factory = factories ? factories->find(node.sourceId) : nullptr;
        auto customObject =
            factory ? factory->create(controlDesc) : arp::core::UniquePtr<Control>{};
        const auto created = factory
            ? runtime->createControl(std::move(customObject), std::move(controlDesc), parentWidget)
            : runtime->createControl(std::move(controlDesc), parentWidget);
        if (!created.ok()) {
            return UiArpuiRuntimeLoadResult::failure(
                created.diagnostic().code, created.diagnostic().message);
        }
        bindings.push_back(NodeWidgetBinding{node.nodeId, created.widgetId()});
        if (!node.sourceId.empty()) {
            runtimeBindings.push_back(
                UiRuntimeNodeBinding{node.sourceId, node.nodeId, created.widgetId()});
        }
    }

    return UiArpuiRuntimeLoadResult::success(
        std::move(document), std::move(runtime), std::move(runtimeBindings));
}

UiArpuiRuntimeLoadResult loadArpuiYamlText(const arp::core::pmr::String& text,
    const UiControlFactoryRegistry* factories = nullptr,
    size_t maxWidgetCount = 0)
{
    YamlNode root;
    try {
        root = YamlNode::deserialize(text.data(), text.data() + text.size());
    } catch (const std::exception& err) {
        arp::core::pmr::String message(
            ".arpui YAML parse failed: ", &arp::core::defaultCpuMemoryResource());
        message += err.what();
        return UiArpuiRuntimeLoadResult::failure(
            UiDiagnosticCode::RuntimeLoadFailed, std::move(message));
    }
    if (!root.is_mapping()) {
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText(".arpui YAML root must be a mapping"));
    }

    arp::core::pmr::String type(&arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String contractVersion(&arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String diagnostics(&arp::core::defaultCpuMemoryResource());
    if (!requireYamlString(root, "type", type, diagnostics)
        || !requireYamlString(root, "contractVersion", contractVersion, diagnostics)) {
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed, diagnostics);
    }
    if (type != "ui.document" || contractVersion != "ui.document/v1") {
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText(
                ".arpui YAML requires type ui.document and contractVersion ui.document/v1"));
    }

    UiDocumentDesc documentDesc;
    const YamlNode* canvas = nullptr;
    if (!requireMapping(root, "canvas", canvas, diagnostics)
        || !requireYamlString(*canvas, "id", documentDesc.canvas.canvasId, diagnostics)
        || !requireYamlFloat(*canvas, "width", documentDesc.canvas.width, diagnostics)
        || !requireYamlFloat(*canvas, "height", documentDesc.canvas.height, diagnostics)
        || !requireYamlFloat(*canvas, "dpi", documentDesc.canvas.designDpi, diagnostics)) {
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed, diagnostics);
    }
    documentDesc.canvas.contractVersion = std::move(contractVersion);
    documentDesc.canvas.hasExplicitSize = true;
    documentDesc.canvas.hasExplicitDpi = true;

    if (const auto* textures = findMappingValue(root, "textures")) {
        if (!textures->is_sequence()) {
            return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
                diagnosticText(".arpui YAML textures must be a sequence"));
        }
        for (const auto& textureYaml : textures->get_value_ref<const YamlNode::sequence_type&>()) {
            UiDocumentTextureEntry texture;
            if (!parseArpuiYamlTexture(textureYaml, texture, diagnostics)) {
                return UiArpuiRuntimeLoadResult::failure(
                    UiDiagnosticCode::RuntimeLoadFailed, diagnostics);
            }
            documentDesc.textures.push_back(std::move(texture));
        }
    }

    const auto* rootNode = findMappingValue(root, "root");
    if (rootNode == nullptr || !rootNode->is_mapping()) {
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText(".arpui YAML requires root node mapping"));
    }
    std::uint64_t nextNodeId = 1u;
    arp::core::pmr::Vector<UiDocumentV1IdBinding> bindings;
    if (!parseUiDocumentV1Node(*rootNode,
            UiDocumentNodeId::invalid(),
            nextNodeId,
            bindings,
            documentDesc.nodes,
            diagnostics)) {
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed, diagnostics);
    }

    return createRuntimeFromDocument(
        UiDocument(std::move(documentDesc)), factories, maxWidgetCount);
}

bool extractObjectArrayAfterKey(const arp::core::pmr::String& text,
    const char* key,
    arp::core::pmr::Vector<arp::core::pmr::String>& out)
{
    const auto keyOffset = text.find(key);
    if (keyOffset == arp::core::pmr::String::npos) {
        return false;
    }
    const auto arrayBegin = text.find('[', keyOffset);
    if (arrayBegin == arp::core::pmr::String::npos) {
        return false;
    }
    const auto arrayEnd = text.find(']', arrayBegin + 1u);
    if (arrayEnd == arp::core::pmr::String::npos) {
        return false;
    }
    int depth = 0;
    auto objectBegin = arp::core::pmr::String::npos;
    for (auto index = arrayBegin; index <= arrayEnd; ++index) {
        if (text[index] == '{') {
            if (depth == 0) {
                objectBegin = index;
            }
            ++depth;
        } else if (text[index] == '}') {
            --depth;
            if (depth == 0 && objectBegin != arp::core::pmr::String::npos) {
                out.push_back(text.substr(objectBegin, index - objectBegin + 1u));
                objectBegin = arp::core::pmr::String::npos;
            }
        }
    }
    return !out.empty();
}

bool extractObjectAfterKey(
    const arp::core::pmr::String& text, const char* key, arp::core::pmr::String& out)
{
    const auto keyOffset = text.find(key);
    if (keyOffset == arp::core::pmr::String::npos) {
        return false;
    }
    const auto objectBegin = text.find('{', keyOffset);
    if (objectBegin == arp::core::pmr::String::npos) {
        return false;
    }
    int depth = 0;
    for (auto index = objectBegin; index < text.size(); ++index) {
        if (text[index] == '{') {
            ++depth;
        } else if (text[index] == '}') {
            --depth;
            if (depth == 0) {
                out = text.substr(objectBegin, index - objectBegin + 1u);
                return true;
            }
        }
    }
    return false;
}

UiControlType parseControlType(const arp::core::pmr::String& value)
{
    if (value == "Panel") {
        return UiControlType::Panel;
    }
    if (value == "Button") {
        return UiControlType::Button;
    }
    if (value == "Label") {
        return UiControlType::Label;
    }
    if (value == "Image") {
        return UiControlType::Image;
    }
    if (value == "Slider") {
        return UiControlType::Slider;
    }
    if (value == "Checkbox") {
        return UiControlType::Checkbox;
    }
    if (value == "TextInput") {
        return UiControlType::TextInput;
    }
    if (value == "NumericInput") {
        return UiControlType::NumericInput;
    }
    if (value == "ScrollView") {
        return UiControlType::ScrollView;
    }
    if (value == "Dropdown") {
        return UiControlType::Dropdown;
    }
    if (value == "Tabs") {
        return UiControlType::Tabs;
    }
    if (value == "TreeView") {
        return UiControlType::TreeView;
    }
    if (value == "ColorPicker") {
        return UiControlType::ColorPicker;
    }
    return UiControlType::Unknown;
}

UiLayoutParticipation parseLayoutParticipation(const arp::core::pmr::String& value)
{
    if (value == "stackItem") {
        return UiLayoutParticipation::StackItem;
    }
    return UiLayoutParticipation::Absolute;
}

UiLayoutMode parseLayoutMode(const arp::core::pmr::String& value)
{
    if (value == "verticalStack" || value == "horizontalStack") {
        return UiLayoutMode::Stack;
    }
    if (value == "center") {
        return UiLayoutMode::Center;
    }
    return UiLayoutMode::None;
}

UiStackDirection parseStackDirection(const arp::core::pmr::String& value)
{
    if (value == "horizontalStack") {
        return UiStackDirection::Horizontal;
    }
    return UiStackDirection::Vertical;
}

UiTextureAlphaMode parseAlphaMode(const arp::core::pmr::String& value)
{
    if (value == "opaque") {
        return UiTextureAlphaMode::Opaque;
    }
    if (value == "straight") {
        return UiTextureAlphaMode::Straight;
    }
    if (value == "premultiplied") {
        return UiTextureAlphaMode::Premultiplied;
    }
    return UiTextureAlphaMode::Unknown;
}

UiTextureMipIntent parseMipIntent(const arp::core::pmr::String& value)
{
    if (value == "none") {
        return UiTextureMipIntent::None;
    }
    if (value == "generate") {
        return UiTextureMipIntent::Generate;
    }
    return UiTextureMipIntent::Unknown;
}

UiTextureCompressionIntent parseCompressionIntent(const arp::core::pmr::String& value)
{
    if (value == "ui-lossless") {
        return UiTextureCompressionIntent::UiLossless;
    }
    if (value == "platform-default") {
        return UiTextureCompressionIntent::PlatformDefault;
    }
    return UiTextureCompressionIntent::Unknown;
}

UiTextureSamplerIntent parseSamplerIntent(const arp::core::pmr::String& value)
{
    if (value == "linear-clamp") {
        return UiTextureSamplerIntent::LinearClamp;
    }
    if (value == "nearest-clamp") {
        return UiTextureSamplerIntent::NearestClamp;
    }
    return UiTextureSamplerIntent::Unknown;
}

} // namespace

UiDocument::UiDocument(UiDocumentDesc desc) : desc_(std::move(desc))
{}

Document::Document(DocumentDesc desc)
    : desc_(std::move(desc)), document_(UiDocument(toLegacyDocumentDesc(desc_)))
{}

std::uint64_t UiDocument::stableHash() const
{
    core::StableHash64 hash;
    appendContentFingerprint(hash);
    return hash.value() == 0u ? 1u : hash.value();
}

void UiDocument::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    detail::UiContentFingerprint hash(hasher);
    hash.text("UiDocumentContent/2");
    hash.text(desc_.canvas.canvasId);
    hash.text(desc_.canvas.contractVersion);
    hash.scalar(desc_.canvas.width);
    hash.scalar(desc_.canvas.height);
    hash.scalar(desc_.canvas.designDpi);
    hash.number(desc_.canvas.hasExplicitSize);
    hash.number(desc_.canvas.hasExplicitDpi);
    hash.number(desc_.nodes.size());
    for (const auto& node : desc_.nodes) {
        hash.number(node.nodeId.value());
        hash.number(node.parentNodeId.value());
        hash.text(node.sourceId);
        hash.number(static_cast<std::uint64_t>(node.controlType));
        hash.scalar(node.layout.x);
        hash.scalar(node.layout.y);
        hash.scalar(node.layout.width);
        hash.scalar(node.layout.height);
        hash.number(static_cast<std::uint64_t>(node.layoutMode));
        hash.number(static_cast<std::uint64_t>(node.stackLayout.direction));
        hash.scalar(node.stackLayout.spacing);
        hash.scalar(node.stackLayout.padding.left);
        hash.scalar(node.stackLayout.padding.top);
        hash.scalar(node.stackLayout.padding.right);
        hash.scalar(node.stackLayout.padding.bottom);
        hash.number(static_cast<std::uint64_t>(node.layoutParticipation));
        hash.text(node.styleId);
        hash.text(node.fontToken);
        hash.text(node.text);
        hash.number(node.maxTextBytes);
        hash.text(node.stateId);
        hash.text(node.eventRole);
        hash.text(node.psdLayerBinding.sourceLayerId);
        hash.number(node.resourceBindings.size());
        for (const auto& binding : node.resourceBindings) {
            hash.text(binding.role);
            hash.text(binding.resourceId);
            hash.number(binding.required ? 1u : 0u);
        }
    }
    hash.number(desc_.textures.size());
    for (const auto& texture : desc_.textures) {
        hash.text(texture.resourceId);
        hash.text(texture.sourceLayerId);
        hash.text(texture.atlasId);
        hash.scalar(texture.sourceRect.x);
        hash.scalar(texture.sourceRect.y);
        hash.scalar(texture.sourceRect.width);
        hash.scalar(texture.sourceRect.height);
        hash.scalar(texture.pivot.x);
        hash.scalar(texture.pivot.y);
        hash.scalar(texture.nineSlice.left);
        hash.scalar(texture.nineSlice.top);
        hash.scalar(texture.nineSlice.right);
        hash.scalar(texture.nineSlice.bottom);
        hash.number(static_cast<std::uint64_t>(texture.alphaMode));
        hash.number(texture.srgb ? 1u : 0u);
        hash.number(texture.hasExplicitSrgb ? 1u : 0u);
        hash.number(static_cast<std::uint64_t>(texture.mipIntent));
        hash.number(static_cast<std::uint64_t>(texture.compressionIntent));
        hash.number(static_cast<std::uint64_t>(texture.samplerIntent));
    }
}

UiDocumentValidationResult UiDocumentValidationResult::success()
{
    return UiDocumentValidationResult(UiDocumentDiagnostic{});
}

UiDocumentValidationResult UiDocumentValidationResult::failure(
    UiDocumentDiagnosticCode code, arp::core::pmr::String message)
{
    UiDocumentDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.message = std::move(message);
    return UiDocumentValidationResult(std::move(diagnostic));
}

UiDocumentValidationResult::UiDocumentValidationResult(UiDocumentDiagnostic diagnostic)
    : diagnostic_(std::move(diagnostic))
{}

UiDocumentValidationResult UiDocumentValidator::validate(const UiDocument& document) const
{
    const auto& canvas = document.canvas();
    if (canvas.canvasId.empty() || canvas.contractVersion.empty() || !canvas.hasExplicitSize
        || canvas.width <= 0.0f || canvas.height <= 0.0f || !canvas.hasExplicitDpi
        || canvas.designDpi <= 0.0f) {
        return UiDocumentValidationResult::failure(UiDocumentDiagnosticCode::MissingCanvasMetadata,
            diagnosticText("UI document requires explicit canvas metadata"));
    }

    for (std::size_t outer = 0; outer < document.nodes().size(); ++outer) {
        const auto& node = document.nodes()[outer];
        if (!node.nodeId.valid()) {
            return UiDocumentValidationResult::failure(UiDocumentDiagnosticCode::InvalidNodeId,
                diagnosticText("UI document node id must be non-zero"));
        }
        for (std::size_t inner = outer + 1u; inner < document.nodes().size(); ++inner) {
            if (node.nodeId == document.nodes()[inner].nodeId) {
                return UiDocumentValidationResult::failure(
                    UiDocumentDiagnosticCode::DuplicateNodeId,
                    diagnosticText("UI document node id must be unique"));
            }
        }
        if (node.parentNodeId.valid() && !hasParent(document, node.parentNodeId)) {
            return UiDocumentValidationResult::failure(UiDocumentDiagnosticCode::InvalidParentId,
                diagnosticText("UI document parent id must reference an existing node"));
        }
        if (!isSupportedControlType(node.controlType)) {
            return UiDocumentValidationResult::failure(
                UiDocumentDiagnosticCode::UnsupportedControlType,
                diagnosticText("UI document control type is unsupported"));
        }
        if (node.controlType == UiControlType::TextInput) {
            const auto checked = TextInputContract::validate(node.text, node.maxTextBytes);
            if (!checked.ok())
                return UiDocumentValidationResult::failure(
                    UiDocumentDiagnosticCode::InvalidTextInputContract,
                    checked.diagnostic().message);
        } else if (node.maxTextBytes != TextInputContract::defaultMaxTextBytes) {
            return UiDocumentValidationResult::failure(
                UiDocumentDiagnosticCode::InvalidTextInputContract,
                diagnosticText("UI-TEXT-BUDGET-TYPE: only TextInput accepts maxTextBytes"));
        }
        if (node.controlType == UiControlType::Image) {
            bool hasRequiredResource = false;
            for (const auto& binding : node.resourceBindings) {
                hasRequiredResource =
                    hasRequiredResource || (binding.required && !binding.resourceId.empty());
            }
            if (!hasRequiredResource) {
                return UiDocumentValidationResult::failure(
                    UiDocumentDiagnosticCode::MissingRequiredResourceBinding,
                    diagnosticText("UI image node requires an explicit resource binding"));
            }
        }
        if (node.psdLayerBinding.sourceLayerId.empty()) {
            return UiDocumentValidationResult::failure(
                UiDocumentDiagnosticCode::MissingPsdLayerBinding,
                diagnosticText("UI document node requires an explicit PSD layer binding"));
        }
    }
    for (const auto& texture : document.textures()) {
        if (!validTextureMetadata(texture)) {
            return UiDocumentValidationResult::failure(
                UiDocumentDiagnosticCode::InvalidTextureMetadata,
                diagnosticText(
                    "UI texture entry requires explicit source layer, rect, pivot, nine-slice, alpha, sRGB, mip, compression, sampler, atlas, and resource metadata"));
        }
    }
    return UiDocumentValidationResult::success();
}

namespace {

bool startsWith(const arp::core::pmr::String& value, const char* prefix)
{
    const auto prefixLength = std::char_traits<char>::length(prefix);
    return value.size() >= prefixLength && value.compare(0u, prefixLength, prefix) == 0;
}

bool hasCommandBinding(const UiGameScreenDesc& screen, const arp::core::pmr::String& commandRole)
{
    return std::any_of(screen.commandBindings.begin(),
        screen.commandBindings.end(),
        [&](const UiGameCommandBinding& binding) {
            return binding.commandRole == commandRole && !binding.gameBinding.empty();
        });
}

} // namespace

void UiGameScreenDocument::addScreen(UiGameScreenDesc screen)
{
    screens_.push_back(std::move(screen));
}

std::uint64_t UiGameScreenDocument::stableHash() const
{
    arp::core::StableHash64 hash;
    for (const auto& screen : screens_) {
        hash.appendString(screen.screenId);
        hash.appendString(screen.viewId);
        hash.appendString(screen.stateId);
        hash.appendU64(static_cast<std::uint64_t>(screen.safeAreaPolicy));
        hash.appendU64(static_cast<std::uint64_t>(screen.modalLayer));
        for (const auto& action : screen.inputActions) {
            hash.appendString(action.actionId);
            hash.appendString(action.commandRole);
        }
        for (const auto& command : screen.commandBindings) {
            hash.appendString(command.commandRole);
            hash.appendString(command.gameBinding);
        }
        for (const auto& binding : screen.dataBindings) {
            hash.appendString(binding.bindingId);
            hash.appendString(binding.gameBinding);
            hash.appendString(binding.visibilityCondition);
        }
        for (const auto& edge : screen.navigation) {
            hash.appendString(edge.fromViewId);
            hash.appendString(edge.toViewId);
            hash.appendString(edge.inputAction);
        }
        for (const auto& scope : screen.focusScopes) {
            hash.appendString(scope.scopeId);
            hash.appendString(scope.defaultViewId);
        }
        for (const auto& transition : screen.transitions) {
            hash.appendString(transition.fromStateId);
            hash.appendString(transition.toStateId);
            hash.appendString(transition.commandRole);
        }
    }
    return hash.value();
}

UiGameScreenValidationResult::UiGameScreenValidationResult(UiGameScreenDiagnostic diagnostic)
    : diagnostic_(std::move(diagnostic))
{}

UiGameScreenValidationResult UiGameScreenValidationResult::success()
{
    return UiGameScreenValidationResult(UiGameScreenDiagnostic{});
}

UiGameScreenValidationResult UiGameScreenValidationResult::failure(
    UiGameScreenDiagnosticCode code, arp::core::pmr::String message)
{
    return UiGameScreenValidationResult(UiGameScreenDiagnostic{code, std::move(message)});
}

UiGameScreenValidationResult UiGameScreenValidator::validate(
    const UiGameScreenDocument& document) const
{
    if (document.screens().empty()) {
        return UiGameScreenValidationResult::failure(
            UiGameScreenDiagnosticCode::UnknownScreenOrState,
            diagnosticText("UI game screen document requires at least one screen"));
    }

    for (const auto& screen : document.screens()) {
        if (screen.screenId.empty() || screen.viewId.empty() || screen.stateId.empty()
            || !startsWith(screen.stateId, "state.")) {
            return UiGameScreenValidationResult::failure(
                UiGameScreenDiagnosticCode::UnknownScreenOrState,
                diagnosticText("UI game screen requires explicit screen, view, and state ids"));
        }
        if (screen.allowLayerNameSemanticInference) {
            return UiGameScreenValidationResult::failure(
                UiGameScreenDiagnosticCode::LayerNameSemanticInference,
                diagnosticText("UI game screen must not infer semantics from layer names"));
        }
        if (screen.safeAreaPolicy == UiGameSafeAreaPolicy::Invalid) {
            return UiGameScreenValidationResult::failure(
                UiGameScreenDiagnosticCode::InvalidSafeAreaPolicy,
                diagnosticText("UI game screen requires explicit safe-area policy"));
        }
        if (screen.modalLayer == UiGameModalLayer::Invalid) {
            return UiGameScreenValidationResult::failure(
                UiGameScreenDiagnosticCode::ModalLayerConflict,
                diagnosticText("UI game screen requires explicit modal layer"));
        }
        if (screen.commandBindings.empty()) {
            return UiGameScreenValidationResult::failure(
                UiGameScreenDiagnosticCode::MissingCommandBinding,
                diagnosticText("UI game screen requires command bindings"));
        }
        for (const auto& action : screen.inputActions) {
            if (!startsWith(action.actionId, "input.") || action.commandRole.empty()) {
                return UiGameScreenValidationResult::failure(
                    UiGameScreenDiagnosticCode::InvalidInputAction,
                    diagnosticText("UI game input action requires input.* id and command role"));
            }
            if (!hasCommandBinding(screen, action.commandRole)) {
                return UiGameScreenValidationResult::failure(
                    UiGameScreenDiagnosticCode::MissingCommandBinding,
                    diagnosticText("UI game input action references missing command binding"));
            }
        }
        for (const auto& command : screen.commandBindings) {
            if (command.commandRole.empty() || command.gameBinding.empty()) {
                return UiGameScreenValidationResult::failure(
                    UiGameScreenDiagnosticCode::MissingGameBinding,
                    diagnosticText("UI game command binding requires game binding"));
            }
        }
        for (const auto& binding : screen.dataBindings) {
            if (binding.bindingId.empty() || binding.gameBinding.empty()) {
                return UiGameScreenValidationResult::failure(
                    UiGameScreenDiagnosticCode::MissingGameBinding,
                    diagnosticText("UI game data binding requires explicit game binding"));
            }
        }
        for (const auto& edge : screen.navigation) {
            if (edge.fromViewId.empty() || edge.toViewId.empty() || edge.inputAction.empty()) {
                return UiGameScreenValidationResult::failure(
                    UiGameScreenDiagnosticCode::UnknownScreenOrState,
                    diagnosticText("UI game navigation requires explicit views and input action"));
            }
            if (edge.fromViewId == edge.toViewId) {
                return UiGameScreenValidationResult::failure(
                    UiGameScreenDiagnosticCode::CyclicNavigation,
                    diagnosticText("UI game navigation must not contain self-cycle"));
            }
        }
        for (const auto& scope : screen.focusScopes) {
            if (scope.scopeId.empty() || scope.defaultViewId.empty()) {
                return UiGameScreenValidationResult::failure(
                    UiGameScreenDiagnosticCode::UnknownScreenOrState,
                    diagnosticText("UI game focus scope requires explicit default view"));
            }
        }
        for (const auto& transition : screen.transitions) {
            if (transition.fromStateId.empty() || transition.toStateId.empty()
                || transition.commandRole.empty() || !startsWith(transition.fromStateId, "state.")
                || !startsWith(transition.toStateId, "state.")) {
                return UiGameScreenValidationResult::failure(
                    UiGameScreenDiagnosticCode::UnknownScreenOrState,
                    diagnosticText("UI game transition requires explicit states and command"));
            }
            if (!hasCommandBinding(screen, transition.commandRole)) {
                return UiGameScreenValidationResult::failure(
                    UiGameScreenDiagnosticCode::MissingCommandBinding,
                    diagnosticText("UI game transition references missing command binding"));
            }
        }
    }
    return UiGameScreenValidationResult::success();
}

UiResult UiControlFactoryRegistry::registerFactory(
    arp::core::pmr::String id, arp::core::UniquePtr<IUiControlFactory> factory)
{
    if (id.empty() || !factory || find(id))
        return UiResult::failure(UiDiagnosticCode::InvalidFactoryBinding,
            diagnosticText(
                "UI-FACTORY-BINDING-INVALID: sourceId/factory required and duplicate bindings forbidden."));
    if (!bindings_.append(Binding{std::move(id), std::move(factory)}))
        return UiResult::failure(
            UiDiagnosticCode::EventCapacityExceeded, diagnosticText("UI-FACTORY-BUDGET-EXCEEDED"));
    return UiResult::success();
}

const IUiControlFactory* UiControlFactoryRegistry::find(const arp::core::pmr::String& id) const
{
    const auto* binding = bindings_.findIf([&](const Binding& value) {
        return value.sourceId == id;
    });
    return binding ? binding->factory.get() : nullptr;
}

UiArpuiRuntimeLoadResult UiArpuiRuntimeLoadResult::success(UiDocument document,
    arp::core::UniquePtr<UiControlRuntime> runtime,
    arp::core::pmr::Vector<UiRuntimeNodeBinding> bindings)
{
    UiArpuiRuntimeLoadResult result;
    result.document_ = std::move(document);
    result.runtime_ = std::move(runtime);
    result.bindings_ = std::move(bindings);
    return result;
}

UiArpuiRuntimeLoadResult UiArpuiRuntimeLoadResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    UiArpuiRuntimeLoadResult result;
    result.diagnostic_.code = code;
    result.diagnostic_.message = std::move(message);
    return result;
}

WidgetId UiArpuiRuntimeLoadResult::widgetIdForSourceNodeId(
    const arp::core::pmr::String& sourceId) const
{
    const auto found =
        std::find_if(bindings_.begin(), bindings_.end(), [&](const UiRuntimeNodeBinding& binding) {
            return binding.sourceId == sourceId;
        });
    return found == bindings_.end() ? WidgetId::invalid() : found->widgetId;
}

UiArpuiRuntimeLoadResult UiArpuiRuntimeLoader::load(const UiArpuiRuntimeLoadDesc& desc) const
{
    const auto read = readDocumentText(desc.path,
        desc.sourceYaml,
        desc.sourceYamlProvided,
        desc.maxDocumentBytes,
        desc.sourceContext,
        desc.origin);
    if (!read.success) {
        return UiArpuiRuntimeLoadResult::failure(
            UiDiagnosticCode::RuntimeLoadFailed, read.diagnostics);
    }
    if (looksLikeJsonDocument(read.text)) {
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText(
                ".arpui runtime loader accepts YAML only; JSON .arpui assets are not supported."));
    }
    return loadArpuiYamlText(read.text, desc.factories, desc.maxWidgetCount);
}

UiArpuiIrBuildResult UiArpuiIrBuildResult::success(arp::core::pmr::String documentYaml)
{
    UiArpuiIrBuildResult result;
    result.documentYaml_ = std::move(documentYaml);
    return result;
}

UiArpuiIrBuildResult UiArpuiIrBuildResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    UiArpuiIrBuildResult result;
    result.diagnostic_ = UiDiagnostic{code, std::move(message)};
    return result;
}

UiArpuiIrBuildResult UiArpuiIrBuilder::build(const UiArpuiIrBuildDesc& desc) const
{
    const auto read = readDocumentText(desc.path,
        desc.sourceYaml,
        desc.sourceYamlProvided,
        desc.maxDocumentBytes,
        desc.sourceContext,
        desc.origin);
    if (!read.success) {
        return UiArpuiIrBuildResult::failure(UiDiagnosticCode::RuntimeLoadFailed, read.diagnostics);
    }

    arp::core::pmr::String diagnostics(&arp::core::defaultCpuMemoryResource());
    if (looksLikeJsonDocument(read.text)) {
        return UiArpuiIrBuildResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText(
                "arpui IR builder accepts YAML only; JSON authoring IR is not supported."));
    }

    YamlNode root;
    try {
        root = YamlNode::deserialize(read.text.data(), read.text.data() + read.text.size());
    } catch (const std::exception& err) {
        diagnostics = diagnosticText("arpui IR YAML parse failed: ");
        diagnostics += err.what();
        return UiArpuiIrBuildResult::failure(UiDiagnosticCode::RuntimeLoadFailed, diagnostics);
    }
    if (!root.is_mapping()) {
        return UiArpuiIrBuildResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText("arpui IR YAML root must be a mapping"));
    }
    arp::core::pmr::String type(&arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String contractVersion(&arp::core::defaultCpuMemoryResource());
    if (!requireYamlString(root, "type", type, diagnostics)
        || !requireYamlString(root, "contractVersion", contractVersion, diagnostics)) {
        return UiArpuiIrBuildResult::failure(UiDiagnosticCode::RuntimeLoadFailed, diagnostics);
    }
    if (type != "ArpUiAuthoringIR" || contractVersion != "arpui.ir.v1") {
        return UiArpuiIrBuildResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText(
                "arpui IR requires type ArpUiAuthoringIR and contractVersion arpui.ir.v1"));
    }

    UiDocumentDesc documentDesc;
    const YamlNode* canvas = nullptr;
    if (!requireMapping(root, "canvas", canvas, diagnostics)
        || !requireYamlString(*canvas, "id", documentDesc.canvas.canvasId, diagnostics)
        || !requireYamlFloat(*canvas, "width", documentDesc.canvas.width, diagnostics)
        || !requireYamlFloat(*canvas, "height", documentDesc.canvas.height, diagnostics)
        || !requireYamlFloat(*canvas, "dpi", documentDesc.canvas.designDpi, diagnostics)) {
        return UiArpuiIrBuildResult::failure(UiDiagnosticCode::RuntimeLoadFailed, diagnostics);
    }
    documentDesc.canvas.contractVersion = diagnosticText("ui.document/v1");
    documentDesc.canvas.hasExplicitSize = true;
    documentDesc.canvas.hasExplicitDpi = true;

    if (const auto* textures = findMappingValue(root, "textures")) {
        if (!textures->is_sequence()) {
            return UiArpuiIrBuildResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
                diagnosticText("arpui IR textures must be a sequence"));
        }
        for (const auto& textureYaml : textures->get_value_ref<const YamlNode::sequence_type&>()) {
            UiDocumentTextureEntry texture;
            if (!parseArpuiYamlTexture(textureYaml, texture, diagnostics)) {
                return UiArpuiIrBuildResult::failure(
                    UiDiagnosticCode::RuntimeLoadFailed, diagnostics);
            }
            documentDesc.textures.push_back(std::move(texture));
        }
    }

    const auto* nodes = findMappingValue(root, "nodes");
    if (nodes == nullptr || !nodes->is_sequence()) {
        return UiArpuiIrBuildResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText("arpui IR requires nodes sequence"));
    }
    for (const auto& nodeYaml : nodes->get_value_ref<const YamlNode::sequence_type&>()) {
        UiDocumentNodeDesc node;
        if (!parseArpuiYamlNode(nodeYaml, node, diagnostics)) {
            return UiArpuiIrBuildResult::failure(UiDiagnosticCode::RuntimeLoadFailed, diagnostics);
        }
        documentDesc.nodes.push_back(std::move(node));
    }

    auto documentYaml = buildUiDocumentV1YamlFromDesc(documentDesc);
    if (documentYaml.size() > desc.maxDocumentBytes) {
        return UiArpuiIrBuildResult::failure(
            UiDiagnosticCode::RuntimeLoadFailed, diagnosticText("UI-DOCUMENT-GENERATED-BUDGET"));
    }

    const auto loaded = loadArpuiYamlText(documentYaml);
    if (!loaded.ok()) {
        return UiArpuiIrBuildResult::failure(
            UiDiagnosticCode::RuntimeLoadFailed, loaded.diagnostic().message);
    }
    return UiArpuiIrBuildResult::success(std::move(documentYaml));
}

UiArpuiRuntimeLoadResult UiArpuiIrRuntimeLoader::load(const UiArpuiIrBuildDesc& desc) const
{
    const auto built = UiArpuiIrBuilder().build(desc);
    if (!built.ok()) {
        return UiArpuiRuntimeLoadResult::failure(
            built.diagnostic().code, built.diagnostic().message);
    }
    return loadArpuiYamlText(built.documentYaml());
}

namespace {

bool validArpuiPackageStatus(const arp::core::pmr::String& status)
{
    return status == "Contract Complete" || status == "Prototype Complete";
}

bool dependencyDeclared(
    const arp::core::pmr::Vector<arp::resource::ResourceDependency>& dependencies,
    const arp::core::pmr::String& resourceId)
{
    return std::any_of(dependencies.begin(),
        dependencies.end(),
        [&](const arp::resource::ResourceDependency& dependency) {
            return dependency.id == resourceId;
        });
}

bool validateArpuiPackageDocument(const arp::core::pmr::String& documentYaml,
    const arp::core::pmr::Vector<arp::resource::ResourceDependency>& dependencies,
    arp::core::pmr::String& diagnostics)
{
    const auto loaded = loadArpuiYamlText(documentYaml);
    if (!loaded.ok()) {
        diagnostics = loaded.diagnostic().message;
        return false;
    }
    for (const auto& texture : loaded.document().textures()) {
        if (!arp::resource::isResourceUuid(texture.resourceId)) {
            diagnostics = diagnosticText(".arpui texture resource link requires UUID resourceId");
            return false;
        }
        if (!dependencyDeclared(dependencies, texture.resourceId)) {
            diagnostics =
                diagnosticText("unresolved .arpui texture dependency: ", texture.resourceId);
            return false;
        }
    }
    return true;
}

arp::core::pmr::String arpuiPayloadYaml(const arp::core::pmr::String& documentYaml)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "type: ";
    yaml += kArpuiResourceType;
    yaml += "\nfields:\n";
    yaml += "  contractVersion: ui.document/v1\n";
    yaml += "  documentYaml: |\n";
    appendIndentedBlock(yaml, documentYaml, "    ");
    return yaml;
}

arp::core::pmr::String dependencyYaml(
    const arp::core::pmr::Vector<arp::resource::ResourceDependency>& dependencies)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    if (dependencies.empty()) {
        yaml += "dependencies: []\n";
        return yaml;
    }
    yaml += "dependencies:\n";
    for (const auto& dependency : dependencies) {
        yaml += "  - id: ";
        yaml += dependency.id;
        yaml += "\n    usage: ";
        yaml += dependency.usage;
        yaml += "\n";
    }
    return yaml;
}

arp::core::pmr::String arpuiEnvelopeYaml(const UiArpuiResourcePackageDesc& desc,
    const arp::core::pmr::String& payloadYaml,
    std::uint32_t contentHash)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "format: ArpRuntimeResource\n";
    yaml += "version: 1\n";
    yaml += "resourceId: ";
    yaml += desc.resourceId;
    yaml += "\nresourceType: ";
    yaml += kArpuiResourceType;
    yaml += "\n";
    if (!desc.alias.empty()) {
        yaml += "alias: ";
        yaml += desc.alias;
        yaml += "\n";
    }
    yaml += "status: ";
    yaml += desc.status;
    yaml += "\ncontentHash: ";
    appendUint(yaml, contentHash);
    yaml += "\n";
    yaml += dependencyYaml(desc.dependencies);
    yaml += "payload:\n";
    appendIndentedBlock(yaml, payloadYaml, "  ");
    return yaml;
}

bool extractArpuiDocumentYamlFromEnvelope(const arp::core::pmr::String& envelopeYaml,
    arp::core::pmr::String& documentYaml,
    arp::core::pmr::String& diagnostics)
{
    YamlNode root;
    try {
        root =
            YamlNode::deserialize(envelopeYaml.data(), envelopeYaml.data() + envelopeYaml.size());
    } catch (const std::exception& err) {
        diagnostics = diagnosticText(".arpui envelope YAML parse failed: ");
        diagnostics += err.what();
        return false;
    }
    const YamlNode* payload = findMappingValue(root, "payload");
    if (payload == nullptr || !payload->is_mapping()) {
        diagnostics = diagnosticText(".arpui envelope missing payload");
        return false;
    }
    const YamlNode* type = findMappingValue(*payload, "type");
    if (type == nullptr || !type->is_string() || yamlString(*type) != kArpuiResourceType) {
        diagnostics = diagnosticText(".arpui envelope payload type mismatch");
        return false;
    }
    const YamlNode* fields = findMappingValue(*payload, "fields");
    if (fields == nullptr || !fields->is_mapping()) {
        diagnostics = diagnosticText(".arpui envelope payload missing fields");
        return false;
    }
    const YamlNode* contractVersion = findMappingValue(*fields, "contractVersion");
    if (contractVersion == nullptr || !contractVersion->is_string()
        || yamlString(*contractVersion) != "ui.document/v1") {
        diagnostics = diagnosticText("unknown .arpui contract version");
        return false;
    }
    const YamlNode* yaml = findMappingValue(*fields, "documentYaml");
    if (yaml == nullptr || !yaml->is_string()) {
        diagnostics = diagnosticText(".arpui envelope payload missing documentYaml");
        return false;
    }
    documentYaml = yamlString(*yaml);
    return true;
}

} // namespace

UiArpuiResourcePackageBuildResult UiArpuiResourcePackageBuilder::build(
    const UiArpuiResourcePackageDesc& desc) const
{
    UiArpuiResourcePackageBuildResult result;
    if (!arp::resource::isResourceUuid(desc.resourceId)) {
        result.diagnostics = diagnosticText(".arpui package resource requires UUID resourceId");
        return result;
    }
    if (!validArpuiPackageStatus(desc.status)) {
        result.diagnostics = diagnosticText("invalid .arpui package status");
        return result;
    }
    if (desc.documentYaml.empty()) {
        result.diagnostics = diagnosticText(".arpui package requires documentYaml payload");
        return result;
    }
    if (!validateArpuiPackageDocument(desc.documentYaml, desc.dependencies, result.diagnostics)) {
        return result;
    }

    const auto payloadYaml = arpuiPayloadYaml(desc.documentYaml);
    const auto hash = arp::resource::computeResourcePayloadHash(payloadYaml);
    if (!hash.success) {
        result.diagnostics = hash.diagnostics;
        return result;
    }

    result.entry.resourceId = desc.resourceId;
    result.entry.resourceType =
        arp::core::pmr::String(kArpuiResourceType, &arp::core::defaultCpuMemoryResource());
    result.entry.alias = desc.alias;
    result.entry.status = desc.status;
    result.entry.envelopePath =
        arp::core::pmr::String(kArpuiEnvelopePath, &arp::core::defaultCpuMemoryResource());
    result.entry.contentHash = hash.hash;
    result.entry.dependencies = desc.dependencies;
    result.entry.envelopeYaml = arpuiEnvelopeYaml(desc, payloadYaml, hash.hash);
    result.success = true;
    return result;
}

UiArpuiRuntimeLoadResult UiArpuiPackageRuntimeLoader::load(
    const UiArpuiPackageRuntimeLoadDesc& desc) const
{
    if (!desc.maxMetadataBytes || desc.maxMetadataBytes == SIZE_MAX || !desc.maxResources
        || desc.maxResources == SIZE_MAX || !desc.maxDocumentBytes
        || desc.maxDocumentBytes == SIZE_MAX) {
        return UiArpuiRuntimeLoadResult::failure(
            UiDiagnosticCode::RuntimeLoadFailed, diagnosticText("UI-DOCUMENT-SOURCE-BUDGET"));
    }
    const auto read = desc.sources
        ? arp::resource::InternalAssetPackageReader().readSources(
              desc.packageDirectory, *desc.sources, desc.maxMetadataBytes, desc.maxResources)
        : arp::resource::InternalAssetPackageReader().read(
              desc.packageDirectory, desc.maxMetadataBytes, desc.maxResources);
    if (!read.success) {
        return UiArpuiRuntimeLoadResult::failure(
            UiDiagnosticCode::RuntimeLoadFailed, read.diagnostics);
    }
    const arp::resource::InternalAssetPackageEntry* entry = nullptr;
    for (const auto& candidate : read.package.entries) {
        if (candidate.resourceId == desc.resourceId) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr) {
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText("missing .arpui resource: ", desc.resourceId));
    }
    if (entry->resourceType != kArpuiResourceType) {
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText(".arpui package resource type mismatch"));
    }
    if (!validArpuiPackageStatus(entry->status)) {
        return UiArpuiRuntimeLoadResult::failure(
            UiDiagnosticCode::RuntimeLoadFailed, diagnosticText("invalid .arpui package status"));
    }

    arp::core::pmr::String documentYaml(&arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String diagnostics(&arp::core::defaultCpuMemoryResource());
    if (!extractArpuiDocumentYamlFromEnvelope(entry->envelopeYaml, documentYaml, diagnostics)) {
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed, diagnostics);
    }
    auto decoded = readDocumentText({}, documentYaml, true, desc.maxDocumentBytes, {}, {});
    if (!decoded.success) {
        return UiArpuiRuntimeLoadResult::failure(
            UiDiagnosticCode::RuntimeLoadFailed, decoded.diagnostics);
    }
    documentYaml = std::move(decoded.text);
    if (!validateArpuiPackageDocument(documentYaml, entry->dependencies, diagnostics)) {
        return UiArpuiRuntimeLoadResult::failure(UiDiagnosticCode::RuntimeLoadFailed, diagnostics);
    }
    return loadArpuiYamlText(documentYaml, desc.factories, desc.maxWidgetCount);
}

DocumentComponentMountResult::DocumentComponentMountResult(UiDiagnostic diagnostic)
    : diagnostic_(std::move(diagnostic))
{}

DocumentComponentMountResult DocumentComponentMountResult::success()
{
    return DocumentComponentMountResult(UiDiagnostic{});
}

DocumentComponentMountResult DocumentComponentMountResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    return DocumentComponentMountResult(UiDiagnostic{code, std::move(message)});
}

DocumentComponentMountResult DocumentComponentMounter::mount(DocumentComponent& component) const
{
    const auto& desc = component.desc();
    if (desc.documentSource.empty()) {
        return DocumentComponentMountResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText("DocumentComponent requires an explicit .arpui document source."));
    }
    if (desc.inputRoute.empty() || desc.renderRoute.empty()) {
        return DocumentComponentMountResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText("DocumentComponent requires explicit input and render routes."));
    }

    UiArpuiRuntimeLoadDesc loadDesc;
    loadDesc.path = std::filesystem::u8path(desc.documentSource.begin(), desc.documentSource.end());
    auto loaded = UiArpuiRuntimeLoader().load(loadDesc);
    return mount(component, loaded);
}

DocumentComponentMountResult DocumentComponentMounter::mount(
    DocumentComponent& component, const UiArpuiRuntimeLoadResult& loaded) const
{
    const auto& desc = component.desc();
    if (desc.documentSource.empty() || desc.inputRoute.empty() || desc.renderRoute.empty())
        return DocumentComponentMountResult::failure(UiDiagnosticCode::RuntimeLoadFailed,
            diagnosticText("UI-DOCUMENT-SOURCE-AND-ROUTES-REQUIRED"));
    if (!loaded.ok()) {
        return DocumentComponentMountResult::failure(loaded.diagnostic().code,
            arp::core::pmr::String(
                loaded.diagnostic().message.c_str(), &arp::core::defaultCpuMemoryResource()));
    }
    if (!loaded.hasRuntime())
        return DocumentComponentMountResult::failure(
            UiDiagnosticCode::RuntimeLoadFailed, diagnosticText("UI-DOCUMENT-RUNTIME-REQUIRED"));

    const auto layout = loaded.runtime().canvas().resolveLayout();
    if (!layout.ok()) {
        return DocumentComponentMountResult::failure(layout.diagnostic().code,
            arp::core::pmr::String(
                layout.diagnostic().message.c_str(), &arp::core::defaultCpuMemoryResource()));
    }
    const auto extracted = RenderExtractor().extract(loaded.runtime().canvas(), layout);
    if (!extracted.ok()) {
        return DocumentComponentMountResult::failure(extracted.diagnostic().code,
            arp::core::pmr::String(
                extracted.diagnostic().message.c_str(), &arp::core::defaultCpuMemoryResource()));
    }
    auto planned = DrawPlanner().plan(extracted.queue());
    if (!planned.ok()) {
        return DocumentComponentMountResult::failure(planned.diagnostic().code,
            arp::core::pmr::String(
                planned.diagnostic().message.c_str(), &arp::core::defaultCpuMemoryResource()));
    }

    auto drawPlan = planned.plan();
    for (auto& record : drawPlan.mutableRecords()) {
        if (record.kind != DrawRecordKind::Image || record.textureResourceId.empty()) {
            continue;
        }
        const auto found = std::find_if(loaded.document().textures().begin(),
            loaded.document().textures().end(),
            [&](const UiDocumentTextureEntry& texture) {
                return texture.resourceId == record.textureResourceId;
            });
        if (found == loaded.document().textures().end()) {
            continue;
        }
        record.textureSourceRect = found->sourceRect;
        record.hasTextureSourceRect = true;
    }

    const auto root = loaded.runtime().rootId();
    component.markMountedForRuntime(
        root, root, static_cast<uint32_t>(loaded.runtime().controlCount()), std::move(drawPlan));
    return DocumentComponentMountResult::success();
}

bool isSupportedControlType(UiControlType controlType)
{
    return controlType == UiControlType::Panel || controlType == UiControlType::Label
        || controlType == UiControlType::Button || controlType == UiControlType::Slider
        || controlType == UiControlType::Checkbox || controlType == UiControlType::TextInput
        || controlType == UiControlType::NumericInput || controlType == UiControlType::ScrollView
        || controlType == UiControlType::Dropdown || controlType == UiControlType::Tabs
        || controlType == UiControlType::TreeView || controlType == UiControlType::ColorPicker
        || controlType == UiControlType::Image;
}

const char* controlTypeName(UiControlType controlType)
{
    switch (controlType) {
    case UiControlType::Panel:
        return "Panel";
    case UiControlType::Label:
        return "Label";
    case UiControlType::Button:
        return "Button";
    case UiControlType::Slider:
        return "Slider";
    case UiControlType::Checkbox:
        return "Checkbox";
    case UiControlType::TextInput:
        return "TextInput";
    case UiControlType::NumericInput:
        return "NumericInput";
    case UiControlType::ScrollView:
        return "ScrollView";
    case UiControlType::Dropdown:
        return "Dropdown";
    case UiControlType::Tabs:
        return "Tabs";
    case UiControlType::TreeView:
        return "TreeView";
    case UiControlType::ColorPicker:
        return "ColorPicker";
    case UiControlType::Image:
        return "Image";
    case UiControlType::Unknown:
    default:
        return "Unknown";
    }
}

void UiTheme::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    detail::UiContentFingerprint hash(hasher);
    hash.text("UiThemeContent/1");
    hash.text(themeId_);
    hash.number(tokens_.size());
    for (const auto& token : tokens_) {
        hash.text(token.id);
        hash.number(static_cast<std::uint64_t>(token.value.kind));
        hash.color(token.value.color);
        hash.text(token.value.text);
    }
}

UiThemeTokenValue UiThemeTokenValue::fromColor(float r, float g, float b, float a)
{
    UiThemeTokenValue value;
    value.kind = UiThemeTokenValueKind::Color;
    value.color = {r, g, b, a};
    return value;
}

UiThemeTokenValue UiThemeTokenValue::fromText(arp::core::pmr::String textValue)
{
    UiThemeTokenValue value;
    value.kind = UiThemeTokenValueKind::Text;
    value.text = std::move(textValue);
    return value;
}

UiTheme::UiTheme(arp::core::pmr::String themeId) : themeId_(std::move(themeId))
{}

void UiTheme::addToken(UiThemeToken token)
{
    tokens_.push_back(std::move(token));
}

const UiThemeToken* UiTheme::findToken(const arp::core::pmr::String& tokenId) const
{
    const auto found = std::find_if(tokens_.begin(), tokens_.end(), [&](const UiThemeToken& token) {
        return token.id == tokenId;
    });
    return found == tokens_.end() ? nullptr : &*found;
}

UiAnchors UiAnchors::fill()
{
    return UiAnchors{UiAnchorMode::Fill};
}

UiAnchors UiAnchors::stretch()
{
    return UiAnchors{UiAnchorMode::Stretch};
}

UiAnchors UiAnchors::invalidForTest()
{
    return UiAnchors{UiAnchorMode::Invalid};
}

UiBoxEdges UiBoxEdges::all(float value)
{
    return UiBoxEdges{value, value, value, value};
}

UiBoxEdges UiBoxEdges::axis(float horizontal, float vertical)
{
    return UiBoxEdges{horizontal, vertical, horizontal, vertical};
}

UiSizeConstraints UiSizeConstraints::minMax(
    float minWidth, float minHeight, float maxWidth, float maxHeight)
{
    return UiSizeConstraints{minWidth, minHeight, maxWidth, maxHeight};
}

bool UiSizeConstraints::valid() const
{
    return std::isfinite(minWidth) && std::isfinite(minHeight) && std::isfinite(maxWidth)
        && std::isfinite(maxHeight) && minWidth >= 0.0f && minHeight >= 0.0f && maxWidth >= minWidth
        && maxHeight >= minHeight;
}

UiStackLayout UiStackLayout::vertical(float spacing)
{
    UiStackLayout layout;
    layout.direction = UiStackDirection::Vertical;
    layout.spacing = spacing;
    return layout;
}

UiStackLayout UiStackLayout::horizontal(float spacing)
{
    UiStackLayout layout;
    layout.direction = UiStackDirection::Horizontal;
    layout.spacing = spacing;
    return layout;
}

UiGridLayout UiGridLayout::fixed(
    std::uint32_t columns, std::uint32_t rows, float columnGap, float rowGap)
{
    return UiGridLayout{columns, rows, columnGap, rowGap};
}

bool UiGridLayout::valid() const
{
    return columns > 0u && rows > 0u && std::isfinite(columnGap) && std::isfinite(rowGap)
        && columnGap >= 0.0f && rowGap >= 0.0f;
}

UiClipping UiClipping::inherit()
{
    return UiClipping{UiClippingMode::Inherit};
}

UiClipping UiClipping::clipToBounds()
{
    return UiClipping{UiClippingMode::ClipToBounds};
}

UiStyle UiStyle::fromToken(arp::core::pmr::String tokenId)
{
    UiStyle style;
    style.baseTokenId_ = std::move(tokenId);
    return style;
}

void UiStyle::addState(UiStyleState state, arp::core::pmr::String tokenId)
{
    stateTokens_.push_back(UiStyleStateToken{state, std::move(tokenId)});
}

UiLayoutStyleThemeDocument::UiLayoutStyleThemeDocument(arp::core::pmr::String documentId)
    : documentId_(std::move(documentId))
{}

void UiLayoutStyleThemeDocument::addNode(UiLayoutStyleNodeDesc node)
{
    nodes_.push_back(std::move(node));
}

UiLayoutStyleThemeResult::UiLayoutStyleThemeResult(
    UiDiagnostic diagnostic, arp::core::pmr::Vector<UiResolvedLayoutStyleNode> nodes)
    : diagnostic_(std::move(diagnostic)), nodes_(std::move(nodes))
{}

UiLayoutStyleThemeResult UiLayoutStyleThemeResult::success(
    arp::core::pmr::Vector<UiResolvedLayoutStyleNode> nodes)
{
    return UiLayoutStyleThemeResult(UiDiagnostic{}, std::move(nodes));
}

UiLayoutStyleThemeResult UiLayoutStyleThemeResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    return UiLayoutStyleThemeResult(UiDiagnostic{code, std::move(message)}, {});
}

const UiResolvedLayoutStyleNode& UiLayoutStyleThemeResult::requireNode(
    UiDocumentNodeId nodeId) const
{
    const auto found =
        std::find_if(nodes_.begin(), nodes_.end(), [nodeId](const UiResolvedLayoutStyleNode& node) {
            return node.nodeId == nodeId;
        });
    assert(found != nodes_.end());
    return *found;
}

namespace {

LayoutRect intersectLayoutRect(LayoutRect lhs, LayoutRect rhs)
{
    const auto x0 = std::max(lhs.x, rhs.x);
    const auto y0 = std::max(lhs.y, rhs.y);
    const auto x1 = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
    const auto y1 = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
    return LayoutRect::fromOriginSize(x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0));
}

const UiLayoutStyleNodeDesc* findLayoutNode(
    const arp::core::pmr::Vector<UiLayoutStyleNodeDesc>& nodes, UiDocumentNodeId nodeId)
{
    const auto found =
        std::find_if(nodes.begin(), nodes.end(), [nodeId](const UiLayoutStyleNodeDesc& node) {
            return node.nodeId == nodeId;
        });
    return found == nodes.end() ? nullptr : &*found;
}

const UiResolvedLayoutStyleNode* findResolvedLayoutNode(
    const arp::core::pmr::Vector<UiResolvedLayoutStyleNode>& nodes, UiDocumentNodeId nodeId)
{
    const auto found =
        std::find_if(nodes.begin(), nodes.end(), [nodeId](const UiResolvedLayoutStyleNode& node) {
            return node.nodeId == nodeId;
        });
    return found == nodes.end() ? nullptr : &*found;
}

float preferredLayoutWidth(const UiLayoutStyleNodeDesc& node, float parentWidth)
{
    if (node.fixedSize.width > 0.0f) {
        return node.fixedSize.width;
    }
    if (node.percentSize.width > 0.0f) {
        return parentWidth * node.percentSize.width;
    }
    return parentWidth - node.margin.left - node.margin.right;
}

float preferredLayoutHeight(const UiLayoutStyleNodeDesc& node, float parentHeight)
{
    if (node.fixedSize.height > 0.0f) {
        return node.fixedSize.height;
    }
    if (node.percentSize.height > 0.0f) {
        return parentHeight * node.percentSize.height;
    }
    return parentHeight - node.margin.top - node.margin.bottom;
}

float clampLayoutWidth(const UiLayoutStyleNodeDesc& node, float width)
{
    return std::min(std::max(width, node.constraints.minWidth), node.constraints.maxWidth);
}

float clampLayoutHeight(const UiLayoutStyleNodeDesc& node, float height)
{
    return std::min(std::max(height, node.constraints.minHeight), node.constraints.maxHeight);
}

arp::core::pmr::String resolveStyleTokenForState(const UiStyle& style, UiStyleState state)
{
    if (state == UiStyleState::Normal) {
        return style.baseTokenId();
    }
    const auto found = std::find_if(style.stateTokens().begin(),
        style.stateTokens().end(),
        [state](const UiStyleStateToken& token) {
            return token.state == state;
        });
    return found == style.stateTokens().end() ? style.baseTokenId() : found->tokenId;
}

LayoutRect stackContentRect(
    const UiResolvedLayoutStyleNode& parent, const UiLayoutStyleNodeDesc& parentDesc)
{
    return LayoutRect::fromOriginSize(parent.rect.x + parentDesc.padding.left,
        parent.rect.y + parentDesc.padding.top,
        parent.rect.width - parentDesc.padding.left - parentDesc.padding.right,
        parent.rect.height - parentDesc.padding.top - parentDesc.padding.bottom);
}

float priorStackSiblingLength(const UiLayoutStyleThemeDocument& document,
    const arp::core::pmr::Vector<UiResolvedLayoutStyleNode>& resolved,
    const UiLayoutStyleNodeDesc& node,
    UiStackDirection direction)
{
    float length = 0.0f;
    std::size_t siblingCount = 0u;
    for (const auto& sibling : document.nodes()) {
        if (sibling.nodeId == node.nodeId) {
            break;
        }
        if (sibling.parentNodeId != node.parentNodeId) {
            continue;
        }
        const auto* resolvedSibling = findResolvedLayoutNode(resolved, sibling.nodeId);
        if (resolvedSibling == nullptr) {
            continue;
        }
        length += direction == UiStackDirection::Vertical ? resolvedSibling->rect.height
                                                          : resolvedSibling->rect.width;
        ++siblingCount;
    }
    return siblingCount == 0u ? 0.0f : length;
}

} // namespace

UiLayoutStyleThemeResult UiLayoutStyleThemeResolver::resolve(
    const UiLayoutStyleThemeDocument& document) const
{
    arp::core::pmr::Vector<UiResolvedLayoutStyleNode> resolved;
    resolved.reserve(document.nodes().size());

    for (const auto& node : document.nodes()) {
        if (!node.nodeId.valid() || !node.anchors.valid()) {
            return UiLayoutStyleThemeResult::failure(UiDiagnosticCode::InvalidLayoutAnchor,
                diagnosticText("UI layout node requires explicit valid anchors."));
        }
        if (!node.constraints.valid()) {
            return UiLayoutStyleThemeResult::failure(UiDiagnosticCode::ImpossibleLayoutConstraint,
                diagnosticText("UI layout node has impossible min/max constraints."));
        }
        if (node.style.baseTokenId().empty()
            || document.theme().findToken(node.style.baseTokenId()) == nullptr) {
            return UiLayoutStyleThemeResult::failure(UiDiagnosticCode::MissingThemeToken,
                diagnosticText("UI style references a missing theme token."));
        }
        for (const auto& state : node.style.stateTokens()) {
            if (state.tokenId.empty() || document.theme().findToken(state.tokenId) == nullptr) {
                return UiLayoutStyleThemeResult::failure(UiDiagnosticCode::InvalidStyleState,
                    diagnosticText("UI style state references a missing theme token."));
            }
        }
        if (node.layout == UiLayoutMode::Grid && !node.grid.valid()) {
            return UiLayoutStyleThemeResult::failure(UiDiagnosticCode::ImpossibleLayoutConstraint,
                diagnosticText("UI grid layout requires explicit non-zero rows and columns."));
        }

        LayoutRect parentRect = document.canvasRect();
        LayoutRect parentClipRect = document.canvasRect();
        const UiLayoutStyleNodeDesc* parentDesc = nullptr;
        if (node.parentNodeId.valid()) {
            const auto* parentFound = findResolvedLayoutNode(resolved, node.parentNodeId);
            parentDesc = findLayoutNode(document.nodes(), node.parentNodeId);
            if (parentFound == nullptr || parentDesc == nullptr) {
                return UiLayoutStyleThemeResult::failure(
                    UiDiagnosticCode::ImpossibleLayoutConstraint,
                    diagnosticText("UI layout node parent must be resolved before child."));
            }
            parentRect = parentFound->rect;
            parentClipRect = parentFound->clipRect;
        }

        if ((node.percentSize.width < 0.0f || node.percentSize.height < 0.0f)
            || (node.fixedSize.width < 0.0f || node.fixedSize.height < 0.0f)) {
            return UiLayoutStyleThemeResult::failure(UiDiagnosticCode::ImpossibleLayoutConstraint,
                diagnosticText("UI layout size metadata cannot be negative."));
        }

        LayoutRect layoutBounds = parentRect;
        float gapOffset = 0.0f;
        if (parentDesc != nullptr && parentDesc->layout == UiLayoutMode::Stack) {
            layoutBounds =
                stackContentRect(*findResolvedLayoutNode(resolved, node.parentNodeId), *parentDesc);
            if (layoutBounds.width < 0.0f || layoutBounds.height < 0.0f) {
                return UiLayoutStyleThemeResult::failure(
                    UiDiagnosticCode::ImpossibleLayoutConstraint,
                    diagnosticText("UI stack layout padding cannot fit inside parent rect."));
            }
            const auto siblingLength =
                priorStackSiblingLength(document, resolved, node, parentDesc->stack.direction);
            std::size_t priorSiblingCount = 0u;
            for (const auto& sibling : document.nodes()) {
                if (sibling.nodeId == node.nodeId) {
                    break;
                }
                if (sibling.parentNodeId == node.parentNodeId) {
                    ++priorSiblingCount;
                }
            }
            gapOffset =
                siblingLength + static_cast<float>(priorSiblingCount) * parentDesc->stack.spacing;
        }

        auto width = preferredLayoutWidth(node, layoutBounds.width);
        auto height = preferredLayoutHeight(node, layoutBounds.height);
        if (width < node.constraints.minWidth || height < node.constraints.minHeight) {
            return UiLayoutStyleThemeResult::failure(UiDiagnosticCode::ImpossibleLayoutConstraint,
                diagnosticText("UI layout constraints cannot fit within the parent rect."));
        }
        width = clampLayoutWidth(node, width);
        height = clampLayoutHeight(node, height);

        auto x = layoutBounds.x + node.margin.left;
        auto y = layoutBounds.y + node.margin.top;
        if (parentDesc != nullptr && parentDesc->layout == UiLayoutMode::Stack) {
            if (parentDesc->stack.direction == UiStackDirection::Vertical) {
                y = layoutBounds.y + gapOffset + node.margin.top;
                if (node.fixedSize.width <= 0.0f && node.percentSize.width <= 0.0f) {
                    width = clampLayoutWidth(
                        node, layoutBounds.width - node.margin.left - node.margin.right);
                }
            } else {
                x = layoutBounds.x + gapOffset + node.margin.left;
                if (node.fixedSize.height <= 0.0f && node.percentSize.height <= 0.0f) {
                    height = clampLayoutHeight(
                        node, layoutBounds.height - node.margin.top - node.margin.bottom);
                }
            }
        }

        const auto parentAllowsStackOverflow = parentDesc != nullptr
            && parentDesc->layout == UiLayoutMode::Stack
            && parentDesc->clipping.mode == UiClippingMode::ClipToBounds;
        const auto crossAxisOverflow = parentDesc != nullptr
            && parentDesc->layout == UiLayoutMode::Stack
            && ((parentDesc->stack.direction == UiStackDirection::Vertical
                    && (x < layoutBounds.x || x + width > layoutBounds.x + layoutBounds.width))
                || (parentDesc->stack.direction == UiStackDirection::Horizontal
                    && (y < layoutBounds.y || y + height > layoutBounds.y + layoutBounds.height)));
        const auto anyOverflow = x < layoutBounds.x || y < layoutBounds.y
            || x + width > layoutBounds.x + layoutBounds.width
            || y + height > layoutBounds.y + layoutBounds.height;
        if (anyOverflow && (!parentAllowsStackOverflow || crossAxisOverflow)) {
            return UiLayoutStyleThemeResult::failure(UiDiagnosticCode::ImpossibleLayoutConstraint,
                diagnosticText("UI layout constraints cannot fit within the parent rect."));
        }

        const auto rect = LayoutRect::fromOriginSize(x, y, width, height);
        const auto clipsToBounds = node.clipping.mode == UiClippingMode::ClipToBounds;
        const auto clipRect =
            clipsToBounds ? intersectLayoutRect(parentClipRect, rect) : parentClipRect;
        resolved.push_back(UiResolvedLayoutStyleNode{
            node.nodeId,
            rect,
            clipRect,
            node.grid,
            node.zOrder,
            clipsToBounds,
            resolveStyleTokenForState(node.style, node.state),
        });
    }

    return UiLayoutStyleThemeResult::success(std::move(resolved));
}

} // namespace arp::ui
