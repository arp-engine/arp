#include "rhi/backends/vulkan/vulkan_original_bindless_command.h"

#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_bindless.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/frontend/bindless_graphics_execution.h"
#include "rhi/frontend/bindless_produced_program.h"
#include "rhi/frontend/bindless_root_execution.h"
#include "rhi/frontend/bindless_root_fence_execution.h"
#include "rhi/frontend/completed_color_texture_view.h"
#include "rhi/frontend/transfer_validation.h"

#include <algorithm>
#include <cstring>

namespace arp::rhi_vulkan {
using namespace rhi;

namespace {
// Same complete custody count before the public seal claim and at command
// preparation. A rejected cold owner budget must leave the original Root usable.
std::uint64_t rootFenceSubmissionOwners(const VulkanBindlessRootNativeClosure& closure,
    std::uint64_t reservationOwners,
    const OriginalCommandBufferReadbackBinding& binding,
    const VulkanOriginalBindlessGraphicsPass* graphics = nullptr,
    const VulkanBindlessRootNativeClosure* next = nullptr,
    std::size_t clearPasses = 0u,
    const VulkanBindlessRootNativeClosure* final = nullptr) noexcept
{
    return std::uint64_t(closure.retainedOwnerCount()) + reservationOwners
        + binding.originalProgram()->retainedOwnerCount()
        + OriginalCommandBufferReadbackBinding::retainedOwnerCount + 2u
        + (graphics ? graphics->retainedOwnerCount() : 0u)
        + (next ? next->retainedOwnerCount() + clearPasses * 4u + 6u : 0u)
        + (final ? final->retainedOwnerCount() : 0u);
}
} // namespace

// Retains the existing command and its original transaction/Result observations.
// The original custody owns the seal,
// journal, closure, pass and Result; the queue pool remains the only in-flight
// owner after caller drop. No wrapper completion flag can commit the journal.
class VulkanOriginalBindlessGraphicsExecutionView final : public OriginalBindlessGraphicsExecution {
    friend class VulkanBindlessCrossExtension;

    class ConstructionKey {
        friend class VulkanBindlessCrossExtension;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static std::shared_ptr<VulkanOriginalBindlessGraphicsExecutionView> share(
        core::UniquePtr<VulkanOriginalBindlessGraphicsExecutionView> view)
    {
        return shareOriginal(std::move(view));
    }

    VulkanOriginalBindlessGraphicsExecutionView(
        ConstructionKey, std::size_t diagnostics, std::uint32_t version)
        : version_(version)
    {
        diagnostic_.reserve(diagnostics);
    }

    const char* cancel() const noexcept override
    {
        if (!command_)
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-CANCELLED";
        if (command_->impl_->submitted || command_->impl_->originalSubmissionUnproven)
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-CANCEL-IN-FLIGHT";
        command_.reset();
        return nullptr;
    }

    bool pollCompletion() const noexcept override
    {
        if (!command_)
            return false;
        const bool done = command_->submittedFenceCompleted();
        if (done)
            retireCompletedVulkanRhiSubmittedCommands(*command_->runtime());
        return done;
    }

    bool recordingProven() const noexcept override { return command_ && command_->impl_->ended; }

    bool submissionCompleted() const noexcept override
    {
        return command_ && command_->impl_->submitFenceCompleted && !completionDiagnostic();
    }

    const char* completionDiagnostic() const noexcept override
    {
        if (!command_)
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-CANCELLED";
        // The original native completion releases custody after committing the
        // journal. Read the same retained transaction/Result after that drain;
        // never preserve a wrapper-only completion flag or extend pass custody.
        return command_->impl_->submitFenceCompleted && transaction_ && result_
                && transaction_->status() == OriginalCommandStateTransaction::Status::Committed
                && result_->publishedBytes()
            ? nullptr
            : "RHI-BINDLESS-GRAPHICS-EXECUTION-COMPLETION-UNPROVEN";
    }

protected:
    std::uint32_t executionContractVersion() const noexcept override { return version_; }

    const char* recordOriginal() const noexcept override
    {
        if (!command_)
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-CANCELLED";
        auto custody = std::dynamic_pointer_cast<VulkanOriginalBindlessCommand>(
            command_->impl_->originalBindlessCustody);
        if (!custody || custody->command_ != command_.get()
            || custody->phase_ != OriginalCommandNativeLifecycleContract::Phase::Claimed)
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-RECORD-PHASE";
        try {
            core::CpuMemoryQuotaScope scope(originalMetadataQuota());
            if (custody->record(*command_, diagnostic_))
                return nullptr;
        } catch (const std::bad_alloc&) {
            command_.reset();
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-DIAGNOSTIC-CAPACITY";
        }
        command_.reset();
        return diagnostic_.c_str();
    }

