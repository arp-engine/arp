#pragma once

#include "core/encoding/yaml_contract.h"
#include "resource/vegetation_catalog_resource.h"

namespace arp::editor {
// Editor authoring schema only. Resource-owned validators/planners retain all
// import, metadata, lifetime and acquisition semantics.
class EditorTerrainPreviewVegetation final {
public:
    enum class Input { Absent, LegacyV2, SourcesV2 };

    static EditorTerrainPreviewVegetation decode(
        const core::YamlContract::Node&, core::StringView previewContract);
    core::UniquePtr<resource::VegetationCatalogResource> sourceResource(
        const std::filesystem::path& root, resource::ResourceFileOrigin) const;

    Input input = Input::Absent;
    core::pmr::String resourceId, catalog;

private:
    std::optional<resource::VegetationCatalogSourceLoadLimits> limits_;
};
} // namespace arp::editor
