#pragma once
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/backends/webgpu/webgpu_owned_texture_readback_context.h"
#include "rhi/backends/webgpu/webgpu_owned_texture_readback_preparation.h"
#include "tests/fixtures/webgpu_host_diagnostic_device_fixture.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_original_device_loss_registration_fixture.h"

namespace arp::test {
class WebGpuOwnedReadbackContextFixture final {
    using Extension = rhi_webgpu::WebGpuOwnedTextureReadbackExtension;
    using Plan = rhi::TextureReadbackColdBudgetPlan;

    static Extension& extension(rhi::GpuDevice& device)
    {
        auto* publicExtension = static_cast<rhi::TextureReadbackOwnedPreparationExtension*>(
            device.queryInterface(rhi::kExtensionTextureReadbackOwnedPreparation));
        auto* backend = dynamic_cast<Extension*>(publicExtension);
        assert(backend && !backend->executionAvailable());
        return *backend;
    }

    static void verifyDecision(const rhi::TextureReadbackOwnedPreparationExtension& e,
        bool ready,
        const char* diagnostic = nullptr)
    {
        using Contract = rhi::TextureReadbackOwnedPreparationContract;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto decision = e.executionDecision();
        assert(decision.contractVersion == 1u && decision.executionAvailable == ready
            && e.executionAvailable() == ready
            && (decision.disposition == Contract::Disposition::PrototypeContextReady) == ready);
        if (ready)
            assert(!decision.diagnostic);
        else
            assert(decision.diagnostic && std::string_view(decision.diagnostic) == diagnostic);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }

