#pragma once

#include "core/api.h"

namespace arp::assets {

struct AnimatedMeshAsset;

/// Converts a right-handed animated mesh to the engine's left-handed space by
/// reflecting X across mesh geometry, skin bind data, and animation channels.
ARP_API void convertAnimatedMeshRightHandedToLeftHanded(AnimatedMeshAsset& asset);

} // namespace arp::assets
