#include "ui/ui_theme_resource.h"

#include "reflection/reflection.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <fkYAML/node.hpp>
#include <fstream>
#include <system_error>
#include <utility>

namespace arp::ui {

namespace {

using YamlNode = fkyaml::node;

arp::core::pmr::String themeResourceText(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String pathToText(const std::filesystem::path& path)
{
    using StringAllocator = arp::core::pmr::String::allocator_type;
    return path.generic_string<char, std::char_traits<char>, StringAllocator>(StringAllocator{
        &arp::core::defaultCpuMemoryResource(),
    });
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

bool yamlNumber(const YamlNode& node, float& out)
{
    if (node.is_float_number()) {
        out = static_cast<float>(node.get_value<double>());
        return std::isfinite(out);
    }
    if (node.is_integer()) {
        out = static_cast<float>(node.get_value<YamlNode::integer_type>());
        return std::isfinite(out);
    }
    return false;
}

bool yamlUnsigned(const YamlNode& node, std::uint32_t& out)
{
    if (!node.is_integer()) {
        return false;
    }
    const auto value = node.get_value<YamlNode::integer_type>();
    if (value <= 0) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool yamlStringSequence(const YamlNode& node, arp::core::pmr::Vector<arp::core::pmr::String>& out)
{
    if (!node.is_sequence()) {
        return false;
    }
    for (const auto& item : node.get_value_ref<const YamlNode::sequence_type&>()) {
        if (!item.is_string()) {
            return false;
        }
        out.push_back(yamlString(item));
    }
    return true;
}

bool validResourceStatus(const arp::core::pmr::String& status)
{
    return arp::resource::validateResourceStatus({status.data(), status.size()},
        {
            arp::resource::ResourceStatus::ContractComplete,
            arp::resource::ResourceStatus::PrototypeComplete,
        },
        "UI theme or atlas metadata resource")
        .success;
}

bool readTextFile(const std::filesystem::path& path,
    const char* label,
    const arp::resource::ResourceLoadContext& context,
    arp::core::pmr::String& out,
    arp::core::pmr::String& diagnostics)
{
    const auto pathText = pathToText(path);
    if (context.sources != nullptr) {
        const auto* source = context.sources->find(pathText);
        if (source != nullptr) {
            out.assign(reinterpret_cast<const char*>(source->bytes.data()), source->bytes.size());
            if (out.empty()) {
                diagnostics = themeResourceText(label);
                diagnostics += " preloaded source is empty: ";
                diagnostics += pathText;
                return false;
            }
            return true;
        }
    }
    if (context.requirePreloadedSources) {
        diagnostics = themeResourceText(label);
        diagnostics += " requires preloaded source: ";
        diagnostics += pathText;
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        diagnostics = themeResourceText(label);
        diagnostics += " file is missing: ";
        diagnostics += path.generic_string().c_str();
        return false;
    }
    out.clear();
    char ch = '\0';
    while (input.get(ch)) {
        out.push_back(ch);
    }
    if (out.empty()) {
        diagnostics = themeResourceText(label);
        diagnostics += " file is empty: ";
        diagnostics += path.generic_string().c_str();
        return false;
    }
    return true;
}

bool parseYamlText(const arp::core::pmr::String& source,
    const char* label,
    YamlNode& out,
    arp::core::pmr::String& diagnostics)
{
    try {
        out = YamlNode::deserialize(source.data(), source.data() + source.size());
    } catch (const std::exception& err) {
        diagnostics = themeResourceText(label);
        diagnostics += " parse failed: ";
        diagnostics += err.what();
        return false;
    }
    if (!out.is_mapping()) {
        diagnostics = themeResourceText(label);
        diagnostics += " root must be a mapping.";
        return false;
    }
    return true;
}

bool requireRootContract(const YamlNode& root,
    const char* typeValue,
    const char* contractValue,
    const char* label,
    arp::core::pmr::String& diagnostics)
{
    const auto* type = findMappingValue(root, "type");
    const auto* contractVersion = findMappingValue(root, "contractVersion");
    if (type == nullptr || !type->is_string() || contractVersion == nullptr
        || !contractVersion->is_string() || yamlString(*type) != themeResourceText(typeValue)
        || yamlString(*contractVersion) != themeResourceText(contractValue)) {
        diagnostics = themeResourceText(label);
        diagnostics += " requires type ";
        diagnostics += typeValue;
        diagnostics += " and contractVersion ";
        diagnostics += contractValue;
        return false;
    }
    return true;
}

bool fontTokenExists(
    const arp::core::pmr::Vector<UiFontFaceDesc>& fonts, const arp::core::pmr::String& tokenId)
{
    return std::find_if(fonts.begin(), fonts.end(), [&](const UiFontFaceDesc& font) {
        return font.tokenId == tokenId;
    }) != fonts.end();
}

std::filesystem::path resolveAtlasMetadataPath(const UiThemeResourceLoadDesc& desc,
    const YamlNode& themeRoot,
    arp::core::pmr::String& expectedAtlasResourceId,
    arp::core::pmr::String& diagnostics)
{
    const auto* atlasReferences = findMappingValue(themeRoot, "atlasReferences");
    if (atlasReferences == nullptr || !atlasReferences->is_sequence()
        || atlasReferences->get_value_ref<const YamlNode::sequence_type&>().empty()) {
        diagnostics = themeResourceText("UI theme requires at least one atlasReferences entry.");
        return {};
    }
    const auto& first = atlasReferences->get_value_ref<const YamlNode::sequence_type&>().front();
    const auto* resourceId = findMappingValue(first, "resourceId");
    const auto* metadataPath = findMappingValue(first, "metadataPath");
    if (resourceId == nullptr || !resourceId->is_string() || metadataPath == nullptr
        || !metadataPath->is_string()) {
        diagnostics = themeResourceText(
            "UI theme atlasReferences entry requires resourceId and metadataPath.");
        return {};
    }
    expectedAtlasResourceId = yamlString(*resourceId);

    if (!desc.atlasMetadataPath.empty()) {
        return desc.atlasMetadataPath;
    }
    auto resolved = std::filesystem::path(yamlString(*metadataPath).c_str());
    if (resolved.is_absolute()) {
        return resolved;
    }
    return desc.themePath.parent_path() / resolved.filename();
}

bool parseThemeYaml(const UiThemeResourceLoadDesc& desc,
    const YamlNode& root,
    UiThemeResourceData& out,
    std::filesystem::path& atlasMetadataPath,
    arp::core::pmr::String& expectedAtlasResourceId,
    arp::core::pmr::String& diagnostics)
{
    if (!requireRootContract(root, "ui.theme", kUiThemeResourceContract, "UI theme", diagnostics)) {
        return false;
    }
    const auto* status = findMappingValue(root, "status");
    if (status == nullptr || !status->is_string() || !validResourceStatus(yamlString(*status))) {
        diagnostics =
            themeResourceText("UI theme requires Contract Complete or Prototype Complete status.");
        return false;
    }
    const auto* themeId = findMappingValue(root, "themeId");
    const auto* colorTokens = findMappingValue(root, "colorTokens");
    const auto* fontTokens = findMappingValue(root, "fontTokens");
    const auto* styleBindings = findMappingValue(root, "styleBindings");
    if (themeId == nullptr || !themeId->is_string() || colorTokens == nullptr
        || !colorTokens->is_sequence() || fontTokens == nullptr || !fontTokens->is_sequence()
        || styleBindings == nullptr || !styleBindings->is_sequence()) {
        diagnostics = themeResourceText(
            "UI theme requires themeId, fontTokens, colorTokens, and styleBindings.");
        return false;
    }

    out.theme = UiTheme(yamlString(*themeId));

    for (const auto& fontToken : fontTokens->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto* tokenId = findMappingValue(fontToken, "tokenId");
        const auto* family = findMappingValue(fontToken, "family");
        const auto* sourcePath = findMappingValue(fontToken, "sourcePath");
        const auto* sizePx = findMappingValue(fontToken, "sizePx");
        const auto* weight = findMappingValue(fontToken, "weight");
        const auto* lineHeightPx = findMappingValue(fontToken, "lineHeightPx");
        UiFontFaceDesc font;
        if (tokenId == nullptr || !tokenId->is_string() || family == nullptr || !family->is_string()
            || sourcePath == nullptr || !sourcePath->is_string() || sizePx == nullptr
            || weight == nullptr || lineHeightPx == nullptr || !yamlUnsigned(*sizePx, font.sizePx)
            || !yamlUnsigned(*weight, font.weight)
            || !yamlUnsigned(*lineHeightPx, font.lineHeightPx)) {
            diagnostics = themeResourceText(
                "UI theme font token requires tokenId, family, sourcePath, sizePx, weight, and lineHeightPx.");
            return false;
        }
        font.tokenId = yamlString(*tokenId);
        font.family = yamlString(*family);
        font.sourcePath = std::filesystem::path(yamlString(*sourcePath).c_str());
        out.theme.addToken({font.tokenId, UiThemeTokenValue::fromText(font.family)});
        out.fonts.push_back(std::move(font));
    }

    arp::core::pmr::UnorderedMap<arp::core::pmr::String, UiControlColorValue> colors{
        &arp::core::defaultCpuMemoryResource()};
    for (const auto& token : colorTokens->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto* tokenId = findMappingValue(token, "tokenId");
        const auto* rgba = findMappingValue(token, "rgba");
        if (tokenId == nullptr || !tokenId->is_string() || rgba == nullptr
            || !rgba->is_sequence()) {
            diagnostics = themeResourceText("UI theme color token requires tokenId and rgba.");
            return false;
        }
        const auto& values = rgba->get_value_ref<const YamlNode::sequence_type&>();
        if (values.size() != 4u) {
            diagnostics = themeResourceText("UI theme color token requires four rgba values.");
            return false;
        }
        UiControlColorValue color;
        if (!yamlNumber(values[0], color.r) || !yamlNumber(values[1], color.g)
            || !yamlNumber(values[2], color.b) || !yamlNumber(values[3], color.a)) {
            diagnostics =
                themeResourceText("UI theme color token rgba values must be finite numbers.");
            return false;
        }
        const auto colorTokenId = yamlString(*tokenId);
        colors[colorTokenId] = color;
        out.theme.addToken(
            {colorTokenId, UiThemeTokenValue::fromColor(color.r, color.g, color.b, color.a)});
    }

    for (const auto& binding : styleBindings->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto* styleId = findMappingValue(binding, "styleId");
        const auto* baseToken = findMappingValue(binding, "baseToken");
        const auto* fontToken = findMappingValue(binding, "fontToken");
        if (styleId == nullptr || !styleId->is_string() || baseToken == nullptr
            || !baseToken->is_string() || fontToken == nullptr || !fontToken->is_string()) {
            diagnostics = themeResourceText(
                "UI theme style binding requires styleId, baseToken, and fontToken.");
            return false;
        }
        const auto baseTokenId = yamlString(*baseToken);
        const auto colorFound = colors.find(baseTokenId);
        if (colorFound == colors.end()) {
            diagnostics =
                themeResourceText("UI theme style binding references missing color token: ");
            diagnostics += baseTokenId;
            return false;
        }
        const auto fontTokenId = yamlString(*fontToken);
        if (!fontTokenExists(out.fonts, fontTokenId)) {
            diagnostics =
                themeResourceText("UI theme style binding references missing font token: ");
            diagnostics += fontTokenId;
            return false;
        }
        const auto styleTokenId = yamlString(*styleId);
        out.styleColors[styleTokenId] = colorFound->second;
        out.styleFonts[styleTokenId] = fontTokenId;
        out.theme.addToken({
            styleTokenId,
            UiThemeTokenValue::fromColor(colorFound->second.r,
                colorFound->second.g,
                colorFound->second.b,
                colorFound->second.a),
        });
    }

    atlasMetadataPath = resolveAtlasMetadataPath(desc, root, expectedAtlasResourceId, diagnostics);
    return !atlasMetadataPath.empty();
}

bool parseAtlasYaml(const YamlNode& root,
    const arp::core::pmr::String& expectedAtlasResourceId,
    UiAtlasMetadataResourceData& out,
    arp::core::pmr::String& diagnostics)
{
    if (!requireRootContract(
            root, "ui.atlas", kUiAtlasResourceContract, "UI atlas metadata", diagnostics)) {
        return false;
    }
    const auto* status = findMappingValue(root, "status");
    if (status == nullptr || !status->is_string() || !validResourceStatus(yamlString(*status))) {
        diagnostics = themeResourceText(
            "UI atlas metadata requires Contract Complete or Prototype Complete status.");
        return false;
    }
    const auto* atlasId = findMappingValue(root, "atlasId");
    const auto* resourceId = findMappingValue(root, "resourceId");
    const auto* baseImageAtlas = findMappingValue(root, "baseImageAtlas");
    if (atlasId == nullptr || !atlasId->is_string() || resourceId == nullptr
        || !resourceId->is_string() || baseImageAtlas == nullptr || !baseImageAtlas->is_mapping()) {
        diagnostics = themeResourceText(
            "UI atlas metadata requires atlasId, resourceId, and baseImageAtlas.");
        return false;
    }
    const auto atlasResourceId = yamlString(*resourceId);
    if (!expectedAtlasResourceId.empty() && atlasResourceId != expectedAtlasResourceId) {
        diagnostics = themeResourceText(
            "UI atlas metadata resourceId does not match expected atlas reference.");
        return false;
    }
    const auto* sourcePath = findMappingValue(*baseImageAtlas, "sourcePath");
    const auto* width = findMappingValue(*baseImageAtlas, "width");
    const auto* height = findMappingValue(*baseImageAtlas, "height");
    const auto* colorSpace = findMappingValue(*baseImageAtlas, "colorSpace");
    if (sourcePath == nullptr || !sourcePath->is_string() || width == nullptr || height == nullptr
        || colorSpace == nullptr || !colorSpace->is_string() || !yamlNumber(*width, out.width)
        || !yamlNumber(*height, out.height) || out.width <= 0.0f || out.height <= 0.0f) {
        diagnostics = themeResourceText(
            "UI atlas metadata requires sourcePath, colorSpace, width, and height.");
        return false;
    }
    const auto colorSpaceValue = yamlString(*colorSpace);
    if (colorSpaceValue != "srgb" && colorSpaceValue != "linear") {
        diagnostics = themeResourceText("UI atlas metadata colorSpace must be srgb or linear.");
        return false;
    }
    out.atlasId = yamlString(*atlasId);
    out.resourceId = atlasResourceId;
    out.sourcePath = std::filesystem::path(yamlString(*sourcePath).c_str());
    out.srgb = colorSpaceValue == "srgb";

    const auto* fontFaces = findMappingValue(root, "fontFaces");
    if (fontFaces == nullptr || !fontFaces->is_sequence()
        || fontFaces->get_value_ref<const YamlNode::sequence_type&>().empty()) {
        diagnostics = themeResourceText("UI atlas metadata requires fontFaces.");
        return false;
    }
    for (const auto& faceNode : fontFaces->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto* tokenId = findMappingValue(faceNode, "tokenId");
        const auto* family = findMappingValue(faceNode, "family");
        const auto* faceSourcePath = findMappingValue(faceNode, "sourcePath");
        const auto* weight = findMappingValue(faceNode, "weight");
        const auto* lineHeightPx = findMappingValue(faceNode, "lineHeightPx");
        const auto* fallbackChain = findMappingValue(faceNode, "fallbackChain");
        const auto* supportedScripts = findMappingValue(faceNode, "supportedScripts");
        const auto* charset = findMappingValue(faceNode, "charset");
        UiFontFaceDesc face;
        if (tokenId == nullptr || !tokenId->is_string() || family == nullptr || !family->is_string()
            || faceSourcePath == nullptr || !faceSourcePath->is_string() || weight == nullptr
            || lineHeightPx == nullptr || fallbackChain == nullptr || supportedScripts == nullptr
            || charset == nullptr || !charset->is_string() || !yamlUnsigned(*weight, face.weight)
            || !yamlUnsigned(*lineHeightPx, face.lineHeightPx)) {
            diagnostics = themeResourceText(
                "UI atlas metadata fontFaces entries require tokenId, family, sourcePath, fallbackChain, supportedScripts, charset, weight, and lineHeightPx.");
            return false;
        }
        face.tokenId = yamlString(*tokenId);
        face.family = yamlString(*family);
        face.sourcePath = std::filesystem::path(yamlString(*faceSourcePath).c_str());
        if (!yamlStringSequence(*fallbackChain, face.fallbackChain) || face.fallbackChain.empty()) {
            diagnostics = themeResourceText(
                "UI atlas metadata fontFaces fallbackChain must be a non-empty string sequence.");
            return false;
        }
        if (!yamlStringSequence(*supportedScripts, face.supportedScripts)
            || face.supportedScripts.empty()) {
            diagnostics = themeResourceText(
                "UI atlas metadata fontFaces supportedScripts must be a non-empty string sequence.");
            return false;
        }
        face.charset = yamlString(*charset);
        if (face.charset.empty()) {
            diagnostics =
                themeResourceText("UI atlas metadata fontFaces charset must be non-empty.");
            return false;
        }
        const auto* sizeRangePx = findMappingValue(faceNode, "sizeRangePx");
        if (sizeRangePx != nullptr && sizeRangePx->is_mapping()) {
            const auto* minSize = findMappingValue(*sizeRangePx, "min");
            if (minSize != nullptr) {
                if (!yamlUnsigned(*minSize, face.sizePx)) {
                    diagnostics = themeResourceText(
                        "UI atlas metadata fontFaces sizeRangePx.min must be positive.");
                    return false;
                }
            }
        }
        // Optional renderMode: "alpha" (default, full-coverage AlphaMask at
        // sizePx) or "sdf" (explicit scalable Signed Distance Field text).
        const auto* renderMode = findMappingValue(faceNode, "renderMode");
        if (renderMode != nullptr) {
            if (!renderMode->is_string()) {
                diagnostics =
                    themeResourceText("UI atlas metadata fontFaces renderMode must be a string.");
                return false;
            }
            face.renderMode = yamlString(*renderMode);
            if (face.renderMode != "alpha" && face.renderMode != "sdf") {
                diagnostics = themeResourceText(
                    "UI atlas metadata fontFaces renderMode must be alpha or sdf.");
                return false;
            }
        }
        out.fontFaces.push_back(std::move(face));
    }

    const auto* glyphPolicy = findMappingValue(root, "glyphPolicy");
    const auto* pagePolicy = findMappingValue(root, "pagePolicy");
    if (glyphPolicy == nullptr || !glyphPolicy->is_mapping() || pagePolicy == nullptr
        || !pagePolicy->is_mapping()) {
        diagnostics = themeResourceText("UI atlas metadata requires glyphPolicy and pagePolicy.");
        return false;
    }
    const auto* glyphPolicyMode = findMappingValue(*glyphPolicy, "mode");
    const auto* pagePolicyMode = findMappingValue(*pagePolicy, "mode");
    if (glyphPolicyMode == nullptr || !glyphPolicyMode->is_string() || pagePolicyMode == nullptr
        || !pagePolicyMode->is_string()) {
        diagnostics =
            themeResourceText("UI atlas metadata glyphPolicy and pagePolicy require mode.");
        return false;
    }
    out.glyphPolicy = yamlString(*glyphPolicyMode);
    out.pagePolicy = yamlString(*pagePolicyMode);

    const auto* slices = findMappingValue(root, "slices");
    if (slices == nullptr || !slices->is_sequence()
        || slices->get_value_ref<const YamlNode::sequence_type&>().empty()) {
        diagnostics = themeResourceText("UI atlas metadata requires slices.");
        return false;
    }
    for (const auto& sliceNode : slices->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto* sliceId = findMappingValue(sliceNode, "sliceId");
        const auto* rect = findMappingValue(sliceNode, "rect");
        UiAtlasSliceMetadata slice;
        if (sliceId == nullptr || !sliceId->is_string() || rect == nullptr || !rect->is_mapping()) {
            diagnostics = themeResourceText("UI atlas metadata slice requires sliceId and rect.");
            return false;
        }
        const auto* x = findMappingValue(*rect, "x");
        const auto* y = findMappingValue(*rect, "y");
        const auto* rectWidth = findMappingValue(*rect, "width");
        const auto* rectHeight = findMappingValue(*rect, "height");
        if (x == nullptr || y == nullptr || rectWidth == nullptr || rectHeight == nullptr
            || !yamlNumber(*x, slice.x) || !yamlNumber(*y, slice.y)
            || !yamlNumber(*rectWidth, slice.width) || !yamlNumber(*rectHeight, slice.height)
            || slice.width <= 0.0f || slice.height <= 0.0f) {
            diagnostics = themeResourceText(
                "UI atlas metadata slice rect requires finite positive width and height.");
            return false;
        }
        slice.sliceId = yamlString(*sliceId);
        out.slices.push_back(std::move(slice));
    }
    return true;
}

void appendUnsigned(arp::core::pmr::String& out, std::uint64_t value)
{
    char buffer[32]{};
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (converted.ec == std::errc{}) {
        out.append(buffer, static_cast<std::size_t>(converted.ptr - buffer));
    }
}

void appendIndentedBlock(
    arp::core::pmr::String& out, const arp::core::pmr::String& textBlock, const char* indent)
{
    out += indent;
    for (const char ch : textBlock) {
        out.push_back(ch);
        if (ch == '\n') {
            out += indent;
        }
    }
    if (out.empty() || out.back() != '\n') {
        out += "\n";
    }
}

arp::core::pmr::String themePayloadYaml(const arp::core::pmr::String& resourceType,
    const UiThemeResourceLoadDesc& desc,
    const std::filesystem::path& atlasMetadataPath,
    const UiThemeResourceData& data)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "type: ";
    yaml += resourceType;
    yaml += "\nfields:\n";
    yaml += "  contractVersion: ";
    yaml += kUiThemeResourceContract;
    yaml += "\n  themeId: ";
    yaml += data.theme.themeId();
    yaml += "\n  themeSource: ";
    yaml += pathToText(desc.themePath);
    yaml += "\n  atlasMetadataSource: ";
    yaml += pathToText(atlasMetadataPath);
    yaml += "\n  atlasResourceId: ";
    yaml += data.atlasResourceId;
    yaml += "\n  atlasWidth: ";
    appendUnsigned(yaml, static_cast<std::uint64_t>(data.atlasWidth));
    yaml += "\n  atlasHeight: ";
    appendUnsigned(yaml, static_cast<std::uint64_t>(data.atlasHeight));
    yaml += "\n  fontTokenCount: ";
    appendUnsigned(yaml, data.fonts.size());
    yaml += "\n  styleBindingCount: ";
    appendUnsigned(yaml, data.styleColors.size());
    yaml += "\n";
    return yaml;
}

arp::core::pmr::String atlasPayloadYaml(const arp::core::pmr::String& resourceType,
    const UiAtlasMetadataResourceLoadDesc& desc,
    const UiAtlasMetadataResourceData& data)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "type: ";
    yaml += resourceType;
    yaml += "\nfields:\n";
    yaml += "  contractVersion: ";
    yaml += kUiAtlasResourceContract;
    yaml += "\n  atlasId: ";
    yaml += data.atlasId;
    yaml += "\n  atlasMetadataSource: ";
    yaml += pathToText(desc.atlasMetadataPath);
    yaml += "\n  atlasResourceId: ";
    yaml += data.resourceId;
    yaml += "\n  sourcePath: ";
    yaml += pathToText(data.sourcePath);
    yaml += "\n  width: ";
    appendUnsigned(yaml, static_cast<std::uint64_t>(data.width));
    yaml += "\n  height: ";
    appendUnsigned(yaml, static_cast<std::uint64_t>(data.height));
    yaml += "\n  sliceCount: ";
    appendUnsigned(yaml, data.slices.size());
    yaml += "\n  fontFaceCount: ";
    appendUnsigned(yaml, data.fontFaces.size());
    yaml += "\n  glyphPolicy: ";
    yaml += data.glyphPolicy;
    yaml += "\n  pagePolicy: ";
    yaml += data.pagePolicy;
    yaml += "\n";
    return yaml;
}

arp::core::pmr::String themeEnvelopeYaml(const UiThemeResourceLoadDesc& desc,
    const arp::core::pmr::String& resourceType,
    const arp::core::pmr::String& payloadYaml,
    const arp::core::pmr::String& atlasManagerResourceId,
    std::uint32_t contentHash)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "format: ArpRuntimeResource\n";
    yaml += "version: 1\n";
    yaml += "resourceId: ";
    yaml += desc.resourceId;
    yaml += "\nresourceType: ";
    yaml += resourceType;
    yaml += "\n";
    if (!desc.alias.empty()) {
        yaml += "alias: ";
        yaml += desc.alias;
        yaml += "\n";
    }
    yaml += "displayName: UI Theme Resource\n";
    yaml += "status: ";
    yaml += desc.status;
    yaml += "\ncontentHash: ";
    appendUnsigned(yaml, contentHash);
    yaml += "\ndependencies:\n";
    yaml += "  - id: ";
    yaml += atlasManagerResourceId;
    yaml += "\n    usage: ";
    yaml += contracts::kUsageAtlasMetadata;
    yaml += "\n";
    yaml += "payload:\n";
    appendIndentedBlock(yaml, payloadYaml, "  ");
    return yaml;
}

arp::core::pmr::String atlasEnvelopeYaml(const UiAtlasMetadataResourceLoadDesc& desc,
    const arp::core::pmr::String& resourceType,
    const arp::core::pmr::String& payloadYaml,
    std::uint32_t contentHash)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "format: ArpRuntimeResource\n";
    yaml += "version: 1\n";
    yaml += "resourceId: ";
    yaml += desc.resourceId;
    yaml += "\nresourceType: ";
    yaml += resourceType;
    yaml += "\n";
    if (!desc.alias.empty()) {
        yaml += "alias: ";
        yaml += desc.alias;
        yaml += "\n";
    }
    yaml += "displayName: UI Atlas Metadata Resource\n";
    yaml += "status: ";
    yaml += desc.status;
    yaml += "\ncontentHash: ";
    appendUnsigned(yaml, contentHash);
    yaml += "\ndependencies: []\n";
    yaml += "payload:\n";
    appendIndentedBlock(yaml, payloadYaml, "  ");
    return yaml;
}

void registerUiAtlasMetadataResourceReflection()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    (void)arp::reflection::registration::class_<UiAtlasMetadataResource>{
        kUiAtlasMetadataResourceType,
    };
    arp::reflection::class_of<UiAtlasMetadataResource>()->set_name(kUiAtlasMetadataResourceType);
    registered = true;
}

