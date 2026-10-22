#include "3d/lod/lod_profile.h"
#include "3d/terrain/terrain_lod.h"
#include "3d/vegetation/animation_lod.h"

#include <cassert>
#include <cstdio>

int main()
{
    const auto profile = arp::terrain::terrainLodProfile(64.0f);
    int tier = -1;
    for (float distance : {0.0f, 150.0f, 350.0f, 700.0f, 350.0f, 100.0f, 0.0f}) {
        tier = profile.resolveTier(distance, tier);
        assert(tier >= 0 && tier < profile.tierCount);
        assert(arp::vegetation::tierToWindClass(tier, profile.tierCount) <= 3u);
    }
    assert(tier == 0);
    std::puts("LOD runtime smoke: camera roundtrip passed without PCG");
}
