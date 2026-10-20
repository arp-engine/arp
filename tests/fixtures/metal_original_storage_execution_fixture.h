#pragma once
#include "original_storage_execution_fixture.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_original_bindless_command.h"

namespace arp::test {
struct MetalStorageExecutionBackendFixture {
    static constexpr bool callback = false;
    using Closure = rhi_metal::MetalRhiBindlessRootNativeClosure;
    static constexpr unsigned nativePayloadObjects = 3u;
    inline static constexpr const char* label = "metal";
    inline static constexpr const char* marker = "METAL";

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Metal storage: %s\n", id);
        assert(!id);
    }

    template <class Execution, class Result, class Quotas, class Capacity, class Snapshot>
    static void complete(rhi::GpuDevice& device,
        Execution& execution,
        const Result& result,
        const Quotas& q,
        const Capacity& capacity,
        const Snapshot& snapshot,
        unsigned terminal,
        core::pmr::String& diagnostic,
        unsigned executionVersion = 9u)
    {
        using namespace rhi;
        using namespace rhi_metal;
        auto gate = createMetalRhiNativeSharedEventPayload("public-storage-gate");
        auto blocker = createMetalRhiNativeCommandPayload("public-storage-blocker", {});
        assert(gate && blocker);
        auto blocked = blocker->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        assert(blocker->finish(diagnostic)
            && blocker->submit(queue, blocked, gate, 1u, {}, 0u, diagnostic));
        require(CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept {
            return OriginalBindlessRootExecution::submit(execution, {executionVersion, true});
        }));
        assert(execution->pendingRecords() == 1u && !result->publishedBytes());
        assert(capacity->used() == 2u && snapshot->originalSubmissionBudgetOwner()->used() == 1u);
        if (terminal == 1u)
            execution.reset();
        else
            assert(execution->cancelExecution());
        if (terminal == 2u)
            device.retireOriginalDeviceLifetime();
        const auto residual = q[6]->limitBytes() - q[6]->liveBytes();
        require(CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
            signalMetalRhiNativeSharedEvent(gate, 1u);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (q[6]->liveBytes() != residual) {
                if (std::chrono::steady_clock::now() > deadline)
                    return "METAL-STORAGE-COMPLETION-TIMEOUT";
                std::this_thread::yield();
            }
            return nullptr;
        }));
        blocked->wait();
    }

    template <class T, std::size_t N>
    static bool readBuffer(
        const rhi::GpuBuffer& buffer, std::array<T, N>& values, core::pmr::String& diagnostic)
    {
        return rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
            values.data(),
            sizeof(values),
            0u,
            buffer.desc().sizeBytes,
            diagnostic);
    }

    static std::array<std::uint16_t, 20u> readPixels(const rhi::GpuTexture& texture);
};

using MetalOriginalStorageExecutionFixture =
    OriginalStorageExecutionFixture<MetalStorageExecutionBackendFixture>;
} // namespace arp::test