void registerUiThemeResourceReflection()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    (void)arp::reflection::registration::class_<UiThemeResource>{kUiThemeResourceType};
    arp::reflection::class_of<UiThemeResource>()->set_name(kUiThemeResourceType);
    registered = true;
}

} // namespace

arp::resource::ResourceLoadResult UiAtlasMetadataResource::load(
    arp::resource::ResourceManager& resourceManager,
    const arp::resource::ResourceLoadContext& context)
{
    arp::resource::ResourceLoadResult result;
    const auto& desc = loadDesc;
    if (!arp::resource::isResourceUuid(desc.resourceId)) {
        result.diagnostics =
            themeResourceText("UI atlas metadata resource requires UUID resourceId.");
        return result;
    }
    if (!validResourceStatus(desc.status)) {
        result.diagnostics = themeResourceText("invalid UI atlas metadata resource status");
        return result;
    }

    arp::core::pmr::String atlasSource(&arp::core::defaultCpuMemoryResource());
    if (!readTextFile(desc.atlasMetadataPath,
            "UI atlas metadata",
            context,
            atlasSource,
            result.diagnostics)) {
        return result;
    }
    YamlNode atlasRoot;
    if (!parseYamlText(atlasSource, "UI atlas metadata", atlasRoot, result.diagnostics)) {
        return result;
    }

    data = arp::core::makeShared<UiAtlasMetadataResourceData>();
    if (!parseAtlasYaml(atlasRoot, desc.expectedAtlasResourceId, *data, result.diagnostics)) {
        return result;
    }

    return resourceManager.admitResourceMetadata(*this, context.policy);
}

