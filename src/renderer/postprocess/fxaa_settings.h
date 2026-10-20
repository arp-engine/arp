#pragma once

#include "rhi/semantics/render_target_coordinates.h"

namespace arp::renderer {

struct FxaaSettings {
    bool enabled = true;
    float contrastThreshold = 0.125f;
    rhi::semantics::TextureUvYTransform inputUvYTransform =
        rhi::semantics::TextureUvYTransform::Identity;
};

} // namespace arp::renderer
