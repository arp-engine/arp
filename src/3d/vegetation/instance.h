#pragma once
#include "3d/vegetation/instance_layout.h"

namespace arp::vegetation {
/// CPU-side instance data (convenient field access, converted to
/// InstanceDataGpu at upload time).
struct InstanceData {
    float x = 0.0f;          ///< World X
    float y = 0.0f;          ///< World Y (terrain height)
    float z = 0.0f;          ///< World Z
    float scale = 1.0f;      ///< Uniform scale (XZ plane; Y uses scaleY if nonzero)
    float yaw = 0.0f;        ///< Y rotation (radians)
    float phase = 0.0f;      ///< Wind phase [0, 1)
    uint32_t instanceId = 0; ///< Stable deterministic ID
    uint32_t lodClass = 0;   ///< 0 = near (full), 1 = mid, 2 = far

    // P8: Terrain fusion transform - burial, tilt, non-uniform scale.
    // Default {0,0,0,0} = no tilt, uniform Y scale, no burial (backward compat).
    float pitchX = 0.0f;  ///< Tilt around X axis (radians) - rocks lean with slope
    float rollZ = 0.0f;   ///< Tilt around Z axis (radians) - asymmetric lean
    float scaleY = 0.0f;  ///< Y scale multiplier (0 = use uniform scale)
    float burialY = 0.0f; ///< Vertical offset (negative = sink into ground)

    // P0 (Track C): Packed metadata. Default values preserve the pre-extension
    // pipeline behavior (species 0, age 0, geometryLod 0, shadowLod 0, intact).
    // Populate via the explicit setters in P1+ placement paths; the legacy
    // grid-jittered scatter leaves these zeroed.
    uint8_t speciesId = 0;   ///< P1: species palette index
    uint8_t ageClass = 0;    ///< P1: 0=sapling, 1=mature, 2=old
    uint8_t moisture = 0;    ///< P2: 0..255 microhabitat moisture
    uint8_t slopeClass = 0;  ///< P2: 0=flat, 1=gentle, 2=moderate, 3=steep
    uint8_t geometryLod = 0; ///< P4: runtime geometry LOD (0/1/2), filled by unified LodProfile
    uint8_t shadowLod = 0;   ///< P6: shadow caster LOD (0=full, 1=proxy, 2=off)
    uint16_t stateFlags = 0; ///< P8: bit0=burning, bit1=chopped, bit2=frozen

    template <class Surface>
    auto anchorToSurface(const Surface& surface) -> decltype(surface.sample(x, z).status)
    {
        using Status = decltype(surface.sample(x, z).status);
        if (!std::isfinite(x) || !std::isfinite(z))
            return Status::InvalidData;
        const auto sampled = surface.sample(x, z);
        if (!sampled.ready())
            return sampled.status;
        if (!std::isfinite(sampled.sample.height))
            return Status::InvalidData;
        y = sampled.sample.height;
        return Status::Ready;
    }

    InstanceDataGpu toGpu() const
    {
        InstanceDataGpu g;
        g.positionScale[0] = x;
        g.positionScale[1] = y;
        g.positionScale[2] = z;
        g.positionScale[3] = scale;
        g.yawPhaseIdLod[0] = yaw;
        g.yawPhaseIdLod[1] = phase;
        g.yawPhaseIdLod[2] = static_cast<float>(instanceId);
        g.yawPhaseIdLod[3] = static_cast<float>(lodClass);
        g.extraTransform[0] = pitchX;
        g.extraTransform[1] = rollZ;
        g.extraTransform[2] = scaleY;
        g.extraTransform[3] = burialY;
        setInstanceMetadata(
            g, speciesId, ageClass, moisture, slopeClass, geometryLod, shadowLod, stateFlags);
        return g;
    }
};

/// A batch of instances sharing the same template. One batch = one
/// drawIndexed call with instanceCount = instances.size().
struct InstanceBatch {
    uint16_t templateId = 0; ///< Index into VegetationTemplateRegistry
    float baseColor[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    uint32_t materialIndex = UINT32_MAX; ///< P6: per-batch material (set by render session)
    arp::core::pmr::Vector<InstanceData> instances{&arp::core::defaultCpuMemoryResource()};

    void clear() { instances.clear(); }

    bool empty() const { return instances.empty(); }

    std::size_t size() const { return instances.size(); }
};

} // namespace arp::vegetation
