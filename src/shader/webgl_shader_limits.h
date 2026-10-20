#pragma once
#include <cstdint>

namespace arp::shader {
// Queried facts are validation inputs only, never native execution authority.
struct WebGlShaderLimits {
    std::uint32_t attributes = 0, varyings = 0, vertexUniformVectors = 0,
                  fragmentUniformVectors = 0, vertexTextures = 0, fragmentTextures = 0,
                  combinedTextures = 0;
    bool vertexHighpFloat = false, fragmentHighpFloat = false, vertexHighpInt = false,
         fragmentHighpInt = false, derivatives = false, textureLod = false;
};

} // namespace arp::shader
