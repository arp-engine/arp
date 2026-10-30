#include "ui/ui_atlas_image_resource.h"

#include "assets/image_decoder.h"
#include "reflection/reflection.h"
#include "resource/resource_source_io.h"
#include "ui/detail/ui_content_fingerprint.h"
#include "ui/ui_atlas_metadata_resource.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <system_error>
#include <utility>

namespace arp::ui {

namespace {

arp::core::pmr::String atlasImageText(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String pathToText(const std::filesystem::path& path)
{
    return arp::core::FilePath::utf8(path);
}

bool validAtlasImageStatus(const arp::core::pmr::String& status)
{
    return arp::resource::validateResourceStatus({status.data(), status.size()},
        {
            arp::resource::ResourceStatus::ContractComplete,
            arp::resource::ResourceStatus::PrototypeComplete,
        },
        "UI atlas image resource")
        .success;
}

arp::core::pmr::String mimeTypeForAtlasImagePath(const std::filesystem::path& path)
{
    auto extension = arp::core::pmr::String(
        path.extension().generic_string().c_str(), &arp::core::defaultCpuMemoryResource());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension == ".png") {
        return atlasImageText("image/png");
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return atlasImageText("image/jpeg");
    }
    if (extension == ".tga") {
        return atlasImageText("image/tga");
    }
    return {};
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

arp::core::pmr::String atlasImagePayloadYaml(const arp::core::pmr::String& resourceType,
    const UiAtlasImageResourceLoadDesc& desc,
    const UiAtlasImageResourceData& data)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "type: ";
    yaml += resourceType;
    yaml += "\nfields:\n";
    yaml += "  contractVersion: ";
    yaml += kUiAtlasImageResourceContract;
    yaml += "\n  atlasResourceId: ";
    yaml += data.atlasResourceId;
    yaml += "\n  sourceRoot: ";
    yaml += pathToText(desc.sourceRoot);
    yaml += "\n  sourcePath: ";
    yaml += pathToText(data.sourcePath);
    yaml += "\n  width: ";
    appendUnsigned(yaml, data.width);
    yaml += "\n  height: ";
    appendUnsigned(yaml, data.height);
    yaml += "\n  srgb: ";
    yaml += data.srgb ? "true" : "false";
    yaml += "\n  rgba8Bytes: ";
    appendUnsigned(yaml, data.rgba8.size());
    yaml += "\n";
    return yaml;
}

arp::core::pmr::String atlasImageEnvelopeYaml(const UiAtlasImageResourceLoadDesc& desc,
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
    yaml += "displayName: UI Atlas Image Resource\n";
    yaml += "status: ";
    yaml += desc.status;
    yaml += "\ncontentHash: ";
    appendUnsigned(yaml, contentHash);
    yaml += "\ndependencies:\n";
    yaml += "  - id: ";
    yaml += atlasMetadataManagerResourceId;
    yaml += "\n    usage: ";
    yaml += contracts::kUsageAtlasMetadata;
    yaml += "\n";
    yaml += "payload:\n";
    appendIndentedBlock(yaml, payloadYaml, "  ");
    return yaml;
}

void registerUiAtlasImageResourceReflection()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    (void)arp::reflection::registration::class_<UiAtlasImageResource>{
        kUiAtlasImageResourceType,
    };
    arp::reflection::class_of<UiAtlasImageResource>()->set_name(kUiAtlasImageResourceType);
    registered = true;
}

} // namespace

void UiAtlasImageResource::appendContentFingerprint(core::StableHash64& hasher) const noexcept
{
    detail::UiContentFingerprint hash(hasher);
    hash.resource(*this, kUiAtlasImageResourceContract);
    hash.number(data != nullptr);
    if (!data)
        return;
    hash.text(data->atlasResourceId);
    hash.path(data->sourcePath);
    hash.number(data->width);
    hash.number(data->height);
    hash.number(data->srgb);
    hash.number(data->rgba8.size());
    hasher.appendBytes(data->rgba8.data(), data->rgba8.size());
}

