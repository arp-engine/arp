#pragma once
// Legacy source compatibility. Internal runtime code uses canonical domains.
#include "3d/lod/lod_profile.h"
#include "3d/terrain/terrain_lod.h"
#include "3d/vegetation/animation_lod.h"

namespace arp::pcg {
using lod::LodProfile;
using lod::LodTier;
using terrain::terrainLodProfile;
using vegetation::tierToWindClass;
} // namespace arp::pcg
