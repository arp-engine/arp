#pragma once
#include "rhi/backends/webgl/webgl_resources.h"
#include "rhi/conformance/original_output.h"
#include "rhi/semantics/webgl_prepared_commands.h"

namespace arp::rhi::webgl {
// Cold composed adapter of the existing L2 interfaces. One pipeline/FBO/pass,
// explicit Prototype admission. No generic device capability is opened.
class WebGlExecution final {
public:
    static core::UniquePtr<CommandEncoder> prepare(WebGlResources&,
        const std::shared_ptr<RenderPipeline>&,
        const std::shared_ptr<WebGlFramebuffer>&,
        const semantics::WebGlPreparedCommandsRequest&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic);
    static core::UniquePtr<CommandEncoder> prepare(WebGlResources&,
        core::ArrayView<std::shared_ptr<RenderPipeline>>,
        const std::shared_ptr<WebGlFramebuffer>&,
        const semantics::WebGlPreparedCommandsRequest&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic);
    static core::UniquePtr<CommandEncoder> prepare(WebGlResources&,
        core::ArrayView<std::shared_ptr<RenderPipeline>>,
        core::ArrayView<std::shared_ptr<WebGlFramebuffer>>,
        const semantics::WebGlPreparedCommandsRequest&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic);

    struct Submission {
        const char* diagnostic = nullptr;
        std::uint32_t acceptedOperations = 0;
        bool success = false, mayHaveNativeSideEffects = false, gpuCompleted = false;
    };

    static Submission submit(WebGlResources&, const CommandBuffer&) noexcept;

private:
    friend class WebGlOrderedGraphicsPayload;
    static const char* validateConformance(
        WebGlResources&, const CommandBuffer&, const OriginalConformanceRun&) noexcept;
    static const char* observeConformance(
        WebGlResources&, const CommandBuffer&, OriginalConformanceRun&) noexcept;

public:
    struct CommandBudgetReport {
        bool originalPayload = false;
        std::uint64_t cpuLimitBytes = 0, cpuLiveBytes = 0, cpuPeakBytes = 0;
        std::uint64_t uniformCapacityBytes = 0, uniformRecordedBytes = 0;
        std::uint64_t recordedOperations = 0, recordedDraws = 0;
    };

    static CommandBudgetReport commandBudget(const CommandBuffer&) noexcept;
    static core::UniquePtr<GpuQueue> createQueue(WebGlResources&);
};
} // namespace arp::rhi::webgl
