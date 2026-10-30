#include "ui/ui_font_set_resource.h"

#include "reflection/reflection.h"
#include "resource/resource_source_io.h"
#include "ui/detail/ui_content_fingerprint.h"
#include "ui/ui_atlas_metadata_resource.h"
#include "ui/ui_font_source.h"

#include <algorithm>
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

void UiFontSetResource::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    detail::UiContentFingerprint hash(hasher);
    hash.resource(*this, kUiFontSetResourceContract);
    hash.number(data != nullptr);
    if (!data)
        return;
    hash.text(data->glyphPolicy);
    hash.text(data->pagePolicy);
    hash.number(data->fonts.size());
    for (const auto& font : data->fonts)
        hash.font(font);
}

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

    core::pmr::Vector<arp::resource::ResourceFileReadIntent> intents;
    if (!collectFileReadIntents(resourceManager, intents, result.diagnostics))
        return result;
    core::pmr::Vector<core::SharedPtr<const UiFontSource>> sources;
    std::size_t totalBytes = 0;
    for (const auto& intent : intents) {
        auto read = arp::resource::readResourceSource(intent.path,
            context,
            std::min(desc.maxFontBytes, desc.maxTotalSourceBytes - totalBytes),
            false,
            &desc.origin);
        if (!read.success) {
            result.diagnostics = std::move(read.diagnostics);
            return result;
        }
        UiFontSourceDesc sourceDesc;
        sourceDesc.path = intent.path;
        sourceDesc.pathScope = desc.origin.pathScope;
        sourceDesc.mount = desc.origin.mount;
        sourceDesc.maxBytes = desc.maxFontBytes;
        if (context.sources)
            if (const auto* source = context.sources->find(intent.path))
                sourceDesc.version = source->version;
        auto source =
            UiFontSource::create(std::move(sourceDesc), std::move(read.bytes), result.diagnostics);
        if (!source)
            return result;
        totalBytes += source->bytes().size();
        sources.push_back(std::move(source));
    }

    data = arp::core::makeShared<UiFontSetResourceData>();
    data->glyphPolicy = metadata.glyphPolicy;
    data->pagePolicy = metadata.pagePolicy;
    for (const auto& font : metadata.fontFaces) {
        if (!validateFontFace(font, result.diagnostics)) {
            return result;
        }
        auto resolvedFont = font;
        if (!arp::resource::resolveResourceSourcePath(desc.sourceRoot,
                core::FilePath::utf8(font.sourcePath),
                desc.origin,
                resolvedFont.sourcePath)) {
            result.diagnostics =
                fontSetText("UI font source path escapes the explicit sourceRoot.");
            return result;
        }
        const auto path = core::FilePath::utf8(resolvedFont.sourcePath);
        const auto source = std::find_if(sources.begin(), sources.end(), [&](const auto& source) {
            return source->desc().path == path;
        });
        if (source == sources.end()) {
            result.diagnostics = "UI-FONT-SET-SOURCE-MISSING";
            return result;
        }
        resolvedFont.source = *source;
        resolvedFont.maxSourceBytes = desc.maxFontBytes;
        data->fonts.push_back(std::move(resolvedFont));
    }

    addDependency(arp::resource::ResourceDependency{
        metadataHandle.id(),
        fontSetText(contracts::kUsageAtlasMetadataFonts.data()),
    });
    return resourceManager.admitResourceMetadata(*this, context.policy);
}

bool UiFontSetResource::collectFileReadIntents(arp::resource::ResourceManager& manager,
    core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostic) const
{
    const auto& desc = loadDesc;
    if (!desc.maxFaces || desc.maxFaces == SIZE_MAX || !desc.maxFontBytes
        || desc.maxFontBytes == SIZE_MAX || !desc.maxTotalSourceBytes
        || desc.maxTotalSourceBytes == SIZE_MAX) {
        diagnostic = "UI-FONT-SET-BUDGET";
        return false;
    }
    const auto metadata = manager.findByAlias<UiAtlasMetadataResource>(desc.atlasMetadataAlias);
    if (!metadata.ready() || !metadata.get()->data) {
        diagnostic = "UI-FONT-SET-METADATA-NOT-READY";
        return false;
    }
    const auto& faces = metadata.get()->data->fontFaces;
    if (faces.empty() || faces.size() > desc.maxFaces) {
        diagnostic = "UI-FONT-SET-FACE-BUDGET";
        return false;
    }
    for (const auto& face : faces) {
        if (!validateFontFace(face, diagnostic))
            return false;
        std::filesystem::path path;
        if (!arp::resource::resolveResourceSourcePath(
                desc.sourceRoot, core::FilePath::utf8(face.sourcePath), desc.origin, path)) {
            diagnostic = "UI-FONT-SET-SOURCE-PATH";
            return false;
        }
        const auto name = core::FilePath::utf8(path);
        if (std::find_if(intents.begin(),
                intents.end(),
                [&](const auto& intent) {
                    return intent.path == name;
                })
            != intents.end())
            continue;
        if (!arp::resource::appendResourceReadIntent(name,
                std::min(desc.maxFontBytes, desc.maxTotalSourceBytes),
                intents,
                diagnostic,
                desc.origin))
            return false;
        intents.back().role = "ui.font.bytes";
    }
    return true;
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
