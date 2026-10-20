#pragma once
#include "original_storage_execution_fixture.h"
#include "rhi/backends/webgpu/webgpu_bindless_root.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_map_receipt.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"

namespace arp::test {
struct WebGpuStorageExecutionBackendFixture {
    using Closure = rhi_webgpu::WebGpuBindlessRootNativeClosure;
    static constexpr bool callback = true;
    static constexpr unsigned nativePayloadObjects = 7u;
    inline static constexpr const char* label = "webgpu";
    inline static constexpr const char* marker = "WEBGPU";

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "WebGPU storage: %s\n", id);
        assert(!id);
    }

    template <class Root> static WGPUDevice nativeDevice(const Root& root)
    {
        const auto pipeline = std::visit(
            [](const auto& prepared) -> std::shared_ptr<rhi_webgpu::WebGpuNativePipelineOwnership> {
                return rhi_webgpu::webGpuNativePayloadFor(*prepared->owners()->desc().pipeline);
            },
            root->originalPrepared());
        assert(pipeline);
        return static_cast<WGPUDevice>(pipeline->issuedContract()->nativeDevice());
    }

    template <class Root> static void beforeRecord(const Root& root, unsigned terminal)
    {
        if (terminal == 5u || terminal == 6u)
            WebGpuNativeSubmissionProbeFixture::instrumentRecording(
                nativeDevice(root), terminal == 6u);
    }

    static void afterRecord(unsigned terminal)
    {
        if (terminal == 5u || terminal == 6u)
            WebGpuNativeSubmissionProbeFixture::restoreRecording();
    }

    template <class Execution,
        class Result,
        class Quotas,
        class Capacity,
        class Snapshot,
        class Root>
    static void complete(rhi::GpuDevice& device,
        Execution& execution,
        const Result& result,
        const Quotas& q,
        const Capacity& capacity,
        const Snapshot& snapshot,
        unsigned terminal,
        core::pmr::String&,
        const Root& root,
        unsigned executionVersion = 10u)
    {
        using Issuer = rhi::OriginalBindlessRootExecution;
        using Probe = WebGpuNativeSubmissionProbeFixture;
        const bool instrumented = terminal >= 6u && terminal <= 9u;
        if (instrumented) {
            const auto native = nativeDevice(root);
            const auto queue = wgpuDeviceGetQueue(native);
            assert(queue);
            Probe::instrument(native,
                queue,
                terminal == 6u       ? Probe::Probe::Reverse
                    : terminal == 7u ? Probe::Probe::NativeError
                    : terminal == 8u ? Probe::Probe::ThrowBefore
                                     : Probe::Probe::ThrowAfter,
                nullptr,
                [](void*) noexcept {});
            wgpuQueueRelease(queue);
        }
        require(CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
            const auto* submitId = Issuer::submit(execution, {executionVersion, true});
            if (terminal == 8u || terminal == 9u)
                assert(submitId);
            else
                require(submitId);
            assert(!result->publishedBytes() && execution->pendingRecords() == 1u);
            assert(!capacity->used() && snapshot->originalSubmissionBudgetOwner()->used() == 1u);
            if (terminal == 1u)
                execution.reset();
            if (terminal == 2u)
                device.retireOriginalDeviceLifetime();
            if (terminal == 10u)
                assert(execution->cancelExecution());
            const auto ready = [&] {
                return !snapshot->originalSubmissionBudgetOwner()->used();
            };
            const auto browserDeadline = EM_ASM_DOUBLE({
                // clang-format off
                        return Module.tableBrowserCompletionDeadline || 0;
                // clang-format on
            });
            const bool drained = executionVersion == 12u && browserDeadline > 0.0
                ? WebGpuAsyncWaitFixture::untilDeadline(ready, browserDeadline)
                : WebGpuAsyncWaitFixture::until(ready);
            if (!drained)
                std::fprintf(stderr,
                    "WebGPU storage drain: terminal=%u execution=%s pending=%u submission-used=%llu\n",
                    terminal,
                    execution && execution->executionDiagnostic() ? execution->executionDiagnostic()
                                                                  : "none",
                    execution ? execution->pendingRecords() : 0u,
                    static_cast<unsigned long long>(
                        snapshot->originalSubmissionBudgetOwner()->used()));
            assert(drained);
            return nullptr;
        }));
        if (instrumented)
            Probe::restore();
    }

    // Independent test observation, after genuine public completion. This copy
    // never publishes or changes an L2 resource state/content/completion fact.
    template <class Copy>
    static void observe(WGPUDevice device, void* bytes, std::size_t size, Copy copy)
    {
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = size;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        const auto buffer = wgpuDeviceCreateBuffer(device, &desc);
        assert(buffer);
        const auto encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        assert(encoder);
        copy(encoder, buffer);
        const auto command = wgpuCommandEncoderFinish(encoder, nullptr);
        assert(command);
        const auto queue = wgpuDeviceGetQueue(device);
        assert(queue);
        wgpuQueueSubmit(queue, 1u, &command);
        rhi_webgpu::WebGpuNativeBufferMapReceipt receipt;
        bool done = false;
        require(receipt.prepareCold(buffer, size, &done, [](void* p, auto outcome) noexcept {
            assert(outcome == rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome::Success);
            *static_cast<bool*>(p) = true;
        }));
        require(receipt.start());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return done;
        }));
        require(receipt.readAndUnmap(bytes, size));
        wgpuQueueRelease(queue);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        wgpuBufferRelease(buffer);
    }

    template <class T, std::size_t N>
    static bool readBuffer(
        const rhi::GpuBuffer& buffer, std::array<T, N>& values, core::pmr::String&)
    {
        const auto native = rhi_webgpu::webGpuNativePayloadFor(buffer);
        assert(native && native->buffer);
        observe(native->ownerDevice, values.data(), sizeof(values), [&](auto encoder, auto result) {
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, native->buffer, 0u, result, 0u, sizeof(values));
        });
        return true;
    }

    static std::array<std::uint16_t, 20u> readPixels(const rhi::GpuTexture& texture)
    {
        const auto native = rhi_webgpu::webGpuNativePayloadFor(texture);
        assert(native && native->texture);
        std::array<std::uint8_t, 3u * 256u> bytes{};
        observe(native->ownerDevice, bytes.data(), bytes.size(), [&](auto encoder, auto result) {
            for (unsigned i = 0u; i != 2u; ++i) {
                auto src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                src.texture = native->texture;
                src.mipLevel = i + 1u;
                auto dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
                dst.buffer = result;
                dst.layout.offset = i ? 512u : 0u;
                dst.layout.bytesPerRow = 256u;
                dst.layout.rowsPerImage = i ? 1u : 2u;
                WGPUExtent3D extent{i ? 1u : 2u, i ? 1u : 2u, 1u};
                wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
            }
        });
        std::array<std::uint16_t, 20u> packed{};
        unsigned pixel = 0u;
        for (unsigned i = 0u; i != 2u; ++i)
            for (unsigned y = 0u; y != (i ? 1u : 2u); ++y)
                for (unsigned x = 0u; x != (i ? 1u : 2u); ++x)
                    std::memcpy(packed.data() + 4u * pixel++,
                        bytes.data() + (i ? 512u : 0u) + y * 256u + x * 8u,
                        8u);
        assert(pixel == 5u);
        return packed;
    }
};

using WebGpuOriginalStorageExecutionFixture =
    OriginalStorageExecutionFixture<WebGpuStorageExecutionBackendFixture>;
} // namespace arp::test