    static void verifyClosed(
        Extension& e, core::UniquePtr<rhi::GpuTexture>& source, const char* expected)
    {
        const char* id = nullptr;
        const auto* original = source.get();
        rhi::TextureReadbackOwnedDelivery output;
        output.outcome = rhi::TextureReadbackOwnedDelivery::Outcome::Rejected;
        output.diagnostic = "Authored busy output";
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        assert(!e.prepareOwnedOriginal(source, {}, {}, {}, id) && std::string_view(id) == expected
            && source.get() == original);
        assert(!e.pollOriginal(output, id) && std::string_view(id) == expected
            && output.outcome == rhi::TextureReadbackOwnedDelivery::Outcome::Rejected
            && std::string_view(output.diagnostic) == "Authored busy output");
        assert(a == core::defaultCpuAllocator().stats().allocationCount && !e.executionAvailable());
    }

public:
    static void verifyUnavailable(rhi::GpuDevice& device)
    {
        auto& e = extension(device);
        const char* id = nullptr;
        auto recipe = TextureReadbackColdBudgetFixture::prepare();
        const auto q = recipe.originalPlan->quotas();
        verifyDecision(e, false, "RHI-READBACK-OWNED-CONTEXT-UNCONFIGURED");
        const auto baseline = q[0]->liveBytes();
        core::UniquePtr<rhi::GpuTexture> source;
        verifyClosed(e, source, "RHI-READBACK-OWNED-CONTEXT-UNCONFIGURED");
        auto forged = std::shared_ptr<const Plan>(q[1], recipe.originalPlan.get());
        assert(
            std::string_view(e.configureOriginalPlan(forged)) == "RHI-READBACK-COLD-BUDGET-OWNER");
        forged.reset();
        id = e.configureOriginalPlan(recipe.originalPlan);
        assert(id && std::string_view(id) == "RHI-DEVICE-LOSS-REGISTRATION-NATIVE-UNAVAILABLE");
        assert(!e.context_ && q[0]->liveBytes() == baseline
            && Plan::originalLossBindingAvailable(recipe.originalPlan));
        assert(std::string_view(e.shutdownOriginal()) == "RHI-READBACK-OWNED-CONTEXT-UNCONFIGURED");
        recipe = {};
        for (const auto& quota : q)
            assert(!quota->liveBytes());
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(rhi::GpuDevice& device, const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        auto& e = extension(device);
        auto recipe = TextureReadbackColdBudgetFixture::prepare();
        auto q = recipe.originalPlan->quotas();
        verifyDecision(e, false, "RHI-READBACK-OWNED-CONTEXT-UNCONFIGURED");
        auto baseline = q[0]->liveBytes();
        assert(!e.configureOriginalPlan(recipe.originalPlan));
        const auto peak = q[0]->liveBytes() - baseline;
        verifyDecision(e, true);
        assert(e.context_
            && core::SharedOwnerProjection::sameOwner(e.context_->plan, recipe.originalPlan)
            && core::SharedOwnerProjection::sameOwner(e.context_->registry, e.registry_)
            && core::SharedOwnerProjection::sameOwner(
                e.context_->staging->originalPlan(), recipe.originalPlan)
            && core::SharedOwnerProjection::sameOwner(
                e.context_->completion->originalReadbackPlan(), recipe.originalPlan)
            && !e.context_->staging->usedBytes() && !e.context_->publication->pending());
        WebGpuOriginalDeviceLossRegistrationFixture::rememberDeviceContext(
            e.context_->completion, 0u);
        rhi::TextureDesc textureDesc;
        textureDesc.extent = {2u, 2u, 1u};
        textureDesc.format = rhi::TextureFormat::Rgba8Unorm;
        textureDesc.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
        textureDesc.initialState = rhi::ResourceState::CopySrc;
        auto source = device.createTexture(textureDesc);
        assert(source);
        rhi_webgpu::WebGpuTransferResourceObservation observation;
        const auto sourceId = source->id();
        assert(!e.context_->registry->observeTransferResources(&sourceId, 1u, &observation)
            && observation.texture && observation.texture->texture
            && core::SharedOwnerProjection::sameOwner(
                observation.texture, rhi_webgpu::webGpuNativePayloadFor(*source)));
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        assert(std::string_view(e.configureOriginalPlan(recipe.originalPlan))
            == "RHI-READBACK-OWNED-CONTEXT-BUSY");
        {
            const auto* exact = source.get();
            const char* id = nullptr;
            assert(e.executionAvailable() && !e.prepareOwnedOriginal(source, {}, {}, {}, id)
                && std::string_view(id) == "RHI-READBACK-OWNED-VERSION" && source.get() == exact);
            rhi::TextureReadbackOwnedDelivery busy;
            busy.outcome = rhi::TextureReadbackOwnedDelivery::Outcome::Rejected;
            busy.diagnostic = "Authored busy output";
            assert(!e.pollOriginal(busy, id)
                && std::string_view(id) == "RHI-READBACK-OWNED-OUTPUT-BUSY"
                && std::string_view(busy.diagnostic) == "Authored busy output");
        }
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        // Exercise final adoption and exhaustive quota rollback through the public
        // explicit Prototype extension, retaining original native assertions.
        verifyColdAdmission(e, device, textureDesc);
        const auto afterCold = core::defaultCpuAllocator().stats().allocationCount;
        assert(!e.shutdownOriginal() && q[0]->liveBytes() == baseline);
        verifyDecision(e, false, "RHI-READBACK-OWNED-CONTEXT-UNCONFIGURED");
        assert(std::string_view(e.configureOriginalPlan(recipe.originalPlan))
            == "RHI-READBACK-OWNED-CONTEXT-CONSUMED");
        assert(afterCold == core::defaultCpuAllocator().stats().allocationCount);
        source.reset();
        observation = {};
        recipe = {};
        for (const auto& quota : q)
            assert(!quota->liveBytes());
        recipe = TextureReadbackColdBudgetFixture::prepare();
        q = recipe.originalPlan->quotas();
        baseline = q[0]->liveBytes();
        assert(std::string_view(e.configureOriginalPlan(recipe.originalPlan))
            == "RHI-READBACK-OWNED-CONTEXT-CONSUMED");
        auto pressureDevice = arp::tests::createWebGpuContractDevice(desc);
        assert(pressureDevice);
        auto& pressureExtension = extension(*pressureDevice);
        {
            core::CpuMemoryQuotaLease pressure(q[0], q[0]->limitBytes() - baseline - peak + 1u);
            assert(
                pressure.charged() && pressureExtension.configureOriginalPlan(recipe.originalPlan));
            assert(!pressureExtension.context_ && !pressureExtension.bindingConsumed_
                && Plan::originalLossBindingAvailable(recipe.originalPlan));
        }
        {
            core::CpuMemoryQuotaLease pressure(q[0], q[0]->limitBytes() - baseline);
            assert(
                pressure.charged() && pressureExtension.configureOriginalPlan(recipe.originalPlan));
        }
        {
            core::CpuMemoryQuotaLease pressure(q[0], q[0]->limitBytes() - baseline - peak);
            assert(pressure.charged()
                && !pressureExtension.configureOriginalPlan(recipe.originalPlan));
            assert(!pressureExtension.shutdownOriginal());
        }
        pressureDevice.reset();
        assert(q[0]->liveBytes() == baseline);
        recipe = {};
        for (const auto& quota : q)
            assert(!quota->liveBytes());
        auto peer = arp::tests::createWebGpuContractDevice(desc);
        assert(peer);
        auto& peerExtension = extension(*peer);
        assert(!core::SharedOwnerProjection::sameOwner(peerExtension.registry_, e.registry_));
        recipe = TextureReadbackColdBudgetFixture::prepare();
        q = recipe.originalPlan->quotas();
        assert(!peerExtension.configureOriginalPlan(recipe.originalPlan));
        WebGpuOriginalDeviceLossRegistrationFixture::rememberDeviceContext(
            peerExtension.context_->completion, 1u);
        const auto beforeDrop = core::defaultCpuAllocator().stats().allocationCount;
        peer.reset();
        assert(beforeDrop == core::defaultCpuAllocator().stats().allocationCount);
        recipe = {};
        for (const auto& quota : q)
            assert(!quota->liveBytes());
        lossDevice_ = arp::tests::createWebGpuContractDevice(desc);
        assert(lossDevice_);
        lossRecipe_ = TextureReadbackColdBudgetFixture::prepare();
        auto& lossExtension = extension(*lossDevice_);
        assert(!lossExtension.configureOriginalPlan(lossRecipe_.originalPlan));
        WebGpuOriginalDeviceLossRegistrationFixture::rememberDeviceContext(
            lossExtension.context_->completion, 2u);
        std::printf(
            "BINDLESS_RHI_OWNED_READBACK_DEVICE_CONTEXT_NATIVE_OK %llu actual-GpuDevice-registry plan-staging-publication-completion exact-less-exhausted Prototype-admission source-output-preserved idle-shutdown destructor allocation0\n",
            static_cast<unsigned long long>(peak));
        std::puts(
            "BINDLESS_RHI_OWNED_RESOLVED_LOWERING_NATIVE_OK actual-GpuDevice original-plan native-loss-registration ready-retired decision-consumed allocation0");
    }

    static void verifyLoss()
    {
        assert(lossDevice_ && rhi_webgpu::webGpuNativeDeviceLost());
        auto& e = extension(*lossDevice_);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return e.context_->completion->originalLossDelivered();
        }));
        verifyDecision(e, false, "RHI-READBACK-OWNED-CONTEXT-DEVICE-LOST");
        const auto q = lossRecipe_.originalPlan->quotas();
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        assert(!e.shutdownOriginal());
        lossDevice_.reset();
        lossRecipe_ = {};
        for (const auto& quota : q)
            assert(!quota->liveBytes());
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpOwnedContextLossStates.every(function(s, i) {
                        return s && s.observed && !s.userdata && !s.callback && s.deliveries === (i < 2 ? 0 : 1) &&
                               s.phase === (i < 2 ? 3 : 2);
                    });
                // clang-format on
            }) != 0;
        }));
        EM_ASM({
            // clang-format off
                delete Module.arpOwnedContextLossStates;
            // clang-format on
        });
        std::puts(
            "BINDLESS_RHI_OWNED_READBACK_DEVICE_CONTEXT_LOSS_OK actual-loss delayed-detach-safe metadata-retained-until-shutdown quotas4-zero allocation0 execution-closed");
        std::puts(
            "BINDLESS_RHI_OWNED_RESOLVED_LOWERING_LOSS_OK actual-original-loss decision-closed allocation0");
    }

