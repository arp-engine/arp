#pragma once
#include "rhi/backends/webgpu/webgpu_bindless_root.h"
#include "rhi/backends/webgpu/webgpu_canonical_callback_layout.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/frontend/bindless_mixed_entry_flow.h"
#include "rhi/frontend/bindless_produced_geometry.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/bindless_root_native_sequence.h"
#include "rhi/frontend/bindless_root_readback_sequence.h"
#include "rhi/frontend/bindless_scene_content.h"

namespace arp::rhi_webgpu {
// Same canonical inventory/graph/stream/seal. No encoder, replacement journal,
// map issuance, callback userdata or completion authority is created here.
class WebGpuBindlessRootColdAdmission final : public core::QuotaBoundSharedObjectIdentity {
    using Closure = WebGpuBindlessRootNativeClosure;
    using Reservation = rhi::OriginalCommandLifecycleReservation;
    using Binding = rhi::OriginalCommandBufferReadbackBinding;
    using Contract = rhi::OriginalBindlessRootCallbackColdContract;
    using Readbacks = rhi::OriginalBindlessRootReadbackSequence;
    using Roots = rhi::OriginalBindlessRootNativeSequence<Closure>;

    class ConstructionKey {
        friend class WebGpuBindlessRootColdAdmission;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = Contract::Budget;
    using Requirements = rhi::OriginalCommandNativeColdRequirements;
    static constexpr std::uint32_t baseCallbackTickets = WebGpuCanonicalCallbackLayout::slotCount;
    static constexpr std::uint32_t baseCallbackRegistrations =
        WebGpuCanonicalCallbackLayout::registrationCount;

    WebGpuBindlessRootColdAdmission(ConstructionKey,
        std::shared_ptr<const Closure> closure,
        std::shared_ptr<const Reservation> reservation,
        std::shared_ptr<const Binding> binding,
        Budget budget,
        Requirements required,
        std::uint32_t owners,
        std::shared_ptr<const Closure> next,
        std::shared_ptr<const Closure> final,
        const Readbacks::Input& readbackInput = {},
        const Roots::Input& rootInput = {},
        const rhi::OriginalBindlessSceneContentInputs& scene = {})
        : roots_({std::move(closure), std::move(next), std::move(final)}, rootInput),
          closure_(roots_.atOrEmpty(0u)), reservation_(std::move(reservation)),
          readbacks_(std::move(binding), readbackInput), binding_(readbacks_.faultBinding()),
          budget_(budget), required_(required), owners_(owners), next_(roots_.atOrEmpty(1u)),
          final_(roots_.atOrEmpty(2u)), scene_(scene)
    {}

