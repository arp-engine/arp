#pragma once

#include "3d/vegetation/template.h"
#include "assets/asset_pipeline.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "resource/resource_contract.h"
#include "resource/resource_manager.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>

namespace arp::resource {

inline constexpr ResourceContractDescriptor kVegetationCatalogResourceContract{
    "VegetationCatalogResource",
    "vegetation.catalog/v2",
};

struct VegetationCatalogSourceInfo {
    core::pmr::String creator{&core::defaultCpuMemoryResource()};
    core::pmr::String pack{&core::defaultCpuMemoryResource()};
    core::pmr::String sourceUrl{&core::defaultCpuMemoryResource()};
    core::pmr::String downloadUrl{&core::defaultCpuMemoryResource()};
    core::pmr::String upstreamPage{&core::defaultCpuMemoryResource()};
    core::pmr::String license{&core::defaultCpuMemoryResource()};
    core::pmr::String verifiedOn{&core::defaultCpuMemoryResource()};
    std::uint32_t upstreamContentLengthBytes = 0u;
    std::uint32_t verifiedZipBytes = 0u;
};

struct VegetationCatalogImportedInfo {
    core::pmr::String format{&core::defaultCpuMemoryResource()};
    core::pmr::String generatedBy{&core::defaultCpuMemoryResource()};
};

struct VegetationCatalogValidationInfo {
    arp::reflection::hlist required = arp::reflection::makeEmptyHList();
    arp::reflection::hlist prohibited = arp::reflection::makeEmptyHList();
};

struct VegetationCatalogAssetRecord {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    core::pmr::String source{&core::defaultCpuMemoryResource()};
    core::pmr::String role{&core::defaultCpuMemoryResource()};
    core::pmr::String category{&core::defaultCpuMemoryResource()};
    core::pmr::String heightClass{&core::defaultCpuMemoryResource()};
    // Required by vegetation.catalog/v2. This is intentionally authored
    // metadata: v2 never derives runtime behavior from an imported filename.
    core::pmr::String templateCategory{&core::defaultCpuMemoryResource()};
};

struct VegetationCatalogAssetEntry {
    struct MaterialBinding final {
        std::uint32_t slot = 0u;
        core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    };

    core::pmr::String id{&core::defaultCpuMemoryResource()};
    core::pmr::String source{&core::defaultCpuMemoryResource()};
    core::pmr::String role{&core::defaultCpuMemoryResource()};
    core::pmr::String category{&core::defaultCpuMemoryResource()};
    core::pmr::String heightClass{&core::defaultCpuMemoryResource()};
    vegetation::TemplateCategory templateCategory = vegetation::TemplateCategory::ProceduralGrass;
    std::uint16_t templateId = 0u;
    core::pmr::Vector<MaterialBinding> materials{&core::defaultCpuMemoryResource()};
};

struct VegetationSourceCatalog {
    std::uint32_t contractVersion = 1u;
    core::pmr::String status{&core::defaultCpuMemoryResource()};
    VegetationCatalogSourceInfo source;
    core::pmr::String runtimeCatalog{&core::defaultCpuMemoryResource()};
    VegetationCatalogImportedInfo imported;
    arp::reflection::hlist assets = arp::reflection::makeEmptyHList();
    VegetationCatalogValidationInfo validation;
};

struct VegetationCatalogResourceData {
    core::pmr::String sourceCatalogPath{&core::defaultCpuMemoryResource()};
    core::pmr::String runtimeCatalogPath{&core::defaultCpuMemoryResource()};
    core::pmr::String status{&core::defaultCpuMemoryResource()};
    std::uint32_t contractVersion = 0u;
    VegetationCatalogSourceInfo source;
    VegetationCatalogImportedInfo imported;
    VegetationCatalogValidationInfo validation;
    vegetation::VegetationTemplateConfig templateConfig{};
    core::pmr::Vector<VegetationCatalogAssetEntry> assets{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<vegetation::VegetationTemplate> templates{&core::defaultCpuMemoryResource()};
    core::pmr::UnorderedMap<core::pmr::String, std::uint16_t> templateIdsByAssetId{
        &core::defaultCpuMemoryResource()};

    const VegetationCatalogAssetEntry* findAsset(std::string_view id) const noexcept;
    const vegetation::VegetationTemplate* findTemplate(std::string_view id) const noexcept;
    const VegetationCatalogAssetEntry* findAssetByTemplateId(
        std::uint16_t templateId) const noexcept;
    std::uint16_t familyBase(vegetation::TemplateCategory category) const noexcept;
    std::uint16_t familyVariantCount(vegetation::TemplateCategory category) const noexcept;
    std::uint16_t templateIdFor(std::string_view id) const noexcept;
};

enum class VegetationCatalogInput { LegacySourceCatalog, Cooked };

struct VegetationCatalogResourceLoadDesc {
    std::filesystem::path catalogPath;
    core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String alias{&core::defaultCpuMemoryResource()};
    core::pmr::String status{&core::defaultCpuMemoryResource()};
    VegetationCatalogInput input = VegetationCatalogInput::LegacySourceCatalog;
    ResourceFileOrigin sourceOrigin;
};

struct VegetationCatalogResource : public ResourceBase {
    VegetationCatalogResource() = default;

    explicit VegetationCatalogResource(VegetationCatalogResourceLoadDesc desc)
        : ResourceBase(ResourceReference{
              desc.resourceId,
              core::pmr::String(kVegetationCatalogResourceContract.type.data(),
                  &core::defaultCpuMemoryResource()),
              desc.alias,
              desc.status,
          }),
          loadDesc(std::move(desc))
    {}

    ResourceLoadResult load(
        ResourceManager& resourceManager, const ResourceLoadContext& context) override;
    bool collectFileReadIntents(ResourceManager& manager,
        core::pmr::Vector<ResourceFileReadIntent>& intents,
        core::pmr::String& diagnostics) const override;
    bool planFileReadIntents(ResourceManager& manager,
        const ResourceLoadContext& context,
        core::pmr::Vector<ResourceFileReadIntent>& intents,
        core::pmr::String& diagnostics) const override;
    std::size_t processedByteCount() const noexcept override;
    void appendContentFingerprint(core::StableHash64& hasher) const noexcept override;

    ResourceGraphStage processingStage() const noexcept override
    {
        return ResourceGraphStage::TypedProcessing;
    }

    std::shared_ptr<VegetationCatalogResourceData> data;
    VegetationCatalogResourceLoadDesc loadDesc;

private:
    std::size_t cookedBytes_ = 0u;
    std::uint64_t cookedFingerprint_ = 0u;
};

struct VegetationCatalogResourceLoadResult {
    bool success = false;
    ResourceLoadResult resource;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

VegetationCatalogResourceLoadResult loadVegetationCatalogResource(
    ResourceManager& resourceManager, const VegetationCatalogResourceLoadDesc& desc);

} // namespace arp::resource
