#pragma once

#include "core/memory/cpu_allocator.h"
#include "resource/resource_manager.h"
#include "ui/ui_document.h"
#include "ui/ui_resource_contracts.h"

#include <filesystem>
#include <memory>
#include <utility>

namespace arp::ui {

inline constexpr const char* kUiDocumentResourceType = contracts::kDocument.type.data();
inline constexpr const char* kUiDocumentResourceContract =
    contracts::kDocument.contractVersion.data();

struct UiDocumentRuntimeBinding {
    arp::core::pmr::String sourceId{&arp::core::defaultCpuMemoryResource()};
    WidgetId widgetId;
    LayoutRect rect;
    arp::core::pmr::String eventRole{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    std::uint64_t order = 0u;
    bool visible = false;
    bool enabled = false;
};

struct UiDocumentResourceData {
    std::filesystem::path sourcePath;
    arp::core::pmr::String documentContract{&arp::core::defaultCpuMemoryResource()};
    UiDocument document;
    DrawPlan drawPlan;
    arp::core::pmr::Vector<UiDocumentRuntimeBinding> bindings{
        &arp::core::defaultCpuMemoryResource()};
};

struct UiDocumentResourceLoadDesc {
    std::filesystem::path documentPath;
    arp::core::pmr::String documentContract{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String alias{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String status{&arp::core::defaultCpuMemoryResource()};
    // Preserve the previous graph read cap; callers may select a smaller finite cap.
    std::size_t maxDocumentBytes = 4u * 1024u * 1024u;
    arp::resource::ResourceFileOrigin origin;
};

struct UiDocumentResource : public arp::resource::ResourceBase {
    UiDocumentResource() = default;

    explicit UiDocumentResource(UiDocumentResourceLoadDesc desc)
        : arp::resource::ResourceBase(arp::resource::ResourceReference{
              desc.resourceId,
              arp::core::pmr::String(
                  kUiDocumentResourceType, &arp::core::defaultCpuMemoryResource()),
              desc.alias,
              desc.status,
          }),
          loadDesc(std::move(desc))
    {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& resourceManager,
        const arp::resource::ResourceLoadContext& context) override;
    void appendContentFingerprint(core::StableHash64& hash) const noexcept override;
    bool collectFileReadIntents(arp::resource::ResourceManager& resourceManager,
        arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
        arp::core::pmr::String& diagnostics) const override;

    arp::resource::ResourceGraphStage processingStage() const noexcept override
    {
        return arp::resource::ResourceGraphStage::YamlParse;
    }

    std::shared_ptr<UiDocumentResourceData> data;
    UiDocumentResourceLoadDesc loadDesc;
};

struct UiDocumentResourceLoadResult {
    bool success = false;
    arp::resource::ResourceLoadResult resource;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

UiDocumentResourceLoadResult loadUiDocumentResource(
    arp::resource::ResourceManager& resourceManager, const UiDocumentResourceLoadDesc& desc);

} // namespace arp::ui
