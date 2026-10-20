#pragma once
#include "core/bounded_sequence.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_cold_admission.h"
#include "rhi/backends/webgpu/webgpu_original_render_pass_payload.h"
#include "rhi/frontend/command_push_upload.h"
#include "rhi/semantics/bindless_root_program_lowering.h"

namespace arp::rhi_webgpu {
// Inline in the original command shell: the original stream stays authoritative,
// with no replacement graph, journal, Source reservation or owner domain.
// Native handles are borrowed only from
// that shell's strongly retained authentic closure and prepared binding.
class WebGpuBindlessRootNativeProgram final {
    friend class WebGpuNativeCommandPreparation;
    friend class WebGpuOrdinaryRecording;

public:
    using Request = rhi::OriginalBindlessRootProgramRequest;
    // Uniform buffer, acquired group layout, bind group; shared payload and
    // its allocator ledger token. The program itself is inline in the shell.
    static constexpr std::uint32_t pushNativeObjects = 3u, pushRetainedOwners = 2u;

    struct Record {
        rhi::OriginalCommandStreamProgram::Operation original;
        rhi::CommandScalarArguments::Dispatch dispatch{};
        rhi::CommandScalarArguments::Draw draw{};
        rhi::CommandScalarArguments::DrawIndexed indexed{};
        rhi::CommandScalarArguments::DrawIndirect indirect{};
        rhi::CommandScalarArguments::VertexBinding vertex{};
        rhi::CommandScalarArguments::IndexBinding index{};
        std::uint32_t physicalVertexSlot = UINT32_MAX;
        std::uint64_t bufferBindingBytes = 0u;
        rhi::ViewportState viewport{};
        rhi::ScissorState scissor{};
        std::size_t passIndex = SIZE_MAX;
        rhi::BufferCopyDesc copy{};
        rhi::PushConstantsDesc push{};
        rhi::semantics::PushConstantUploadSlice pushSlice{};
        const WebGpuBindlessRootNativeClosure* closure = nullptr;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WGPUBuffer source = nullptr, destination = nullptr;
        WGPUComputePipeline computePipeline = nullptr;
        WGPURenderPipeline renderPipeline = nullptr;
        WGPUBindGroup group = nullptr;
        std::uint32_t groupIndex = UINT32_MAX;
#endif
    };

    explicit WebGpuBindlessRootNativeProgram(std::size_t count) : records_(count), passes_(count) {}

    ~WebGpuBindlessRootNativeProgram()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (pushGroup_)
            wgpuBindGroupRelease(pushGroup_);
        if (pushLayout_)
            wgpuBindGroupLayoutRelease(pushLayout_);
#endif
    }

    WebGpuBindlessRootNativeProgram(const WebGpuBindlessRootNativeProgram&) = delete;
    WebGpuBindlessRootNativeProgram& operator=(const WebGpuBindlessRootNativeProgram&) = delete;

    const auto& records() const noexcept { return records_.values(); }

    std::uint32_t queriedDispatchLimit() const noexcept { return dispatchLimit_; }

    std::uint64_t operationVisits() const noexcept { return visits_; }

private:
    core::BoundedSequence<Record> records_;
    core::BoundedSequence<WebGpuOriginalRenderPassPayload> passes_;
    std::uint32_t dispatchLimit_ = 0u;
    std::uint64_t visits_ = 0u;
    std::uint32_t groupIndex_ = UINT32_MAX;
    rhi::OriginalCommandPushUploadPlan::Limits pushLimits_{};
    std::uint64_t pushResidentBytes_ = 0u;
    std::uint32_t pushBindingBytes_ = 0u, pushGroupIndex_ = UINT32_MAX, pushBinding_ = UINT32_MAX;
    std::shared_ptr<WebGpuNativeBufferPayload> pushBuffer_;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUBindGroupLayout pushLayout_ = nullptr;
    WGPUBindGroup pushGroup_ = nullptr;
    WGPUComputePipeline pipeline_ = nullptr;
    WGPURenderPipeline renderPipeline_ = nullptr;
    WGPUBindGroup group_ = nullptr;
#endif
};
} // namespace arp::rhi_webgpu
