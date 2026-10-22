#pragma once
#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace arp::vegetation {
/// GPU vertex format for instanced rendering. Extends MeshVertex with wind
/// weights (COLOR_0 channel). Used for foliage cards, tree variants, and rock
/// variants. Rocks use default wind (0, 0, 1) = no bend, no flutter, max
/// stiffness.
///
/// Layout (60 bytes):
///   offset 0:  position  (float3, POSITION0)
///   offset 12: normal    (float3, NORMAL0)
///   offset 24: uv        (float2, TEXCOORD0)
///   offset 32: tangent   (float4, TANGENT0)
///   offset 48: wind      (float3, COLOR0) = (primaryBend, flutter, stiffness)
struct InstanceVertex {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 0.0f, 1.0f};
    float uv[2] = {0.0f, 0.0f};
    float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    float primaryBend = 0.0f; ///< COLOR_0.R: 0 = root pinned, 1 = tip
    float flutter = 0.0f;     ///< COLOR_0.G: 0 = wood, >0 = foliage
    float stiffness = 1.0f;   ///< COLOR_0.B: 0 = flexible, 1 = rigid
};

struct InstanceVertexLocalBounds {
    float center[3] = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
};

inline bool computeInstanceVertexLocalBounds(const InstanceVertex* vertices,
    std::size_t vertexCount,
    InstanceVertexLocalBounds& result) noexcept
{
    if (vertices == nullptr || vertexCount == 0u) {
        result = {};
        return false;
    }
    float minValue[3]{vertices[0].position[0], vertices[0].position[1], vertices[0].position[2]};
    float maxValue[3]{minValue[0], minValue[1], minValue[2]};
    for (std::size_t index = 1u; index < vertexCount; ++index) {
        for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
            minValue[axis] = std::min(minValue[axis], vertices[index].position[axis]);
            maxValue[axis] = std::max(maxValue[axis], vertices[index].position[axis]);
        }
    }
    for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
        result.center[axis] = 0.5f * (minValue[axis] + maxValue[axis]);
    }
    float radiusSquared = 0.0f;
    for (std::size_t index = 0u; index < vertexCount; ++index) {
        const float x = vertices[index].position[0] - result.center[0];
        const float y = vertices[index].position[1] - result.center[1];
        const float z = vertices[index].position[2] - result.center[2];
        radiusSquared = std::max(radiusSquared, x * x + y * y + z * z);
    }
    result.radius = std::sqrt(radiusSquared);
    return true;
}

