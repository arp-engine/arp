#include "rhi/backends/vulkan/vulkan_bindless.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/backends/vulkan/vulkan_original_bindless_command.h"
#include "rhi/frontend/bindless_root_fence_execution.h"

namespace arp::rhi_vulkan {
using namespace rhi;

// A public view of the original command/custody and result. The existing queue
// pool alone retains an in-flight command after callers drop this view.
class VulkanOriginalBindlessComputeExecutionView final : public OriginalBindlessRootExecution {
    friend class VulkanBindlessCrossExtension;

    class ConstructionKey {
        friend class VulkanBindlessCrossExtension;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    VulkanOriginalBindlessComputeExecutionView(
        ConstructionKey, std::size_t diagnostics, std::uint32_t version)
        : executionVersion_(version)
    {
        diagnostic_.reserve(diagnostics);
    }

    static auto share(core::UniquePtr<VulkanOriginalBindlessComputeExecutionView> view)
    {
        return shareOriginal(std::move(view));
    }

    const char* cancelExecution() const noexcept override
    {
        if (!command_)
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-CANCELLED";
        if (command_->impl_->submitted || command_->impl_->originalSubmissionUnproven)
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-CANCEL-IN-FLIGHT";
        command_.reset();
        return nullptr;
    }

    bool recordingProven() const noexcept override { return command_ && command_->impl_->ended; }

    bool submissionCompleted() const noexcept override
    {
        return command_ && command_->impl_->submitFenceCompleted && !executionDiagnostic();
    }

    bool pollCompletion() const noexcept override
    {
        if (!command_)
            return false;
        const bool done = command_->submittedFenceCompleted();
        if (done)
            retireCompletedVulkanRhiSubmittedCommands();
        return done;
    }

    const char* executionDiagnostic() const noexcept override
    {
        if (!command_)
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-CANCELLED";
        return command_->impl_->submitFenceCompleted && transaction_ && result_
                && transaction_->status() == OriginalCommandStateTransaction::Status::Committed
                && result_->publishedBytes()
            ? nullptr
            : "RHI-BINDLESS-ROOT-FENCE-EXECUTION-COMPLETION-UNPROVEN";
    }

    std::uint32_t pendingRecords() const noexcept override
    {
        return command_
                && (command_->impl_->submitted || command_->impl_->originalSubmissionUnproven)
                && !command_->impl_->submitFenceCompleted
            ? 1u
            : 0u;
    }

protected:
    const char* recordExecution(const Request& request) const noexcept override
    {
        if (request.contractVersion != executionVersion_)
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-VERSION";
        if (!command_)
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-CANCELLED";
        auto custody = std::dynamic_pointer_cast<VulkanOriginalBindlessCommand>(
            command_->impl_->originalBindlessCustody);
        if (!custody || custody->command_ != command_.get()
            || custody->phase_ != OriginalCommandNativeLifecycleContract::Phase::Claimed)
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-RECORD-PHASE";
        try {
            core::CpuMemoryQuotaScope scope(originalMetadataQuota());
            if (custody->record(*command_, diagnostic_))
                return nullptr;
        } catch (const std::bad_alloc&) {
            command_.reset();
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-DIAGNOSTIC-CAPACITY";
        }
        command_.reset();
        return diagnostic_.c_str();
    }

