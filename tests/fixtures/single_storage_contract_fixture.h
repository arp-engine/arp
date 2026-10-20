#pragma once
#include "rhi/gpu_pipeline.h"

namespace arp::test {
// Shared explicit one-binding L2 layout/reflection fixture; no binary authority.
class SingleStorageContractFixture {
public:
    explicit SingleStorageContractFixture(
        rhi::ShaderStage stage, rhi::DescriptorType type = rhi::DescriptorType::StorageBuffer)
    {
        rhi::DescriptorLayoutDesc set;
        set.name = "Writes";
        layout.descriptorLayouts.push_back(std::move(set));
        layout.descriptorSets.push_back({0u, "Writes"});
        addBinding(3u, type, stage);
    }

    void addBinding(std::uint32_t binding, rhi::DescriptorType type, rhi::ShaderStage stage)
    {
        layout.descriptorLayouts[0].bindings.push_back({binding, type, stage, 1u});
        reflection.descriptorBindings.push_back({0u, binding, type, stage, 1u});
    }

    rhi::PipelineLayoutDesc layout;
    rhi::ShaderReflectionDesc reflection;
};
} // namespace arp::test
