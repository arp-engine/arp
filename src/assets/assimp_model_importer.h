#pragma once

#include "assets/glb_animated_mesh_importer.h"
#include "core/api.h"

#include <filesystem>

namespace arp::assets {

struct AssimpModelImportOptions {
    bool importMesh = true;
    bool importAnimations = true;
};

class ARP_API AssimpModelImporter {
public:
    bool assimpAvailable() const;

    AnimatedMeshImportResult importFile(
        const std::filesystem::path& path, const AssimpModelImportOptions& options = {}) const;
};

} // namespace arp::assets