bool UiAtlasMetadataResource::collectFileReadIntents(arp::resource::ResourceManager&,
    arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
    arp::core::pmr::String& diagnostics) const
{
    if (loadDesc.atlasMetadataPath.empty()) {
        diagnostics = themeResourceText("UI atlas metadata resource requires source path.");
        return false;
    }
    arp::resource::ResourceFileReadIntent intent;
    intent.pathScope = arp::core::io::FilePathScope::HostFilesystem;
    intent.path = pathToText(loadDesc.atlasMetadataPath);
    intent.role = themeResourceText("ui.atlas.metadata.yaml");
    intent.maxBytes = 1024u * 1024u;
    intents.push_back(std::move(intent));
    return true;
}

UiAtlasMetadataResourceLoadResult loadUiAtlasMetadataResource(
    arp::resource::ResourceManager& resourceManager, const UiAtlasMetadataResourceLoadDesc& desc)
{
    UiAtlasMetadataResourceLoadResult result;
    result.resource =
        resourceManager.load<UiAtlasMetadataResource>(arp::resource::ResourceLoadContext{}, desc);
    if (!result.resource.success) {
        result.diagnostics = result.resource.diagnostics;
        return result;
    }
    result.success = true;
    return result;
}

arp::resource::ResourceLoadResult UiThemeResource::load(
    arp::resource::ResourceManager& resourceManager,
    const arp::resource::ResourceLoadContext& context)
{
    arp::resource::ResourceLoadResult result;
    const auto& desc = loadDesc;
    if (!arp::resource::isResourceUuid(desc.resourceId)) {
        result.diagnostics = themeResourceText("UI theme resource requires UUID resourceId.");
        return result;
    }
    if (!validResourceStatus(desc.status)) {
        result.diagnostics = themeResourceText("invalid UI theme resource status");
        return result;
    }

    arp::core::pmr::String themeSource(&arp::core::defaultCpuMemoryResource());
    if (!readTextFile(desc.themePath, "UI theme", context, themeSource, result.diagnostics)) {
        return result;
    }
    YamlNode themeRoot;
    if (!parseYamlText(themeSource, "UI theme", themeRoot, result.diagnostics)) {
        return result;
    }

    data = arp::core::makeShared<UiThemeResourceData>();
    std::filesystem::path atlasMetadataPath;
    arp::core::pmr::String expectedAtlasResourceId(&arp::core::defaultCpuMemoryResource());
    if (!parseThemeYaml(desc,
            themeRoot,
            *data,
            atlasMetadataPath,
            expectedAtlasResourceId,
            result.diagnostics)) {
        return result;
    }

    if (desc.atlasMetadataAlias.empty()) {
        result.diagnostics = themeResourceText("UI theme requires explicit atlasMetadataAlias.");
        return result;
    }
    const auto atlasHandle =
        resourceManager.findByAlias<UiAtlasMetadataResource>(desc.atlasMetadataAlias);
    if (!atlasHandle.ready() || atlasHandle.get() == nullptr || !atlasHandle.get()->data) {
        result.diagnostics = atlasHandle.diagnostics().empty()
            ? themeResourceText("UI theme atlas metadata resource is not ready.")
            : atlasHandle.diagnostics();
        return result;
    }
    const auto& atlas = *atlasHandle.get()->data;
    if (atlas.resourceId != expectedAtlasResourceId) {
        result.diagnostics =
            themeResourceText("UI theme atlas resourceId does not match admitted atlas metadata.");
        return result;
    }
    data->atlasResourceId = atlas.resourceId;
    data->atlasMetadataAlias = desc.atlasMetadataAlias;
    data->atlasSourcePath = atlas.sourcePath;
    data->atlasWidth = atlas.width;
    data->atlasHeight = atlas.height;
    data->atlasSrgb = atlas.srgb;

    addDependency(arp::resource::ResourceDependency{
        atlasHandle.id(),
        themeResourceText(contracts::kUsageAtlasMetadata.data()),
    });
    return resourceManager.admitResourceMetadata(*this, context.policy);
}