    const char* submitOriginal() const noexcept override
    {
        if (!command_)
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-CANCELLED";
        if (!command_->impl_->ended || command_->impl_->submitted
            || command_->impl_->originalSubmissionUnproven)
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-SUBMIT-PHASE";
        try {
            core::CpuMemoryQuotaScope scope(originalMetadataQuota());
            return command_->submit(diagnostic_) ? nullptr : diagnostic_.c_str();
        } catch (const std::bad_alloc&) {
            return "RHI-BINDLESS-GRAPHICS-EXECUTION-DIAGNOSTIC-CAPACITY";
        }
    }

private:
    const std::uint32_t version_;
    mutable std::shared_ptr<VulkanRhiNativeCommandPayload> command_;
    std::shared_ptr<const OriginalCommandStateTransaction> transaction_;
    std::shared_ptr<const OriginalCommandBufferReadbackResultSnapshot> result_;
    mutable core::pmr::String diagnostic_{&core::defaultCpuMemoryResource()};
};

std::shared_ptr<const OriginalBindlessGraphicsExecution>
VulkanBindlessCrossExtension::prepareOriginalGraphicsExecutionPayload(GpuDevice& device,
    const OriginalBindlessGraphicsExecutionInputs& inputs,
    const OriginalBindlessGraphicsExecutionBudget& budget,
    core::pmr::String& diagnostic) const
{
    const auto fail =
        [&](const char* id) -> std::shared_ptr<const OriginalBindlessGraphicsExecution> {
        diagnostic = id;
        return {};
    };
    const char* id = nullptr;
    try {
        core::CpuMemoryQuotaScope scope(inputs.viewQuota);
        auto view = VulkanOriginalBindlessGraphicsExecutionView::share(
            core::makeUnique<VulkanOriginalBindlessGraphicsExecutionView>(
                VulkanOriginalBindlessGraphicsExecutionView::ConstructionKey{},
                budget.maxDiagnosticBytes,
                budget.intent.contractVersion));
        core::pmr::Vector<std::shared_ptr<const VulkanOriginalGeometryContent>> geometry{
            &core::defaultCpuMemoryResource()};
        geometry.reserve(inputs.geometryCount);
        for (std::size_t i = 0u; i < inputs.geometryCount; ++i) {
            auto actor =
                std::dynamic_pointer_cast<const VulkanOriginalGeometryContent>(inputs.geometry[i]);
            if ((id = VulkanOriginalGeometryContent::validateOriginal(actor)))
                return fail(id);
            geometry.push_back(std::move(actor));
        }
        core::pmr::Vector<std::shared_ptr<const VulkanOriginalGeometryContent>> shader{
            &core::defaultCpuMemoryResource()};
        if ((id = OriginalBindlessInitializedShaderContract::project(
                 budget.closure.initializedShader,
                 inputs.initializedShader,
                 inputs.initializedShaderCount,
                 shader)))
            return fail(id);
        auto closure = VulkanBindlessRootNativeClosure::prepareOriginal(inputs.root,
            inputs.inventory,
            device,
            budget.closure,
            inputs.closureQuota,
            id,
            {geometry.data(), geometry.size(), shader.data(), shader.size()});
        if (!closure)
            return fail(id);
        if ((id = OriginalBindlessInitializedGraphicsContract::inspectNative(
                 closure, inputs.readback->originalProgram(), inputs.initializedGraphics)))
            return fail(id);
        auto pass = VulkanOriginalBindlessGraphicsPass::prepareOriginal(closure,
            inputs.transaction,
            inputs.readback->originalProgram(),
            device,
            budget.pass,
            inputs.passCapacity,
            inputs.passQuota,
            id);
        if (!pass)
            return fail(id);
        if (budget.closure.contractVersion == 3u)
            if ((id = VulkanOriginalBindlessCommand::validateGeometryFetch(
                     closure, inputs.readback->originalProgram(), budget.fetch)))
                return fail(id);
        const auto reservationOwners = std::uint64_t(inputs.transaction->retainedOwnerCount())
            + OriginalCommandRecordingSeal::additionalOwnerCountFor(budget.lifecycle)
            + OriginalCommandLifecycleReservation::additionalOwnerCount;
        const auto baseOwners =
            rootFenceSubmissionOwners(*closure, reservationOwners, *inputs.readback, pass.get());
        if (baseOwners > budget.lifecycle.maxSubmissionOwners
            || OriginalBindlessRootReadbackSequence::additionalOwnerBound(inputs.readbacks)
                > budget.lifecycle.maxSubmissionOwners - baseOwners)
            return fail("RHI-VULKAN-ROOT-FENCE-OWNER-CAPACITY");
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
            diagnostic,
            pass,
            budget.fetch,
            true,
            {},
            {},
            {},
            inputs.readbacks);
        if (!view->command_)
            return {};
        view->transaction_ = inputs.transaction;
        view->result_ = inputs.readback->originalResult();
        diagnostic.clear();
        return view;
    } catch (const std::bad_alloc&) {
        return fail("RHI-BINDLESS-GRAPHICS-EXECUTION-VIEW-CAPACITY");
    }
}

VulkanOriginalBindlessCommand::VulkanOriginalBindlessCommand(ConstructionKey,
    std::shared_ptr<const Closure> closure,
    std::shared_ptr<const Reservation> reservation,
    std::shared_ptr<const Binding> binding,
    BindlessFaultSubmissionLease fault,
    std::shared_ptr<const VulkanOriginalBindlessGraphicsPass> graphics,
    std::shared_ptr<const Closure> nextClosure,
    std::shared_ptr<const Closure> finalClosure,
    const rhi::OriginalBindlessRootReadbackSequence::Input& readbackInput,
    const OriginalBindlessRootNativeSequence<Closure>::Input& rootInput,
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

VulkanOriginalBindlessCommand::~VulkanOriginalBindlessCommand()
{
    for (auto& pass : clearPasses_)
        destroyVulkanRhiNativeRenderPassResources(*closure_->runtime_, pass.native);
    if (phase_ == Life::Phase::Claimed || phase_ == Life::Phase::RecordingPending
        || phase_ == Life::Phase::Recorded || phase_ == Life::Phase::SubmitReserved)
        reservation_->originalSeal()->rollbackOriginalRecordingPlan();
    else if (phase_ != Life::Phase::Completed)
        reservation_->originalSeal()->invalidateOriginalOwnership();
}

const char* VulkanOriginalBindlessCommand::advance(Life::Event event) noexcept
{
    const auto transition = Life::inspectTransition(phase_, event);
    if (!transition.accepted())
        return transition.diagnostic;
    phase_ = transition.next;
    return nullptr;
}

const char* VulkanOriginalBindlessCommand::validateGeometryFetch(
    const std::shared_ptr<const Closure>& closure,
    const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& program,
    const rhi::OriginalIndexedDrawFetchContract::Budget& fetch) noexcept
{
    if (fetch.contractVersion != 2u)
        return "RHI-VULKAN-GEOMETRY-FENCE-FETCH-VERSION";
    const auto fetched =
        program->inspectInitializedGeometry(fetch, [&](std::size_t source) noexcept {
            return closure->initializedGeometry(source);
        });
    if (fetched.diagnostic)
        return fetched.diagnostic;
    if (!fetched.draws || !fetched.streams || !fetched.analyzedIndices)
        return "RHI-VULKAN-GEOMETRY-FENCE-FETCH-COVERAGE";
    return nullptr;
}

std::shared_ptr<VulkanRhiNativeCommandPayload> VulkanOriginalBindlessCommand::prepareOriginal(
    std::shared_ptr<const Closure> closure,
    std::shared_ptr<const Reservation> reservation,
    std::shared_ptr<const Binding> binding,
    GpuDevice& device,
    core::pmr::String& diagnostic,
    std::shared_ptr<const VulkanOriginalBindlessGraphicsPass> graphics,
    const OriginalIndexedDrawFetchContract::Budget& geometryFetch,
    bool deferRecording,
    std::shared_ptr<const Closure> nextClosure,
    std::shared_ptr<core::AtomicCapacityBudget> attachmentCapacity,
    std::shared_ptr<const Closure> finalClosure,
    const rhi::OriginalBindlessRootReadbackSequence::Input& readbackInput,
    const OriginalBindlessRootNativeSequence<Closure>::Input& rootInput,
    const PassInput& passInput,
    const OriginalBindlessProducedGeometryInputs& producedGeometry)
{
    const auto reject = [&](const char* id) -> std::shared_ptr<VulkanRhiNativeCommandPayload> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = Closure::validateOriginal(closure))
        return reject(id);
    if (const auto* id = Reservation::validateOriginal(reservation))
        return reject(id);
    if (const auto* id = OriginalBindlessRootReadbackSequence::inspectInput(binding, readbackInput))
        return reject(id);
    const bool produced =
        reservation->originalSeal()->originalRoot()->hasOriginalBindlessProducedGeometryProfile();
    if (produced) {
        const auto& limits =
            closure->runtime_->descriptorIndexing().limits.maxComputeWorkGroupCount;
        if (const auto* id = producedGeometry.inspectNative(rootInput.closures,
                rootInput.count,
                reservation->originalSeal()->originalRoot()->originalGraph(),
                binding->originalProgram(),
                binding,
                readbackInput,
                std::min(limits[0u], limits[1u]),
                closure->runtime_->indirectFirstInstanceEnabled()))
            return reject(id);
    } else if (!producedGeometry.empty())
        return reject("RHI-BINDLESS-PRODUCED-EXECUTION-INPUT-SCHEMA");
    const auto executionVersion =
        closure->root_->originalSnapshot()->plan().originalExecutionContractVersion;
    const bool mixed = !produced && executionVersion == 11u;
    const bool storage = !produced && (executionVersion == 9u || mixed);
    if (produced) {
        if (graphics || attachmentCapacity || passInput.empty() || !rootInput.closures
            || rootInput.count < 5u)
            return reject("RHI-VULKAN-PRODUCED-COMMAND-INPUTS");
    } else if (mixed) {
        if (const auto* id = Closure::validateOriginal(nextClosure))
            return reject(id);
        if (const auto* id = Closure::validateOriginal(finalClosure))
            return reject(id);
        if (!attachmentCapacity || graphics || closure->closureContractVersion() != 14u
            || nextClosure->closureContractVersion() != 14u
            || finalClosure->closureContractVersion() != 14u
            || !core::SharedOwnerProjection::sameOwner(closure->inventory_, nextClosure->inventory_)
            || !core::SharedOwnerProjection::sameOwner(
                closure->inventory_, finalClosure->inventory_))
            return reject("RHI-VULKAN-ROOT-MIXED-CORRESPONDENCE");
        const auto visits = std::uint64_t(closure->root_->size()) + nextClosure->root_->size()
            + finalClosure->root_->size();
        const auto flow = OriginalBindlessMixedEntryFlowContract::inspect(
            {closure->root_, nextClosure->root_, finalClosure->root_},
            closure->inventory_,
            {1u, true, visits});
        if (flow.diagnostic)
            return reject(flow.diagnostic);
    } else if (finalClosure)
        return reject("RHI-VULKAN-ROOT-COMMAND-INPUT-SCHEMA");
    else if (storage) {
        if (const auto* id = Closure::validateOriginal(nextClosure))
            return reject(id);
        if (!attachmentCapacity || graphics || closure->closureContractVersion() != 11u
            || nextClosure->closureContractVersion() != 11u
            || !closure->root_->ordinaryDomains().empty()
            || !nextClosure->root_->ordinaryDomains().empty()
            || !OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                closure->root_->originalSnapshot()->plan(), 9u)
            || !core::SharedOwnerProjection::sameOwner(closure->inventory_, nextClosure->inventory_)
            || !core::SharedOwnerProjection::sameOwner(
                closure->root_->originalSnapshot(), nextClosure->root_->originalSnapshot())
            || !core::SharedOwnerProjection::sameOwner(
                closure->root_->authorization(), nextClosure->root_->authorization())
            || core::SharedOwnerProjection::sameOwner(closure->root_, nextClosure->root_))
            return reject("RHI-VULKAN-ROOT-STORAGE-CORRESPONDENCE");
        if (const auto* id =
                OriginalBindlessStorageExecutionContract::validateEntry(*closure->root_, true))
            return reject(id);
        if (const auto* id =
                OriginalBindlessStorageExecutionContract::validateEntry(*nextClosure->root_, false))
            return reject(id);
    } else if (nextClosure || attachmentCapacity)
        return reject("RHI-VULKAN-ROOT-COMMAND-INPUT-SCHEMA");
    if (const auto* id = Reservation::validateOriginal(reservation))
        return reject(id);
    if (const auto* id = OriginalBindlessRootReadbackSequence::inspectInput(binding, readbackInput))
        return reject(id);
    OriginalBindlessGraphicsPassSequenceContract::Footprint passFootprint;
    if (!passInput.empty()) {
        if (reservation->originalSeal()->originalBudget().contractVersion != 21u || graphics
            || attachmentCapacity)
            return reject("RHI-VULKAN-PRODUCED-PASS-LIFECYCLE");
        if (const auto* id = OriginalBindlessGraphicsPassSequenceContract::inspectPrepared<
                VulkanOriginalBindlessGraphicsPass,
                OriginalCommandStateTransaction,
                Closure>(passInput,
                reservation->originalSeal()->originalRoot(),
                binding->originalProgram(),
                rootInput,
                passFootprint))
            return reject(id);
    }
    using Roots = OriginalBindlessRootNativeSequence<Closure>;
    if (const auto* id = Roots::inspectHistorical(rootInput, {closure, nextClosure, finalClosure}))
        return reject(id);
    if (const auto* id = Roots::inspectInput(rootInput,
            closure->originalInventory(),
            binding->originalProgram(),
            [](const auto& selected) noexcept {
                return Closure::validateOriginal(selected);
            }))
        return reject(id);
    const auto& seal = reservation->originalSeal();
    const auto& tx = seal->originalRoot();
    const auto& program = binding->originalProgram();
    const auto& inputs = program->originalInputs();
    const auto& usage = *inputs->originalScalars()->originalUsage();
    if (!usage.originalSources()->originalBudget().maxNativePayloadObjects)
        return reject("RHI-VULKAN-ROOT-FENCE-NATIVE-PAYLOAD-CAPACITY");
    if (!passInput.empty()
        && (passFootprint.nativeObjects
                >= usage.originalSources()->originalBudget().maxNativePayloadObjects
            || passFootprint.owners > seal->originalBudget().maxSubmissionOwners
            || passFootprint.owners > seal->originalBudget().maxRecordingOwners))
        return reject("RHI-VULKAN-PRODUCED-PASS-CAPACITY");
    const auto lifecycle = seal->originalBudget().contractVersion;
    const bool graphicsDepth = lifecycle == 19u;
    const bool depthReads = lifecycle == 14u;
    const bool textureReads = lifecycle == 8u;
    const bool graphicsDraw = lifecycle == 9u || graphicsDepth;
    const bool geometryDraw = graphicsDraw && tx->hasOriginalBindlessGraphicsGeometryProfile();
    if (geometryDraw) {
        const auto geometryLowering =
            semantics::OriginalBindlessGeometryExecutionLowering::current(device.backend());
        if (!geometryLowering.executionAvailable || geometryLowering.diagnostic)
            return reject(geometryLowering.diagnostic);
        if (const auto* id = validateGeometryFetch(closure, program, geometryFetch))
            return reject(id);
    } else if (geometryFetch.contractVersion || geometryFetch.prototypeOptIn
        || geometryFetch.maxCommands || geometryFetch.maxDraws || geometryFetch.maxStreams
        || geometryFetch.maxBindingScans || geometryFetch.maxAnalyzedIndices)
        return reject("RHI-VULKAN-GEOMETRY-FENCE-FETCH-SCHEMA");
    const auto lowering =
        semantics::OriginalCommandNativeLifecycleLowering::lowering(device.backend(), lifecycle);
    if (!lowering.sealedIssuerAvailable || !lowering.executionAvailable || lowering.diagnostic)
        return reject(lowering.diagnostic ? lowering.diagnostic : "RHI-VULKAN-ROOT-FENCE-LOWERING");
    if (device.backend() != Backend::Vulkan
        || (lifecycle != 6u && !textureReads && !graphicsDraw && !depthReads && !storage
            && !produced)
        || reservation->originalRequest().contractVersion
            != (produced            ? 11u
                    : graphicsDepth ? 10u
                    : mixed         ? 9u
                    : storage       ? 8u
                    : depthReads    ? 7u
                    : graphicsDraw  ? 5u
                    : textureReads  ? 4u
                                    : 2u)
        || program->originalBudget().bindlessRoots.contractVersion
            != (produced           ? 6u
                    : geometryDraw ? 5u
                                   : 4u)
        || closure->closureContractVersion()
            != (produced ? OriginalBindlessRootNativeClosureContract::producedGeometryVersion
                    : graphicsDepth ? 16u
                    : mixed         ? 14u
                    : storage       ? 11u
                    : depthReads    ? 9u
                    : geometryDraw  ? 3u
                                    : 1u)
        || !core::SharedOwnerProjection::sameOwner(tx->originalGraph(), binding->originalGraph())
        || !core::SharedOwnerProjection::sameOwner(
            usage.originalSources(), closure->originalInventory())
        || (storage
            && (lifecycle != (mixed ? 17u : 15u)
                || tx->originalContentPlanContractVersion() != (mixed ? 15u : 13u)
                || !tx->hasOriginalBindlessRootShaderResourceProfile()))
        || seal->nativeColdCommandClaimed_)
        return reject("RHI-VULKAN-ROOT-FENCE-COLD-CORRESPONDENCE");
    if (produced && lifecycle != 21u)
        return reject("RHI-VULKAN-PRODUCED-COMMAND-LIFECYCLE");
    if (binding->rootSourceIndex() >= usage.originalSources()->sources().size())
        return reject("RHI-VULKAN-ROOT-FENCE-ROOT-INDEX");
    const auto* root = std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
        &usage.originalSources()->sources()[binding->rootSourceIndex()]);
    if (!root || !core::SharedOwnerProjection::sameOwner(*root, closure->originalRoot())
        || (graphicsDraw
                ? !std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                      (*root)->originalPrepared())
                : !std::holds_alternative<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                      (*root)->originalPrepared())))
        return reject("RHI-VULKAN-ROOT-FENCE-COMPUTE-ROOT");
    if (graphicsDraw) {
        if (const auto* id = VulkanOriginalBindlessGraphicsPass::validateOriginal(graphics))
            return reject(id);
        if (!core::SharedOwnerProjection::sameOwner(graphics->closure_, closure)
            || !core::SharedOwnerProjection::sameOwner(graphics->transaction_, tx)
            || !core::SharedOwnerProjection::sameOwner(graphics->stream_, program)
            || tx->originalContentPlanContractVersion() != (graphicsDepth ? 18u : 9u))
            return reject("RHI-VULKAN-GRAPHICS-FENCE-COLD-CORRESPONDENCE");
        if (usage.originalSources()->originalBudget().maxNativePayloadObjects
            < graphics->nativeObjectCount() + 1u)
            return reject("RHI-VULKAN-GRAPHICS-FENCE-NATIVE-PAYLOAD-CAPACITY");
    } else if (graphics)
        return reject("RHI-VULKAN-GRAPHICS-FENCE-LIFECYCLE");
    // /6 stays buffer-only. /8 admits only original whole RGBA8 sampled views
    // with genuine Completed control3 content and independent native samplers.
    // The same ContentPlan preflight below checks actual shader consumption.
    std::size_t sampledViews = 0u;
    for (const auto& entry : closure->entries_) {
        if (entry.resourcePayload.buffer)
            continue;
        if (produced)
            continue; // Actual produced content was inspected before this seal.
        if (storage)
            continue; // Content13 proves per-cell initialization/write/read in this stream.
        if (!textureReads && !graphicsDraw && !depthReads)
            return reject("RHI-VULKAN-ROOT-FENCE-BUFFER-DOMAIN");
        if (entry.resourcePayload.sampler) {
            const auto& sampler = std::get<std::shared_ptr<const OriginalCommandSamplerLease>>(
                usage.originalSources()->sources()[entry.sourceIndex]);
            if (!graphicsDepth && sampler->originalObject()->desc().compareOp != CompareOp::Never)
                return reject("RHI-VULKAN-TEXTURE-FENCE-COMPARISON-SAMPLER");
            continue;
        }
        const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
            &usage.originalSources()->sources()[entry.sourceIndex]);
        if (!view)
            return reject("RHI-VULKAN-TEXTURE-FENCE-VIEW-SOURCE");
        const auto& object = *(*view)->originalParentLease()->originalObject();
        // The sealed package independently checks actual SPIR-V image shape,
        // while snapshot publication checks this exact physical view binding.
        // Metadata array support alone cannot authorize an array shader.
        const auto dimension = (*view)->originalObject()->desc().dimension;
        const bool ordinaryCube = (*root)->sourceContractVersion() == 2u
            && OriginalCompletedColorTextureViewContract::isSingleCube(
                object.desc(), (*view)->originalObject()->desc());
        if ((dimension != TextureViewDimension::Texture2D
                && dimension != TextureViewDimension::Texture2DArray && !ordinaryCube)
            || (dimension == TextureViewDimension::Texture2D
                && object.desc().arrayLayerCount != 1u))
            return reject("RHI-VULKAN-TEXTURE-FENCE-SHADER-DIMENSION");
        if (depthReads || (graphicsDepth && entry.completedDepth)) {
            if (dimension != TextureViewDimension::Texture2D || !entry.completedDepth
                || entry.completedDepth->budget().contractVersion != 2u)
                return reject("RHI-VULKAN-DEPTH-FENCE-SOURCE-PROFILE");
            ++sampledViews;
            continue;
        }
        const auto completed = object.observeOriginalContent();
        if (!completed)
            return reject("RHI-VULKAN-TEXTURE-FENCE-COMPLETED-CONTENT");
        const auto readVersion = graphicsDraw
            ? OriginalCompletedTextureReadPlanContract::graphicsReadVersion(
                  tx->originalBindlessCompletedReadContractVersion())
            : 8u;
        if (object.desc().format == TextureFormat::Rgba16Float && readVersion != 17u)
            return reject("RHI-VULKAN-GRAPHICS-HDR-READ-PROFILE");
        if (const auto* id = OriginalCompletedTextureReadPlanContract::validateObservation(
                completed->controlVersion(),
                completed->state() == common::OriginalTextureContentControl::State::Completed,
                completed->contentGeneration(),
                completed->revision(),
                readVersion))
            return reject(id);
        ++sampledViews;
    }
    if (textureReads
        && (!sampledViews || tx->originalContentPlanContractVersion() != 8u
            || !tx->hasOriginalBindlessRootShaderResourceProfile()))
        return reject("RHI-VULKAN-TEXTURE-FENCE-CONTENT-PROFILE");
    if (depthReads
        && (!sampledViews || tx->originalContentPlanContractVersion() != 12u
            || !tx->hasOriginalBindlessRootShaderResourceProfile()
            || !OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                closure->root_->originalSnapshot()->plan(), 6u)))
        return reject("RHI-VULKAN-DEPTH-FENCE-CONTENT-PROFILE");
    auto* extension = dynamic_cast<VulkanBindlessCrossExtension*>(
        static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross)));
    if (!extension || extension->runtime_ != closure->runtime_.get() || !extension->nativeResources_
        || closure->runtime_->syntheticRuntime())
        return reject("RHI-VULKAN-ROOT-FENCE-DEVICE");
    if (const auto* id = tx->compareOriginalGraph(binding->originalGraph(), device))
        return reject(id);
    // Complete cold operation allowlist. Pending execution consumes this exact
    // immutable program; it cannot revalidate cold graph observations afterward.
    std::size_t clears = 0u, binds = 0u, dispatches = 0u, copies = 0u;
    std::size_t begins = 0u, ends = 0u, draws = 0u, viewports = 0u, scissors = 0u;
    std::size_t vertexBinds = 0u, indexBinds = 0u;
    OriginalCommandPushConsumption pushConsumer;
    const auto& pushBudget = program->originalBudget().bindlessRoots.pushConsumption;
    const auto* selectedPipeline = graphicsDraw
        ? std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
              closure->root_->originalPrepared())
              ->owners()
              ->desc()
              .pipeline.get()
        : nullptr;
    if (selectedPipeline && selectedPipeline->shaderReflection().pushConstantByteCount) {
        if (!pushBudget.recordingOptIn || pushBudget.recordingContractVersion != 1u
            || extension->capabilities().originalGraphicsPushContractVersion != 1u)
            return reject("RHI-VULKAN-ROOT-PUSH-RECORDING-PROFILE");
        if (const auto* id = pushBudget.diagnostic())
            return reject(id);
        if (const auto* id = semantics::OriginalBindlessPushConsumptionLowering::validateNativeSize(
                selectedPipeline->shaderReflection().pushConstantByteCount,
                kL2AtomicPushConstantLimitBytes,
                closure->runtime_->descriptorIndexing().limits.maxPushConstantsSize))
            return reject(id);
    }
    if (produced) {
        if (const auto* id =
                OriginalBindlessProducedProgramContract::inspectNativePushRecording<Closure>(
                    program,
                    rootInput,
                    extension->capabilities().originalGraphicsPushContractVersion == 1u,
                    closure->runtime_->descriptorIndexing().limits.maxPushConstantsSize))
            return reject(id);
        const auto report = OriginalBindlessProducedProgramContract::inspectNative<Closure>(
            program, rootInput, binding, readbackInput, producedGeometry.commands);
        if (report.diagnostic)
            return reject(report.diagnostic);
        clears = 1u;
        binds = report.bindings;
        dispatches = report.dispatches;
        begins = ends = report.renderPasses;
        draws = report.draws;
        copies = report.copies;
    } else
        for (const auto& op : program->operations()) {
            using K = CommandUsageKind;
            switch (op.kind) {
            case K::BufferBarrier:
            case K::BeginTransferPass:
            case K::EndTransferPass:
            case K::Finish:
                break;
            case K::BeginComputePass:
            case K::EndComputePass:
                if (graphicsDraw)
                    return reject("RHI-VULKAN-GRAPHICS-FENCE-COMPUTE");
                break;
            case K::RetainIndexedSnapshot:
                if (!graphicsDraw)
                    return reject("RHI-VULKAN-ROOT-FENCE-OPERATION");
                break;
            case K::BeginRenderPass:
                if (!storage && (!graphicsDraw || op.commandIndex != graphics->commandIndex()))
                    return reject("RHI-VULKAN-GRAPHICS-FENCE-PASS");
                ++begins;
                break;
            case K::EndRenderPass:
                ++ends;
                if (!graphicsDraw && !storage)
                    return reject("RHI-VULKAN-GRAPHICS-FENCE-PASS");
                break;
            case K::BindPreparedGraphicsBinding:
                pushConsumer.onBind();
                if (!graphicsDraw)
                    return reject("RHI-VULKAN-GRAPHICS-FENCE-BIND");
                ++binds;
                break;
            case K::SetViewport:
                ++viewports;
                if (!graphicsDraw)
                    return reject("RHI-VULKAN-GRAPHICS-FENCE-VIEWPORT");
                break;
            case K::SetScissor:
                ++scissors;
                if (!graphicsDraw)
                    return reject("RHI-VULKAN-GRAPHICS-FENCE-SCISSOR");
                break;
            case K::Draw:
                ++draws;
                if (!graphicsDraw || geometryDraw)
                    return reject("RHI-VULKAN-GRAPHICS-FENCE-DRAW");
                if (const auto* id =
                        pushConsumer.consume(selectedPipeline->shaderReflection(), pushBudget))
                    return reject(id);
                break;
            case K::PushConstants: {
                if (!selectedPipeline || !selectedPipeline->layout() || !binds
                    || !pushBudget.recordingOptIn || pushBudget.recordingContractVersion != 1u)
                    return reject("RHI-VULKAN-ROOT-PUSH-RECORDING-PROFILE");
                PushConstantsDesc block;
                if (const auto* id =
                        pushConsumer.update(inputs->originalScalars()->originalArguments(),
                            op.commandIndex,
                            selectedPipeline->layout()->desc(),
                            selectedPipeline->shaderReflection(),
                            ShaderStage::Vertex | ShaderStage::Fragment,
                            pushBudget,
                            block))
                    return reject(id);
                break;
            }
            case K::BindVertexBuffer:
                ++vertexBinds;
                if (!geometryDraw)
                    return reject("RHI-VULKAN-GEOMETRY-FENCE-BIND");
                break;
            case K::BindIndexBuffer:
                ++indexBinds;
                if (!geometryDraw)
                    return reject("RHI-VULKAN-GEOMETRY-FENCE-BIND");
                break;
            case K::DrawIndexed:
                ++draws;
                if (!geometryDraw)
                    return reject("RHI-VULKAN-GEOMETRY-FENCE-DRAW");
                if (const auto* id =
                        pushConsumer.consume(selectedPipeline->shaderReflection(), pushBudget))
                    return reject(id);
                break;
            case K::ClearSnapshotFault:
                ++clears;
                break;
            case K::BindPreparedComputeBinding:
                if (graphicsDraw)
                    return reject("RHI-VULKAN-GRAPHICS-FENCE-COMPUTE");
                if (storage
                    && (binds >= (mixed ? 3u : 2u) || binds != dispatches || op.referenceCount != 1u
                        || usage.references()[op.firstReference].sourceIndex
                            != closure->inventory_->originalLeaseIndex(binds == 2u
                                    ? finalClosure->root_
                                    : binds ? nextClosure->root_
                                            : closure->root_)))
                    return reject("RHI-VULKAN-ROOT-STORAGE-ENTRY-ORDER");
                ++binds;
                break;
            case K::Dispatch: {
                if (storage && binds != dispatches + 1u)
                    return reject("RHI-VULKAN-ROOT-STORAGE-ENTRY-ORDER");
                ++dispatches;
                if (graphicsDraw)
                    return reject("RHI-VULKAN-GRAPHICS-FENCE-COMPUTE");
                const auto* scalar = inputs->originalScalars()
                                         ->originalArguments()
                                         ->records()[op.commandIndex]
                                         .scalar.get<CommandScalarArguments::Dispatch>();
                const auto& limits = closure->runtime_->descriptorIndexing().limits;
                if (!scalar || scalar->x > limits.maxComputeWorkGroupCount[0]
                    || scalar->y > limits.maxComputeWorkGroupCount[1]
                    || scalar->z > limits.maxComputeWorkGroupCount[2])
                    return reject("RHI-VULKAN-ROOT-FENCE-DISPATCH-LIMIT");
                break;
            }
            case K::BufferCopy:
                if (!OriginalBindlessRootReadbackSequence::containsCopy(
                        binding, readbackInput, op.commandIndex))
                    return reject("RHI-VULKAN-ROOT-FENCE-COPY");
                ++copies;
                break;
            case K::TextureBarrier: {
                if ((!textureReads && !graphicsDraw && !depthReads && !storage)
                    || op.referenceCount != 1u)
                    return reject("RHI-VULKAN-TEXTURE-FENCE-BARRIER-PROFILE");
                const auto index = usage.references()[op.firstReference].sourceIndex;
                const auto* parent =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(
                        &usage.originalSources()->sources()[index]);
                if (!parent)
                    return reject("RHI-VULKAN-TEXTURE-FENCE-BARRIER-SOURCE");
                bool found = false;
                for (const auto& entry : closure->entries_)
                    found = found
                        || (entry.parentPayload.image
                            && core::SharedOwnerProjection::sameOwner(entry.parentPayload.payload,
                                vulkanRhiNativePayload(*(*parent)->originalObject())));
                if (graphicsDraw)
                    for (std::uint32_t i = 0u; i < graphics->attachmentCount_; ++i)
                        found = found
                            || core::SharedOwnerProjection::sameOwner(
                                graphics->attachments_[i].parent,
                                vulkanRhiNativePayload(*(*parent)->originalObject()));
                if (!found)
                    return reject("RHI-VULKAN-TEXTURE-FENCE-BARRIER-CLOSURE");
                break;
            }
            default:
                return reject("RHI-VULKAN-ROOT-FENCE-OPERATION");
            }
            if (op.kind == K::ClearSnapshotFault || op.kind == K::PushConstants
                || op.kind == K::BindPreparedComputeBinding
                || op.kind == K::BindPreparedGraphicsBinding
                || op.kind == K::RetainIndexedSnapshot) {
                if (op.referenceCount != 1u
                    || (usage.references()[op.firstReference].sourceIndex
                            != binding->rootSourceIndex()
                        && !(storage && op.kind == K::BindPreparedComputeBinding
                            && (usage.references()[op.firstReference].sourceIndex
                                    == closure->inventory_->originalLeaseIndex(nextClosure->root_)
                                || (mixed
                                    && usage.references()[op.firstReference].sourceIndex
                                        == closure->inventory_->originalLeaseIndex(
                                            finalClosure->root_))))))
                    return reject("RHI-VULKAN-ROOT-FENCE-FOREIGN-ROOT");
            }
            if (op.kind == K::BufferBarrier || op.kind == K::BufferCopy
                || op.kind == K::BindVertexBuffer || op.kind == K::BindIndexBuffer)
                for (std::size_t i = 0u; i < op.referenceCount; ++i) {
                    const auto index = usage.references()[op.firstReference + i].sourceIndex;
                    const auto* lease =
                        std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                            &usage.originalSources()->sources()[index]);
                    if (!lease)
                        return reject("RHI-VULKAN-ROOT-FENCE-BUFFER-SOURCE");
                    bool found = false;
                    for (const auto& entry : closure->entries_)
                        found = found
                            || ((*lease)->resource() == entry.resource
                                && core::SharedOwnerProjection::sameOwner(
                                    entry.resourcePayload.payload,
                                    vulkanRhiNativePayload(*(*lease)->originalObject())));
                    if (!found && op.kind == K::BufferCopy && i == 1u)
                        found = OriginalBindlessRootReadbackSequence::containsCopyDestination(
                            binding, readbackInput, op.commandIndex, *lease);
                    if (!found)
                        return reject("RHI-VULKAN-ROOT-FENCE-BUFFER-CLOSURE");
                    if ((op.kind == K::BindVertexBuffer || op.kind == K::BindIndexBuffer)
                        && !closure->initializedGeometry(index))
                        return reject("RHI-VULKAN-GEOMETRY-FENCE-CONTENT-COVERAGE");
                }
        }
    if (!produced
        && (clears != 1u
            || binds
                != (mixed         ? 3u
                        : storage ? 2u
                                  : 1u)
            || dispatches
                != (mixed              ? 3u
                        : storage      ? 2u
                        : graphicsDraw ? 0u
                                       : 1u)
            || copies != OriginalBindlessRootReadbackSequence::resultCount(readbackInput)
            || (storage
                && (!begins || begins != ends || begins > closure->budget_.maxAttachmentViews
                    || attachmentCapacity->capacity() != closure->budget_.maxAttachmentViews
                    || begins
                        > (usage.originalSources()->originalBudget().maxNativePayloadObjects - 1u)
                            / 3u))
            || (geometryDraw && (!vertexBinds || indexBinds != 1u))
            || (graphicsDraw
                && (begins != 1u || ends != 1u || draws != 1u || viewports != 1u
                    || scissors != 1u))))
        return reject("RHI-VULKAN-ROOT-FENCE-COMMAND-COUNTS");
    const auto& quota = seal->originalSubmissionQuota();
    const auto owners = passFootprint.owners
        + rootFenceSubmissionOwners(*closure,
            reservation->retainedOwnerCount(),
            *binding,
            graphics.get(),
            storage ? nextClosure.get() : nullptr,
            begins,
            mixed ? finalClosure.get() : nullptr);
    const auto readbackOwners =
        OriginalBindlessRootReadbackSequence::additionalOwnerBound(readbackInput);
    const auto rootOwners = Roots::additionalOwnerBound(rootInput, produced ? 1u : 3u);
    if (owners > seal->originalBudget().maxSubmissionOwners
        || readbackOwners > seal->originalBudget().maxSubmissionOwners - owners
        || rootOwners > seal->originalBudget().maxSubmissionOwners - owners - readbackOwners)
        return reject("RHI-VULKAN-ROOT-FENCE-OWNER-CAPACITY");
    if (core::SharedOwnerProjection::sameControlBlock(quota, closure->originalMetadataQuota())
        || core::SharedOwnerProjection::sameControlBlock(quota, binding->originalMetadataQuota())
        || core::SharedOwnerProjection::sameControlBlock(
            quota, binding->originalResult()->originalMetadataQuota()))
        return reject("RHI-VULKAN-ROOT-FENCE-QUOTA-DOMAIN");
    if (graphics
        && core::SharedOwnerProjection::sameControlBlock(quota, graphics->originalMetadataQuota()))
        return reject("RHI-VULKAN-GRAPHICS-FENCE-QUOTA-DOMAIN");
    if (graphicsDraw || storage || produced)
        for (const char* entrypoint : {"vkCmdBeginRenderPass",
                 "vkCmdEndRenderPass",
                 "vkCmdDraw",
                 "vkCmdSetViewport",
                 "vkCmdSetScissor"})
            if (!closure->runtime_->getDeviceProcAddr(entrypoint))
                return reject("RHI-VULKAN-GRAPHICS-FENCE-ENTRYPOINT");
    if (geometryDraw)
        for (const char* entrypoint :
            {"vkCmdBindVertexBuffers", "vkCmdBindIndexBuffer", "vkCmdDrawIndexed"})
            if (!closure->runtime_->getDeviceProcAddr(entrypoint))
                return reject("RHI-VULKAN-GEOMETRY-FENCE-ENTRYPOINT");
    if (produced)
        for (const char* entrypoint : {"vkCmdBindVertexBuffers",
                 "vkCmdBindIndexBuffer",
                 "vkCmdDrawIndexedIndirect",
                 "vkCmdPushConstants"})
            if (!closure->runtime_->getDeviceProcAddr(entrypoint))
                return reject("RHI-VULKAN-PRODUCED-COMMAND-ENTRYPOINT");
    for (const char* entrypoint : {"vkCmdFillBuffer",
             "vkCmdPipelineBarrier",
             "vkCmdBindPipeline",
             "vkCmdBindDescriptorSets",
             "vkCmdDispatch",
             "vkCmdCopyBuffer",
             "vkEndCommandBuffer",
             "vkQueueSubmit",
             "vkGetFenceStatus"})
        if (!closure->runtime_->getDeviceProcAddr(entrypoint))
            return reject("RHI-VULKAN-ROOT-FENCE-ENTRYPOINT");
    auto fault = Binding::acquireOriginalFaultCustody(binding, diagnostic);
    if (!fault.valid())
        return {};
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto command =
            createVulkanRhiNativeCommandPayload(*extension->nativeResources_, diagnostic);
        if (!command)
            return {};
        if (!command->prepareOriginalSubmissionFence(diagnostic))
            return {};
        command->impl_->pendingBufferBarriers.reserve(program->operations().size());
        if (textureReads || graphicsDraw || depthReads || storage || produced)
            command->impl_->pendingImageBarriers.reserve(program->operations().size());
        auto custody =
            shareOriginal(core::makeUnique<VulkanOriginalBindlessCommand>(ConstructionKey{},
                std::move(closure),
                std::move(reservation),
                std::move(binding),
                std::move(fault),
                std::move(graphics),
                std::move(nextClosure),
                std::move(finalClosure),
                readbackInput,
                rootInput,
                passInput));
        if (const auto* id = custody->readbacks_.diagnostic())
            return reject(id);
        if (const auto* id = custody->additionalFaults_.prepare(custody->readbacks_))
            return reject(id);
        if (const auto* id = custody->additionalFaults_.acquire(
                custody->readbacks_, custody->fault_, diagnostic))
            return reject(id);
        if (const auto* id = custody->roots_.validate(
                custody->closure_->originalInventory(), program, [](const auto& selected) noexcept {
                    return Closure::validateOriginal(selected);
                }))
            return reject(id);
        if (storage && !custody->prepareStoragePasses(attachmentCapacity, diagnostic))
            return {};
        custody->command_ = command.get();
        seal->nativeColdCommandClaimed_ = true;
        command->impl_->originalBindlessCustody = custody;
        if (!deferRecording && !custody->record(*command, diagnostic))
            return {};
        diagnostic.clear();
        return command;
    } catch (const std::bad_alloc&) {
        return reject("RHI-VULKAN-ROOT-FENCE-ALLOCATION");
    }
}

