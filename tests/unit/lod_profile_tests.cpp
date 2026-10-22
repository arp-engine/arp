#include "3d/lod/lod_profile.h"
#include "3d/terrain/terrain_lod.h"

#include <cassert>
#include <limits>

int main()
{
    auto profile = arp::terrain::terrainLodProfile(64.0f);
    assert(profile.validate() == nullptr);
    assert(profile.resolveTier(127.0f, -1) == 0);
    assert(profile.resolveTier(128.0f, -1) == 1);
    assert(profile.resolveTier(140.0f, 0) == 0);
    assert(profile.resolveTier(148.0f, 0) == 1);
    assert(profile.resolveTier(120.0f, 1) == 1);
    assert(profile.resolveTier(108.0f, 1) == 0);
    assert(profile.shouldCull(3220.0f, true));
    assert(!profile.shouldCull(3200.0f, true));
    assert(profile.shouldCull(3200.0f, false));
    assert(profile.resolveTier(std::numeric_limits<float>::quiet_NaN(), 0) == -1);
    assert(profile.shouldCull(std::numeric_limits<float>::quiet_NaN(), true));
    profile.tierCount = 0;
    assert(profile.validate() != nullptr);
    assert(profile.resolveTier(0.0f, 0) == -1);
    assert(profile.shouldCull(0.0f, true));
    profile = arp::terrain::terrainLodProfile(64.0f);
    profile.tiers[1].farBound = profile.tiers[0].farBound;
    assert(profile.validate() != nullptr);
    profile.tierCount = 5;
    assert(profile.validate() != nullptr);
    assert(arp::lod::ScreenDiameterLod::diameter(2.0f, 1.0f, 10.0f) == 0.1f);
}
