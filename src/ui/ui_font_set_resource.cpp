#include "ui/ui_font_set_resource.h"

#include "reflection/reflection.h"
#include "ui/ui_atlas_metadata_resource.h"

#include <charconv>
#include <filesystem>
#include <system_error>
#include <utility>

namespace arp::ui {

namespace {

arp::core::pmr::String fontSetText(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

bool validFontSetStatus(const arp::core::pmr::String& status)
{
    return arp::resource::validateResourceStatus({status.data(), status.size()},
        {
            arp::resource::ResourceStatus::ContractComplete,
            arp::resource::ResourceStatus::PrototypeComplete,
        },
        "UI font set resource")
        .success;
}

bool validateFontFace(const UiFontFaceDesc& font, arp::core::pmr::String& diagnostics)
{
    if (font.tokenId.empty() || font.family.empty() || font.sourcePath.empty()
        || font.fallbackChain.empty() || font.supportedScripts.empty() || font.charset.empty()
        || font.weight == 0u || font.lineHeightPx == 0u) {
        diagnostics = fontSetText(
            "UI font set metadata requires tokenId, family, sourcePath, fallbackChain, supportedScripts, charset, weight, and lineHeightPx.");
        return false;
    }
    return true;
}

std::filesystem::path resolveFontSourcePath(
    const std::filesystem::path& sourceRoot, const std::filesystem::path& declaredPath)
{
    if (declaredPath.empty() || declaredPath.is_absolute() || sourceRoot.empty()) {
        return declaredPath;
    }
    for (const auto& component : declaredPath) {
        if (component == "..") {
            return {};
        }
    }
    return (sourceRoot / declaredPath).lexically_normal();
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

arp::core::pmr::String fontSetPayloadYaml(
    const arp::core::pmr::String& resourceType, const UiFontSetResourceData& data)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "type: ";
    yaml += resourceType;
    yaml += "\nfields:\n";
    yaml += "  contractVersion: ";
    yaml += kUiFontSetResourceContract;
    yaml += "\n  glyphPolicy: ";
    yaml += data.glyphPolicy;
    yaml += "\n  pagePolicy: ";
    yaml += data.pagePolicy;
    yaml += "\n  fontCount: ";
    appendUnsigned(yaml, data.fonts.size());
    yaml += "\n";
    return yaml;
}

arp::core::pmr::String fontSetEnvelopeYaml(const UiFontSetResourceLoadDesc& desc,
    const arp::core::pmr::String& resourceType,
    const arp::core::pmr::String& payloadYaml,
    const arp::core::pmr::String& atlasMetadataManagerResourceId,
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
    yaml += "displayName: UI Font Set Resource\n";
    yaml += "status: ";
    yaml += desc.status;
    yaml += "\ncontentHash: ";
    appendUnsigned(yaml, contentHash);
    yaml += "\ndependencies:\n";
    yaml += "  - id: ";
    yaml += atlasMetadataManagerResourceId;
    yaml += "\n    usage: ";
    yaml += contracts::kUsageAtlasMetadataFonts;
    yaml += "\n";
    yaml += "payload:\n";
    appendIndentedBlock(yaml, payloadYaml, "  ");
    return yaml;
}

void registerUiFontSetResourceReflection()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    (void)arp::reflection::registration::class_<UiFontSetResource>{
        kUiFontSetResourceType,
    };
    arp::reflection::class_of<UiFontSetResource>()->set_name(kUiFontSetResourceType);
    registered = true;
}

} // namespace

arp::resource::ResourceLoadResult UiFontSetResource::load(
    arp::resource::ResourceManager& resourceManager,
    const arp::resource::ResourceLoadContext& context)
{
    arp::resource::ResourceLoadResult result;
    const auto& desc = loadDesc;
    if (!arp::resource::isResourceUuid(desc.resourceId)) {
        result.diagnostics = fontSetText("UI font set resource requires UUID resourceId.");
        return result;
    }
    if (!validFontSetStatus(desc.status)) {
        result.diagnostics = fontSetText("invalid UI font set resource status");
        return result;
    }
    if (desc.atlasMetadataAlias.empty()) {
        result.diagnostics = fontSetText("UI font set resource requires atlasMetadataAlias.");
        return result;
    }

    const auto metadataHandle =
        resourceManager.findByAlias<UiAtlasMetadataResource>(desc.atlasMetadataAlias);
    if (!metadataHandle.ready() || metadataHandle.get() == nullptr || !metadataHandle.get()->data) {
        result.diagnostics = metadataHandle.diagnostics().empty()
            ? fontSetText("UI font set atlas metadata resource is not ready.")
            : metadataHandle.diagnostics();
        return result;
    }
    const auto& metadata = *metadataHandle.get()->data;
    if (!desc.requiredGlyphPolicy.empty() && metadata.glyphPolicy != desc.requiredGlyphPolicy) {
        result.diagnostics = fontSetText("UI font set glyph policy mismatch.");
        return result;
    }
    if (!desc.requiredPagePolicy.empty() && metadata.pagePolicy != desc.requiredPagePolicy) {
        result.diagnostics = fontSetText("UI font set page policy mismatch.");
        return result;
    }
    if (metadata.fontFaces.empty()) {
        result.diagnostics = fontSetText("UI font set requires at least one font face.");
        return result;
    }

    data = arp::core::makeShared<UiFontSetResourceData>();
    data->glyphPolicy = metadata.glyphPolicy;
    data->pagePolicy = metadata.pagePolicy;
    for (const auto& font : metadata.fontFaces) {
        if (!validateFontFace(font, result.diagnostics)) {
            return result;
        }
        auto resolvedFont = font;
        resolvedFont.sourcePath = resolveFontSourcePath(desc.sourceRoot, font.sourcePath);
        if (resolvedFont.sourcePath.empty()) {
            result.diagnostics =
                fontSetText("UI font source path escapes the explicit sourceRoot.");
            return result;
        }
        if (!desc.sourceRoot.empty()) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(resolvedFont.sourcePath, error) || error) {
                result.diagnostics =
                    fontSetText("UI font source file is missing under the explicit sourceRoot: ");
                result.diagnostics += resolvedFont.sourcePath.generic_string().c_str();
                return result;
            }
        }
        data->fonts.push_back(std::move(resolvedFont));
    }

    addDependency(arp::resource::ResourceDependency{
        metadataHandle.id(),
        fontSetText(contracts::kUsageAtlasMetadataFonts.data()),
    });
    return resourceManager.admitResourceMetadata(*this, context.policy);
}

UiFontSetResourceLoadResult loadUiFontSetResource(
    arp::resource::ResourceManager& resourceManager, const UiFontSetResourceLoadDesc& desc)
{
    UiFontSetResourceLoadResult result;
    result.resource =
        resourceManager.load<UiFontSetResource>(arp::resource::ResourceLoadContext{}, desc);
    if (!result.resource.success) {
        result.diagnostics = result.resource.diagnostics;
        return result;
    }
    result.success = true;
    return result;
}

} // namespace arp::ui