const char* VulkanOriginalBindlessCommand::validatePending() const noexcept
{
    const auto& seal = reservation_->originalSeal();
    if (const auto* id = reservation_->validateRetainedOwnership())
        return id;
    if (const auto* id =
            additionalFaults_.validatePending(readbacks_, seal->originalRoot(), fault_))
        return id;
    for (const auto& selected : roots_.closures())
        if (const auto* id = Closure::validateOriginalPending(selected, seal))
            return id;
    if (graphics_)
        if (const auto* id = graphics_->validateNativeOwners(seal))
            return id;
    for (const auto& pass : producedPasses_)
        if (const auto* id = pass->validateNativeOwners(seal))
            return id;
    if (nextClosure_ && producedPasses_.empty()) {
        if (finalClosure_) {
            for (const auto& entry : closure_->entries_)
                if (entry.completedDepth)
                    if (const auto* id = seal->validateCompletedDepthReadPending(
                            {entry.completedDepth,
                                ResourceState::ShaderRead,
                                PipelineStage::ComputeShader,
                                ResourceAccess::ShaderRead},
                            2u))
                        return id;
        }
        return validateStoragePasses();
    }
    if (lifecycleVersion() != 8u && lifecycleVersion() != 9u && lifecycleVersion() != 14u
        && lifecycleVersion() != 19u)
        return nullptr;
    const auto validateTexture = [&](const Closure::Entry& entry) noexcept -> const char* {
        if (!entry.parentPayload.image)
            return nullptr;
        const auto& view = std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
            closure_->inventory_->sources()[entry.sourceIndex]);
        const auto& object = *view->originalParentLease()->originalObject();
        const bool graphicsDepth = lifecycleVersion() == 19u;
        if (lifecycleVersion() == 14u || (graphicsDepth && entry.completedDepth)) {
            if (const auto* id = seal->validateCompletedDepthReadPending(
                    {entry.completedDepth,
                        ResourceState::ShaderRead,
                        graphicsDepth ? PipelineStage::FragmentShader
                                      : PipelineStage::ComputeShader,
                        ResourceAccess::ShaderRead},
                    graphicsDepth ? 3u : 2u))
                return id;
            return nullptr;
        }
        const auto completed = object.observeOriginalContent();
        if (!completed)
            return "RHI-VULKAN-TEXTURE-FENCE-COMPLETED-CONTENT";
        if (const auto* id = seal->validateCompletedTextureReadPending(object,
                *completed,
                graphicsDepth ? 8u
                    : lifecycleVersion() == 9u
                    ? (seal->originalRoot()->originalBindlessCompletedReadContractVersion() == 2u
                              ? 7u
                              : 5u)
                    : 4u))
            return id;
        return nullptr;
    };
    const auto& root = *closure_->originalRoot();
    if (seal->originalRoot()->hasOriginalBindlessGraphicsGeometryProfile()
        && root.originalShaderEntryAccessVersion() == 4u) {
        // Physical descriptor ordinals include samplers/readback slots. Resolve
        // the authenticated shader declaration's canonical source instead.
        for (std::size_t i = 0u; i < root.size(); ++i) {
            BindlessResourceAccess access;
            std::size_t source = SIZE_MAX;
            bool active = false;
            if (const auto* id = root.readOriginalShaderDeclaration(
                    i, ShaderStage::Vertex | ShaderStage::Fragment, access, source, active, 4u))
                return id;
            if (!active || access.kind != BindlessAccessKind::Texture)
                continue;
            const auto entry = std::find_if(closure_->entries_.begin(),
                closure_->entries_.end(),
                [&](const auto& candidate) noexcept {
                    return candidate.sourceIndex == source;
                });
            if (entry == closure_->entries_.end() || !entry->parentPayload.image)
                return "RHI-VULKAN-ROOT-PENDING-TEXTURE-SOURCE";
            if (const auto* id = validateTexture(*entry))
                return id;
        }
    } else {
        for (const auto& entry : closure_->entries_)
            if (const auto* id = validateTexture(entry))
                return id;
    }
    return nullptr;
}

