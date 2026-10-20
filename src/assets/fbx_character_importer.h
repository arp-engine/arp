#pragma once

#include "assets/glb_animated_mesh_importer.h"
#include "core/api.h"

#include <filesystem>

namespace arp::assets {

struct FbxCharacterImportOptions {
    bool importMesh = true;
    bool importAnimations = true;
    bool flipUvY = false;
    /// Explicit source metadata: reflect X across geometry, skin, and clips.
    bool convertToLeftHanded = false;
};

class ARP_API FbxCharacterImporter {
public:
    bool assimpAvailable() const;

    AnimatedMeshImportResult importFile(
        const std::filesystem::path& path, const FbxCharacterImportOptions& options = {}) const;
};

} // namespace arp::assets
