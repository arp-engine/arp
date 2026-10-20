#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace arp::renderer {

// Backend-neutral vertex stream consumed by a skinned render pipeline.  Joint
// indices deliberately use float lanes for now: the cross-backend RHI vertex
// contract currently guarantees Float32x4 but not integer vertex formats.
// The vertex shader validates/casts these values to palette indices.
struct GpuSkinningInfluenceVertex {
    std::array<float, 4> joints{};
    std::array<float, 4> weights{};
};

// The palette belongs in a UniformBuffer, never in per-draw push constants.
// 128 affine matrices occupy 8192 bytes, below Vulkan's 16 KiB guaranteed UBO
// range, and leave room for the object transform in the same double-buffered
// allocation. The animation layer owns column-major matrices, and this
// payload keeps those bytes unchanged. Slang's backend-neutral float4x4
// contract lowers mul(matrix, vector) to vector * matrix, so the shader
// matrix is the transpose of the column-vector animation matrix. Keeping the
// canonical column-major bytes here produces that transpose through the
// reflected shader ABI on every backend.
// The currently shipped Viking berserker rig has 219 joints.  Keep this a
// power-of-two headroom above that count: 256 mat4 values plus the object
// transform is ~16.5 KiB, safely within the 64 KiB minimum UBO range while
// avoiding silent palette truncation.
constexpr std::uint32_t kMaxGpuSkinningJoints = 256u;

inline void packColumnMajorMatrixForShader(
    std::array<float, 16>& destination, const std::array<float, 16>& source)
{
    // animation::Mat4 stores M[column * 4 + row]. Shader-facing float4x4
    // fields use ARP's default Slang contract, which consumes row-major
    // M[row * 4 + column] bytes on every backend.
    for (std::uint32_t row = 0u; row < 4u; ++row) {
        for (std::uint32_t column = 0u; column < 4u; ++column)
            destination[row * 4u + column] = source[column * 4u + row];
    }
}

struct GpuSkinningPalette {
    std::array<std::array<float, 16>, kMaxGpuSkinningJoints> skinMatrices{};
    std::array<float, 4> worldTranslationAndScale{0.0f, 0.0f, 0.0f, 1.0f};
    // xyzw quaternion. Kept separate from translation so the shader can
    // rotate positions, normals and tangents without a CPU vertex bake.
    std::array<float, 4> worldRotation{0.0f, 0.0f, 0.0f, 1.0f};
    std::uint32_t jointCount = 0u;
    std::uint32_t pad0 = 0u;
    std::uint32_t pad1 = 0u;
    std::uint32_t pad2 = 0u;
};

static_assert(sizeof(GpuSkinningInfluenceVertex) == 32u,
    "GPU skinning influence stream must be two tightly packed float4 attributes");
static_assert(sizeof(GpuSkinningPalette) % 16u == 0u,
    "GPU skinning palette must be std140/UniformBuffer aligned");

} // namespace arp::renderer