    const char* submitExecution(const Request& request) const noexcept override
    {
        if (request.contractVersion != executionVersion_)
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-VERSION";
        if (!command_)
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-CANCELLED";
        if (!command_->impl_->ended || command_->impl_->submitted
            || command_->impl_->originalSubmissionUnproven)
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-SUBMIT-PHASE";
        try {
            core::CpuMemoryQuotaScope scope(originalMetadataQuota());
            return command_->submit(diagnostic_) ? nullptr : diagnostic_.c_str();
        } catch (const std::bad_alloc&) {
            return "RHI-BINDLESS-ROOT-FENCE-EXECUTION-DIAGNOSTIC-CAPACITY";
        }
    }

private:
    const std::uint32_t executionVersion_;
    mutable std::shared_ptr<VulkanRhiNativeCommandPayload> command_;
    std::shared_ptr<const OriginalCommandStateTransaction> transaction_;
    std::shared_ptr<const OriginalCommandBufferReadbackResultSnapshot> result_;
    mutable core::pmr::String diagnostic_{&core::defaultCpuMemoryResource()};
};

std::shared_ptr<const OriginalBindlessRootExecution>
VulkanBindlessCrossExtension::prepareOriginalRootExecutionPayload(GpuDevice& device,
    const OriginalBindlessRootFenceExecutionInputs& inputs,
    const OriginalBindlessRootFenceExecutionBudget& budget,
    core::pmr::String& diagnostic) const
{
    const auto callerQuota = core::currentCpuMemoryQuota();
    const auto fail = [&](const char* id) -> std::shared_ptr<const OriginalBindlessRootExecution> {
        core::CpuMemoryQuotaScope callerScope(callerQuota);
        diagnostic = id;
        return {};
    };
    const char* id = nullptr;
    try {
        core::CpuMemoryQuotaScope scope(inputs.viewQuota);
        auto view = VulkanOriginalBindlessComputeExecutionView::share(
            core::makeUnique<VulkanOriginalBindlessComputeExecutionView>(
                VulkanOriginalBindlessComputeExecutionView::ConstructionKey{},
                budget.maxDiagnosticBytes,
                budget.intent.contractVersion));
        const auto prepareClosure = [&](const auto& root, const char*& failure) {
            core::CpuMemoryQuotaScope nativeScope(inputs.closureQuota);
            const auto& limits = inputs.roots.closureBudget(root, budget.closure);
            const auto selected = inputs.roots.initializedSources(
                root, {nullptr, 0u, inputs.initializedShader, inputs.initializedShaderCount});
            core::pmr::Vector<std::shared_ptr<const VulkanOriginalGeometryContent>> geometry{
                &core::defaultCpuMemoryResource()};
            core::pmr::Vector<std::shared_ptr<const VulkanOriginalGeometryContent>> shader{
                &core::defaultCpuMemoryResource()};
            if ((failure = OriginalBindlessInitializedSourcesContract::project(
                     limits, selected, geometry, shader)))
                return std::shared_ptr<const VulkanBindlessRootNativeClosure>{};
            return VulkanBindlessRootNativeClosure::prepareOriginal(root,
                inputs.inventory,
                device,
                limits,
                inputs.closureQuota,
                failure,
                {geometry.data(), geometry.size(), shader.data(), shader.size()});
        };
        using Sequence = OriginalBindlessRootNativeSequence<VulkanBindlessRootNativeClosure>;
        std::array<Sequence::Owner, 3u> historical;
        if ((id = Sequence::prepareHistoricalInput(
                 {inputs.root, inputs.nextRoot, inputs.finalRoot}, historical, prepareClosure)))
            return fail(id);
        const auto& closure = historical[0u];
        const auto& nextClosure = historical[1u];
        const auto& finalClosure = historical[2u];
        core::pmr::Vector<Sequence::Owner> closures{&core::defaultCpuMemoryResource()};
        {
            core::CpuMemoryQuotaScope nativeScope(inputs.closureQuota);
            if ((id = Sequence::preparePublicInput(inputs.roots,
                     {closure, nextClosure, finalClosure},
                     inputs.inventory,
                     inputs.readback->originalProgram(),
                     closures,
                     prepareClosure)))
                return fail(id);
        }
        const auto nativeRoots = inputs.roots.empty()
            ? Sequence::Input{}
            : Sequence::Input{closures.data(), closures.size(), inputs.roots.budget};
        const auto& dispatchLimits = runtime_->descriptorIndexing().limits.maxComputeWorkGroupCount;
        if ((id = inputs.producedGeometry.inspectNative(closures.data(),
                 closures.size(),
                 inputs.transaction->originalGraph(),
                 inputs.readback->originalProgram(),
                 inputs.readback,
                 inputs.readbacks,
                 std::min(dispatchLimits[0u], dispatchLimits[1u]),
                 capabilities().indirectFirstInstance)))
            return fail(id);
        if (inputs.transaction->hasOriginalBindlessProducedGeometryProfile()) {
            const auto program = rhi::OriginalBindlessProducedProgramContract::inspectNative<
                VulkanBindlessRootNativeClosure>(inputs.readback->originalProgram(),
                nativeRoots,
                inputs.readback,
                inputs.readbacks,
                inputs.producedGeometry.commands);
            if ((id = program.diagnostic))
                return fail(id);
        }
        core::pmr::Vector<std::shared_ptr<const VulkanOriginalBindlessGraphicsPass>> passes{
            &core::defaultCpuMemoryResource()};
        {
            core::CpuMemoryQuotaScope nativeScope(inputs.producedPasses.quota);
            if ((id = OriginalBindlessGraphicsPassSequenceContract::prepare(closures.data(),
                     closures.size(),
                     inputs.readback->originalProgram(),
                     inputs.producedPasses,
                     passes,
                     [&](const auto& selected, const auto& passBudget, const char*& failure) {
                         return VulkanOriginalBindlessGraphicsPass::prepareOriginal(selected,
                             inputs.transaction,
                             inputs.readback->originalProgram(),
                             device,
                             passBudget,
                             inputs.producedPasses.capacity,
                             inputs.producedPasses.quota,
                             failure);
                     })))
                return fail(id);
        }
        auto seal = OriginalCommandRecordingSeal::prepareOriginal(
            inputs.transaction, device, budget.lifecycle, inputs.submissionQuota, {}, id);
        if (!seal)
            return fail(id);
        auto reservation =
            OriginalCommandLifecycleReservation::prepareOriginal(seal, budget.capacity, id);
        if (!reservation)
            return fail(id);
        view->command_ = VulkanOriginalBindlessCommand::prepareOriginal(closure,
            reservation,
            inputs.readback,
            device,
            view->diagnostic_,
            {},
            {},
            true,
            nextClosure,
            inputs.attachmentCapacity,
            finalClosure,
            inputs.readbacks,
            nativeRoots,
            {passes.data(), passes.size()},
            inputs.producedGeometry);
        if (!view->command_)
            return fail(view->diagnostic_.c_str());
        view->transaction_ = inputs.transaction;
        view->result_ = inputs.readback->originalResult();
        diagnostic.clear();
        return view;
    } catch (const std::bad_alloc&) {
        return fail("RHI-BINDLESS-ROOT-FENCE-EXECUTION-VIEW-CAPACITY");
    }
}
} // namespace arp::rhi_vulkan
