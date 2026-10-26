#pragma once

#include "editor/project/project_manifest.h"

namespace arp::editor {

// Authoring-only references; never included in exported runtime startup data.
struct ProjectMetadata {
    core::pmr::Vector<core::pmr::String> terrainPreviews;
};

class ProjectMetadataCodec final {
public:
    static constexpr std::uint32_t contractVersion = 1;

    static bool decode(std::string_view yaml,
        ProjectMetadata& destination,
        ProjectDiagnostic& diagnostic,
        ProjectManifestBudget budget = {});
};

} // namespace arp::editor
