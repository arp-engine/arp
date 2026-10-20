#pragma once
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/gpu_bindless.h"

namespace arp::rhi::null_backend {
std::shared_ptr<const BindlessPreparedComputeBinding> prepareComputeBinding(
    std::shared_ptr<const BindlessComputeBindingOwners>, core::pmr::String&);
bool isPreparedComputeBinding(const BindlessPreparedComputeBinding&) noexcept;
std::shared_ptr<const BindlessPreparedGraphicsBinding> prepareGraphicsBinding(
    std::shared_ptr<const BindlessGraphicsBindingOwners>, core::pmr::String&);
bool isPreparedGraphicsBinding(const BindlessPreparedGraphicsBinding&) noexcept;

struct BindlessPipelineState final {
    std::shared_ptr<const BindlessAuthorization> authorization;
    core::UniquePtr<PipelineLayout> layout;
    ShaderPackageDesc packageView;
    PipelineValidationReport validation;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    RenderPipelineDesc renderState;
    BindlessGraphicsPipelineEntries graphicsEntries;
    ShaderStage activeStages = ShaderStage::None;
};

core::UniquePtr<ComputePipeline> createBindlessComputePipeline(const BindlessCrossExtension&,
    std::shared_ptr<const BindlessAuthorization>,
    core::StringView entry,
    core::pmr::String& diagnostic);
core::UniquePtr<RenderPipeline> createBindlessRenderPipeline(const BindlessCrossExtension&,
    std::shared_ptr<const BindlessAuthorization>,
    const RenderPipelineDesc&,
    core::StringView vertexEntry,
    core::StringView fragmentEntry,
    core::pmr::String& diagnostic,
    const BindlessGraphicsPushRequest* push = nullptr);
std::shared_ptr<const BindlessPipelineState> bindlessState(const ComputePipeline&);
std::shared_ptr<const BindlessPipelineState> bindlessState(const RenderPipeline&);
ValidationResult validateBindlessSubmission(const CommandBuffer&, const BindlessCrossExtension*);

} // namespace arp::rhi::null_backend
