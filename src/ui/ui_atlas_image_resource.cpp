#include "ui/ui_atlas_image_resource.h"

#include "assets/image_decoder.h"
#include "reflection/reflection.h"
#include "ui/ui_atlas_metadata_resource.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
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
    using StringAllocator = arp::core::pmr::String::allocator_type;
    return path.generic_string<char, std::char_traits<char>, StringAllocator>(StringAllocator{
        &arp::core::defaultCpuMemoryResource(),
    });
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

bool readBinaryFile(const std::filesystem::path& path,
    const arp::resource::ResourceLoadContext& context,
    arp::core::pmr::Vector<std::uint8_t>& out,
    arp::core::pmr::String& diagnostics)
{
    const auto pathText = pathToText(path);
    if (context.sources != nullptr) {
        const auto* source = context.sources->find(pathText);
        if (source != nullptr) {
            out = source->bytes;
            if (out.empty()) {
                diagnostics = atlasImageText("UI atlas image preloaded source is empty: ");
                diagnostics += pathText;
                return false;
            }
            return true;
        }
    }
    if (context.requirePreloadedSources) {
        diagnostics = atlasImageText("UI atlas image requires preloaded source: ");
        diagnostics += pathText;
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        diagnostics = atlasImageText("UI atlas image source file is missing: ");
        diagnostics += path.generic_string().c_str();
        return false;
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0) {
        diagnostics = atlasImageText("UI atlas image source file is empty: ");
        diagnostics += path.generic_string().c_str();
        return false;
    }
    input.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(out.data()), size);
    if (!input.good()) {
        diagnostics = atlasImageText("UI atlas image source file is unreadable: ");
        diagnostics += path.generic_string().c_str();
        out.clear();
        return false;
    }
    return true;
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

std::filesystem::path resolveDeclaredAtlasImagePath(
    const std::filesystem::path& sourceRoot, const std::filesystem::path& declaredPath)
{
    if (declaredPath.is_absolute()) {
        return declaredPath;
    }
    return sourceRoot / declaredPath;
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
    if (desc.sourceRoot.empty()) {
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

    const auto resolvedSourcePath =
        resolveDeclaredAtlasImagePath(desc.sourceRoot, metadata.sourcePath);
    const auto mimeType = mimeTypeForAtlasImagePath(resolvedSourcePath);
    if (mimeType.empty()) {
        result.diagnostics = atlasImageText("UI atlas image source mime type is unsupported: ");
        result.diagnostics += resolvedSourcePath.generic_string().c_str();
        return result;
    }

    arp::core::pmr::Vector<std::uint8_t> encodedBytes(&arp::core::defaultCpuMemoryResource());
    if (!readBinaryFile(resolvedSourcePath, context, encodedBytes, result.diagnostics)) {
        return result;
    }

    const auto decoded = assets::decodeImageToRgba8(encodedBytes, mimeType, metadata.srgb);
    if (!decoded.success) {
        result.diagnostics = atlasImageText("UI atlas image decode failed: ");
        result.diagnostics += decoded.diagnostics.c_str();
        return result;
    }
    if (decoded.image.width != static_cast<std::uint32_t>(metadata.width)
        || decoded.image.height != static_cast<std::uint32_t>(metadata.height)
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
    data->rgba8.assign(decoded.image.rgba8.begin(), decoded.image.rgba8.end());

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
    const auto resolvedSourcePath =
        resolveDeclaredAtlasImagePath(loadDesc.sourceRoot, metadataHandle.get()->data->sourcePath);
    arp::resource::ResourceFileReadIntent intent;
    intent.pathScope = arp::core::io::FilePathScope::HostFilesystem;
    intent.path = pathToText(resolvedSourcePath);
    intent.role = atlasImageText("ui.atlas.image.encoded");
    intent.maxBytes = 64u * 1024u * 1024u;
    intents.push_back(std::move(intent));
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