bool UiThemeResource::collectFileReadIntents(arp::resource::ResourceManager&,
    arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
    arp::core::pmr::String& diagnostics) const
{
    if (loadDesc.themePath.empty() || loadDesc.atlasMetadataPath.empty()) {
        diagnostics =
            themeResourceText("UI theme resource requires theme and atlas metadata paths.");
        return false;
    }
    arp::resource::ResourceFileReadIntent theme;
    theme.pathScope = arp::core::io::FilePathScope::HostFilesystem;
    theme.path = pathToText(loadDesc.themePath);
    theme.role = themeResourceText("ui.theme.yaml");
    theme.maxBytes = 1024u * 1024u;
    intents.push_back(std::move(theme));

    arp::resource::ResourceFileReadIntent atlas;
    atlas.pathScope = arp::core::io::FilePathScope::HostFilesystem;
    atlas.path = pathToText(loadDesc.atlasMetadataPath);
    atlas.role = themeResourceText("ui.atlas.metadata.yaml");
    atlas.maxBytes = 1024u * 1024u;
    intents.push_back(std::move(atlas));
    return true;
}

UiThemeResourceLoadResult loadUiThemeResource(
    arp::resource::ResourceManager& resourceManager, const UiThemeResourceLoadDesc& desc)
{
    UiThemeResourceLoadResult result;
    result.resource =
        resourceManager.load<UiThemeResource>(arp::resource::ResourceLoadContext{}, desc);
    if (!result.resource.success) {
        result.diagnostics = result.resource.diagnostics;
        return result;
    }
    result.success = true;
    return result;
}

} // namespace arp::ui
