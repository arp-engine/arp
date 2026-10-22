#pragma once
#include "bindless_native_scene_execution_fixture.h"

#if defined(ARP_BINDLESS_SCENE_VULKAN)
#include "vulkan_original_root_terminal_fixture.h"
#elif defined(ARP_BINDLESS_SCENE_METAL)
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_original_bindless_command.h"
#endif

namespace arp::test {
// Observe the original public command with real native completion held back.
// Vulkan hooks inject negative results only; Metal uses a real queue wait.
// No replacement success, journal, lifecycle or resource closure is introduced.
class BindlessNativeSceneSubmissionFixture final {
public:
    static void observe(rhi::GpuDevice& device,
        std::shared_ptr<const rhi::OriginalBindlessGraphicsExecution>& execution,
        const std::shared_ptr<rhi::OriginalCommandStateTransaction>& tx,
        const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        rhi::OriginalBindlessGraphicsExecutionIntent intent,
        unsigned terminal)
    {
        using namespace rhi;
        const auto& capacity = root->originalSnapshot()->originalSubmissionBudgetOwner();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
#if defined(ARP_BINDLESS_SCENE_VULKAN)
        using namespace rhi_vulkan;
        using Hooks = RootFenceNegativeObservation;
        assert(terminal >= 5u && terminal <= 9u);
        Hooks::withholdCompletion = true;
        Hooks::uncertainQueueResult = terminal == 7u;
        const auto* id = OriginalBindlessGraphicsExecution::submit(execution, intent);
        assert((id != nullptr) == (terminal == 7u));
        Hooks::uncertainQueueResult = false;
        assert(capacity->used() == 1u && !execution->pollCompletion());
        if (terminal == 5u)
            execution.reset();
        else
            assert(execution->cancel());
        if (terminal == 6u)
            device.retireOriginalDeviceLifetime();
        if (terminal == 9u) {
            drainVulkanRhiSubmittedCommandsForShutdown();
            assert(capacity->used() == 1u);
        }
        Hooks::withholdCompletion = false;
        Hooks::uncertainFenceResult = terminal == 8u;
        while (capacity->used()) {
            if (execution)
                execution->pollCompletion();
            retireCompletedVulkanRhiSubmittedCommands();
            if (terminal == 7u || terminal == 9u
                || (terminal == 8u
                    && tx->status() == OriginalCommandStateTransaction::Status::Invalidated)) {
                Hooks::uncertainFenceResult = false;
                device.waitIdle();
            }
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        Hooks::uncertainFenceResult = false;
#elif defined(ARP_BINDLESS_SCENE_METAL)
        using namespace rhi_metal;
        assert(terminal == 5u || terminal == 6u);
        auto gate = createMetalRhiNativeSharedEventPayload("scene-public-completion-gate");
        auto blocker = createMetalRhiNativeCommandPayload("scene-public-completion-blocker", {});
        assert(gate && blocker);
        auto blocked = blocker->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        core::pmr::String diagnostic;
        assert(blocker->finish(diagnostic)
            && blocker->submit(queue, blocked, gate, 1u, {}, 0u, diagnostic));
        assert(!OriginalBindlessGraphicsExecution::submit(execution, intent));
        assert(capacity->used() == 1u && !execution->pollCompletion());
        if (terminal == 5u)
            execution.reset();
        else {
            assert(execution->cancel());
            device.retireOriginalDeviceLifetime();
        }
        assert(capacity->used() == 1u);
        signalMetalRhiNativeSharedEvent(gate, 1u);
        while (capacity->used()) {
            if (execution)
                execution->pollCompletion();
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        blocked->wait();
#endif
        execution.reset();
    }
};
} // namespace arp::test
