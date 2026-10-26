#pragma once
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_single_entry_input.h"
#include "rhi/frontend/pipeline_synchronous_creation_validation.h"
#include "rhi/semantics/native_backend_capabilities.h"

namespace arp::rhi_metal {
struct MetalRhiNativePipelinePayload;

// Cold backend-private ordinary creation. Bindless authorization is never used.
// Returned objects own their copied layout/interface and exact original input.
// No stub, file reload, generic accepted-wrapper or output-audit authority.
class MetalOriginalPipeline final {
public:
    using Budget = rhi::OriginalSynchronousPipelineCreationContract::Budget;
    // Shared backend construction after immutable definition validation. This
    // returns native ownership only, never an accepted L2 creation receipt.
    static std::shared_ptr<MetalRhiNativePipelinePayload> createValidatedComputePayload(
        const rhi::ComputePipelineDesc&,
        const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>&,
        const rhi::PipelineValidationReport&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*&);
    static std::shared_ptr<MetalRhiNativePipelinePayload> createValidatedGraphicsPayload(
        const rhi::RenderPipelineDesc&,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&,
        const rhi::PipelineValidationReport&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*&);
    static rhi::PipelineValidationReport validate(const rhi::RenderPipelineDesc&,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&,
        const rhi::semantics::NativeBackendCapabilityContract&);
    static rhi::PipelineValidationReport validate(const rhi::ComputePipelineDesc&,
        const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>&,
        const rhi::semantics::NativeBackendCapabilityContract&);
    static core::UniquePtr<rhi::RenderPipeline> create(const rhi::RenderPipelineDesc&,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&,
        const rhi::semantics::NativeBackendCapabilityContract&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*&);
    static core::UniquePtr<rhi::ComputePipeline> create(const rhi::ComputePipelineDesc&,
        const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>&,
        const rhi::semantics::NativeBackendCapabilityContract&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*&);
    static const char* diagnostic(const MetalRhiNativePipelinePayload&) noexcept;
    static std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> input(
        const rhi::RenderPipeline&) noexcept;
    static std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> input(
        const rhi::ComputePipeline&) noexcept;
};
} // namespace arp::rhi_metal
