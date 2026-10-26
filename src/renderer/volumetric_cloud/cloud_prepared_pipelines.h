#pragma once

#include "renderer/volumetric_cloud/cloud_reconstruction.h"
#include "rhi/frontend/original_prepared_command_device.h"
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_single_entry_input.h"

namespace arp::renderer::cloud {

enum class CloudPreparedResolve { Unspecified, Temporal1, Reconstruction1 };

// A separate explicit selection; CloudQuality/1 continues to select sampling
// only. This does not grant resource, descriptor or command admission.
struct CloudPreparedPipelineSelection {
    std::uint32_t contractVersion = 0u;
    CloudPreparedResolve resolve = CloudPreparedResolve::Unspecified;
    CloudReconstructionConfig reconstruction;
};

struct CloudPreparedResourceRequest {
    CloudRenderConfig config;
    rhi::Extent2D extent{};
    std::uint64_t assetBytes = 0u;
    std::uint32_t maxDescriptorSets = 0u, maxDescriptorElements = 0u;
};

struct CloudPreparedResourcePlan {
    CloudExecutionPlan raw;
    CloudReconstructionResources reconstruction;
    std::uint32_t descriptorSets = 0u, descriptorElements = 0u;
};

// Exact cooked publications supplied by the distribution manifest. Loading
// never searches for alternate targets or falls back to another algorithm.
struct CloudPreparedShaderPublications {
    core::pmr::String raymarch, temporal, composite, spatialResolve, spatialComposite;
};

struct CloudPreparedPipelineBudget {
    // Cold scratch and retained layout/definition allocations in this owner.
    std::uint64_t maxCpuOrchestrationBytes = 0u;
    shader::SingleEntryShaderLoadBudget computeLoad;
    shader::GraphicsShaderModuleLoadBudget graphicsLoad;
    rhi::OriginalSingleEntryPipelineInput::Budget computeInput;
    rhi::OriginalGraphicsPipelineInput::Budget graphicsInput;
    rhi::OriginalSynchronousComputeCreationContract::Budget computeCreation;
    rhi::OriginalSynchronousGraphicsCreationContract::Budget graphicsCreation;
    rhi::OriginalAcceptedComputePipelineFactoryContract::Budget computeWrapper;
    rhi::OriginalAcceptedGraphicsPipelineFactoryContract::Budget graphicsWrapper;
};

struct CloudPreparedPipelineQuotas {
    std::shared_ptr<core::CpuMemoryQuota> artifact, input, owner, native, wrapper;
};

struct CloudPreparedPipelineCreateContext {
    rhi::GpuDevice& device;
    CloudPreparedPipelineSelection selection;
    CloudPreparedResourceRequest resources;
    CloudPreparedShaderPublications publications;
    rhi::OriginalRenderPassPreparationRequest preparation;
    CloudPreparedPipelineBudget budget;
    std::shared_ptr<core::CpuMemoryQuota> orchestrationQuota;
    std::array<CloudPreparedPipelineQuotas, kCloudPipelineCount> quotas;
    std::shared_ptr<const rhi::ComputeDispatchValidationDomain> dispatchDomain;
};

// Cold pipeline orchestration for the existing original ordinary executor.
// No encoder/journal, descriptor publication, wait or frame-time IO lives here.
// Accepted wrappers retain the original immutable input/layout/creation owners.
class ARP_API CloudPreparedPipelines final {
public:
    CloudPreparedPipelines() = default;
    CloudPreparedPipelines(const CloudPreparedPipelines&) = delete;
    CloudPreparedPipelines& operator=(const CloudPreparedPipelines&) = delete;

    static CloudResult validateSelection(const CloudPreparedPipelineSelection&) noexcept;
    // Pure logical/numeric preflight. Original preparation and native allocation
    // admission remain mandatory; this is not a physical GPU peak estimate.
    static CloudResult planResources(const CloudPreparedPipelineSelection&,
        const CloudPreparedResourceRequest&,
        const rhi::ComputeResourceCapabilities&,
        CloudPreparedResourcePlan&) noexcept;
    static CloudResult planResources(const CloudPreparedPipelineSelection&,
        const CloudPreparedResourceRequest&,
        const rhi::ComputeResourceCapabilities&,
        const CloudOriginalDispatchAdmission&,
        CloudPreparedResourcePlan&) noexcept;
    static CloudResult validateInputs(const CloudPreparedPipelineSelection&,
        const rhi::ShaderPackageDesc& raymarch,
        const rhi::ShaderPackageDesc& resolve,
        const rhi::ShaderPackageDesc& composite) noexcept;

    // Atomic publication into an empty owner; failures release their prefix.
    // Re-creation refuses without changing the existing owner. Release first.
    CloudResult create(const CloudPreparedPipelineCreateContext&, core::pmr::String& diagnostic);
    void release() noexcept;

    bool created() const noexcept { return composite_ != nullptr; }

    const CloudPreparedPipelineSelection& selection() const noexcept { return selection_; }

    const CloudPreparedResourcePlan& resources() const noexcept { return resources_; }

    const auto& raymarch() const noexcept { return compute_[0]; }

    const auto& resolve() const noexcept { return compute_[1]; }

    const auto& composite() const noexcept { return composite_; }

    const auto& dispatchDomain() const noexcept { return dispatchDomain_; }

private:
    static CloudResult planResourcesWithAdmission(const CloudPreparedPipelineSelection&,
        const CloudPreparedResourceRequest&,
        const rhi::ComputeResourceCapabilities&,
        const CloudOriginalDispatchAdmission*,
        CloudPreparedResourcePlan&) noexcept;
    CloudPreparedPipelineSelection selection_;
    CloudPreparedResourcePlan resources_;
    std::array<std::shared_ptr<rhi::ComputePipeline>, 2u> compute_;
    std::shared_ptr<rhi::RenderPipeline> composite_;
    std::shared_ptr<const rhi::ComputeDispatchValidationDomain> dispatchDomain_;
};

} // namespace arp::renderer::cloud