const char* VulkanOriginalBindlessCommand::validateStoragePasses(bool nativeObjects) const noexcept
{
    if (!nextClosure_ || !attachmentLease_ || !attachmentLease_->charged()
        || attachmentLease_->units() != clearPasses_.size() || clearPasses_.empty())
        return "RHI-VULKAN-ROOT-STORAGE-PASS-CUSTODY";
    for (const auto& pass : clearPasses_) {
        if (nativeObjects
            && (!pass.native.renderPass || !pass.native.framebuffer
                || pass.native.imageViewCount != 1u || !pass.native.imageViews[0u]))
            return "RHI-VULKAN-ROOT-STORAGE-PASS-NATIVE-OWNER";
        if (const auto* id = OriginalCommandTextureViewSource::validateOriginal(pass.source))
            return id;
        const auto& object = *pass.source->originalParentLease()->originalObject();
        const auto parent = vulkanRhiNativePayload(object),
                   view = vulkanRhiNativePayload(*pass.source->originalObject());
        const auto* nativeView = view ? view->backendVulkanOriginalTextureViewDesc() : nullptr;
        if (!parent || !view || !parent->backendVulkanOriginalContentExclusive()
            || !pass.parent.matches(parent) || !pass.view.matches(view) || !nativeView
            || !view->backendVulkanTextureViewDescAvailable()
            || !pass.source->originalViewLease()->layout().matches(*nativeView)
            || pass.parent.format != VK_FORMAT_R16G16B16A16_SFLOAT
            || pass.parent.image != pass.view.image || pass.parent.format != pass.view.format
            || !core::SharedOwnerProjection::sameOwner(
                parent, view->backendVulkanTextureViewParentPayload())
            || parent->backendVulkanDeviceHandle() != closure_->runtime_->logicalDevice()
            || parent->backendVulkanSwapchainFrameSyncAvailable())
            return "RHI-VULKAN-ROOT-STORAGE-PASS-NATIVE-IDENTITY";
        const auto& d = pass.source->originalObject()->desc();
        bool retained = false;
        for (const auto& element : closure_->root_->elements())
            if (const auto* source =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &element.owner)) {
                const auto& other = (*source)->originalObject()->desc();
                retained = retained
                    || (core::SharedOwnerProjection::sameOwner(
                            (*source)->originalParentLease(), pass.source->originalParentLease())
                        && other.baseMipLevel == d.baseMipLevel
                        && other.mipLevelCount == d.mipLevelCount
                        && other.baseArrayLayer == d.baseArrayLayer
                        && other.arrayLayerCount == d.arrayLayerCount && other.aspect == d.aspect);
            }
        if (!retained)
            return "RHI-VULKAN-ROOT-STORAGE-PASS-RANGE-CLOSURE";
    }
    return nullptr;
}

