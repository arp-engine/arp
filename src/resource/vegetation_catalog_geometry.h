#pragma once

#include "3d/vegetation/template_geometry_contract.h"
#include "resource/vegetation_catalog_resource.h"

namespace arp::resource {

// Catalog resource identity and explicit material slots, independent of any GPU.
class VegetationCatalogGeometryContract final {
public:
    static const char* validateIndex(const VegetationCatalogResourceData& catalog) noexcept
    {
        if (catalog.contractVersion != 1u || catalog.assets.empty()
            || catalog.assets.size() >= UINT16_MAX
            || catalog.assets.size() != catalog.templates.size()
            || catalog.assets.size() != catalog.templateIdsByAssetId.size())
            return "VEGETATION-GEOMETRY-CATALOG-INDEX";
        for (std::size_t i = 0u; i < catalog.assets.size(); ++i) {
            const auto& asset = catalog.assets[i];
            const auto& geometry = catalog.templates[i];
            const auto entry = catalog.templateIdsByAssetId.find(asset.id);
            if (asset.id.empty() || asset.id.find('\0') != core::pmr::String::npos
                || asset.templateId != i + 1u || geometry.id != asset.templateId
                || asset.templateCategory != geometry.category
                || entry == catalog.templateIdsByAssetId.end() || entry->second != asset.templateId)
                return "VEGETATION-GEOMETRY-CATALOG-INDEX";
        }
        return nullptr;
    }

    static const VegetationCatalogAssetEntry::MaterialBinding* findMaterialBinding(
        const VegetationCatalogAssetEntry& asset, std::uint32_t slot) noexcept
    {
        for (const auto& binding : asset.materials)
            if (binding.slot == slot)
                return &binding;
        return nullptr;
    }

    static const char* validateBindings(const vegetation::VegetationTemplate& geometry,
        const VegetationCatalogAssetEntry& asset) noexcept
    {
        if (const auto* diagnostic = vegetation::TemplateGeometryContract::validate(geometry))
            return diagnostic;
        if (geometry.id != asset.templateId || geometry.category != asset.templateCategory)
            return "VEGETATION-GEOMETRY-TEMPLATE-IDENTITY";
        if (asset.materials.empty() || asset.materials.size() > geometry.sections.size())
            return "VEGETATION-GEOMETRY-MATERIAL-BINDINGS";
        for (std::size_t i = 0u; i < asset.materials.size(); ++i) {
            const auto& binding = asset.materials[i];
            if (binding.slot == UINT32_MAX || !isResourceUuid(binding.resourceId)
                || (i && asset.materials[i - 1u].slot >= binding.slot))
                return "VEGETATION-GEOMETRY-MATERIAL-BINDINGS";
            if (std::none_of(
                    geometry.sections.begin(), geometry.sections.end(), [&](const auto& section) {
                        return section.materialSlot == binding.slot;
                    }))
                return "VEGETATION-GEOMETRY-MATERIAL-UNUSED";
        }
        for (const auto& section : geometry.sections)
            if (!findMaterialBinding(asset, section.materialSlot))
                return "VEGETATION-GEOMETRY-MATERIAL-MISSING";
        return nullptr;
    }
};

} // namespace arp::resource
