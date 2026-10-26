#pragma once

#include "resource/asset_package_payload.h"
#include "scene/component_factory.h"

namespace arp::scene {
// Registers exactly SceneAssetResource and ScenePrefabResource. The retained
// factory supplies immutable value schemas, never runtime construction here.
ARP_API bool registerSceneAssetPackageCodecs(resource::AssetPackageCodecRegistry&,
    core::SharedPtr<const SceneComponentFactory>,
    core::pmr::String& diagnostic);
} // namespace arp::scene