bool VulkanOriginalBindlessCommand::prepareStoragePasses(
    const std::shared_ptr<core::AtomicCapacityBudget>& capacity, core::pmr::String& diagnostic)
{
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    const auto& program = binding_->originalProgram();
    const auto& usage = *program->originalInputs()->originalScalars()->originalUsage();
    const auto& runtime = *closure_->runtime_;
    const auto count = std::count_if(
        program->operations().begin(), program->operations().end(), [](const auto& op) {
            return op.kind == CommandUsageKind::BeginRenderPass;
        });
    if (!capacity || !count || count > closure_->budget_.maxAttachmentViews)
        return reject("RHI-VULKAN-ROOT-STORAGE-PASS-CAPACITY");
    for (const char* name : {"vkCreateRenderPass",
             "vkCreateImageView",
             "vkCreateFramebuffer",
             "vkDestroyRenderPass",
             "vkDestroyImageView",
             "vkDestroyFramebuffer"})
        if (!runtime.getDeviceProcAddr(name))
            return reject("RHI-VULKAN-ROOT-STORAGE-PASS-ENTRYPOINT");
    attachmentLease_ = core::makeUnique<core::AtomicCapacityLease>(capacity, count);
    if (!attachmentLease_->charged())
        return reject("RHI-VULKAN-ROOT-STORAGE-PASS-CAPACITY");
    clearPasses_.reserve(count);
    for (const auto& op : program->operations()) {
        if (op.kind != CommandUsageKind::BeginRenderPass)
            continue;
        const auto* value = program->originalInputs()->descriptorAt(op.commandIndex);
        const auto* args =
            value ? std::get_if<std::shared_ptr<const RenderPassArguments>>(value) : nullptr;
        if (!args || !*args || op.referenceCount != 1u)
            return reject("RHI-VULKAN-ROOT-STORAGE-PASS-DESCRIPTOR");
        const auto& desc = (*args)->desc();
        if (desc.colorAttachments.size() != 1u || desc.hasDepthStencilAttachment
            || desc.layeredRendering || !desc.subpasses.empty()
            || desc.colorAttachments.front().loadOp != LoadOp::Clear
            || desc.colorAttachments.front().storeOp != StoreOp::Store
            || desc.colorAttachments.front().hasResolveTarget)
            return reject("RHI-VULKAN-ROOT-STORAGE-PASS-CLEAR-STORE");
        const auto index = usage.references()[op.firstReference].sourceIndex;
        const auto* source = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
            &closure_->inventory_->sources()[index]);
        if (!source || !*source)
            return reject("RHI-VULKAN-ROOT-STORAGE-PASS-SOURCE");
        const auto& d = (*source)->originalObject()->desc();
        const auto& object = *(*source)->originalParentLease()->originalObject();
        if ((*source)->resource() != desc.colorAttachments.front().target
            || d.format != TextureFormat::Rgba16Float
            || desc.colorAttachments.front().format != d.format || object.desc().format != d.format
            || d.dimension != TextureViewDimension::Texture2D
            || d.usage != TextureUsage::RenderAttachment || d.aspect != TextureAspect::Color
            || d.mipLevelCount != 1u || d.arrayLayerCount != 1u || d.baseArrayLayer
            || object.desc().arrayLayerCount != 1u || object.desc().sampleCount != 1u
            || object.desc().extent.depth != 1u)
            return reject("RHI-VULKAN-ROOT-STORAGE-PASS-VIEW");
        const auto parent = vulkanRhiNativePayload(object),
                   view = vulkanRhiNativePayload(*(*source)->originalObject());
        if (!parent || !view)
            return reject("RHI-VULKAN-ROOT-STORAGE-PASS-PAYLOAD");
        const Extent3D range{semantics::TextureTransferValidation::mipExtent(
                                 object.desc().extent.width, d.baseMipLevel),
            semantics::TextureTransferValidation::mipExtent(
                object.desc().extent.height, d.baseMipLevel),
            1u};
        const auto& limits = runtime.descriptorIndexing().limits;
        if (!range.width || !range.height || range.width > limits.maxFramebufferWidth
            || range.height > limits.maxFramebufferHeight)
            return reject("RHI-VULKAN-ROOT-STORAGE-PASS-EXTENT");
        ClearPass pass;
        pass.commandIndex = op.commandIndex;
        pass.source = *source;
        pass.extent = range;
        pass.parent = Closure::Observation::capture(parent);
        pass.view = Closure::Observation::capture(view);
        clearPasses_.push_back(std::move(pass));
    }
    if (const auto* id = validateStoragePasses(false))
        return reject(id);
    for (auto& pass : clearPasses_) {
        const auto& d = pass.source->originalObject()->desc();
        const VulkanRhiNativeClearStorePassFactory::Attachment a{pass.parent.image,
            pass.parent.format,
            {VK_IMAGE_ASPECT_COLOR_BIT,
                d.baseMipLevel,
                d.mipLevelCount,
                d.baseArrayLayer,
                d.arrayLayerCount}};
        if (!VulkanRhiNativeClearStorePassFactory::create(
                runtime, &a, 1u, false, pass.extent, pass.native))
            return reject("RHI-VULKAN-ROOT-STORAGE-PASS-CREATE");
    }
    return true;
}