private:
    static void verifyColdAdmission(
        Extension& e, rhi::GpuDevice& device, const rhi::TextureDesc& desc)
    {
        using Preparation = rhi_webgpu::WebGpuOwnedTextureReadbackPreparation;
        using Source = rhi_webgpu::WebGpuOriginalTextureReadbackSource;
        using Destination = rhi_webgpu::WebGpuOriginalTextureReadbackDestination;
        using Publication = rhi_webgpu::WebGpuOriginalTextureReadbackPublication;
        using Lease = rhi::TextureReadbackSourceLease;
        using Phase = rhi::TextureReadbackOwnedPreparationContract::Phase;
        auto& c = *e.context_;
        const auto& r = c.recipe;
        const auto& q = c.plan->quotas();
        const char* id = nullptr;
        auto candidate = device.createTexture(desc);
        assert(candidate);
        const auto originalId = candidate->id();
        rhi::ReadbackTextureDesc d;
        d.label = "atomic 原始";
        d.sourceResource = originalId;
        d.destinationReadbackBuffer = rhi::allocateGpuResourceId();
        d.sizeBytes = 512u;
        d.sourceSubresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
        d.extent = desc.extent;
        d.bytesPerRow = 256u;
        d.rowsPerImage = 2u;
        auto input = rhi::TextureReadbackInputSnapshot::prepareOriginal(d, r.input, q[1], q[1], id);
        assert(input);
        auto layout = rhi::TextureReadbackLayout::prepareOriginal(input, desc, r.layout, q[1], id);
        assert(layout);
        auto request = rhi::TextureReadbackRequestDeclaration::prepareOriginal(
            layout, {rhi::allocateTextureReadbackRequestId(), 37u, 43u}, r.request, q[1], id);
        assert(request);
        auto statistics =
            rhi::TextureReadbackStatisticsPlan::prepareOriginal(layout, r.statistics, q[1], id);
        assert(statistics);
        rhi::TextureReadbackOwnedPreparationIntent intent;
        intent.contractVersion = 1u;
        intent.prototypeOptIn = true;
        intent.content = {1u, true, rhi::TextureReadbackContentIntent::Mode::QueueOrderedSnapshot};
        intent.passLabel = "原始读回";
        intent.sourceBarrier.resource = originalId;
        intent.sourceBarrier.before = intent.sourceBarrier.after = rhi::ResourceState::CopySrc;
        intent.sourceBarrier.textureSubresources = rhi::defaultTextureSubresourceRange(desc);
        intent.sourceBarrier.sourceStages = intent.sourceBarrier.destinationStages =
            rhi::PipelineStage::Transfer;
        intent.sourceBarrier.sourceAccess = intent.sourceBarrier.destinationAccess =
            rhi::ResourceAccess::TransferRead;
        const auto assemble = [&] {
            return std::dynamic_pointer_cast<Preparation>(
                e.prepareOwnedOriginal(candidate, request, statistics, intent, id));
        };
        EM_ASM({
            // clang-format off
                Module.arpAtomicReadbackCreate = Module.preinitializedWebGPUDevice.createBuffer;
                Module.arpAtomicReadbackCreateCalls = 0;
                Module.preinitializedWebGPUDevice.createBuffer = function(d) {
                    ++Module.arpAtomicReadbackCreateCalls;
                    return Module.arpAtomicReadbackCreate.call(this, d);
                };
            // clang-format on
        });
        const auto* exact = candidate.get();
        const auto baselineDevice = q[0]->liveBytes();
        {
            auto lease = Lease::prepareColdOriginal(candidate, request, r.sourceLease, q[1], id);
            assert(lease);
            auto source = Source::prepareColdLeasedSnapshotOriginal(
                layout, c.registry, lease, *candidate, intent.content, r.source, q[1], id);
            assert(source);
            assert(source->validate() && source->validateReadyDeclaration()
                && source->validateReadbackContentProfile() && !source->sourceTexture());
            assert(!Destination::prepareDeviceOriginal(source, r.destination, c.staging, id));
            auto destination = Destination::prepareDeviceColdOriginal(
                source, *candidate, r.destination, c.staging, id);
            assert(destination);
            assert(Destination::initialize(destination) && !destination->creationCallbacksIssued()
                && !destination->nativeResource());
            auto reservation =
                Publication::reserveRequestOriginal(c.publication, request, statistics, id);
            assert(reservation);
            assert(reservation->bindDestinationOriginalCold(destination));
            assert(!reservation->bindColdCandidateDestinationOriginal(destination, *candidate));
            assert(candidate.get() == exact && !source->sourceTexture());
        }
        assert(!c.publication->pending() && !c.staging->usedBytes());
        // Independent real high-water measurements precede exhausted probes.
        std::array<std::uint64_t, 4> baseline{}, peak{};
        for (unsigned i = 0; i < 4; ++i)
            baseline[i] = q[i]->liveBytes();
        const auto factory = [&] {
            std::array<std::uint64_t, 4> before{};
            for (unsigned i = 0; i < 4; ++i)
                before[i] = q[i]->liveBytes();
            auto result = assemble();
            assert(result ? !candidate && result->destination_->source()->sourceTexture() == exact
                          : candidate.get() == exact);
            if (result)
                assert(result->phase() == Phase::CreationPrepared && !result->diagnostic()
                    && c.publication->pending() == 1u && c.staging->usedBytes() == 512u);
            else {
                assert(!c.publication->pending() && !c.staging->usedBytes());
                for (unsigned i = 0; i < 4; ++i)
                    assert(q[i]->liveBytes() == before[i]);
            }
            return result;
        };
        // Admission success consumes the candidate; measure one complete graph
        // with quota markers, then cancel/drop and construct the same descriptor
        // with fresh original request metadata for the exact quota checks below.
        {
            std::array<core::CpuMemoryQuotaLease, 3> markers;
            for (unsigned i = 1; i < 4; ++i)
                markers[i - 1] =
                    core::CpuMemoryQuotaLease(q[i], q[i]->peakBytes() - q[i]->liveBytes() + 1u);
            for (auto& m : markers)
                assert(m.charged());
            std::array<std::uint64_t, 4> marked{};
            for (unsigned i = 0; i < 4; ++i)
                marked[i] = q[i]->liveBytes();
            auto result = factory();
            assert(result);
            for (unsigned i = 1; i < 4; ++i) {
                peak[i] = q[i]->peakBytes() - marked[i];
                assert(peak[i]);
            }
            assert(q[0]->liveBytes() == baselineDevice);
            assert(result->cancel() && result->phase() == Phase::Retired);
        }
        assert(!c.publication->pending() && !c.staging->usedBytes());
        // New source ids require new immutable request/layout owners. Reuse the
        // exact device allocation contract, never mutate frozen resource ids.
        const auto refresh = [&] {
            candidate = device.createTexture(desc);
            assert(candidate);
            exact = candidate.get();
            d.sourceResource = candidate->id();
            intent.sourceBarrier.resource = d.sourceResource;
            request.reset();
            statistics.reset();
            layout.reset();
            input.reset();
            input = rhi::TextureReadbackInputSnapshot::prepareOriginal(d, r.input, q[1], q[1], id);
            assert(input);
            layout = rhi::TextureReadbackLayout::prepareOriginal(input, desc, r.layout, q[1], id);
            assert(layout);
            request = rhi::TextureReadbackRequestDeclaration::prepareOriginal(
                layout, {rhi::allocateTextureReadbackRequestId(), 37u, 43u}, r.request, q[1], id);
            assert(request);
            statistics =
                rhi::TextureReadbackStatisticsPlan::prepareOriginal(layout, r.statistics, q[1], id);
            assert(statistics);
            for (unsigned i = 0; i < 4; ++i)
                baseline[i] = q[i]->liveBytes();
        };
        for (unsigned domain = 1; domain < 4; ++domain) {
            refresh();
            // Sweep every unavailable byte credit, including intermediate
            // factory/control-block/drain allocations. All failures must unwind
            // to the same source, pending slots and four-domain live baselines.
            for (std::uint64_t available = 0u; available < peak[domain]; ++available) {
                core::CpuMemoryQuotaLease pressure(
                    q[domain], q[domain]->limitBytes() - baseline[domain] - available);
                assert(pressure.charged() && !factory());
            }
            {
                core::CpuMemoryQuotaLease pressure(
                    q[domain], q[domain]->limitBytes() - baseline[domain] - peak[domain] + 1u);
                assert(pressure.charged() && !factory());
            }
            {
                core::CpuMemoryQuotaLease pressure(
                    q[domain], q[domain]->limitBytes() - baseline[domain]);
                assert(pressure.charged() && !factory());
            }
            {
                core::CpuMemoryQuotaLease pressure(
                    q[domain], q[domain]->limitBytes() - baseline[domain] - peak[domain]);
                assert(pressure.charged());
                auto value = factory();
                assert(value);
            }
            assert(!c.publication->pending() && !c.staging->usedBytes());
        }
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpAtomicReadbackCreateCalls;
            // clang-format on
        }) == 0);
        refresh();
        auto savedIntent = intent;
        intent.content.mode = rhi::TextureReadbackContentIntent::Mode::CompletedOriginalUpload;
        assert(!factory());
        intent = savedIntent;
        intent.sourceBarrier.before = rhi::ResourceState::CopyDst;
        assert(!factory());
        intent = savedIntent;
        auto savedRequest = request;
        request =
            std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>(q[1], savedRequest.get());
        assert(!factory());
        request = savedRequest;
        savedRequest.reset();
        auto& mutableDesc = const_cast<rhi::TextureDesc&>(candidate->desc());
        ++mutableDesc.extent.width;
        assert(!factory());
        --mutableDesc.extent.width;
        auto result = factory();
        assert(result);
        auto forged = std::shared_ptr<Preparation>(q[1], result.get());
        assert(!forged->hasQuotaBoundSharedOwner(forged));
        forged.reset();
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease requestPressure(q[1], q[1]->limitBytes() - q[1]->liveBytes()),
                resourcePressure(q[2], q[2]->limitBytes() - q[2]->liveBytes());
            assert(requestPressure.charged() && resourcePressure.charged());
            assert(!result->initializeCreation());
        }
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return result->phase() == Phase::CreationReady;
        }));
        assert(result->initializeCreation() && result->record() && result->submit()
            && result->startMapping());
        assert(result->cancel() && !result->cancel());
        result.reset();
        assert(!c.publication->pending() && !c.staging->usedBytes()
            && q[0]->liveBytes() == baselineDevice);
        using Scopes = WebGpuErrorScopeHandoffFixture<85u>;
        for (unsigned mode = 0; mode < 2; ++mode) {
            refresh();
            result = factory();
            assert(result);
            auto destination = result->destination_;
            std::weak_ptr<Destination> retained = destination;
            if (mode == 1u)
                EM_ASM({
                    // clang-format off
                        Module.arpAtomicReadbackBadCreate = Module.preinitializedWebGPUDevice.createBuffer;
                        Module.preinitializedWebGPUDevice.createBuffer = function(d) {
                            return Module.arpAtomicReadbackBadCreate.call(this, Object.assign({}, d, {usage : 0}));
                        };
                    // clang-format on
                });
            Scopes::holdScopes();
            const auto hot = core::defaultCpuAllocator().stats().allocationCount;
            (void)result->initializeCreation();
            assert(hot == core::defaultCpuAllocator().stats().allocationCount);
            Scopes::restoreScopes();
            Scopes::waitHeldScopes();
            if (mode == 1u)
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.createBuffer = Module.arpAtomicReadbackBadCreate;
                        delete Module.arpAtomicReadbackBadCreate;
                    // clang-format on
                });
            if (mode == 0u) {
                assert(result->cancel() && result->phase() == Phase::CanceledDraining);
                result.reset();
                destination.reset();
            }
            assert(c.publication->pending() == 1u && c.staging->usedBytes() == 512u
                && !retained.expired());
            Scopes::releaseScope(0u);
            assert(WebGpuAsyncWaitFixture::until([&] {
                auto owner = retained.lock();
                return owner && owner->creationScopeObserved(0u);
            }));
            Publication::Delivery output;
            assert(!Publication::pollOriginal(c.publication, output, id) && !id);
            assert(c.publication->pending() == 1u && c.staging->usedBytes() == 512u);
            Scopes::releaseScopes();
            if (mode == 0u)
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return retained.expired() && !c.publication->pending();
                }));
            else {
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return destination->creationScopesObserved();
                }));
                assert(result->phase() == Phase::Rejected && result->diagnostic());
                assert(Publication::pollOriginal(c.publication, output, id) && !id
                    && output.outcome == Publication::Outcome::Rejected
                    && core::SharedOwnerProjection::sameOwner(output.originalRequest, request)
                    && !output.result && !output.numericalResult);
                output = {};
                result.reset();
                destination.reset();
                assert(retained.expired());
            }
            retained.reset();
            assert(!c.publication->pending() && !c.staging->usedBytes());
            for (unsigned i = 0; i < 4; ++i)
                assert(q[i]->liveBytes() == baseline[i]);
        }
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createBuffer = Module.arpAtomicReadbackCreate;
                delete Module.arpAtomicReadbackCreate;
                delete Module.arpAtomicReadbackCreateCalls;
            // clang-format on
        });
        std::printf(
            "BINDLESS_RHI_READBACK_ATOMIC_CREATION_NATIVE_OK %llu %llu %llu actual-GpuDevice cold-creation0 final-adoption quota3-exact-less-exhausted rollback hot-closed-before-commit creation-ready allocation0 base-capability-closed\n",
            static_cast<unsigned long long>(peak[1]),
            static_cast<unsigned long long>(peak[2]),
            static_cast<unsigned long long>(peak[3]));
        std::puts(
            "BINDLESS_RHI_READBACK_ATOMIC_CREATION_DRAIN_OK pending-cancel-drop partial-scopes original-rejection native-retention quotas4-zero-delta allocation0");
    }

    static inline core::UniquePtr<rhi::GpuDevice> lossDevice_;
    static inline TextureReadbackColdBudgetFixture::Recipe lossRecipe_;
#endif
};
} // namespace arp::test