    static const char* deriveRequirements(const std::shared_ptr<const Closure>& closure,
        const std::shared_ptr<const rhi::OriginalCommandStateTransaction>& transaction,
        const std::shared_ptr<const Binding>& binding,
        const Budget& budget,
        Requirements& required,
        const std::shared_ptr<const Closure>& next = {},
        const std::shared_ptr<const Closure>& final = {},
        const Readbacks::Input& readbackInput = {},
        const Roots::Input& rootInput = {}) noexcept
    {
        required = {};
        if (const auto* id = Contract::validateIntent(budget))
            return id;
        if (const auto* id = Readbacks::inspectMapDestinations(binding, readbackInput))
            return id;
        if (!budget.pushUpload.empty()
            && (!webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost()))
            return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-RUNTIME";
        if (const auto* id = Closure::validateOriginal(closure))
            return id;
        if (const auto* id = Roots::inspectHistorical(rootInput, {closure, next, final}))
            return id;
        if (const auto* id = Roots::inspectInput(rootInput,
                closure->originalInventory(),
                binding->originalProgram(),
                [](const auto& selected) noexcept {
                    return Closure::validateOriginal(selected);
                }))
            return id;
        if (const auto* id = Readbacks::inspectRootCoverage(
                binding,
                readbackInput,
                rootInput.empty() ? 1u : rootInput.count,
                [&](std::size_t i) noexcept -> const auto& {
                    return rootInput.empty() ? closure->originalRoot()
                                             : rootInput.closures[i]->originalRoot();
                },
                rootInput.budget.snapshotScope))
            return id;
        if (const auto* id =
                Contract::validateCorrespondence(transaction, binding, budget.contractVersion))
            return id;
        const bool produced = budget.contractVersion == 7u;
        const bool geometry = !produced && budget.initializedGeometry.contractVersion != 0u;
        if (produced ? !transaction->hasOriginalBindlessProducedGeometryProfile()
                    || budget.initializedGeometry.contractVersion != 3u || !rootInput.closures
                    || rootInput.count < 5u
                     : geometry != closure->hasInitializedGeometry()
                    || geometry != transaction->hasOriginalBindlessGraphicsGeometryProfile()
                    || transaction->hasOriginalBindlessProducedGeometryProfile())
            return "RHI-WEBGPU-ROOT-CALLBACK-GEOMETRY-CORRESPONDENCE";
        if (geometry)
            if (const auto* id = closure->validateGeometryFetch(
                    binding->originalProgram(), budget.initializedGeometry))
                return id;
        const auto& sources = transaction->originalGraph()
                                  ->originalInputs()
                                  ->originalScalars()
                                  ->originalUsage()
                                  ->originalSources();
        if (!core::SharedOwnerProjection::sameOwner(sources, closure->originalInventory())
            || binding->rootSourceIndex() != sources->originalLeaseIndex(closure->originalRoot()))
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-INVENTORY";
        const bool mixed = budget.contractVersion == 5u;
        const bool storage = budget.contractVersion == 4u || mixed;
        if (mixed) {
            for (const auto& entry : {closure, next, final}) {
                if (const auto* id = Closure::validateOriginal(entry))
                    return id;
                if (!core::SharedOwnerProjection::sameOwner(sources, entry->originalInventory()))
                    return "RHI-WEBGPU-ROOT-MIXED-INVENTORY";
                if (!core::SharedOwnerProjection::sameOwner(
                        closure->originalMetadataQuota(), entry->originalMetadataQuota()))
                    return "RHI-WEBGPU-ROOT-MIXED-QUOTA-DOMAIN";
            }
            const auto visits = std::uint64_t(closure->originalRoot()->size())
                + next->originalRoot()->size() + final->originalRoot()->size();
            const auto flow = rhi::OriginalBindlessMixedEntryFlowContract::inspect(
                {closure->originalRoot(), next->originalRoot(), final->originalRoot()},
                sources,
                {1u, true, visits});
            if (flow.diagnostic)
                return flow.diagnostic;
        } else if (final && !produced)
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-INPUT-SCHEMA";
        else if (storage) {
            if (const auto* id = Closure::validateOriginal(next))
                return id;
            if (!core::SharedOwnerProjection::sameOwner(
                    closure->originalInventory(), next->originalInventory())
                || sources->originalLeaseIndex(next->originalRoot()) == SIZE_MAX)
                return "RHI-WEBGPU-ROOT-STORAGE-INVENTORY";
            if (const auto* id = rhi::OriginalBindlessStorageExecutionContract::validatePair(
                    *closure->originalRoot(), *next->originalRoot()))
                return id;
        } else if (next && !produced)
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-INPUT-SCHEMA";
        const bool graphics = budget.contractVersion == 2u || budget.contractVersion == 6u;
        if (graphics
            != std::holds_alternative<std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>>(
                closure->originalRoot()->originalPrepared()))
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-COMPUTE";
        if (graphics && !closure->attachmentViewCount())
            return "RHI-WEBGPU-ROOT-ATTACHMENT-MISSING";
        const auto& operations = binding->originalProgram()->operations();
        if (operations.size() > budget.maxCommands || operations.size() > UINT32_MAX)
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-COMMAND-CAPACITY";
        if (produced) {
            const auto inspected =
                rhi::OriginalBindlessProducedProgramContract::inspectNative<Closure>(
                    binding->originalProgram(),
                    rootInput,
                    binding,
                    readbackInput,
                    {rootInput.budget.snapshotScope.empty() ? 1u : 2u,
                        budget.prototypeOptIn,
                        budget.maxOperationVisits,
                        rootInput.budget.snapshotScope.maxVisits});
            if (inspected.diagnostic)
                return inspected.diagnostic;
        }
        std::size_t clears = 0u, binds = 0u, dispatches = 0u, copies = 0u, passes = 0u,
                    renderPasses = 0u, indirectDraws = 0u;
        std::uint64_t pushes = 0u, pushBytes = 0u;
        const rhi::OriginalBindlessRootProgramRequest programRequest{budget.contractVersion,
            budget.prototypeOptIn,
            budget.maxCommands,
            budget.maxOperationVisits,
            budget.pushUpload,
            produced       ? 2u
                : geometry ? 1u
                           : 0u,
            produced || geometry};
        for (const auto& op : operations) {
            using K = rhi::CommandUsageKind;
            if (const auto* id = rhi::OriginalBindlessRootProgramContract::validateOperation(
                    op.kind, programRequest))
                return id;
            switch (op.kind) {
            case K::BufferBarrier:
            case K::BeginTransferPass:
            case K::EndTransferPass:
            case K::EndComputePass:
            case K::EndRenderPass:
            case K::Finish:
            case K::TextureBarrier:
            case K::SetViewport:
            case K::SetScissor:
            case K::BindVertexBuffer:
            case K::BindIndexBuffer:
                break;
            case K::PushConstants: {
                const auto& arguments = transaction->originalGraph()
                                            ->originalInputs()
                                            ->originalScalars()
                                            ->originalArguments();
                if (op.commandIndex >= arguments->records().size())
                    return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-COMMAND";
                const auto& value = arguments->records()[op.commandIndex];
                if (++pushes > budget.pushUpload.maxUpdates
                    || pushBytes > budget.pushUpload.maxPayloadBytes
                    || value.byteCount > budget.pushUpload.maxPayloadBytes - pushBytes)
                    return "RHI-ORIGINAL-PUSH-UPLOAD-PAYLOAD-CAPACITY";
                pushBytes += value.byteCount;
                break;
            }
            case K::BeginRenderPass:
                ++renderPasses;
                ++passes;
                break;
            case K::BeginComputePass:
                ++passes;
                break;
            case K::ClearSnapshotFault:
                ++clears;
                break;
            case K::BindPreparedComputeBinding:
            case K::BindPreparedGraphicsBinding:
                ++binds;
                break;
            case K::Dispatch:
            case K::Draw:
            case K::DrawIndexed:
                ++dispatches;
                break;
            case K::DrawIndexedIndirect:
                ++indirectDraws;
                break;
            case K::BufferCopy:
                if (!Readbacks::containsCopy(binding, readbackInput, op.commandIndex))
                    return "RHI-WEBGPU-ROOT-CALLBACK-COLD-COPY";
                ++copies;
                break;
            default:
                return "RHI-WEBGPU-ROOT-CALLBACK-COLD-OPERATION";
            }
        }
        if (!budget.pushUpload.empty()
            && (!pushes || pushBytes > sources->originalBudget().maxPushBytes))
            return "RHI-WEBGPU-ROOT-PUSH-UPLOAD-PAYLOAD";
        const auto expectedClears = Readbacks::faultCount(binding, readbackInput);
        if (clears != expectedClears || copies != Readbacks::resultCount(readbackInput) || !passes
            || (produced ? !renderPasses || passes - renderPasses != 4u || dispatches != 4u
                        || !indirectDraws || binds != rootInput.count
                    : storage ? !renderPasses || passes - renderPasses != (mixed ? 3u : 2u)
                        || binds != (mixed ? 3u : 2u) || dispatches != (mixed ? 3u : 2u)
                              : binds != passes || dispatches < passes
                        || (!graphics && (passes != 1u || dispatches != 1u))))
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-WORKLOAD";
        const auto result = rhi::OriginalCommandBufferReadbackResultContract::inspect(
            readbackInput.empty() ? &binding : readbackInput.bindings,
            Readbacks::resultCount(readbackInput),
            budget.maxOperationVisits,
            budget.readback,
            readbackInput.snapshotScope);
        if (result.diagnostic)
            return result.diagnostic;
        rhi::OriginalCommandReadbackMapContract::Requirements maps;
        if (const auto* id =
                rhi::OriginalCommandReadbackMapContract::deriveRequirements(result.required,
                    sources->originalBudget(),
                    baseCallbackTickets,
                    baseCallbackRegistrations,
                    budget.readbackMap,
                    maps))
            return id;
        if (maps.totalCallbackTickets > UINT32_MAX || maps.totalCallbackRegistrations > UINT32_MAX)
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-CALLBACK-OVERFLOW";
        Requirements candidate;
        candidate.originalCommands = static_cast<std::uint32_t>(operations.size());
        candidate.encoders = candidate.commandBuffers = 1u;
        candidate.recordingScopeCallbacks = candidate.submissionScopeCallbacks =
            WebGpuErrorScopeContract::count;
        candidate.queueCallbacks = 1u;
        candidate.runtimeLossCallbacks = candidate.existingDeviceLossCallbacks =
            rhi::OriginalDeviceNativeLossContract::callbackCount;
        candidate.runtimeLossRegistrations = candidate.existingDeviceLossRegistrations =
            rhi::OriginalDeviceNativeLossContract::registrationCount;
        candidate.capacity = {6u,
            true,
            1u,
            static_cast<std::uint32_t>(maps.totalCallbackTickets),
            static_cast<std::uint32_t>(maps.totalCallbackRegistrations)};
        candidate.readback = result.required;
        candidate.readbackMap = maps;
        required = candidate;
        return nullptr;
    }

