#include "renderer/ui/ui_atlas_texture_resource.h"

#include "reflection/reflection.h"
#include "ui/ui_atlas_image_resource.h"

#include <charconv>
#include <iostream>
#include <system_error>
#include <utility>

namespace arp::renderer::ui {

namespace {

arp::core::pmr::String textureResourceText(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

bool validTextureResourceStatus(const arp::core::pmr::String& status)
{
    return arp::resource::validateResourceStatus({status.data(), status.size()},
        {
            arp::resource::ResourceStatus::PrototypeComplete,
            arp::resource::ResourceStatus::BackendValidated,
        },
        "UI atlas texture resource")
        .success;
}

bool validateIntent(const UiAtlasTextureUploadIntent& intent, arp::core::pmr::String& diagnostics)
{
    if (intent.resourceUsage != "ui.atlas.sampled") {
        diagnostics = textureResourceText(
            "UI atlas texture resource requires resourceUsage ui.atlas.sampled.");
        return false;
    }
    if (intent.uploadPolicy != "renderer-owned-staging") {
        diagnostics = textureResourceText(
            "UI atlas texture resource requires renderer-owned-staging uploadPolicy.");
        return false;
    }
    if (intent.requiredFormat != "Rgba8Unorm") {
        diagnostics = textureResourceText("UI atlas texture resource requires Rgba8Unorm format.");
        return false;
    }
    if (intent.requiredDescriptorSet != "UiAtlasSet") {
        diagnostics = textureResourceText(
            "UI atlas texture resource requires UiAtlasSet descriptor binding.");
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

arp::core::pmr::String texturePayloadYaml(
    const arp::core::pmr::String& resourceType, const UiAtlasTextureResourceData& data)
{
    arp::core::pmr::String yaml(&arp::core::defaultCpuMemoryResource());
    yaml += "type: ";
    yaml += resourceType;
    yaml += "\nfields:\n";
    yaml += "  contractVersion: ";
    yaml += kUiAtlasTextureResourceContract;
    yaml += "\n  resourceUsage: ";
    yaml += data.intent.resourceUsage;
    yaml += "\n  uploadPolicy: ";
    yaml += data.intent.uploadPolicy;
    yaml += "\n  requiredFormat: ";
    yaml += data.intent.requiredFormat;
    yaml += "\n  requiredDescriptorSet: ";
    yaml += data.intent.requiredDescriptorSet;
    yaml += "\n  backendOwnedUploadStaging: ";
    yaml += data.intent.backendOwnedUploadStaging ? "true" : "false";
    yaml += "\n  atlasResourceId: ";
    yaml += data.atlasTexture.resourceId.c_str();
    yaml += "\n  width: ";
    appendUnsigned(yaml, data.atlasTexture.extent.width);
    yaml += "\n  height: ";
    appendUnsigned(yaml, data.atlasTexture.extent.height);
    yaml += "\n  rgba8Bytes: ";
    appendUnsigned(yaml, data.atlasTexture.rgba8.size());
    yaml += "\n";
    return yaml;
}

arp::core::pmr::String textureEnvelopeYaml(const UiAtlasTextureResourceLoadDesc& desc,
    const arp::core::pmr::String& resourceType,
    const arp::core::pmr::String& payloadYaml,
    const arp::core::pmr::String& imageManagerResourceId,
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
    yaml += "displayName: UI Atlas Texture Resource\n";
    yaml += "status: ";
    yaml += desc.status;
    yaml += "\ncontentHash: ";
    appendUnsigned(yaml, contentHash);
    yaml += "\ndependencies:\n";
    yaml += "  - id: ";
    yaml += imageManagerResourceId;
    yaml += "\n    usage: ";
    yaml += arp::ui::contracts::kUsageAtlasImage;
    yaml += "\n";
    yaml += "payload:\n";
    appendIndentedBlock(yaml, payloadYaml, "  ");
    return yaml;
}

void registerUiAtlasTextureResourceReflection()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    (void)arp::reflection::registration::class_<UiAtlasTextureResource>{
        kUiAtlasTextureResourceType,
    };
    arp::reflection::class_of<UiAtlasTextureResource>()->set_name(kUiAtlasTextureResourceType);
    registered = true;
}

} // namespace

arp::resource::ResourceLoadResult UiAtlasTextureResource::load(
    arp::resource::ResourceManager& resourceManager,
    const arp::resource::ResourceLoadContext& context)
{
    std::cerr << "[ui-atlas-texture] resource load begin\n";
    arp::resource::ResourceLoadResult result;
    const auto& desc = loadDesc;
    if (!arp::resource::isResourceUuid(desc.resourceId)) {
        result.diagnostics =
            textureResourceText("UI atlas texture resource requires UUID resourceId.");
        return result;
    }
    if (!validTextureResourceStatus(desc.status)) {
        result.diagnostics = textureResourceText("invalid UI atlas texture resource status");
        return result;
    }
    if (desc.atlasImageAlias.empty()) {
        result.diagnostics =
            textureResourceText("UI atlas texture resource requires atlasImageAlias.");
        return result;
    }

    const auto imageHandle =
        resourceManager.findByAlias<arp::ui::UiAtlasImageResource>(desc.atlasImageAlias);
    if (!imageHandle.ready() || imageHandle.get() == nullptr || !imageHandle.get()->data) {
        result.diagnostics = imageHandle.diagnostics().empty()
            ? textureResourceText("UI atlas texture image resource is not ready.")
            : imageHandle.diagnostics();
        return result;
    }

    if (!validateIntent(desc.intent, result.diagnostics)) {
        return result;
    }

    const auto& image = *imageHandle.get()->data;
    data = arp::core::makeShared<UiAtlasTextureResourceData>();
    data->intent = desc.intent;
    data->intent.backendOwnedUploadStaging = true;
    data->exposesNativeBackendHandles = false;
    data->atlasTexture.resourceId = image.atlasResourceId.c_str();
    data->atlasTexture.extent = {image.width, image.height};
    data->atlasTexture.srgb = image.srgb;
    data->atlasTexture.rgba8 = image.rgba8;
    if (!data->atlasTexture.valid()) {
        result.diagnostics =
            textureResourceText("UI atlas texture resource received invalid RGBA8 payload.");
        return result;
    }

    addDependency(arp::resource::ResourceDependency{
        imageHandle.id(),
        textureResourceText(arp::ui::contracts::kUsageAtlasImage.data()),
    });
    return resourceManager.admitResourceMetadata(*this, context.policy);
}

UiAtlasTextureResourceLoadResult loadUiAtlasTextureResource(
    arp::resource::ResourceManager& resourceManager, const UiAtlasTextureResourceLoadDesc& desc)
{
    UiAtlasTextureResourceLoadResult result;
    std::cerr << "[ui-atlas-texture] typed load begin rid=" << desc.resourceId.c_str()
              << " alias=" << desc.alias.c_str() << " imageAlias=" << desc.atlasImageAlias.c_str()
              << '\n';
    auto typed = arp::core::makeUnique<UiAtlasTextureResource>(desc);
    std::cerr << "[ui-atlas-texture] resource object=" << typed.get() << '\n';
    arp::core::UniquePtr<arp::resource::ResourceBase> base{std::move(typed)};
    result.resource =
        resourceManager.loadResource(std::move(base), arp::resource::ResourceLoadContext{});
    std::cerr << "[ui-atlas-texture] loadResource end success=" << result.resource.success << '\n';
    if (!result.resource.success) {
        result.diagnostics = result.resource.diagnostics;
        return result;
    }
    result.success = true;
    return result;
}

} // namespace arp::renderer::ui
