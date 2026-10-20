#pragma once

#include "resource_dependency_arguments_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
// Negative admission only. Empty argument payloads never establish native
// success or ownership; the gate must reject before inspecting any of them.
class WebGpuGenericRecordingAdmissionFixture final {
    class UnobservedQuery final : public rhi::QueryPool {
    public:
        std::uint32_t count() const override
        {
            ++observations;
            return 1u;
        }

        rhi::QueryType type() const override
        {
            ++observations;
            return rhi::QueryType::Timestamp;
        }

        mutable unsigned observations = 0u;
    };

public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        using BufferTransition = bool (WebGpuNativeCommandPayload::*)(
            const WebGpuNativeBufferPayload&, const rhi::ResourceBarrierDesc&, core::pmr::String&);
        using TextureTransition = bool (WebGpuNativeCommandPayload::*)(
            const WebGpuNativeTexturePayload&, const rhi::ResourceBarrierDesc&, core::pmr::String&);
        using BufferMemory = bool (WebGpuNativeCommandPayload::*)(const WebGpuNativeBufferPayload&,
            const rhi::ResourceMemoryBarrierDesc&,
            core::pmr::String&);
        using TextureMemory =
            bool (WebGpuNativeCommandPayload::*)(const WebGpuNativeTexturePayload&,
                const rhi::ResourceMemoryBarrierDesc&,
                core::pmr::String&);
        static_assert(std::is_same_v<decltype(&WebGpuNativeCommandPayload::recordBufferBarrier),
            BufferTransition>);
        static_assert(std::is_same_v<decltype(&WebGpuNativeCommandPayload::recordTextureBarrier),
            TextureTransition>);
        static_assert(
            std::is_same_v<decltype(&WebGpuNativeCommandPayload::recordBufferMemoryBarrier),
                BufferMemory>);
        static_assert(
            std::is_same_v<decltype(&WebGpuNativeCommandPayload::recordTextureMemoryBarrier),
                TextureMemory>);
        WebGpuNativeCommandPayload missing({});
        assert(std::string_view(missing.nonTransferRecordingDiagnostic())
            == webGpuGenericRecordingUsageDiagnostic);
        verifyEntries(missing);
        auto impl = core::makeUnique<WebGpuNativeCommandPayload::Impl>();
        auto* state = impl.get();
        WebGpuNativeCommandPayload shell(std::move(impl));
        verifyEntries(shell);
        assert(state->ordinaryUnsupported && !state->finished && !state->submitted);
        state->ordinaryPrepared = true;
        assert(std::string_view(shell.nonTransferRecordingDiagnostic())
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
        assert(std::string_view(shell.transitionRecordingDiagnostic())
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
        assert(std::string_view(shell.memoryBarrierRecordingDiagnostic())
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
        state->ordinaryPrepared = false;
        state->ordinaryTransfer = true;
        assert(std::string_view(shell.nonTransferRecordingDiagnostic())
            == "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND");
        assert(std::string_view(shell.transitionRecordingDiagnostic())
            == "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND");
        assert(std::string_view(shell.memoryBarrierRecordingDiagnostic())
            == "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND");
    }

    static void verifyEntries(rhi_webgpu::WebGpuNativeCommandPayload& command)
    {
        using namespace rhi_webgpu;
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        WebGpuNativeBufferPayload buffer;
        WebGpuNativeTexturePayload texture;
        WebGpuNativeRenderPipelinePayload graphics;
        WebGpuNativeComputePipelinePayload compute;
        core::pmr::Vector<const WebGpuNativeTexturePayload*> attachments;
        UnobservedQuery query;
        auto retainedTexture = core::makeShared<WebGpuNativeTexturePayload>();
        std::weak_ptr<WebGpuNativeTexturePayload> textureOwner = retainedTexture;
        rhi::BindlessAccessArray accesses(nullptr, 0u);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        unsigned rejected = 0u;
        const auto reject = [&](bool result) {
            assert(!result && diagnostic == webGpuGenericRecordingUsageDiagnostic);
            ++rejected;
        };
        reject(command.recordBufferBarrier(buffer, {}, diagnostic));
        reject(command.recordTextureBarrier(texture, {}, diagnostic));
        reject(command.recordBufferMemoryBarrier(buffer, {}, diagnostic));
        reject(command.recordTextureMemoryBarrier(texture, {}, diagnostic));
        reject(command.recordRegisteredTransfer(rhi::BufferCopyDesc{}, diagnostic));
        reject(command.recordRegisteredTransfer(rhi::BufferToTextureCopyDesc{}, diagnostic));
        reject(command.recordRegisteredTransfer(rhi::TextureToBufferCopyDesc{}, diagnostic));
        reject(command.recordRegisteredTransfer(rhi::TextureToTextureCopyDesc{}, diagnostic));
        reject(command.recordBufferCopy(buffer, buffer, {}, diagnostic));
        reject(command.recordBufferToTextureCopy(buffer, texture, {}, diagnostic));
        reject(command.recordTextureToBufferCopy(texture, buffer, {}, diagnostic));
        reject(command.recordTextureToTextureCopy(texture, texture, {}, diagnostic));
        reject(command.recordTextureUpload(texture, {}, diagnostic));
        reject(command.recordBindlessFaultLatchClear(
            std::shared_ptr<WebGpuNativeBufferPayload>{}, diagnostic));
        reject(command.recordBindlessFaultLatchClear(
            std::shared_ptr<const rhi::BindlessSnapshot>{}, diagnostic));
        reject(command.retainIndexedPipeline({}, {}, diagnostic));
        reject(command.retainIndexedSnapshot({}, diagnostic));
        reject(command.recordPreparedZeroBaseIndirectProducer({}, diagnostic));
        reject(command.recordPreparedIndirectProcessor({}, accesses, accesses, diagnostic));
        reject(bool(command.scheduleBindlessFaultReadback(
            std::shared_ptr<WebGpuNativeBufferPayload>{}, diagnostic)));
        reject(bool(command.scheduleBindlessFaultReadback(
            std::shared_ptr<const rhi::BindlessSnapshot>{}, diagnostic)));
        reject(command.scheduleTextureReadback(
            {}, rhi::TextureFormat::Unknown, {}, "unprepared", diagnostic));
        reject(command.recordRenderPassBegin({}, attachments, attachments, nullptr, diagnostic));
        reject(command.recordGraphicsPipelineBind(graphics, diagnostic));
        reject(command.recordRenderBundleExecute({}, diagnostic));
        reject(command.recordSetViewport({}, diagnostic));
        reject(command.recordSetScissor({}, diagnostic));
        reject(command.recordRenderDescriptorSetBind(0u, {}, nullptr, 0u, diagnostic));
        reject(command.recordVertexBufferBind(0u, buffer, 0u, diagnostic));
        reject(command.recordIndexBufferBind(buffer, rhi::IndexFormat::Uint16, 0u, diagnostic));
        reject(command.recordDrawIndexed(1u, 1u, 0u, 0, 0u, diagnostic));
        reject(command.recordDrawIndexedIndirect(buffer, 0u, 1u, 0u, diagnostic));
        reject(command.recordDrawIndexedIndirectCount(buffer, 0u, buffer, 0u, 1u, 0u, diagnostic));
        reject(command.recordDraw(3u, 1u, 0u, 0u, diagnostic));
        reject(command.recordTimestamp(query, 0u, diagnostic));
        reject(command.recordRenderPassEnd(diagnostic));
        reject(command.recordComputePassBegin({}, diagnostic));
        reject(command.recordComputePipelineBind(compute, diagnostic));
        reject(command.recordDescriptorSetBind(0u, {}, nullptr, 0u, diagnostic));
        reject(command.recordPushConstants(rhi::ShaderStage::Compute, 0u, 4u, nullptr, diagnostic));
        reject(command.recordDispatch(1u, 1u, 1u, diagnostic));
        reject(command.recordDispatchIndirect(buffer, 0u, diagnostic));
        reject(command.recordComputePassEnd(diagnostic));
        reject(command.recordPushDebugGroup("unprepared", diagnostic));
        reject(command.recordPopDebugGroup(diagnostic));
        reject(command.recordInsertDebugMarker("unprepared", diagnostic));
        reject(command.finish(diagnostic));
        command.retainTextureForSubmission({});
        command.retainTextureForSubmission(retainedTexture);
        retainedTexture.reset();
        assert(textureOwner.expired());
        assert(!query.observations && !command.finished() && rejected == 47u);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
};
} // namespace arp::test
