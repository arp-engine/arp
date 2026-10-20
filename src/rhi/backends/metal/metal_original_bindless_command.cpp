#include "rhi/backends/metal/metal_original_bindless_command.h"

#include "rhi/backends/metal/metal_native_commands.h"

namespace arp::rhi_metal {
using namespace rhi;

MetalOriginalBindlessCommand::MetalOriginalBindlessCommand(ConstructionKey,
    std::shared_ptr<const Closure> closure,
    std::shared_ptr<const Reservation> reservation,
    std::shared_ptr<const Binding> binding,
    BindlessFaultSubmissionLease fault,
    std::shared_ptr<const MetalOriginalBindlessGraphicsPass> graphics,
    std::shared_ptr<const Closure> nextClosure,
    std::shared_ptr<const Closure> finalClosure,
    const rhi::OriginalBindlessRootReadbackSequence::Input& readbackInput,
    const rhi::OriginalBindlessRootNativeSequence<Closure>::Input& rootInput,
    const PassInput& passInput)
    : roots_({std::move(closure), std::move(nextClosure), std::move(finalClosure)}, rootInput),
      closure_(roots_.atOrEmpty(0u)), nextClosure_(roots_.atOrEmpty(1u)),
      finalClosure_(roots_.atOrEmpty(2u)), reservation_(std::move(reservation)),
      readbacks_(std::move(binding), readbackInput), binding_(readbacks_.faultBinding()),
      graphics_(std::move(graphics)), fault_(std::move(fault))
{
    producedPasses_.reserve(passInput.count);
    for (std::size_t i = 0u; i < passInput.count; ++i)
        producedPasses_.push_back(passInput.passes[i]);
}

MetalOriginalBindlessCommand::~MetalOriginalBindlessCommand()
{
    if (phase_ == Life::Phase::Claimed || phase_ == Life::Phase::RecordingPending
        || phase_ == Life::Phase::Recorded || phase_ == Life::Phase::SubmitReserved)
        reservation_->originalSeal()->rollbackOriginalRecordingPlan();
    else if (phase_ != Life::Phase::Completed && phase_ != Life::Phase::Abandoned)
        reservation_->originalSeal()->invalidateOriginalOwnership();
}

const char* MetalOriginalBindlessCommand::advance(Life::Event event) noexcept
{
    const auto transition = Life::inspectTransition(phase_, event);
    if (!transition.accepted())
        return transition.diagnostic;
    phase_ = transition.next;
    return nullptr;
}

const char* MetalOriginalBindlessCommand::validatePending() const noexcept
{
    if (const auto* id = reservation_->validateRetainedOwnership())
        return id;
    if (const auto* id = additionalFaults_.validatePending(
            readbacks_, reservation_->originalSeal()->originalRoot(), fault_))
        return id;
    for (const auto& selected : roots_.closures())
        if (const auto* id =
                Closure::validateOriginalPending(selected, reservation_->originalSeal()))
            return id;
    if (nextClosure_ && producedPasses_.empty()) {
        if (const auto* id = nextClosure_->storageExecutionDiagnostic())
            return id;
        if (finalClosure_) {
            if (const auto* id = finalClosure_->storageExecutionDiagnostic())
                return id;
            for (const auto& entry : closure_->entries_)
                if (entry.completedDepth)
                    if (const auto* id =
                            reservation_->originalSeal()->validateCompletedDepthReadPending(
                                {entry.completedDepth,
                                    ResourceState::ShaderRead,
                                    PipelineStage::ComputeShader,
                                    ResourceAccess::ShaderRead},
                                2u))
                        return id;
        }
        if (const auto* id = validateStoragePasses())
            return id;
    }
    if (graphics_) {
        if (const auto* id = graphics_->validateNativeOwners(reservation_->originalSeal()))
            return id;
        // All reads consume the existing journal, including at real completion.
        // Depth uses its exact producer proof; color keeps its own pending stamp.
        const auto& seal = reservation_->originalSeal();
        const bool ordinaryDepth = seal->originalBudget().contractVersion == 19u;
        if (ordinaryDepth)
            for (const auto& entry : closure_->entries_)
                if (entry.completedDepth)
                    if (const auto* id = seal->validateCompletedDepthReadPending(
                            {entry.completedDepth,
                                ResourceState::ShaderRead,
                                PipelineStage::FragmentShader,
                                ResourceAccess::ShaderRead},
                            3u))
                        return id;
        for (std::size_t group = 1u; group < closure_->groups_.size(); ++group)
            for (const auto& view :
                closure_->groups_[group].descriptors()->originalCompletedTextureSources) {
                const auto& object = *view->originalParentLease()->originalObject();
                if (ordinaryDepth && view->originalObject()->desc().aspect == TextureAspect::Depth)
                    continue;
                const auto completed = object.observeOriginalContent();
                if (!completed)
                    return "RHI-METAL-ORDINARY-COLOR-COMPLETED-CONTENT";
                if (const auto* id = seal->validateCompletedTextureReadPending(object,
                        *completed,
                        ordinaryDepth ? 8u
                            : seal->originalRoot()->originalBindlessCompletedReadContractVersion()
                                == 2u
                            ? 7u
                            : 5u))
                    return id;
            }
    }
    for (const auto& pass : producedPasses_)
        if (const auto* id = pass->validateNativeOwners(reservation_->originalSeal()))
            return id;
    return nullptr;
}

bool MetalOriginalBindlessCommand::beforeSubmit(
    const MetalRhiNativeCommandPayload& command, core::pmr::String& diagnostic) noexcept
{
    const auto* id = command_ == &command ? nullptr : "RHI-METAL-ROOT-COMMAND-IDENTITY";
    if (!id)
        id = validatePending();
    if (!id)
        id = advance(Life::Event::ReserveSubmission);
    if (id) {
        diagnostic = id;
        return false;
    }
    return true;
}

const char* MetalOriginalBindlessCommand::beforeNativeCommit() noexcept
{
    return advance(Life::Event::MaySubmit);
}

void MetalOriginalBindlessCommand::abandonUnsubmitted() noexcept
{
    if (phase_ == Life::Phase::SubmitPending || phase_ == Life::Phase::Completed
        || phase_ == Life::Phase::QuarantinedAfterSubmit)
        return;
    (void)advance(Life::Event::Cancel);
    if (phase_ == Life::Phase::QuarantinedBeforeSubmit)
        (void)advance(Life::Event::BeforeSubmitScopesDrained);
    reservation_->originalSeal()->rollbackOriginalRecordingPlan();
    additionalFaults_.release();
    fault_.release();
}

// A view over the original native custody and its actual completed state.
// Dropping the view after submission never cancels the callback's retention.
class MetalOriginalBindlessExecutionView final : public OriginalBindlessRootExecution {
    class ConstructionKey {
        friend class MetalOriginalBindlessExecutionView;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalOriginalBindlessExecutionView(ConstructionKey, std::uint64_t diagnostics)
    {
        diagnostic_.reserve(diagnostics);
    }