    static std::shared_ptr<const WebGpuBindlessRootColdAdmission> prepareOriginal(
        std::shared_ptr<const Closure> closure,
        std::shared_ptr<const Reservation> reservation,
        std::shared_ptr<const Binding> binding,
        rhi::GpuDevice& device,
        const Budget& budget,
        const char*& diagnostic,
        std::shared_ptr<const Closure> next = {},
        std::shared_ptr<const Closure> final = {},
        const Readbacks::Input& readbackInput = {},
        const Roots::Input& rootInput = {},
        const rhi::OriginalBindlessProducedGeometryInputs& producedGeometry = {},
        const rhi::OriginalBindlessSceneContentInputs& scene = {})
    {
        diagnostic = rhi::semantics::OriginalBindlessRootCallbackColdLowering::lowering(
            device.backend(), budget.contractVersion)
                         .diagnostic;
        if (diagnostic)
            return {};
        Requirements required;
        std::uint32_t owners = 0u;
        if ((diagnostic = inspect(closure,
                 reservation,
                 binding,
                 budget,
                 required,
                 owners,
                 next,
                 final,
                 readbackInput,
                 rootInput)))
            return {};
        if ((diagnostic = rhi::OriginalCommandResourceSources::validateRegisteredLifetime(
                 closure->originalInventory(), device)))
            return {};
        if (budget.contractVersion == 7u) {
            auto* extension = static_cast<rhi::BindlessCrossExtension*>(
                device.queryInterface(rhi::kExtensionBindlessCross));
            if (!extension) {
                diagnostic = "RHI-WEBGPU-PRODUCED-GEOMETRY-DEVICE-ISSUER";
                return {};
            }
            diagnostic = producedGeometry.inspectNative(rootInput.closures,
                rootInput.count,
                reservation->originalSeal()->originalRoot()->originalGraph(),
                binding->originalProgram(),
                binding,
                readbackInput,
                webGpuNativeDeviceLimits().maxComputeWorkgroupsPerDimension,
                extension->capabilities().indirectFirstInstance);
        } else if (!producedGeometry.empty())
            diagnostic = "RHI-BINDLESS-PRODUCED-EXECUTION-INPUT-SCHEMA";
        if (diagnostic)
            return {};
        diagnostic = rhi::OriginalBindlessSceneContentContract::inspectNative(
            closure, binding->originalProgram(), scene);
        if (diagnostic)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(
                reservation->originalSeal()->originalRoot()->originalMetadataQuota());
            auto result =
                shareOriginal(core::makeUnique<WebGpuBindlessRootColdAdmission>(ConstructionKey{},
                    std::move(closure),
                    std::move(reservation),
                    std::move(binding),
                    budget,
                    required,
                    owners,
                    std::move(next),
                    std::move(final),
                    readbackInput,
                    rootInput,
                    scene));
            if ((diagnostic = result->validate()))
                return {};
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-ROOT-CALLBACK-COLD-ALLOCATION";
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-ROOT-CALLBACK-COLD-ALLOCATION";
        }
        return {};
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuBindlessRootColdAdmission>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self)
            ? "RHI-WEBGPU-ROOT-CALLBACK-COLD-OWNER"
            : self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = roots_.diagnostic())
            return id;
        if (const auto* id = readbacks_.diagnostic())
            return id;
        // Immutable bytes were checked at cold admission. Pending validation
        // checks the retained root identity without observing committed state.
        if (const auto* id = rhi::OriginalBindlessSceneContentContract::validateRootInput(
                closure_->originalRoot(), scene_))
            return id;
        if (const auto* id = roots_.validate(closure_->originalInventory(),
                binding_->originalProgram(),
                [](const auto& selected) noexcept {
                    return Closure::validateOriginal(selected);
                }))
            return id;
        Requirements required;
        std::uint32_t owners = 0u;
        if (const auto* id = inspect(closure_,
                reservation_,
                binding_,
                budget_,
                required,
                owners,
                next_,
                final_,
                readbacks_.input(),
                roots_.input()))
            return id;
        return owners == owners_
                && core::SharedOwnerProjection::sameOwner(originalMetadataQuota(),
                    reservation_->originalSeal()->originalRoot()->originalMetadataQuota())
            ? nullptr
            : "RHI-WEBGPU-ROOT-CALLBACK-COLD-QUOTA";
    }

    const auto& originalRoot() const noexcept
    {
        return reservation_->originalSeal()->originalRoot();
    }

    const auto& originalClosure() const noexcept { return closure_; }

    const auto& originalSceneContent() const noexcept { return scene_; }

    const auto& nextClosure() const noexcept { return next_; }

    const auto& finalClosure() const noexcept { return final_; }

    const auto& originalRootSequence() const noexcept { return roots_; }

    const auto& originalReservation() const noexcept { return reservation_; }

    const auto& originalReadbackBinding() const noexcept { return binding_; }

    const auto& originalReadbackBindings() const noexcept { return readbacks_.bindings(); }

    const auto& originalReadbackSequence() const noexcept { return readbacks_; }

    const auto& requirements() const noexcept { return required_; }

    const auto& originalBudget() const noexcept { return budget_; }

    std::uint32_t retainedOwnerCount() const noexcept { return owners_; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* inspect(const std::shared_ptr<const Closure>& closure,
        const std::shared_ptr<const Reservation>& reservation,
        const std::shared_ptr<const Binding>& binding,
        const Budget& budget,
        Requirements& required,
        std::uint32_t& owners,
        const std::shared_ptr<const Closure>& next,
        const std::shared_ptr<const Closure>& final,
        const Readbacks::Input& readbackInput,
        const Roots::Input& rootInput) noexcept
    {
        if (const auto* id = Reservation::validateOriginal(reservation))
            return id;
        const auto& seal = reservation->originalSeal();
        if (seal->originalBudget().contractVersion != 10u
            && seal->originalBudget().contractVersion != 11u
            && seal->originalBudget().contractVersion != 12u
            && seal->originalBudget().contractVersion != 13u
            && seal->originalBudget().contractVersion != 16u
            && seal->originalBudget().contractVersion != 18u
            && seal->originalBudget().contractVersion != 20u
            && seal->originalBudget().contractVersion != 22u)
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-LIFECYCLE";
        if ((budget.contractVersion == 2u) != (seal->originalBudget().contractVersion == 12u))
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-LIFECYCLE";
        if ((budget.contractVersion == 3u) != (seal->originalBudget().contractVersion == 13u))
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-LIFECYCLE";
        if ((budget.contractVersion == 4u) != (seal->originalBudget().contractVersion == 16u))
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-LIFECYCLE";
        if ((budget.contractVersion == 5u) != (seal->originalBudget().contractVersion == 18u))
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-LIFECYCLE";
        if ((budget.contractVersion == 6u) != (seal->originalBudget().contractVersion == 20u))
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-LIFECYCLE";
        if ((budget.contractVersion == 7u) != (seal->originalBudget().contractVersion == 22u))
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-LIFECYCLE";
        if (const auto* id = deriveRequirements(closure,
                seal->originalRoot(),
                binding,
                budget,
                required,
                next,
                final,
                readbackInput,
                rootInput))
            return id;
        const auto& request = reservation->originalRequest();
        const auto& expected = required.capacity;
        if (request.contractVersion != expected.contractVersion
            || request.prototypeOptIn != expected.prototypeOptIn
            || request.pendingSubmissions != expected.pendingSubmissions
            || request.callbackTickets != expected.callbackTickets
            || request.callbackRegistrations != expected.callbackRegistrations)
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-CAPACITY-MISMATCH";
        const auto& quota = seal->originalRoot()->originalMetadataQuota();
        if (quota->limitBytes() != budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-QUOTA";
        // Canonical source CPU owners are already included by the reservation.
        // Charge only the closure's added native owners and independent facades.
        // Binding's five references are an explicit conservative owner ceiling;
        // native resources and source capacity owners are not charged twice.
        constexpr std::uint32_t closureAndQuota = 2u, admission = 1u, streamAndQuota = 2u,
                                resultQuota = 1u;
        std::uint32_t native = closure->nativeOwnerCount();
        if (next) {
            if (const auto* id = closure->combinedNativeOwnerCount(
                    *next, budget.maxRetainedOwners, native, final.get()))
                return id;
        }
        const auto count = std::uint64_t(reservation->retainedOwnerCount()) + native
            + closure->initializedGeometryOwnerCount()
            + (next ? next->initializedGeometryOwnerCount() : 0u)
            + (final ? final->initializedGeometryOwnerCount() : 0u) + bool(next) + bool(final)
            + closureAndQuota + admission + streamAndQuota + resultQuota
            + Binding::retainedOwnerCount;
        const auto& lifecycle = seal->originalBudget();
        const auto extra = Readbacks::additionalOwnerBound(readbackInput);
        const auto extraRoots = Roots::additionalOwnerBound(rootInput);
        if (count > UINT32_MAX || extra > UINT32_MAX - count
            || extraRoots > UINT32_MAX - count - extra
            || count + extra + extraRoots > budget.maxRetainedOwners
            || count + extra + extraRoots > lifecycle.maxRecordingOwners
            || count + extra + extraRoots > lifecycle.maxSubmissionOwners
            || count + extra + extraRoots > lifecycle.maxCallbackOwners)
            return "RHI-WEBGPU-ROOT-CALLBACK-COLD-OWNER-CAPACITY";
        owners = static_cast<std::uint32_t>(count + extra + extraRoots);
        const auto& graph = seal->originalRoot()->originalGraph();
        const auto& inputs = graph->originalInputs();
        const std::array quotas{
            inputs->originalScalars()->originalUsage()->originalSources()->originalMetadataQuota(),
            inputs->originalMetadataQuota(),
            graph->originalMetadataQuota(),
            quota,
            seal->originalSubmissionQuota(),
            seal->originalCallbackQuota(),
            closure->originalMetadataQuota(),
            binding->originalResult()->originalMetadataQuota(),
            binding->originalProgram()->originalMetadataQuota()};
        return core::SharedOwnerProjection::hasIndependentOwners(quotas.size(),
                   [&](std::size_t i) noexcept -> const auto& {
                       return quotas[i];
                   })
            ? nullptr
            : "RHI-WEBGPU-ROOT-CALLBACK-COLD-QUOTA-DOMAIN";
    }

    const rhi::OriginalBindlessRootNativeSequence<Closure> roots_;
    const std::shared_ptr<const Closure>& closure_;
    const std::shared_ptr<const Reservation> reservation_;
    const rhi::OriginalBindlessRootReadbackSequence readbacks_;
    const std::shared_ptr<const Binding>& binding_;
    const Budget budget_;
    const Requirements required_;
    const std::uint32_t owners_;
    const std::shared_ptr<const Closure>& next_;
    const std::shared_ptr<const Closure>& final_;
    const rhi::OriginalBindlessSceneContentInputs scene_;
};
} // namespace arp::rhi_webgpu