bool VulkanOriginalBindlessCommand::record(
    VulkanRhiNativeCommandPayload& command, core::pmr::String& diagnostic)
{
    const auto& seal = reservation_->originalSeal();
    const auto fail = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    if (const auto* id = seal->applyOriginalRecordingPlan())
        return fail(id);
    if (const auto* id = advance(Life::Event::BeginRecording))
        return fail(id);
    if (const auto* id = validatePending())
        return fail(id);
    const auto& program = binding_->originalProgram();
    const auto& inputs = program->originalInputs();
    const auto& usage = *inputs->originalScalars()->originalUsage();
    const auto payload = [&](const auto& op, std::size_t reference) {
        const auto index = usage.references()[op.firstReference + reference].sourceIndex;
        const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
            usage.originalSources()->sources()[index]);
        return vulkanRhiNativePayload(*lease->originalObject());
    };
    for (const auto& op : program->operations()) {
        using K = CommandUsageKind;
        if (op.kind != K::BufferBarrier && op.kind != K::TextureBarrier
            && !command.flushPendingBarriers(diagnostic))
            return false;
        switch (op.kind) {
        case K::BeginRenderPass: {
            const VulkanOriginalBindlessGraphicsPass* graphics = graphics_.get();
            if (!producedPasses_.empty()) {
                graphics = nullptr;
                for (const auto& pass : producedPasses_)
                    if (pass->commandIndex() == op.commandIndex)
                        graphics = pass.get();
            }
            if (producedPasses_.empty() && nextClosure_) {
                const ClearPass* pass = nullptr;
                for (const auto& value : clearPasses_)
                    if (value.commandIndex == op.commandIndex)
                        pass = &value;
                if (!pass || command.impl_->activeRenderPass.renderPass)
                    return fail("RHI-VULKAN-ROOT-STORAGE-PASS");
                const auto& desc = std::get<std::shared_ptr<const RenderPassArguments>>(
                    *inputs->descriptorAt(op.commandIndex))
                                       ->desc();
                const auto& c = desc.colorAttachments.front().clearColor;
                VkClearValue clear{};
                clear.color = {{c.r, c.g, c.b, c.a}};
                VkRenderPassBeginInfo begin{};
                begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                begin.renderPass = pass->native.renderPass;
                begin.framebuffer = pass->native.framebuffer;
                begin.renderArea.extent = {pass->extent.width, pass->extent.height};
                begin.clearValueCount = 1u;
                begin.pClearValues = &clear;
                auto call = reinterpret_cast<PFN_vkCmdBeginRenderPass>(
                    closure_->runtime_->getDeviceProcAddr("vkCmdBeginRenderPass"));
                call(command.impl_->commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
                command.impl_->activeRenderPass = pass->native;
                command.impl_->originalRenderPassBorrowed = true;
                break;
            }
            if (!graphics || command.impl_->activeRenderPass.renderPass)
                return fail("RHI-VULKAN-GRAPHICS-FENCE-PASS");
            std::array<VkClearValue, OriginalBindlessGraphicsPassContract::maxAttachments> clears{};
            const auto* desc = graphics->descriptor();
            for (std::size_t i = 0u; i < desc->colorAttachments.size(); ++i) {
                const auto& c = desc->colorAttachments[i].clearColor;
                clears[i].color = {{c.r, c.g, c.b, c.a}};
            }
            if (desc->hasDepthStencilAttachment)
                clears[desc->colorAttachments.size()].depthStencil = {
                    desc->depthStencilAttachment.depthClearValue, 0u};
            VkRenderPassBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            begin.renderPass = graphics->native_.renderPass;
            begin.framebuffer = graphics->native_.framebuffer;
            begin.renderArea.extent = {graphics->extent_.width, graphics->extent_.height};
            begin.clearValueCount = graphics->attachmentCount_;
            begin.pClearValues = clears.data();
            auto call = reinterpret_cast<PFN_vkCmdBeginRenderPass>(
                closure_->runtime_->getDeviceProcAddr("vkCmdBeginRenderPass"));
            call(command.impl_->commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
            command.impl_->activeRenderPass = graphics->native_;
            command.impl_->originalRenderPassBorrowed = true;
            break;
        }
        case K::BindPreparedGraphicsBinding: {
            const auto sourceIndex = usage.references()[op.firstReference].sourceIndex;
            const auto* selected = roots_.select(closure_->inventory_, sourceIndex);
            if (!selected)
                return fail("RHI-VULKAN-ROOT-COMMAND-UNRETAINED-ROOT");
            if (!command.recordGraphicsPipelineBind(*selected->pipeline_, diagnostic)
                || !command.recordDescriptorSetBind(*selected->descriptors_,
                    selected->root_->authorization()->package().artifact().set,
                    nullptr,
                    0u,
                    diagnostic))
                return false;
            for (const auto& domain : selected->ordinaryDescriptors_)
                if (!command.recordDescriptorSetBind(
                        *domain.payload, domain.set, nullptr, 0u, diagnostic))
                    return false;
            break;
        }
        case K::PushConstants: {
            const auto& arguments = inputs->originalScalars()->originalArguments();
            const auto* push =
                arguments->records()[op.commandIndex].scalar.get<CommandScalarArguments::Push>();
            if (!push
                || !command.recordPushConstants(push->stages,
                    push->offset,
                    push->size,
                    arguments->bytes(op.commandIndex),
                    diagnostic))
                return false;
            break;
        }
        case K::SetViewport:
            if (!command.recordSetViewport(*inputs->originalScalars()
                                               ->originalArguments()
                                               ->records()[op.commandIndex]
                                               .scalar.get<ViewportState>(),
                    diagnostic))
                return false;
            break;
        case K::SetScissor:
            if (!command.recordSetScissor(*inputs->originalScalars()
                                              ->originalArguments()
                                              ->records()[op.commandIndex]
                                              .scalar.get<ScissorState>(),
                    diagnostic))
                return false;
            break;
        case K::Draw: {
            const auto* draw = inputs->originalScalars()
                                   ->originalArguments()
                                   ->records()[op.commandIndex]
                                   .scalar.get<CommandScalarArguments::Draw>();
            if (!command.recordDraw(draw->vertices,
                    draw->instances,
                    draw->firstVertex,
                    draw->firstInstance,
                    diagnostic))
                return false;
            break;
        }
        case K::BindVertexBuffer: {
            const auto* binding = inputs->originalScalars()
                                      ->originalArguments()
                                      ->records()[op.commandIndex]
                                      .scalar.get<CommandScalarArguments::VertexBinding>();
            if (!binding
                || !command.recordVertexBufferBind(
                    binding->slot, *payload(op, 0u), binding->offset, diagnostic))
                return false;
            break;
        }
        case K::BindIndexBuffer: {
            const auto* binding = inputs->originalScalars()
                                      ->originalArguments()
                                      ->records()[op.commandIndex]
                                      .scalar.get<CommandScalarArguments::IndexBinding>();
            if (!binding
                || !command.recordIndexBufferBind(
                    *payload(op, 0u), binding->format, binding->offset, diagnostic))
                return false;
            break;
        }
        case K::DrawIndexed: {
            const auto* draw = inputs->originalScalars()
                                   ->originalArguments()
                                   ->records()[op.commandIndex]
                                   .scalar.get<CommandScalarArguments::DrawIndexed>();
            // The canonical latest-binding/fetch preflight proves this bind.
            // Refuse before calling the legacy helper's scratch-index fallback.
            if (!draw || !command.impl_->indexBufferBound)
                return fail("RHI-VULKAN-GEOMETRY-FENCE-INDEX-NOT-BOUND");
            if (!command.recordDrawIndexed(draw->indices,
                    draw->instances,
                    draw->firstIndex,
                    draw->vertexOffset,
                    draw->firstInstance,
                    diagnostic))
                return false;
            break;
        }
        case K::DrawIndexedIndirect: {
            const auto* draw = inputs->originalScalars()
                                   ->originalArguments()
                                   ->records()[op.commandIndex]
                                   .scalar.get<CommandScalarArguments::DrawIndirect>();
            if (!draw || !command.impl_->indexBufferBound || op.referenceCount != 1u)
                return fail("RHI-VULKAN-ROOT-PRODUCED-DRAW-OPERAND");
            const auto arguments = payload(op, 0u);
            if (!arguments)
                return fail("RHI-VULKAN-ROOT-PRODUCED-DRAW-CUSTODY");
            // A bounded sequence of single-record commands preserves holes and
            // needs neither multiDrawIndirect nor a fabricated count prefix.
            for (std::uint32_t i = 0u; i < draw->count; ++i)
                if (!command.recordDrawIndexedIndirect(*arguments,
                        draw->offset + std::uint64_t(i) * draw->stride,
                        1u,
                        draw->stride,
                        diagnostic))
                    return false;
            break;
        }
        case K::EndRenderPass: {
            if ((!graphics_ && !nextClosure_ && producedPasses_.empty())
                || !command.impl_->originalRenderPassBorrowed)
                return fail("RHI-VULKAN-GRAPHICS-FENCE-PASS");
            auto call = reinterpret_cast<PFN_vkCmdEndRenderPass>(
                closure_->runtime_->getDeviceProcAddr("vkCmdEndRenderPass"));
            call(command.impl_->commandBuffer);
            command.impl_->activeRenderPass = {};
            command.impl_->originalRenderPassBorrowed = false;
            break;
        }
        case K::BufferBarrier: {
            ResourceBarrierDesc desc;
            if (!std::get<ResourceDependencyArguments>(*inputs->descriptorAt(op.commandIndex))
                    .write(K::BufferBarrier, desc)
                || !command.recordBufferBarrier(*payload(op, 0u), desc, diagnostic))
                return false;
            break;
        }
        case K::TextureBarrier: {
            const auto index = usage.references()[op.firstReference].sourceIndex;
            const auto& parent = std::get<std::shared_ptr<const OriginalCommandTextureLease>>(
                usage.originalSources()->sources()[index]);
            ResourceBarrierDesc desc;
            if (!std::get<ResourceDependencyArguments>(*inputs->descriptorAt(op.commandIndex))
                    .write(K::TextureBarrier, desc)
                || !command.recordValidatedOriginalImageBarrier(
                    *vulkanRhiNativePayload(*parent->originalObject()), desc, diagnostic))
                return false;
            break;
        }
        case K::ClearSnapshotFault: {
            if (op.referenceCount != 1u)
                return fail("RHI-VULKAN-ROOT-COMMAND-FAULT-CLEAR-ROOT");
            const auto source = usage.references()[op.firstReference].sourceIndex;
            const auto* selected = roots_.select(closure_->inventory_, source);
            if (!selected)
                return fail("RHI-VULKAN-ROOT-COMMAND-FAULT-CLEAR-ROOT");
            const auto& latch = selected->originalRoot()->originalSnapshot()->originalBufferSource(
                BindlessSnapshot::OriginalBufferRole::FaultLatch);
            const auto native = vulkanRhiNativePayload(*latch->originalObject());
            auto fill = reinterpret_cast<PFN_vkCmdFillBuffer>(
                closure_->runtime_->getDeviceProcAddr("vkCmdFillBuffer"));
            // Exact four-byte clear at its existing /4 stream ordinal, with
            // explicit CopyDst barrier. No legacy preamble/lease/state mutation.
            fill(command.impl_->commandBuffer,
                native->backendVulkanBufferHandle(),
                0u,
                sizeof(std::uint32_t),
                0u);
            break;
        }
        case K::BindPreparedComputeBinding: {
            const auto sourceIndex = usage.references()[op.firstReference].sourceIndex;
            const auto* selected = roots_.select(closure_->inventory_, sourceIndex);
            if (!selected) {
                diagnostic = "RHI-VULKAN-ROOT-COMMAND-UNRETAINED-ROOT";
                return false;
            }
            if (!command.recordComputePipelineBind(*selected->pipeline_, diagnostic)
                || !command.recordDescriptorSetBind(*selected->descriptors_,
                    selected->root_->authorization()->package().artifact().set,
                    nullptr,
                    0u,
                    diagnostic))
                return false;
            for (const auto& domain : selected->ordinaryDescriptors_)
                if (!command.recordDescriptorSetBind(
                        *domain.payload, domain.set, nullptr, 0u, diagnostic))
                    return false;
            break;
        }
        case K::Dispatch: {
            const auto* scalar = inputs->originalScalars()
                                     ->originalArguments()
                                     ->records()[op.commandIndex]
                                     .scalar.get<CommandScalarArguments::Dispatch>();
            if (!command.recordDispatch(scalar->x, scalar->y, scalar->z, diagnostic))
                return false;
            break;
        }
        case K::BufferCopy: {
            BufferCopyDesc desc;
            if (!std::get<TransferCopyArguments>(*inputs->descriptorAt(op.commandIndex)).write(desc)
                || !command.recordBufferCopy(*payload(op, 0u), *payload(op, 1u), desc, diagnostic))
                return false;
            break;
        }
        case K::Finish:
            if (!command.end(diagnostic))
                return false;
            break;
        default:
            break; // Already admitted structural pass boundaries only.
        }
    }
    if (const auto* id = advance(Life::Event::RecordingValidatedAndDrained))
        return fail(id);
    return true;
}

bool VulkanOriginalBindlessCommand::beforeSubmit(
    const VulkanRhiNativeCommandPayload& command, core::pmr::String& diagnostic) noexcept
{
    const auto& seal = reservation_->originalSeal();
    const char* id = command_ == &command ? nullptr : "RHI-VULKAN-ROOT-FENCE-COMMAND-IDENTITY";
    if (!id)
        id = reservation_->validateRetainedOwnership();
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

void VulkanOriginalBindlessCommand::submissionUnproven(
    const VulkanRhiNativeCommandPayload& command) noexcept
{
    if (command_ != &command)
        return;
    advance(Life::Event::CompletionUnproven);
    reservation_->originalSeal()->invalidateOriginalOwnership();
}

void VulkanOriginalBindlessCommand::fenceCompleted(
    const VulkanRhiNativeCommandPayload& command) noexcept
{
    const auto& seal = reservation_->originalSeal();
    if (command_ != &command || command.impl_->device.syntheticRuntime()
        || !command.impl_->submitted || !command.impl_->submitFenceCompleted)
        return;
    if (phase_ == Life::Phase::QuarantinedAfterSubmit) {
        // An authentic fence can prove safe terminal retirement even when the
        // invocation result was uncertain. Invalid journal/result never revive.
        advance(Life::Event::SubmittedQueueAndCallbacksDrained);
        additionalFaults_.release();
        fault_.release();
        return;
    }
    if (phase_ != Life::Phase::SubmitPending)
        return;
    core::pmr::String diagnostic;
    if (validatePending()) {
        submissionUnproven(command);
        return;
    }
    // Every result was reserved cold and names the actual copy destination.
    // A late map failure can leave private bytes, but publishes no member of
    // this set. The same fence and original journal cover all of them.
    for (const auto& binding : readbacks_.bindings()) {
        const auto& source = binding->originalReadbackSource();
        const auto native = vulkanRhiNativePayload(*source->originalObject());
        const auto& request = binding->originalRequest();
        const auto& result = binding->originalResult();
        const auto* bytes =
            native ? static_cast<const std::uint8_t*>(native->backendMappedData()) : nullptr;
        if (!bytes
            || !native->backendInvalidateMappedRead(
                request->offsetBytes(), request->byteCapacity(), diagnostic)) {
            submissionUnproven(command);
            return;
        }
        std::memcpy(result->bytes_.data(), bytes + request->offsetBytes(), result->bytes_.size());
    }
    if (seal->commitOriginalRootSubmission() || advance(Life::Event::CompletionProven)) {
        submissionUnproven(command);
        return;
    }
    for (const auto& binding : readbacks_.bindings())
        binding->originalResult()->published_ = true;
    additionalFaults_.release();
    fault_.release();
}
} // namespace arp::rhi_vulkan
