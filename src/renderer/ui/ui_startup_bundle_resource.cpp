#include "renderer/ui/ui_startup_bundle_resource.h"

#include "reflection/reflection.h"

#include <charconv>
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
    arp::resource::ResourceManager& resourceManager,
    const arp::resource::ResourceLoadContext& context)
{
    arp::resource::ResourceLoadResult result;
    const auto& desc = loadDesc;
    if (!arp::resource::isResourceUuid(desc.resourceId)) {
        result.diagnostics = bundleText("UI startup bundle resource requires UUID resourceId.");
        return result;
    }
    if (!validBundleStatus(desc.status)) {
        result.diagnostics = bundleText("invalid UI startup bundle resource status");
        return result;
    }

    data = arp::core::makeShared<UiStartupBundleResourceData>();

    const auto theme = resourceManager.findByAlias<arp::ui::UiThemeResource>(desc.themeAlias);
    if (!theme.ready() || theme.get() == nullptr || !theme.get()->data) {
        result.diagnostics = theme.diagnostics();
        return result;
    }
    data->theme = theme.get()->data;
    ResourceBase::addDependency(arp::resource::ResourceDependency{
        theme.id(),
        bundleText(arp::ui::contracts::kUsageTheme.data()),
    });

    const auto atlasMetadata =
        resourceManager.findByAlias<arp::ui::UiAtlasMetadataResource>(desc.atlasMetadataAlias);
    if (!atlasMetadata.ready() || atlasMetadata.get() == nullptr || !atlasMetadata.get()->data) {
        result.diagnostics = atlasMetadata.diagnostics();
        return result;
    }
    data->atlasMetadata = atlasMetadata.get()->data;
    ResourceBase::addDependency(arp::resource::ResourceDependency{
        atlasMetadata.id(),
        bundleText(arp::ui::contracts::kUsageAtlasMetadata.data()),
    });

    const auto atlasImage =
        resourceManager.findByAlias<arp::ui::UiAtlasImageResource>(desc.atlasImageAlias);
    if (!atlasImage.ready() || atlasImage.get() == nullptr || !atlasImage.get()->data) {
        result.diagnostics = atlasImage.diagnostics();
        return result;
    }
    data->atlasImage = atlasImage.get()->data;
    ResourceBase::addDependency(arp::resource::ResourceDependency{
        atlasImage.id(),
        bundleText(arp::ui::contracts::kUsageAtlasImage.data()),
    });

    const auto atlasTexture =
        resourceManager.findByAlias<UiAtlasTextureResource>(desc.atlasTextureAlias);
    if (!atlasTexture.ready() || atlasTexture.get() == nullptr || !atlasTexture.get()->data) {
        result.diagnostics = atlasTexture.diagnostics();
        return result;
    }
    data->atlasTexture = atlasTexture.get()->data;
    ResourceBase::addDependency(arp::resource::ResourceDependency{
        atlasTexture.id(),
        bundleText(arp::ui::contracts::kUsageAtlasTexture.data()),
    });

    const auto fontSet = resourceManager.findByAlias<arp::ui::UiFontSetResource>(desc.fontSetAlias);
    if (!fontSet.ready() || fontSet.get() == nullptr || !fontSet.get()->data) {
        result.diagnostics = fontSet.diagnostics();
        return result;
    }
    data->fontSet = fontSet.get()->data;
    ResourceBase::addDependency(arp::resource::ResourceDependency{
        fontSet.id(),
        bundleText(arp::ui::contracts::kUsageFontSet.data()),
    });

    if (desc.documentAliases.empty()) {
        result.diagnostics = bundleText("UI startup bundle requires at least one document alias.");
        return result;
    }
    for (const auto& alias : desc.documentAliases) {
        const auto document = resourceManager.findByAlias<arp::ui::UiDocumentResource>(alias);
        if (!document.ready() || document.get() == nullptr || !document.get()->data) {
            result.diagnostics = document.diagnostics();
            return result;
        }
        data->documents.push_back(document.get()->data);
        ResourceBase::addDependency(arp::resource::ResourceDependency{
            document.id(),
            bundleText(arp::ui::contracts::kUsageDocument.data()),
        });
    }

    return resourceManager.admitResourceMetadata(*this, context.policy);
}

UiStartupBundleResourceLoadResult loadUiStartupBundleResource(
    arp::resource::ResourceManager& resourceManager, const UiStartupBundleResourceLoadDesc& desc)
{
    UiStartupBundleResourceLoadResult result;
    result.resource =
        resourceManager.load<UiStartupBundleResource>(arp::resource::ResourceLoadContext{}, desc);
    if (!result.resource.success) {
        result.diagnostics = result.resource.diagnostics;
        return result;
    }
    result.success = true;
    return result;
}

} // namespace arp::renderer::ui
