#include "renderer/ui/ui_startup_bundle_resource.h"

#include "reflection/reflection.h"
#include "resource/resource_source_io.h"
#include "ui/ui_font_source.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <new>
#include <system_error>
#include <utility>

namespace arp::renderer::ui {

namespace {

arp::core::pmr::String bundleText(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

bool validBundleStatus(const arp::core::pmr::String& status)
{
    return arp::resource::validateResourceStatus({status.data(), status.size()},
        {
            arp::resource::ResourceStatus::ContractComplete,
            arp::resource::ResourceStatus::PrototypeComplete,
        },
        "UI startup bundle resource")
        .success;
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

void addDependency(arp::core::pmr::Vector<arp::resource::ResourceDependency>& dependencies,
    arp::core::pmr::String id,
    const char* usage)
{
    arp::resource::ResourceDependency dependency;
    dependency.id = std::move(id);
    dependency.usage = bundleText(usage);
    dependencies.push_back(std::move(dependency));
}

arp::core::pmr::String payloadYaml(
    const arp::core::pmr::String& resourceType, const UiStartupBundleResourceData& data)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "type: ";
    yaml += resourceType;
    yaml += "\nfields:\n";
    yaml += "  contractVersion: ";
    yaml += kUiStartupBundleResourceContract;
    yaml += "\n  documentCount: ";
    appendUnsigned(yaml, data.documents.size());
    yaml += "\n";
    return yaml;
}

arp::core::pmr::String envelopeYaml(const UiStartupBundleResourceLoadDesc& desc,
    const arp::core::pmr::String& resourceType,
    const arp::core::pmr::String& payload,
    const arp::core::pmr::Vector<arp::resource::ResourceDependency>& dependencies,
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
    yaml += "displayName: UI Startup Bundle Resource\n";
    yaml += "status: ";
    yaml += desc.status;
    yaml += "\ncontentHash: ";
    appendUnsigned(yaml, contentHash);
    yaml += "\ndependencies:\n";
    for (const auto& dependency : dependencies) {
        yaml += "  - id: ";
        yaml += dependency.id;
        yaml += "\n    usage: ";
        yaml += dependency.usage;
        yaml += "\n";
    }
    yaml += "payload:\n";
    appendIndentedBlock(yaml, payload, "  ");
    return yaml;
}

void registerUiStartupBundleResourceReflection()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    (void)arp::reflection::registration::class_<UiStartupBundleResource>{
        kUiStartupBundleResourceType,
    };
    arp::reflection::class_of<UiStartupBundleResource>()->set_name(kUiStartupBundleResourceType);
    registered = true;
}

} // namespace

arp::resource::ResourceLoadResult UiStartupBundleResource::load(
    arp::resource::ResourceManager& manager, const arp::resource::ResourceLoadContext& context)
{
    using namespace arp;
    resource::ResourceLoadResult result;
    const auto& desc = loadDesc;
    if (!resource::isResourceUuid(desc.resourceId)) {
        result.diagnostics = "UI-BUNDLE-RESOURCE-ID";
        return result;
    }
    if (!validBundleStatus(desc.status)) {
        result.diagnostics = "UI-BUNDLE-STATUS";
        return result;
    }
    if (reference().id != desc.resourceId || reference().type != kUiStartupBundleResourceType
        || reference().alias != desc.alias || reference().status != desc.status) {
        result.diagnostics = "UI-BUNDLE-IDENTITY-MISMATCH";
        return result;
    }
    const auto& budget = desc.budget;
    if (!budget.maxDocuments || budget.maxDocuments == SIZE_MAX || !budget.maxFontFaces
        || budget.maxFontFaces == SIZE_MAX || !budget.maxRetainedPayloadBytes
        || budget.maxRetainedPayloadBytes == SIZE_MAX || desc.documentAliases.empty()
        || desc.documentAliases.size() > budget.maxDocuments) {
        result.diagnostics = "UI-BUNDLE-BUDGET";
        return result;
    }

    // All preparation belongs to this candidate. The live resource is changed
    // only after admission succeeds, via noexcept moves of core-owned storage.
    UiStartupBundleResource candidate(desc);
    candidate.configureMetadata(reference(), dependencies());
    candidate.data = core::makeShared<UiStartupBundleResourceData>();
    auto& prepared = *candidate.data;
    const auto ready = [&](const auto& handle, const core::pmr::String& alias) {
        if (alias.empty() || !handle.ready() || !handle.get() || !handle.get()->data) {
            result.diagnostics = "UI-BUNDLE-CHILD-NOT-READY: ";
            result.diagnostics += alias;
            return false;
        }
        return true;
    };
    const auto linked = [](const resource::ResourceBase& child,
                            const core::pmr::String& id,
                            core::StringView usage) {
        return std::any_of(
            child.dependencies().begin(), child.dependencies().end(), [&](const auto& dependency) {
                return dependency.id == id && dependency.usage == usage;
            });
    };
    const auto theme = manager.findByAlias<arp::ui::UiThemeResource>(desc.themeAlias);
    const auto metadata =
        manager.findByAlias<arp::ui::UiAtlasMetadataResource>(desc.atlasMetadataAlias);
    const auto image = manager.findByAlias<arp::ui::UiAtlasImageResource>(desc.atlasImageAlias);
    const auto texture = manager.findByAlias<UiAtlasTextureResource>(desc.atlasTextureAlias);
    const auto fonts = manager.findByAlias<arp::ui::UiFontSetResource>(desc.fontSetAlias);
    if (!ready(theme, desc.themeAlias) || !ready(metadata, desc.atlasMetadataAlias)
        || !ready(image, desc.atlasImageAlias) || !ready(texture, desc.atlasTextureAlias)
        || !ready(fonts, desc.fontSetAlias))
        return result;
    if (!linked(*theme.get(), metadata.id(), arp::ui::contracts::kUsageAtlasMetadata)
        || !linked(*image.get(), metadata.id(), arp::ui::contracts::kUsageAtlasMetadata)
        || !linked(*fonts.get(), metadata.id(), arp::ui::contracts::kUsageAtlasMetadataFonts)
        || !linked(*texture.get(), image.id(), arp::ui::contracts::kUsageAtlasImage)) {
        result.diagnostics = "UI-BUNDLE-DEPENDENCY-MISMATCH";
        return result;
    }
    prepared.theme = theme.get()->data;
    prepared.atlasMetadata = metadata.get()->data;
    prepared.atlasImage = image.get()->data;
    prepared.atlasTexture = texture.get()->data;
    prepared.fontSet = fonts.get()->data;
    const auto& atlas = *prepared.atlasMetadata;
    const auto& pixels = *prepared.atlasImage;
    const auto& upload = prepared.atlasTexture->atlasTexture;
    if (prepared.theme->atlasResourceId != atlas.resourceId
        || prepared.theme->atlasWidth != atlas.width || prepared.theme->atlasHeight != atlas.height
        || prepared.theme->atlasSrgb != atlas.srgb
        || prepared.theme->atlasSourcePath != atlas.sourcePath
        || pixels.atlasResourceId != atlas.resourceId || pixels.width != atlas.width
        || pixels.height != atlas.height || pixels.srgb != atlas.srgb || !upload.valid()
        || upload.resourceId != atlas.resourceId || upload.extent.width != pixels.width
        || upload.extent.height != pixels.height || upload.srgb != pixels.srgb
        || upload.rgba8 != pixels.rgba8) {
        result.diagnostics = "UI-BUNDLE-ATLAS-MISMATCH";
        return result;
    }
    const auto& faces = prepared.fontSet->fonts;
    if (faces.empty() || faces.size() > budget.maxFontFaces) {
        result.diagnostics = "UI-BUNDLE-FONT-BUDGET";
        return result;
    }
    if (faces.size() != atlas.fontFaces.size()) {
        result.diagnostics = "UI-BUNDLE-FONT-METADATA";
        return result;
    }
    const auto faceFor = [&](const core::pmr::String& token) {
        return std::find_if(faces.begin(), faces.end(), [&](const auto& face) {
            return face.tokenId == token;
        });
    };
    const auto retain = [&](std::size_t bytes) {
        if (bytes > budget.maxRetainedPayloadBytes - prepared.retainedPayloadBytes) {
            result.diagnostics = "UI-BUNDLE-PAYLOAD-BUDGET";
            return false;
        }
        prepared.retainedPayloadBytes += bytes;
        return true;
    };
    if (!retain(pixels.rgba8.size()) || !retain(upload.rgba8.size()))
        return result;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const auto& face = faces[i];
        if (!arp::ui::validatePreparedUiFontFace(face, result.diagnostics))
            return result;
        const auto first = std::find_if(faces.begin(), faces.end(), [&](const auto& other) {
            return other.tokenId == face.tokenId;
        });
        const auto declared =
            std::find_if(atlas.fontFaces.begin(), atlas.fontFaces.end(), [&](const auto& other) {
                return other.tokenId == face.tokenId;
            });
        std::filesystem::path sourcePath;
        if (first != faces.begin() + static_cast<std::ptrdiff_t>(i)
            || declared == atlas.fontFaces.end()
            || !resource::resolveResourceSourcePath(fonts.get()->loadDesc.sourceRoot,
                core::FilePath::utf8(declared->sourcePath),
                fonts.get()->loadDesc.origin,
                sourcePath)
            || face.source->desc().pathScope != fonts.get()->loadDesc.origin.pathScope
            || !(face.source->desc().mount == fonts.get()->loadDesc.origin.mount)
            || sourcePath != face.sourcePath || face.family != declared->family
            || face.weight != declared->weight || face.sizePx != declared->sizePx
            || face.lineHeightPx != declared->lineHeightPx
            || face.renderMode != declared->renderMode) {
            result.diagnostics = "UI-BUNDLE-FONT-METADATA";
            return result;
        }
        for (const auto& fallback : face.fallbackChain) {
            if (faceFor(fallback) == faces.end()) {
                result.diagnostics = "UI-BUNDLE-FALLBACK-TOKEN";
                return result;
            }
        }
        const auto prefixEnd = faces.begin() + static_cast<std::ptrdiff_t>(i);
        const auto owner = std::find_if(faces.begin(), prefixEnd, [&](const auto& previous) {
            return previous.source == face.source;
        });
        if (owner == prefixEnd && !retain(face.source->bytes().size()))
            return result;
    }
    for (const auto& token : prepared.theme->fonts) {
        const auto resolved = faceFor(token.tokenId);
        const auto declared =
            std::find_if(atlas.fontFaces.begin(), atlas.fontFaces.end(), [&](const auto& face) {
                return face.tokenId == token.tokenId;
            });
        if (resolved == faces.end() || declared == atlas.fontFaces.end()
            || token.sourcePath != declared->sourcePath || token.family != resolved->family
            || token.weight != resolved->weight || token.sizePx != resolved->sizePx
            || token.lineHeightPx != resolved->lineHeightPx) {
            result.diagnostics = "UI-BUNDLE-THEME-FONT";
            return result;
        }
    }
    for (const auto& style : prepared.theme->styleFonts) {
        if (faceFor(style.second) == faces.end()) {
            result.diagnostics = "UI-BUNDLE-STYLE-FONT";
            return result;
        }
    }
    prepared.documents.reserve(desc.documentAliases.size());
    core::pmr::Vector<core::pmr::String> documents;
    documents.reserve(desc.documentAliases.size());
    for (const auto& alias : desc.documentAliases) {
        const auto document = manager.findByAlias<arp::ui::UiDocumentResource>(alias);
        if (!ready(document, alias))
            return result;
        if (std::find(documents.begin(), documents.end(), document.id()) != documents.end()) {
            result.diagnostics = "UI-BUNDLE-DOCUMENT-DUPLICATE";
            return result;
        }
        const auto& content = document.get()->data->document;
        for (const auto& entry : content.textures()) {
            const auto& rect = entry.sourceRect;
            if (entry.resourceId != texture.id() || entry.atlasId != atlas.atlasId
                || !std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width)
                || !std::isfinite(rect.height) || !rect.valid() || rect.x < 0 || rect.y < 0
                || rect.x > atlas.width || rect.y > atlas.height
                || rect.width > atlas.width - rect.x || rect.height > atlas.height - rect.y
                || !entry.hasExplicitSrgb || entry.srgb != atlas.srgb
                || entry.alphaMode != arp::ui::UiTextureAlphaMode::Straight
                || entry.mipIntent != arp::ui::UiTextureMipIntent::None
                || entry.compressionIntent != arp::ui::UiTextureCompressionIntent::UiLossless
                || entry.samplerIntent != arp::ui::UiTextureSamplerIntent::LinearClamp) {
                result.diagnostics = "UI-BUNDLE-DOCUMENT-TEXTURE";
                return result;
            }
        }
        for (const auto& node : content.nodes()) {
            auto token = node.fontToken;
            if (token.empty() && !node.text.empty()) {
                const auto style = prepared.theme->styleFonts.find(node.styleId);
                if (style != prepared.theme->styleFonts.end())
                    token = style->second;
            }
            if ((!node.text.empty() || !token.empty()) && faceFor(token) == faces.end()) {
                result.diagnostics = "UI-BUNDLE-DOCUMENT-FONT";
                return result;
            }
        }
        documents.push_back(document.id());
        prepared.documents.push_back(document.get()->data);
        candidate.addDependency(
            {document.id(), bundleText(arp::ui::contracts::kUsageDocument.data())});
    }
    candidate.addDependency({theme.id(), bundleText(arp::ui::contracts::kUsageTheme.data())});
    candidate.addDependency(
        {metadata.id(), bundleText(arp::ui::contracts::kUsageAtlasMetadata.data())});
    candidate.addDependency({image.id(), bundleText(arp::ui::contracts::kUsageAtlasImage.data())});
    candidate.addDependency(
        {texture.id(), bundleText(arp::ui::contracts::kUsageAtlasTexture.data())});
    candidate.addDependency({fonts.id(), bundleText(arp::ui::contracts::kUsageFontSet.data())});
    result = manager.admitResourceMetadata(candidate, context.policy);
    if (result.success) {
        ResourceBase::operator=(std::move(candidate));
        data = std::move(candidate.data);
    }
    return result;
}

