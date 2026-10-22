#pragma once
#include "original_storage_execution_fixture.h"
#include "vulkan_completed_texture_readback_fixture.h"
#include "vulkan_original_root_terminal_fixture.h"

namespace arp::test {
struct VulkanStorageExecutionBackendFixture {
    static constexpr bool callback = false;
    using Closure = rhi_vulkan::VulkanBindlessRootNativeClosure;
    static constexpr unsigned nativePayloadObjects = 7u;
    inline static constexpr const char* label = "vulkan";
    inline static constexpr const char* marker = "VULKAN";
    inline static std::shared_ptr<rhi_vulkan::VulkanRhiRuntime> runtime;
    // Additional storage-specific failure terminals reuse terminal2's strict
    // invalidation/no-publication oracle: unknown queue, unknown fence, no invocation.
    inline static unsigned failure = 0u;

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
        using namespace rhi_vulkan;
        using Hooks = RootFenceNegativeObservation;
        assert(!failure || terminal == 2u);
        Hooks::withholdCompletion = true;
        Hooks::uncertainQueueResult = failure == 1u || failure == 3u;
        Hooks::rejectQueueInvocation = failure == 3u;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
            const auto* id =
                OriginalBindlessRootExecution::submit(execution, {executionVersion, true});
            if (Hooks::uncertainQueueResult) {
                assert(id);
                return nullptr;
            }
            return id;
        }));
        Hooks::uncertainQueueResult = Hooks::rejectQueueInvocation = false;
        assert(execution->pendingRecords() == 1u && !execution->pollCompletion()
            && !result->publishedBytes());
        assert(capacity->used() == 2u && snapshot->originalSubmissionBudgetOwner()->used() == 1u);
        if (terminal == 1u)
            execution.reset();
        else
            assert(execution->cancelExecution());
        if (terminal == 2u && !failure)
            device.retireOriginalDeviceLifetime();
        // Ensure the actual queue finished before replacing its fence observation
        // with an error. This wait grants no L2 completion or publication.
        if (failure == 2u)
            assert(vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
        Hooks::withholdCompletion = false;
        Hooks::uncertainFenceResult = failure == 2u;
        const auto* id =
            CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
                if (failure == 1u || failure == 3u)
                    device.waitIdle();
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                while (snapshot->originalSubmissionBudgetOwner()->used()) {
                    if (execution)
                        execution->pollCompletion();
                    retireCompletedVulkanRhiSubmittedCommands();
                    if (failure == 2u) {
                        Hooks::uncertainFenceResult = false;
                        device.waitIdle();
                    }
                    if (std::chrono::steady_clock::now() > deadline)
                        return "VULKAN-STORAGE-COMPLETION-TIMEOUT";
                    std::this_thread::yield();
                }
                return nullptr;
            });
        Hooks::uncertainFenceResult = false;
        if (id)
            std::fprintf(stderr, "Vulkan storage completion: %s %s\n", id, diagnostic.c_str());
        assert(!id);
        if (failure)
            std::printf(
                "BINDLESS_VULKAN_PUBLIC_STORAGE_FAILURE failure=%u actual-drain invalidated unpublished hot0\n",
                failure);
    }

    template <class T, std::size_t N>
    static bool readBuffer(
        const rhi::GpuBuffer& buffer, std::array<T, N>& values, core::pmr::String& diagnostic)
    {
        const auto payload = rhi_vulkan::vulkanRhiNativePayload(buffer);
        if (!payload || !payload->backendMappedData()
            || !payload->backendInvalidateMappedRead(0u, sizeof(values), diagnostic))
            return false;
        std::memcpy(values.data(), payload->backendMappedData(), sizeof(values));
        return true;
    }

    // Independent observation after genuine public completion. This test-only
    // copy restores the exact layout and never publishes L2 state or content.
    static std::array<std::uint16_t, 20u> readPixels(const rhi::GpuTexture& texture)
    {
        using namespace rhi;
        std::array<semantics::TextureUploadPackedMip, 2u> mips;
        for (std::size_t i = 0u; i < mips.size(); ++i) {
            auto& mip = mips[i];
            mip.range = defaultTextureSubresourceRange(texture.desc());
            mip.range.baseMipLevel = i + 1u;
            mip.range.mipLevelCount = 1u;
            mip.extent = {i ? 1u : 2u, i ? 1u : 2u, 1u};
            mip.packedOffset = i ? 32u : 0u;
            mip.packedBytesPerRow = i ? 8u : 16u;
            mip.rowCount = i ? 1u : 2u;
        }
        const auto bytes = VulkanCompletedTextureReadbackFixture::read(
            texture, runtime, mips, 40u, ResourceState::ShaderRead);
        std::array<std::uint16_t, 20u> values;
        assert(bytes.size() == sizeof(values));
        std::memcpy(values.data(), bytes.data(), sizeof(values));
        return values;
    }
};

using VulkanOriginalStorageExecutionFixture =
    OriginalStorageExecutionFixture<VulkanStorageExecutionBackendFixture>;
} // namespace arp::test