/// GPU instance data: uploaded to slot 1 (instance-rate buffer). 64 bytes
/// (4 x float4 for safe std140 / uniform buffer alignment and to match the
/// RHI VertexFormat::Float32x4 attribute path shared by all four backends).
///
///   positionScale:      xyz = world position, w = uniform scale
///   yawPhaseIdLod:      x = yaw (radians), y = wind phase [0,1),
///                       z = stable instance ID (as float), w = LOD class (0-2)
///   extraTransform:     x = pitchX (radians, tilt around X),
///                       y = rollZ (radians, tilt around Z),
///                       z = scaleY (0 = use uniform scale, >0 = non-uniform Y),
///                       w = burialY (vertical offset, negative = sink into ground)
///   speciesLodState:    P0 (Track C) extension. Stored as float[4] so the
///                       vertex attribute travels the same Float32x4 path as
///                       the other three slots (the RHI does not expose a
///                       Uint32x4 VertexFormat). The float bits are the raw
///                       bit pattern of packed uint32 metadata; the shader
///                       decodes them via `asuint()`. Packed bit layout:
///                         speciesLodState[0] bits  0-7  : speciesId   (u8, P1)
///                         speciesLodState[0] bits  8-15 : ageClass    (u8, P1)
///                         speciesLodState[0] bits 16-23 : moisture    (u8, P2)
///                         speciesLodState[0] bits 24-31 : slopeClass  (u8, P2)
///                         speciesLodState[1] bits  0-7  : geometryLod (u8, P4)
///                         speciesLodState[1] bits  8-15 : shadowLod   (u8, P6)
///                       InstanceLodCrossfade/1 uses words 2/3 only when word3 bit31 is enabled:
///                       word2 = float start seconds; word3 = enable31, incoming30, seed16..29, durationMs0..15.
///                         speciesLodState[1] bits 16-31 : stateFlags  (u16,P8)
///                         speciesLodState[2..3] reserved (0)
///                       Default {0,0,0,0} = species 0, age 0, no moisture/slope
///                       bias, geometryLod 0, shadowLod 0, state intact. This is
///                       backward-compatible with the prior 48-byte layout: all
///                       existing placement paths leave the field zeroed, so
///                       the shader reads species 0 / lod 0 / intact and
///                       behaves identically to the pre-extension pipeline.
struct InstanceDataGpu {
    float positionScale[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float yawPhaseIdLod[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float extraTransform[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float speciesLodState[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/// Bit-pack a uint32 into the float storage of speciesLodState[idx]. Uses
/// memcpy (not reinterpret_cast) to avoid strict-aliasing UB; the GPU reads
/// the same bits via `asuint()` in the shader. Bit-for-bit identical across
/// all four backends (Vulkan/Metal/WebGPU/Null) since Float32x4 is a raw
/// 16-byte attribute with no format conversion.
inline void packInstanceMetadataFloat(float& storage, uint32_t bits) noexcept
{
    std::memcpy(&storage, &bits, sizeof(float));
}

inline uint32_t unpackInstanceMetadataFloat(const float& storage) noexcept
{
    uint32_t bits = 0u;
    std::memcpy(&bits, &storage, sizeof(uint32_t));
    return bits;
}

/// Decode helpers for the packed speciesLodState field. These mirror the bit
/// layout documented on InstanceDataGpu and are the single source of truth
/// for both CPU placement (P1/P2/P4/P6/P8) and the contract tests. The shader
/// side decodes the same layout in `decodeInstanceMetadata` (foliage.slang /
/// pbr_scene.slang vertexMainInstanced) -- the two must stay byte-identical,
/// asserted by tests/contract/wocc_instance_layout_contract_tests.cpp.
inline uint8_t instanceSpeciesId(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>(unpackInstanceMetadataFloat(g.speciesLodState[0]) & 0xFFu);
}

inline uint8_t instanceAgeClass(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>((unpackInstanceMetadataFloat(g.speciesLodState[0]) >> 8) & 0xFFu);
}

inline uint8_t instanceMoisture(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>((unpackInstanceMetadataFloat(g.speciesLodState[0]) >> 16) & 0xFFu);
}

inline uint8_t instanceSlopeClass(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>((unpackInstanceMetadataFloat(g.speciesLodState[0]) >> 24) & 0xFFu);
}

inline uint8_t instanceGeometryLod(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>(unpackInstanceMetadataFloat(g.speciesLodState[1]) & 0xFFu);
}

inline uint8_t instanceShadowLod(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>((unpackInstanceMetadataFloat(g.speciesLodState[1]) >> 8) & 0xFFu);
}

inline uint16_t instanceStateFlags(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint16_t>(unpackInstanceMetadataFloat(g.speciesLodState[1]) >> 16);
}

inline void setInstanceMetadata(InstanceDataGpu& g,
    uint8_t speciesId,
    uint8_t ageClass,
    uint8_t moisture,
    uint8_t slopeClass,
    uint8_t geometryLod,
    uint8_t shadowLod,
    uint16_t stateFlags) noexcept
{
    const uint32_t word0 = (static_cast<uint32_t>(speciesId))
        | (static_cast<uint32_t>(ageClass) << 8) | (static_cast<uint32_t>(moisture) << 16)
        | (static_cast<uint32_t>(slopeClass) << 24);
    const uint32_t word1 = (static_cast<uint32_t>(geometryLod))
        | (static_cast<uint32_t>(shadowLod) << 8) | (static_cast<uint32_t>(stateFlags) << 16);
    packInstanceMetadataFloat(g.speciesLodState[0], word0);
    packInstanceMetadataFloat(g.speciesLodState[1], word1);
    packInstanceMetadataFloat(g.speciesLodState[2], 0u);
    packInstanceMetadataFloat(g.speciesLodState[3], 0u);
}

} // namespace arp::vegetation
