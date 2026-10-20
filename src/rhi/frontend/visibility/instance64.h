#pragma once
#include <cstddef>
#include <type_traits>

namespace arp::rhi::visibility {
// Closed Instance64 shader ABI. Scene-specific placement types remain above
// RHI; frozen-input inspection requires their exact scalar offsets and bytes.
struct Instance64 {
    float positionScale[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float yawPhaseIdLod[4] = {};
    float extraTransform[4] = {};
    float speciesLodState[4] = {};
};

static_assert(std::is_standard_layout_v<Instance64> && sizeof(Instance64) == 64u
    && offsetof(Instance64, positionScale) == 0u && offsetof(Instance64, yawPhaseIdLod) == 16u
    && offsetof(Instance64, extraTransform) == 32u && offsetof(Instance64, speciesLodState) == 48u);
} // namespace arp::rhi::visibility
