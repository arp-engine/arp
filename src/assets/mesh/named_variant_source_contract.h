#pragma once

#include "assets/glb_static_mesh_importer.h"
#include "assets/mesh/glb_scene_source_contract.h"

namespace arp::assets::detail {
// Keep the named profile and diagnostic identities over the shared scene schema.
class NamedVariantSourceContract final {
public:
    static const char* validate(const JsonValue& root,
        const core::pmr::String& binary,
        const NamedVariantSourceImportLimits& limits)
    {
        return GlbSceneSourceContract::validate(
            root, binary, limits.colorPolicy == NamedVariantColorPolicy::PreserveRgbaWind);
    }
};
} // namespace arp::assets::detail
