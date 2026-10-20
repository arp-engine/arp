#pragma once
#include "rhi/frontend/resource_binding_layout.h"
#include "rhi/gpu_bindless.h"

namespace arp::rhi {
// Copied declaration and physical identity from an authenticated selected entry
// and the same original Root/2 inventory. No owner, content proof, state claim,
// sampler-use equivalence, execution ticket or capability is issued.
struct BindlessOrdinaryOpaqueBinding {
    std::uint32_t contractVersion = 0u, set = 0u;
    DescriptorBindingDesc declaration;
    ShaderStage selectedStage = ShaderStage::None;
    std::size_t sourceIndex = SIZE_MAX;
    GpuResourceId resource, parentResource;
    std::uint64_t registrationGeneration = 0u, parentRegistrationGeneration = 0u;
    TextureViewResourceLayout view{};
    SamplerResourceLayout sampler{};
    // Samplers deliberately have no buffer/texture state access.
    BindlessResourceAccess access;
};
} // namespace arp::rhi