UiStartupBundleResourceLoadResult loadUiStartupBundleResource(
    arp::resource::ResourceManager& manager, const UiStartupBundleResourceLoadDesc& desc)
{
    UiStartupBundleResourceLoadResult result;
    try {
        // Allocate response identity before the registry transaction can commit.
        result.resource.resourceId = desc.resourceId;
        result.resource.resourceType = kUiStartupBundleResourceType;
        arp::core::pmr::Vector<arp::core::UniquePtr<arp::resource::ResourceBase>> candidates;
        candidates.push_back(arp::core::makeUnique<UiStartupBundleResource>(desc));
        auto loaded = manager.loadResourcesAtomically(std::move(candidates), {});
        result.success = result.resource.success = loaded.success;
        result.resource.state = loaded.success ? arp::resource::ResourceState::Ready
                                               : arp::resource::ResourceState::Failed;
        result.resource.diagnostics = std::move(loaded.diagnostics);
        result.diagnostics = result.resource.diagnostics;
        result.diagnosticId = loaded.success ? nullptr : "UI-BUNDLE-LOAD-FAILED";
    } catch (const std::bad_alloc&) {
        result.success = result.resource.success = false;
        result.resource.state = arp::resource::ResourceState::Failed;
        result.resource.diagnostics.clear();
        result.diagnostics.clear();
        result.diagnosticId = "UI-BUNDLE-ALLOCATION";
    }
    return result;
}

} // namespace arp::renderer::ui