arp::resource::ResourceLoadResult UiAtlasImageResource::load(
    arp::resource::ResourceManager& resourceManager,
    const arp::resource::ResourceLoadContext& context)
{
    arp::resource::ResourceLoadResult result;
    const auto& desc = loadDesc;
    if (!arp::resource::isResourceUuid(desc.resourceId)) {
        result.diagnostics = atlasImageText("UI atlas image resource requires UUID resourceId.");
        return result;
    }
    if (!validAtlasImageStatus(desc.status)) {
        result.diagnostics = atlasImageText("invalid UI atlas image resource status");
        return result;
    }
    if (desc.origin.pathScope == arp::core::io::FilePathScope::HostFilesystem
        && desc.sourceRoot.empty()) {
        result.diagnostics =
            atlasImageText("UI atlas image resource requires explicit sourceRoot.");
        return result;
    }
    if (desc.atlasMetadataAlias.empty()) {
        result.diagnostics = atlasImageText("UI atlas image resource requires atlasMetadataAlias.");
        return result;
    }

    const auto metadataHandle =
        resourceManager.findByAlias<UiAtlasMetadataResource>(desc.atlasMetadataAlias);
    if (!metadataHandle.ready() || metadataHandle.get() == nullptr || !metadataHandle.get()->data) {
        result.diagnostics = metadataHandle.diagnostics().empty()
            ? atlasImageText("UI atlas image metadata resource is not ready.")
            : metadataHandle.diagnostics();
        return result;
    }
    const auto& metadata = *metadataHandle.get()->data;

    arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent> intents;
    if (!collectFileReadIntents(resourceManager, intents, result.diagnostics))
        return result;
    const auto resolvedSourcePath = std::filesystem::u8path(intents.front().path);
    const auto mimeType = mimeTypeForAtlasImagePath(resolvedSourcePath);
    if (mimeType.empty()) {
        result.diagnostics = atlasImageText("UI atlas image source mime type is unsupported: ");
        result.diagnostics += resolvedSourcePath.generic_string().c_str();
        return result;
    }

    if (!desc.maxDecodedBytes || desc.maxDecodedBytes == SIZE_MAX) {
        result.diagnostics = "UI-ATLAS-IMAGE-DECODE-BUDGET";
        return result;
    }
    const auto validDimension = [](float value) {
        return std::isfinite(value) && value > 0 && std::floor(value) == value
            && static_cast<double>(value) <= UINT32_MAX;
    };
    if (!validDimension(metadata.width) || !validDimension(metadata.height)) {
        result.diagnostics = "UI-ATLAS-IMAGE-DIMENSIONS";
        return result;
    }
    const auto width = static_cast<std::uint32_t>(metadata.width);
    const auto height = static_cast<std::uint32_t>(metadata.height);
    const auto budget = context.maxDecodedBytes
        ? std::min(desc.maxDecodedBytes, context.maxDecodedBytes)
        : desc.maxDecodedBytes;
    const auto pixels = std::uint64_t(width) * height;
    if (pixels > budget / 4) {
        result.diagnostics = "UI-ATLAS-IMAGE-DECODE-BUDGET";
        return result;
    }
    auto source = arp::resource::readResourceSource(
        intents.front().path, context, desc.maxEncodedBytes, false, &desc.origin);
    if (!source.success) {
        result.diagnostics = std::move(source.diagnostics);
        return result;
    }

    auto decoded = assets::decodeImageToRgba8(
        arp::core::ArrayView<const std::uint8_t>(source.bytes.data(), source.bytes.size()),
        mimeType,
        metadata.srgb,
        static_cast<std::size_t>(pixels * 4));
    if (!decoded.success) {
        result.diagnostics = atlasImageText("UI atlas image decode failed: ");
        result.diagnostics += decoded.diagnostics.c_str();
        return result;
    }
    if (decoded.image.width != width || decoded.image.height != height
        || decoded.image.srgb != metadata.srgb) {
        result.diagnostics = atlasImageText("UI atlas image decoded metadata mismatch.");
        return result;
    }
    if (context.maxDecodedBytes != 0u && decoded.image.rgba8.size() > context.maxDecodedBytes) {
        result.diagnostics = atlasImageText("UI atlas image decoded byte budget exceeded.");
        return result;
    }

    data = arp::core::makeShared<UiAtlasImageResourceData>();
    data->atlasResourceId = metadata.resourceId;
    data->sourcePath = metadata.sourcePath;
    data->width = decoded.image.width;
    data->height = decoded.image.height;
    data->srgb = decoded.image.srgb;
    data->rgba8 = std::move(decoded.image.rgba8);

    addDependency(arp::resource::ResourceDependency{
        metadataHandle.id(),
        atlasImageText(contracts::kUsageAtlasMetadata.data()),
    });
    return resourceManager.admitResourceMetadata(*this, context.policy);
}

bool UiAtlasImageResource::collectFileReadIntents(arp::resource::ResourceManager& resourceManager,
    arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
    arp::core::pmr::String& diagnostics) const
{
    const auto metadataHandle =
        resourceManager.findByAlias<UiAtlasMetadataResource>(loadDesc.atlasMetadataAlias);
    if (!metadataHandle.ready() || metadataHandle.get() == nullptr || !metadataHandle.get()->data) {
        diagnostics = metadataHandle.diagnostics().empty()
            ? atlasImageText("UI atlas image metadata resource is not ready.")
            : metadataHandle.diagnostics();
        return false;
    }
    if (loadDesc.origin.pathScope == arp::core::io::FilePathScope::HostFilesystem
        && loadDesc.sourceRoot.empty()) {
        diagnostics = "UI-ATLAS-IMAGE-ROOT-REQUIRED";
        return false;
    }
    std::filesystem::path resolvedSourcePath;
    if (!arp::resource::resolveResourceSourcePath(loadDesc.sourceRoot,
            pathToText(metadataHandle.get()->data->sourcePath),
            loadDesc.origin,
            resolvedSourcePath)) {
        diagnostics = "UI-ATLAS-IMAGE-SOURCE-PATH";
        return false;
    }
    if (!arp::resource::appendResourceReadIntent(pathToText(resolvedSourcePath),
            loadDesc.maxEncodedBytes,
            intents,
            diagnostics,
            loadDesc.origin))
        return false;
    intents.back().role = atlasImageText("ui.atlas.image.encoded");
    return true;
}

UiAtlasImageResourceLoadResult loadUiAtlasImageResource(
    arp::resource::ResourceManager& resourceManager, const UiAtlasImageResourceLoadDesc& desc)
{
    UiAtlasImageResourceLoadResult result;
    result.resource =
        resourceManager.load<UiAtlasImageResource>(arp::resource::ResourceLoadContext{}, desc);
    if (!result.resource.success) {
        result.diagnostics = result.resource.diagnostics;
        return result;
    }
    result.success = true;
    return result;
}

} // namespace arp::ui
