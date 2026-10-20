#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/vulkan/vulkan_original_bindless_command.h"
#include "vulkan_headless_runtime.h"

#include <cstring>

namespace arp::test {
// Negative observations only. Creation, recording, submission and successful
// completion use the real driver; these hooks never manufacture success.
struct RootFenceNegativeObservation {
    inline static bool withholdCompletion = false, omitQueueEntrypoint = false,
                       uncertainQueueResult = false, omitIdleEntrypoint = false,
                       uncertainIdleResult = false, rejectQueueInvocation = false,
                       uncertainFenceResult = false;
    inline static VkDevice injectedLostDevice = VK_NULL_HANDLE;
    inline static std::uint64_t queryReadCalls = 0u;

    static VKAPI_ATTR VkResult VKAPI_CALL queryResults(VkDevice device,
        VkQueryPool pool,
        std::uint32_t first,
        std::uint32_t count,
        std::size_t bytes,
        void* data,
        VkDeviceSize stride,
        VkQueryResultFlags flags)
    {
        ++queryReadCalls;
        return vkGetQueryPoolResults(device, pool, first, count, bytes, data, stride, flags);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL submit(
        VkQueue queue, std::uint32_t count, const VkSubmitInfo* submissions, VkFence fence)
    {
        if (rejectQueueInvocation)
            return VK_ERROR_UNKNOWN;
        const auto actual = vkQueueSubmit(queue, count, submissions, fence);
        return uncertainQueueResult && actual == VK_SUCCESS ? VK_ERROR_UNKNOWN : actual;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL status(VkDevice device, VkFence fence)
    {
        if (withholdCompletion)
            return VK_NOT_READY;
        const auto actual = vkGetFenceStatus(device, fence);
        return uncertainFenceResult && actual == VK_SUCCESS ? VK_ERROR_UNKNOWN : actual;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL wait(VkDevice device,
        std::uint32_t count,
        const VkFence* fences,
        VkBool32 all,
        std::uint64_t timeout)
    {
        if (withholdCompletion)
            return VK_TIMEOUT;
        const auto actual = vkWaitForFences(device, count, fences, all, timeout);
        return uncertainFenceResult && actual == VK_SUCCESS ? VK_ERROR_UNKNOWN : actual;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL idle(VkDevice device)
    {
        const auto actual = vkDeviceWaitIdle(device);
        if (actual == VK_SUCCESS && injectedLostDevice == device)
            return VK_ERROR_DEVICE_LOST;
        return uncertainIdleResult && actual == VK_SUCCESS ? VK_ERROR_UNKNOWN : actual;
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
    {
        if (std::strcmp(name, "vkGetFenceStatus") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(&status);
        if (std::strcmp(name, "vkWaitForFences") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(&wait);
        if (omitIdleEntrypoint && std::strcmp(name, "vkDeviceWaitIdle") == 0)
            return nullptr;
        if (std::strcmp(name, "vkDeviceWaitIdle") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(&idle);
        if (omitQueueEntrypoint && std::strcmp(name, "vkQueueSubmit") == 0)
            return nullptr;
        if (std::strcmp(name, "vkQueueSubmit") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(&submit);
        if (std::strcmp(name, "vkGetQueryPoolResults") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(&queryResults);
        return vkGetDeviceProcAddr(device, name);
    }
};

struct VulkanOriginalRootTerminalScenario {
    bool uncertainSubmit = false;
    bool deviceIdleDrain = false;
    bool rejectInvocation = false;
    bool uncertainFence = false;
};

// Shared /6 and /8 queue/fence custody checks. The caller supplies its real
// original graph, seal, native closure and readback binding. No new executor,
// lifecycle version, completion receipt or alternate resource journal.
class VulkanOriginalRootTerminalFixture final {
public:
    template <class Command,
        class Closure,
        class Seal,
        class Reservation,
        class Binding,
        class Transaction,
        class Root,
        class Quotas,
        class CheckResources>
    static void verify(const VulkanOriginalRootTerminalScenario& scenario,
        Command& command,
        Closure& closure,
        Seal& seal,
        Reservation& reservation,
        const Binding& binding,
        const Transaction& tx,
        const Root& root,
        rhi::GpuDevice& device,
        const Quotas& hotQuotas,
        core::pmr::String& diagnostic,
        CheckResources checkResources)
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        using Hooks = RootFenceNegativeObservation;
        assert(!scenario.rejectInvocation || scenario.uncertainSubmit);
        assert(!scenario.uncertainFence || scenario.deviceIdleDrain);
        const bool unpublished = scenario.uncertainSubmit || scenario.uncertainFence;
        const auto queryReadsBefore = Hooks::queryReadCalls;
        const auto& result = binding->originalResult();
        std::weak_ptr<const OriginalCommandRecordingSeal> weakSeal = seal;
        std::weak_ptr<const VulkanBindlessRootNativeClosure> weakClosure = closure;
        const auto checkRetained = [&] {
            assert(!weakSeal.expired() && !weakClosure.expired() && !result->publishedBytes()
                && !result->nativeExecutionProven()
                && root->originalSnapshot()->originalSubmissionBudgetOwner()->used() == 1u);
            checkResources();
        };
        Hooks::omitQueueEntrypoint = true;
        bool accepted = true;
        assert(!command->submit({}, diagnostic, &accepted) && !accepted
            && diagnostic == "Vulkan RHI native queue submit missing Vulkan entrypoints"
            && tx->status() == OriginalCommandStateTransaction::Status::Pending);
        checkRetained();
        Hooks::omitQueueEntrypoint = false;
        Hooks::withholdCompletion = true;
        Hooks::uncertainQueueResult = scenario.uncertainSubmit;
        Hooks::rejectQueueInvocation = scenario.rejectInvocation;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            hotQuotas, [&]() noexcept -> const char* {
                bool accepted = true;
                const auto succeeded = command->submit({}, diagnostic, &accepted);
                assert(succeeded == !scenario.uncertainSubmit
                    && accepted == !scenario.uncertainSubmit);
                return nullptr;
            }));
        Hooks::uncertainQueueResult = false;
        Hooks::rejectQueueInvocation = false;
        seal.reset();
        reservation.reset();
        closure.reset();
        // Drop the caller's native command too: the existing retirement queue
        // alone retains the entire native command/canonical resource closure.
        std::weak_ptr<VulkanRhiNativeCommandPayload> weakCommand = command;
        command.reset();
        assert(!weakCommand.expired());
        command = weakCommand.lock();
        checkRetained();
        assert(!command->submittedFenceCompleted());
        assert(!command->waitSubmittedFenceUntil(
                   std::chrono::steady_clock::now() + std::chrono::milliseconds(1), diagnostic)
            && diagnostic
                == (scenario.uncertainSubmit ? "RHI-VULKAN-ROOT-FENCE-QUEUE-QUARANTINED"
                                             : "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED"));
        assert(!drainVulkanRhiSubmittedCommandsUntil(
            std::chrono::steady_clock::now() + std::chrono::milliseconds(1), diagnostic));
        drainVulkanRhiSubmittedCommandsForShutdown();
        retireCompletedVulkanRhiSubmittedCommands();
        checkRetained();
        if (scenario.uncertainFence) {
            Hooks::withholdCompletion = false;
            Hooks::uncertainFenceResult = true;
            assert(!command->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
            Hooks::uncertainFenceResult = false;
            Hooks::withholdCompletion = true;
            assert(tx->status() == OriginalCommandStateTransaction::Status::Invalidated);
            checkRetained();
        }
        assert(tx->status()
            == (unpublished ? OriginalCommandStateTransaction::Status::Invalidated
                            : OriginalCommandStateTransaction::Status::Pending));
        if (scenario.deviceIdleDrain) {
            tests::VulkanHeadlessRuntime foreign(true, Hooks::resolve);
            assert(foreign.runtime()
                && !waitIdleAndRetireVulkanRhiSubmittedCommands(foreign.runtime()));
            checkRetained();
            assert(!command->submittedFenceCompleted());
            Hooks::omitIdleEntrypoint = true;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                hotQuotas, [&]() noexcept -> const char* {
                    device.waitIdle();
                    return nullptr;
                }));
            Hooks::omitIdleEntrypoint = false;
            checkRetained();
            Hooks::uncertainIdleResult = true;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                hotQuotas, [&]() noexcept -> const char* {
                    device.waitIdle();
                    return nullptr;
                }));
            Hooks::uncertainIdleResult = false;
            checkRetained();
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                hotQuotas, [&]() noexcept -> const char* {
                    device.waitIdle();
                    assert(command->submittedFenceCompleted());
                    return nullptr;
                }));
            Hooks::withholdCompletion = false;
        } else {
            Hooks::withholdCompletion = false;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                hotQuotas, [&]() noexcept -> const char* {
                    return command->waitSubmittedFenceUntil(
                               std::chrono::steady_clock::now() + std::chrono::seconds(5),
                               diagnostic)
                        ? nullptr
                        : diagnostic.c_str();
                }));
        }
        assert(weakSeal.expired() && weakClosure.expired()
            && root->originalSnapshot()->originalSubmissionBudgetOwner()->used() == 0u);
        assert(tx->status()
            == (unpublished ? OriginalCommandStateTransaction::Status::Invalidated
                            : OriginalCommandStateTransaction::Status::Committed));
        assert(result->nativeExecutionProven() == !unpublished);
        assert(bool(result->publishedBytes()) == !unpublished);
        // Idle can drain custody after a rejected queue invocation, but it
        // cannot prove that the recorded query reset/writes ever executed.
        if (unpublished)
            assert(Hooks::queryReadCalls == queryReadsBefore);
        checkResources();
        assert(!command->submit(diagnostic));
        retireCompletedVulkanRhiSubmittedCommands();
    }
};
} // namespace arp::test
