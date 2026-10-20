#pragma once
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/descriptor_validation.h"

namespace arp::rhi {
// Value-only admission shared by material encoders. The actual snapshot/Root
// owns resources and authorization; this helper cannot issue either authority.
class BindlessMaterialResourceContract final {
public:
    static bool candidate(
        const BindlessSnapshot& snapshot, const BindlessShaderArtifact& artifact) noexcept
    {
        return snapshot.generation() && snapshot.plan().success && artifact.shaderPackageIdentity
            && snapshot.plan().shaderPackageIdentity == artifact.shaderPackageIdentity
            && snapshot.plan().execution == artifact.execution;
    }

    static bool handle(const BindlessSnapshot& snapshot, BindlessResourceHandle value) noexcept
    {
        const auto actual = snapshot.handle(value.tableId, value.index);
        return value.generation && actual.generation == value.generation
            && actual.tableId == value.tableId && actual.index == value.index;
    }

    static bool sampledDomain(const BindlessShaderArtifact& artifact,
        std::uint32_t imageId,
        std::uint32_t samplerId,
        TextureViewDimension dimension) noexcept
    {
        if (!imageId || !samplerId || imageId == samplerId)
            return false;
        const DescriptorBindingDesc* images = nullptr;
        const DescriptorBindingDesc* sampler = nullptr;
        for (const auto& table : artifact.tables) {
            if (table.tableId == imageId) {
                if (images || table.samplerTableId != samplerId || !table.nonUniform
                    || (static_cast<std::uint32_t>(table.operations)
                           & static_cast<std::uint32_t>(BindlessOperation::SampleGrad))
                        == 0u)
                    return false;
                images = findDescriptorSetBinding(artifact.physicalLayout, table.firstBinding);
                if (!images)
                    return false;
            }
            if (table.tableId == samplerId) {
                if (sampler)
                    return false;
                sampler = findDescriptorSetBinding(artifact.physicalLayout, table.firstBinding);
                if (!sampler)
                    return false;
            }
        }
        return images && images->type == DescriptorType::SampledTexture && !images->depthTexture
            && images->textureViewDimension == dimension && images->sampledTextureFilterable
            && (images->stages & ShaderStage::Fragment) != ShaderStage::None && sampler
            && sampler->type == DescriptorType::Sampler && !sampler->comparisonSampler
            && (sampler->stages & ShaderStage::Fragment) != ShaderStage::None;
    }
};
} // namespace arp::rhi