    static std::shared_ptr<const OriginalBindlessRootExecution> prepare(GpuDevice& device,
        const OriginalBindlessRootFenceExecutionInputs& inputs,
        const OriginalBindlessRootFenceExecutionBudget& budget,
        core::pmr::String& diagnostic)
    {
        const auto caller = core::currentCpuMemoryQuota();
        const auto fail =
            [&](const char* id) -> std::shared_ptr<const OriginalBindlessRootExecution> {
            core::CpuMemoryQuotaScope scope(caller);
            diagnostic = id;
            return {};
        };
        try {
            core::CpuMemoryQuotaScope scope(inputs.viewQuota);
            auto view = shareOriginal(core::makeUnique<MetalOriginalBindlessExecutionView>(
                ConstructionKey{}, budget.maxDiagnosticBytes));
            const char* id = nullptr;
            const auto prepareClosure = [&](const auto& root, const char*& failure) {
                core::CpuMemoryQuotaScope nativeScope(inputs.closureQuota);
                const auto& limits = inputs.roots.closureBudget(root, budget.closure);
                const auto selected = inputs.roots.initializedSources(
                    root, {nullptr, 0u, inputs.initializedShader, inputs.initializedShaderCount});
                core::pmr::Vector<std::shared_ptr<const MetalOriginalGeometryContent>> geometry{
                    &core::defaultCpuMemoryResource()};
                core::pmr::Vector<std::shared_ptr<const MetalOriginalGeometryContent>> shader{
                    &core::defaultCpuMemoryResource()};
                if ((failure = OriginalBindlessInitializedSourcesContract::project(
                         limits, selected, geometry, shader)))
                    return std::shared_ptr<const MetalRhiBindlessRootNativeClosure>{};
                return MetalRhiBindlessRootNativeClosure::prepareOriginal(root,
                    inputs.inventory,
                    device,
                    limits,
                    inputs.closureQuota,
                    failure,
                    {geometry.data(), geometry.size(), shader.data(), shader.size()});
            };
            using Sequence = OriginalBindlessRootNativeSequence<MetalRhiBindlessRootNativeClosure>;
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
            // Direct dispatch uses native MTLSize, while this closed kernel's
            // thread_position_in_grid arithmetic is uint32. Bound groups by
            // that exact ABI; the authentic pipeline separately checks the
            // device's actual threads-per-threadgroup support.
            constexpr auto gridLimit =
                UINT32_MAX / visibility::GpuVisibilityDispatchContract::localSizeX;
            auto* extension = static_cast<BindlessCrossExtension*>(
                device.queryInterface(kExtensionBindlessCross));
            if (!extension)
                return fail("RHI-METAL-PRODUCED-GEOMETRY-DEVICE-ISSUER");
            if ((id = inputs.producedGeometry.inspectNative(closures.data(),
                     closures.size(),
                     inputs.transaction->originalGraph(),
                     inputs.readback->originalProgram(),
                     inputs.readback,
                     inputs.readbacks,
                     gridLimit,
                     extension->capabilities().indirectFirstInstance)))
                return fail(id);
            if (inputs.transaction->hasOriginalBindlessProducedGeometryProfile()) {
                const auto program = rhi::OriginalBindlessProducedProgramContract::inspectNative<
                    MetalRhiBindlessRootNativeClosure>(inputs.readback->originalProgram(),
                    nativeRoots,
                    inputs.readback,
                    inputs.readbacks,
                    inputs.producedGeometry.commands);
                if ((id = program.diagnostic))
                    return fail(id);
            }
            core::pmr::Vector<std::shared_ptr<const MetalOriginalBindlessGraphicsPass>> passes{
                &core::defaultCpuMemoryResource()};
            {
                core::CpuMemoryQuotaScope nativeScope(inputs.producedPasses.quota);
                if ((id = OriginalBindlessGraphicsPassSequenceContract::prepare(closures.data(),
                         closures.size(),
                         inputs.readback->originalProgram(),
                         inputs.producedPasses,
                         passes,
                         [&](const auto& selected, const auto& passBudget, const char*& failure) {
                             return MetalOriginalBindlessGraphicsPass::prepareOriginal(selected,
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
            view->command_ = MetalOriginalBindlessCommand::prepareOriginal(closure,
                reservation,
                inputs.readback,
                device,
                view->diagnostic_,
                true,
                {},
                nextClosure,
                inputs.attachmentCapacity,
                finalClosure,
                {},
                inputs.readbacks,
                nativeRoots,
                {passes.data(), passes.size()},
                inputs.producedGeometry);
            if (!view->command_)
                return fail(view->diagnostic_.c_str());
            view->done_ = view->command_->preparedCompletionState();
            view->profile_ = budget.intent.contractVersion;
            view->transaction_ = inputs.transaction;
            view->result_ = inputs.readback->originalResult();
            diagnostic.clear();
            return view;
        } catch (const std::bad_alloc&) {
            return fail("RHI-METAL-ROOT-EXECUTION-VIEW-CAPACITY");
        } catch (const std::length_error&) {
            return fail("RHI-METAL-ROOT-EXECUTION-VIEW-CAPACITY");
        }
    }

    const char* cancelExecution() const noexcept override
    {
        if (!command_)
            return "RHI-METAL-ROOT-EXECUTION-CANCELLED";
        if (submitted_)
            return "RHI-METAL-ROOT-EXECUTION-CANCEL-IN-FLIGHT";
        command_.reset();
        return nullptr;
    }

    bool recordingProven() const noexcept override
    {
        return command_ && command_->originalRecordingClosed_;
    }

    bool submissionCompleted() const noexcept override
    {
        return submitted_ && done_->isComplete() && !executionDiagnostic();
    }

    const char* executionDiagnostic() const noexcept override
    {
        if (!command_)
            return "RHI-METAL-ROOT-EXECUTION-CANCELLED";
        return done_->isComplete()
                && done_->outcome() == MetalRhiCompletionState::Outcome::Completed
                && transaction_->status() == OriginalCommandStateTransaction::Status::Committed
                && result_->publishedBytes()
            ? nullptr
            : "RHI-METAL-ROOT-EXECUTION-COMPLETION-UNPROVEN";
    }

    std::uint32_t pendingRecords() const noexcept override
    {
        return submitted_ && !done_->isComplete() ? 1u : 0u;
    }

protected:
    const char* recordExecution(const Request& request) const noexcept override
    {
        if (request.contractVersion != profile_)
            return "RHI-METAL-ROOT-EXECUTION-PROFILE";
        if (!command_)
            return "RHI-METAL-ROOT-EXECUTION-CANCELLED";
        auto custody = std::dynamic_pointer_cast<MetalOriginalBindlessCommand>(
            command_->originalCommandCustody_);
        if (!custody || custody->phase_ != OriginalCommandNativeLifecycleContract::Phase::Claimed)
            return "RHI-METAL-ROOT-EXECUTION-RECORD-PHASE";
        try {
            core::CpuMemoryQuotaScope scope(custody->originalMetadataQuota());
            if (custody->record(*command_, diagnostic_)) {
                command_->originalRecordingClosed_ = true;
                return nullptr;
            }
            command_.reset();
            return diagnostic_.c_str();
        } catch (const std::bad_alloc&) {
            command_.reset();
            return "RHI-METAL-ROOT-EXECUTION-RECORD-CAPACITY";
        } catch (const std::length_error&) {
            command_.reset();
            return "RHI-METAL-ROOT-EXECUTION-RECORD-CAPACITY";
        }
    }

    const char* submitExecution(const Request& request) const noexcept override
    {
        if (request.contractVersion != profile_)
            return "RHI-METAL-ROOT-EXECUTION-PROFILE";
        if (!recordingProven() || submitted_)
            return "RHI-METAL-ROOT-EXECUTION-SUBMIT-PHASE";
        try {
            core::CpuMemoryQuotaScope scope(originalMetadataQuota());
            if (!command_->submit(queue_, done_, {}, 0u, {}, 0u, diagnostic_))
                return diagnostic_.c_str();
            submitted_ = true;
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-METAL-ROOT-EXECUTION-SUBMIT-CAPACITY";
        } catch (const std::length_error&) {
            return "RHI-METAL-ROOT-EXECUTION-SUBMIT-CAPACITY";
        }
    }

private:
    mutable std::shared_ptr<MetalRhiNativeCommandPayload> command_;
    std::shared_ptr<MetalRhiCompletionState> done_;
    mutable MetalRhiNativeQueueStats queue_;
    std::shared_ptr<OriginalCommandStateTransaction> transaction_;
    std::shared_ptr<const OriginalCommandBufferReadbackResultSnapshot> result_;
    mutable core::pmr::String diagnostic_{&core::defaultCpuMemoryResource()};
    mutable bool submitted_ = false;
    std::uint32_t profile_ = 0u;
};

const char* MetalOriginalBindlessCommand::validateGeometryFetch(
    const std::shared_ptr<const Closure>& closure,
    const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& program,
    const rhi::OriginalIndexedDrawFetchContract::Budget& fetch) noexcept
{
    if (fetch.contractVersion != 2u)
        return "RHI-METAL-GEOMETRY-FENCE-FETCH-VERSION";
    const auto fetched =
        program->inspectInitializedGeometry(fetch, [&](std::size_t source) noexcept {
            return closure->initializedGeometry(source);
        });
    if (fetched.diagnostic)
        return fetched.diagnostic;
    if (!fetched.draws || !fetched.streams || !fetched.analyzedIndices)
        return "RHI-METAL-GEOMETRY-FENCE-FETCH-COVERAGE";
    return nullptr;
}

std::shared_ptr<const OriginalBindlessRootExecution> prepareMetalRhiOriginalBindlessRootExecution(
    GpuDevice& device,
    const OriginalBindlessRootFenceExecutionInputs& inputs,
    const OriginalBindlessRootFenceExecutionBudget& budget,
    core::pmr::String& diagnostic)
{
    return MetalOriginalBindlessExecutionView::prepare(device, inputs, budget, diagnostic);
}

// A public view over the existing native command custody and real completion.
// It retains observations only; nativeCompleted remains the sole publisher.
class MetalOriginalBindlessGraphicsExecutionView final : public OriginalBindlessGraphicsExecution {
    class ConstructionKey {
        friend class MetalOriginalBindlessGraphicsExecutionView;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalOriginalBindlessGraphicsExecutionView(ConstructionKey, std::uint64_t diagnostics)
    {
        diagnostic_.reserve(diagnostics);
    }

    static std::shared_ptr<const OriginalBindlessGraphicsExecution> prepare(GpuDevice& device,
        const OriginalBindlessGraphicsExecutionInputs& inputs,
        const OriginalBindlessGraphicsExecutionBudget& budget,
        core::pmr::String& diagnostic)
    {
        const auto caller = core::currentCpuMemoryQuota();
        const auto fail =
            [&](const char* id) -> std::shared_ptr<const OriginalBindlessGraphicsExecution> {
            core::CpuMemoryQuotaScope scope(caller);
            diagnostic = id;
            return {};
        };
        if (budget.closure.contractVersion != 1u && budget.closure.contractVersion != 3u
            && budget.closure.contractVersion != 16u)
            return fail("RHI-METAL-ROOT-GRAPHICS-EXECUTION-PROFILE");
        try {
            core::CpuMemoryQuotaScope scope(inputs.viewQuota);
            auto view = shareOriginal(core::makeUnique<MetalOriginalBindlessGraphicsExecutionView>(
                ConstructionKey{}, budget.maxDiagnosticBytes));
            const char* id = nullptr;
            core::pmr::Vector<std::shared_ptr<const MetalOriginalGeometryContent>> geometry{
                &core::defaultCpuMemoryResource()};
            geometry.reserve(inputs.geometryCount);
            for (std::size_t i = 0u; i < inputs.geometryCount; ++i) {
                auto actor = std::dynamic_pointer_cast<const MetalOriginalGeometryContent>(
                    inputs.geometry[i]);
                if ((id = MetalOriginalGeometryContent::validateOriginal(actor)))
                    return fail(id);
                geometry.push_back(std::move(actor));
            }
            core::pmr::Vector<std::shared_ptr<const MetalOriginalGeometryContent>> shader{
                &core::defaultCpuMemoryResource()};
            if ((id = OriginalBindlessInitializedShaderContract::project(
                     budget.closure.initializedShader,
                     inputs.initializedShader,
                     inputs.initializedShaderCount,
                     shader)))
                return fail(id);
            auto closure = MetalRhiBindlessRootNativeClosure::prepareOriginal(inputs.root,
                inputs.inventory,
                device,
                budget.closure,
                inputs.closureQuota,
                id,
                {geometry.data(), geometry.size(), shader.data(), shader.size()});
            if (!closure)
                return fail(id);
            if ((id = OriginalBindlessSceneContentContract::inspectNative(
                     closure, inputs.readback->originalProgram(), inputs.scene)))
                return fail(id);
            if (inputs.geometryCount)
                if ((id = MetalOriginalBindlessCommand::validateGeometryFetch(
                         closure, inputs.readback->originalProgram(), budget.fetch)))
                    return fail(id);
            auto pass = MetalOriginalBindlessGraphicsPass::prepareOriginal(closure,
                inputs.transaction,
                inputs.readback->originalProgram(),
                device,
                budget.pass,
                inputs.passCapacity,
                inputs.passQuota,
                id);
            if (!pass)
                return fail(id);
            const auto reservationOwners = std::uint64_t(inputs.transaction->retainedOwnerCount())
                + OriginalCommandRecordingSeal::additionalOwnerCountFor(budget.lifecycle)
                + OriginalCommandLifecycleReservation::additionalOwnerCount;
            const auto baseOwners = MetalOriginalBindlessCommand::submissionOwnerCount(
                *closure, reservationOwners, *inputs.readback, pass.get());
            if (baseOwners > budget.lifecycle.maxSubmissionOwners
                || OriginalBindlessRootReadbackSequence::additionalOwnerBound(inputs.readbacks)
                    > budget.lifecycle.maxSubmissionOwners - baseOwners)
                return fail("RHI-METAL-ROOT-COMMAND-OWNER-CAPACITY");
            auto seal = OriginalCommandRecordingSeal::prepareOriginal(
                inputs.transaction, device, budget.lifecycle, inputs.submissionQuota, {}, id);
            if (!seal)
                return fail(id);
            auto reservation =
                OriginalCommandLifecycleReservation::prepareOriginal(seal, budget.capacity, id);
            if (!reservation)
                return fail(id);
            view->command_ = MetalOriginalBindlessCommand::prepareOriginal(closure,
                reservation,
                inputs.readback,
                device,
                view->diagnostic_,
                true,
                pass,
                {},
                {},
                {},
                budget.fetch,
                inputs.readbacks);
            if (!view->command_)
                return fail(view->diagnostic_.c_str());
            view->done_ = view->command_->preparedCompletionState();
            view->transaction_ = inputs.transaction;
            view->result_ = inputs.readback->originalResult();
            view->version_ = budget.intent.contractVersion;
            diagnostic.clear();
            return view;
        } catch (const std::bad_alloc&) {
            return fail("RHI-METAL-ROOT-GRAPHICS-VIEW-CAPACITY");
        } catch (const std::length_error&) {
            return fail("RHI-METAL-ROOT-GRAPHICS-VIEW-CAPACITY");
        }
    }

    std::uint32_t executionContractVersion() const noexcept override { return version_; }

    const char* cancel() const noexcept override
    {
        if (!command_)
            return "RHI-METAL-ROOT-GRAPHICS-CANCELLED";
        if (submitted_)
            return "RHI-METAL-ROOT-GRAPHICS-CANCEL-IN-FLIGHT";
        command_.reset();
        return nullptr;
    }

    bool pollCompletion() const noexcept override { return submitted_ && done_->isComplete(); }

    bool recordingProven() const noexcept override
    {
        return command_ && command_->originalRecordingClosed_;
    }

    bool submissionCompleted() const noexcept override
    {
        return pollCompletion() && !completionDiagnostic();
    }

    const char* completionDiagnostic() const noexcept override
    {
        if (!command_)
            return "RHI-METAL-ROOT-GRAPHICS-CANCELLED";
        return pollCompletion() && done_->outcome() == MetalRhiCompletionState::Outcome::Completed
                && transaction_->status() == OriginalCommandStateTransaction::Status::Committed
                && result_->publishedBytes()
            ? nullptr
            : "RHI-METAL-ROOT-GRAPHICS-COMPLETION-UNPROVEN";
    }

protected:
    const char* recordOriginal() const noexcept override
    {
        if (!command_)
            return "RHI-METAL-ROOT-GRAPHICS-CANCELLED";
        auto custody = std::dynamic_pointer_cast<MetalOriginalBindlessCommand>(
            command_->originalCommandCustody_);
        if (!custody || custody->phase_ != OriginalCommandNativeLifecycleContract::Phase::Claimed)
            return "RHI-METAL-ROOT-GRAPHICS-RECORD-PHASE";
        try {
            core::CpuMemoryQuotaScope scope(custody->originalMetadataQuota());
            if (custody->record(*command_, diagnostic_)) {
                command_->originalRecordingClosed_ = true;
                return nullptr;
            }
            command_.reset();
            return diagnostic_.c_str();
        } catch (const std::bad_alloc&) {
            command_.reset();
            return "RHI-METAL-ROOT-GRAPHICS-RECORD-CAPACITY";
        } catch (const std::length_error&) {
            command_.reset();
            return "RHI-METAL-ROOT-GRAPHICS-RECORD-CAPACITY";
        }
    }

    const char* submitOriginal() const noexcept override
    {
        if (!recordingProven() || submitted_)
            return "RHI-METAL-ROOT-GRAPHICS-SUBMIT-PHASE";
        try {
            core::CpuMemoryQuotaScope scope(originalMetadataQuota());
            if (!command_->submit(queue_, done_, {}, 0u, {}, 0u, diagnostic_))
                return diagnostic_.c_str();
            submitted_ = true;
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-METAL-ROOT-GRAPHICS-SUBMIT-CAPACITY";
        } catch (const std::length_error&) {
            return "RHI-METAL-ROOT-GRAPHICS-SUBMIT-CAPACITY";
        }
    }

private:
    mutable std::shared_ptr<MetalRhiNativeCommandPayload> command_;
    std::shared_ptr<MetalRhiCompletionState> done_;
    std::shared_ptr<const OriginalCommandStateTransaction> transaction_;
    std::shared_ptr<const OriginalCommandBufferReadbackResultSnapshot> result_;
    mutable MetalRhiNativeQueueStats queue_;
    mutable core::pmr::String diagnostic_{&core::defaultCpuMemoryResource()};
    mutable bool submitted_ = false;
    std::uint32_t version_ = 1u;
};

std::shared_ptr<const OriginalBindlessGraphicsExecution>
prepareMetalRhiOriginalBindlessGraphicsExecution(GpuDevice& device,
    const OriginalBindlessGraphicsExecutionInputs& inputs,
    const OriginalBindlessGraphicsExecutionBudget& budget,
    core::pmr::String& diagnostic)
{
    return MetalOriginalBindlessGraphicsExecutionView::prepare(device, inputs, budget, diagnostic);
}
} // namespace arp::rhi_metal
