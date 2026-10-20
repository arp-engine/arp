#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_state_transaction_ownership_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/frontend/command_state.h"
#include "webgpu_async_wait_fixture.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class WebGpuOriginalL2DeviceLossFixture final {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static constexpr std::uint64_t bytes = 65536u;

    static std::shared_ptr<core::CpuMemoryQuota> quota()
    {
        return core::makeShared<core::CpuMemoryQuota>(bytes);
    }

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

    static rhi::OriginalDeviceNativeLossBudget budget() { return {{1u, true, 1u, 1u, bytes}, 1u}; }

    static void captureNext(unsigned index, bool fail = false)
    {
        EM_ASM(
            {
                // clang-format off
                    Module.arpOriginalL2LossReceipts = Module.arpOriginalL2LossReceipts || new Array(4).fill(0);
                    const promise = Module.preinitializedWebGPUDevice.lost;
                    Module.arpOriginalL2LossThen = promise.then;
                    const original = promise.then;
                    const index = $0;
                    const fail = $1;
                    promise.then = function(callback) {
                        const result = original.call(this, function(info) {
                            ++Module.arpOriginalL2LossReceipts[index];
                            callback(info);
                        });
                        if (fail)
                            throw new Error('Authored binding failure after actual loss subscription');
                        return result;
                    };
                // clang-format on
            },
            index,
            fail ? 1 : 0);
    }

    static void restoreCapture()
    {
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.lost.then = Module.arpOriginalL2LossThen;
                delete Module.arpOriginalL2LossThen;
            // clang-format on
        });
    }

    class Live final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 6> quotas{
            quota(), quota(), quota(), quota(), quota(), quota()};
        core::UniquePtr<rhi::GpuDevice> device;
        std::shared_ptr<const rhi::OriginalCommandResourceStates> graph;
        std::shared_ptr<core::CpuMemoryQuota> recordingQuota = quota();
        std::shared_ptr<rhi::OriginalCommandStateTransaction> transaction;
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalNativeClosure> canonicalClosure;
        rhi::OriginalDeviceLifetime::Ticket ticket;
        std::optional<rhi::OriginalDeviceNativeLossObservation> lossObservation;
        rhi::OriginalResourceRegistrationTicket bufferRecord, textureRecord, viewRecord;
        std::optional<rhi::GpuBuffer::CommittedStateObservation> bufferObservation;
        std::optional<rhi::GpuTexture::CommittedStateObservation> textureObservation;
        std::optional<rhi::common::RecordedResourceStateControl::PendingObservation> bufferPending;
        std::optional<rhi::common::RecordedWholeResourceStateView::PendingObservation>
            texturePending;
        const rhi::GpuBuffer* buffer = nullptr;
        const rhi::GpuTexture* texture = nullptr;
        core::MutationJournal journal;
    };

    static inline core::UniquePtr<Live> live_;
#endif
public:
    template <class Factory> static void prepare(Factory&& create)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using S = rhi::ResourceState;
        using K = rhi::CommandUsageKind;
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Scalars = rhi::OriginalCommandScalars;
        using Store = rhi::CommandVariableArguments;
        using Scalar = rhi::CommandScalarArguments;
        using Inputs = rhi::OriginalCommandInputs;
        const auto request = budget();
        // Measure a real native subscription before historical exhausted probes.
        auto domain = quota(), subscription = quota();
        const auto cold = [&] {
            auto device = create();
            assert(device
                && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            const auto* id = device->prepareOriginalDeviceNativeLoss(request, subscription);
            if (id) {
                is(id, "RHI-ORIGINAL-DEVICE-LOSS-ALLOCATION");
                device.reset();
            }
            return device;
        };
        CpuQuotaFactoryFixture pressure(subscription);
        const auto peak = pressure.measure(cold);
        pressure.verify(peak, cold);
        assert(!domain->liveBytes() && !subscription->liveBytes());
        {
            auto device = create();
            assert(device);
            is(device->prepareOriginalDeviceNativeLoss(request, subscription),
                "RHI-ORIGINAL-DEVICE-LOSS-DOMAIN-UNPREPARED");
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            is(device->prepareOriginalDeviceNativeLoss(request, domain),
                "RHI-ORIGINAL-DEVICE-LOSS-QUOTA-DOMAIN");
            {
                core::CpuMemoryQuotaLease full(subscription, bytes);
                assert(full.charged());
                is(device->prepareOriginalDeviceNativeLoss(request, subscription),
                    "RHI-ORIGINAL-DEVICE-LOSS-ALLOCATION");
            }
            const auto before = subscription->liveBytes();
            captureNext(0u, true);
            is(device->prepareOriginalDeviceNativeLoss(request, subscription),
                "RHI-ORIGINAL-DEVICE-LOSS-NATIVE-BINDING");
            restoreCapture();
            assert(subscription->liveBytes() == before);
            captureNext(1u);
            assert(!device->prepareOriginalDeviceNativeLoss(request, subscription));
            restoreCapture();
            assert(!device->originalDeviceNativeLossDiagnostic());
            subscription->setLimitBytes(bytes - 1u);
            is(device->originalDeviceNativeLossDiagnostic(), "RHI-DEVICE-LOSS-REGISTRATION-QUOTA");
            subscription->setLimitBytes(bytes);
            assert(!device->originalDeviceNativeLossDiagnostic());
            is(device->prepareOriginalDeviceNativeLoss(request, subscription),
                "RHI-ORIGINAL-DEVICE-LOSS-ALREADY-PREPARED");
            const auto ticket = device->originalDeviceLifetimeTicket();
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{domain, subscription}, [&]() noexcept {
                    if (const auto* id = device->originalDeviceNativeLossDiagnostic())
                        return id;
                    if (const auto* id = device->detachOriginalDeviceNativeLoss())
                        return id;
                    is(ticket.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
                    is(device->detachOriginalDeviceNativeLoss(),
                        "RHI-ORIGINAL-DEVICE-LOSS-RETIRED");
                    return static_cast<const char*>(nullptr);
                }));
        }
        assert(!domain->liveBytes() && !subscription->liveBytes());
        {
            auto device = create();
            assert(device
                && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            captureNext(2u);
            assert(!device->prepareOriginalDeviceNativeLoss(request, subscription));
            restoreCapture();
            // Actual device destruction suppresses captured userdata without delivery.
            device.reset();
            assert(!domain->liveBytes() && !subscription->liveBytes());
        }
        {
            auto device = create();
            auto registry = quota();
            assert(device
                && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 1u, 0u, 0u, 5u, bytes}, registry));
            is(device->prepareOriginalDeviceNativeLoss(request, subscription),
                "RHI-ORIGINAL-DEVICE-LOSS-REGISTRY-ALREADY-PREPARED");
        }
        live_ = core::makeUnique<Live>();
        auto& live = *live_;
        live.device = create();
        assert(live.device);
        assert(
            !live.device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, live.quotas[0]));
        captureNext(3u);
        assert(!live.device->prepareOriginalDeviceNativeLoss(request, live.quotas[1]));
        restoreCapture();
        const char* lossDiagnostic = nullptr;
        auto observed = live.device->observeOriginalDeviceNativeLoss(lossDiagnostic);
        assert(observed && !lossDiagnostic);
        live.lossObservation.emplace(*observed);
        observed.reset();
        assert(!live.device->validateOriginalDeviceNativeLoss(*live.lossObservation));
        assert(!live.device->prepareOriginalResourceRegistration(
            {1u, true, 2u, 1u, 1u, 11u, bytes}, live.quotas[2]));
        live.ticket = live.device->originalDeviceLifetimeTicket();
        rhi::BufferDesc buffer;
        buffer.sizeBytes = 64u;
        buffer.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
        buffer.initialState = S::CopySrc;
        auto b = live.device->createBuffer(buffer);
        assert(b);
        live.buffer = b.get();
        rhi::TextureDesc texture;
        texture.extent = {2u, 2u, 1u};
        texture.format = rhi::TextureFormat::Rgba8Unorm;
        texture.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst
            | rhi::TextureUsage::RenderAttachment;
        texture.initialState = S::CopySrc;
        auto t = live.device->createTexture(texture);
        assert(t);
        live.texture = t.get();
        rhi::TextureViewDesc view;
        view.format = texture.format;
        view.usage = rhi::TextureUsage::RenderAttachment;
        // Negative metadata-corruption test only: the actual parent handle is
        // unchanged; a missing declared native owner must reject registration.
        const auto parentPayload = rhi_webgpu::webGpuNativePayloadFor(*t);
        assert(parentPayload);
        const auto savedNativeOwner = parentPayload->ownerDevice;
        parentPayload->ownerDevice = nullptr;
        auto rejectedView = live.device->createTextureView(*t, view);
        assert(!rejectedView);
        assert(live.device->lastOriginalResourceRegistrationDiagnostic()
            == "RHI-ORIGINAL-DEVICE-LOSS-RESOURCE-NATIVE-SOURCE");
        parentPayload->ownerDevice = savedNativeOwner;
        auto v = live.device->createTextureView(*t, view);
        assert(v);
        const auto viewId = v->id();
        live.bufferRecord = b->originalResourceRegistration();
        live.textureRecord = t->originalResourceRegistration();
        live.viewRecord = v->originalResourceRegistration();
        const auto bufferObservation = b->observeOriginalCommittedState();
        const auto textureObservation =
            t->observeOriginalCommittedState(rhi::defaultTextureSubresourceRange(texture));
        assert(bufferObservation && textureObservation);
        live.bufferObservation.emplace(*bufferObservation);
        live.textureObservation.emplace(*textureObservation);
        assert(live.bufferObservation && live.textureObservation);
        const char* diagnostic = nullptr;
        auto bl = rhi::OriginalCommandBufferLease::prepareOriginal(
            b, {1u, true, 2u, bytes}, live.quotas[3], diagnostic);
        auto tl = rhi::OriginalCommandTextureLease::prepareOriginal(
            t, {1u, true, 2u, bytes}, live.quotas[3], diagnostic);
        auto vl = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            v, {1u, true, 2u, bytes}, live.quotas[3], diagnostic);
        auto vs = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            vl, tl, {1u, true, 5u, bytes}, live.quotas[3], diagnostic);
        assert(bl && tl && vs);
        const std::array<Sources::Source, 3> leases{bl, tl, vs};
        rhi::OriginalCommandAdmissionBudget sourceBudget;
        sourceBudget.contractVersion = 1u;
        sourceBudget.prototypeOptIn = true;
        sourceBudget.maxCommands = 5u;
        sourceBudget.maxReferences = 3u;
        sourceBudget.maxGraphOwners = 13u;
        sourceBudget.maxInputDescriptors = 3u;
        sourceBudget.maxStateObservations = 3u;
        sourceBudget.maxTextureRanges = 2u;
        sourceBudget.maxPassColorAttachments = 1u;
        sourceBudget.maxPassAttachmentReferences = 1u;
        for (const auto d : {rhi::CommandReferenceDomain::Buffer,
                 rhi::CommandReferenceDomain::Texture,
                 rhi::CommandReferenceDomain::TextureView})
            sourceBudget.maxSourceOwners[static_cast<std::size_t>(d)] = 1u;
        sourceBudget.maxCpuBytes[0] = sourceBudget.maxCpuBytes[1] = bytes;
        auto sources = Sources::prepareOriginal(
            leases.data(), leases.size(), sourceBudget, live.quotas[3], diagnostic);
        assert(sources);
        const std::array<rhi::CommandUsage, 5> commands{{{K::BufferBarrier, 0u, 1u},
            {K::TextureBarrier, 1u, 1u},
            {K::BeginRenderPass, 2u, 1u},
            {K::EndRenderPass, 3u, 0u},
            {K::Finish, 3u, 0u}}};
        const std::array<Usage::Reference, 3> refs{{{0u}, {1u}, {2u}}};
        auto usage = Usage::prepareOriginal(sources,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            live.quotas[4],
            diagnostic);
        assert(usage);
        const std::array<Store::Input, 5> fields{
            {{*Scalar::capture(K::BufferBarrier, Scalar::None{})},
                {*Scalar::capture(K::TextureBarrier, Scalar::None{})},
                {*Scalar::capture(K::BeginRenderPass, Scalar::None{})},
                {*Scalar::capture(K::EndRenderPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        auto store = Store::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, 5u, 0u, 0u, 0u, bytes},
            live.quotas[4],
            diagnostic);
        assert(store);
        auto scalars = Scalars::prepareOriginal(usage, store, live.quotas[4], diagnostic);
        assert(scalars);
        rhi::RenderPassDesc pass;
        rhi::RenderPassColorAttachmentDesc color;
        color.target = viewId;
        color.format = texture.format;
        pass.colorAttachments.push_back(color);
        auto frozenPass = rhi::RenderPassArguments::prepareOriginal(
            pass, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, live.quotas[4], diagnostic);
        assert(frozenPass);
        const rhi::ResourceBarrierDesc bb{live.buffer->id(), {}, S::CopySrc, S::CopyDst},
            tb{live.texture->id(),
                rhi::defaultTextureSubresourceRange(texture),
                S::CopySrc,
                S::CopyDst};
        const std::array<Inputs::OriginalDescriptor, 3> descriptors{
            {{0u, *rhi::ResourceDependencyArguments::capture(K::BufferBarrier, bb)},
                {1u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, tb)},
                {2u, frozenPass}}};
        auto inputs = Inputs::prepareOriginal(
            scalars, descriptors.data(), descriptors.size(), live.quotas[4], diagnostic);
        assert(inputs && inputs->retainedOwnerCount() == 13u);
        live.graph = rhi::OriginalCommandResourceStates::prepareOriginal(inputs,
            *live.device,
            {1u, true, 1u, 3u, 3u, 2u, 20u, bytes},
            live.quotas[5],
            diagnostic);
        assert(live.graph && !live.graph->validate());
        live.transaction = OriginalStateTransactionOwnershipFixture::prepareBufferOnlyOriginal(
            *live.device, live.quotas[3], live.quotas[4], live.quotas[5], live.recordingQuota);
        assert(!live.transaction->materializeOriginalPrototypePlan({1u, true, 2u, 0u, 1u, 1u}));
        const auto& originalSources = live.transaction->originalGraph()
                                          ->originalInputs()
                                          ->originalScalars()
                                          ->originalUsage()
                                          ->originalSources()
                                          ->sources();
        const auto& originalBuffer =
            std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(originalSources[0]);
        live.canonicalClosure =
            rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalLossBound(live.transaction,
                *live.device,
                {1u,
                    true,
                    1u,
                    1u,
                    18u,
                    originalBuffer->originalObject()->allocation().estimatedSizeBytes,
                    bytes},
                *live.lossObservation,
                diagnostic);
        assert(live.canonicalClosure && live.canonicalClosure->retainedOwnerCount() == 18u);
        assert(
            !live.transaction->applyOriginalPrototypePlan() && !live.transaction->journalEmpty());
        assert(!live.canonicalClosure->validate());
        assert(live.transaction->plannedTransitionCount() == 1u);
        auto peer = create();
        auto peerDomain = quota();
        assert(peer && !peer->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, peerDomain));
        is(peer->validateOriginalDeviceLifetime(live.ticket),
            "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
        EM_ASM({
            // clang-format off
                Module.arpOriginalL2ForeignLoss = 0;
                navigator.gpu.requestAdapter()
                    .then(function(adapter) {
                        if (!adapter)
                            throw new Error('Foreign adapter unavailable');
                        return adapter.requestDevice();
                    })
                    .then(function(device) {
                        device.destroy();
                        return device.lost;
                    })
                    .then(
                        function() {
                            Module.arpOriginalL2ForeignLoss = 1;
                        },
                        function() {
                            Module.arpOriginalL2ForeignLoss = -1;
                        });
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpOriginalL2ForeignLoss;
                // clang-format on
            }) != 0;
        }));
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalL2ForeignLoss;
            // clang-format on
        }) == 1
            && !live.ticket.diagnostic() && !live.device->originalDeviceNativeLossDiagnostic()
            && !live.graph->validate());
        assert(!live.transaction->validate());
        EM_ASM({
            // clang-format off
                delete Module.arpOriginalL2ForeignLoss;
            // clang-format on
        });
        assert(rhi_webgpu::transitionWebGpuRhiBuffer(
            *const_cast<rhi::GpuBuffer*>(live.buffer), bb, &live.journal)
                .success);
        assert(rhi_webgpu::transitionWebGpuRhiTexture(
            *const_cast<rhi::GpuTexture*>(live.texture), tb, &live.journal)
                .success);
        live.bufferPending = rhi_webgpu::webGpuNativePayloadFor(*live.buffer)
                                 ->recordedState()
                                 .observePending(live.journal);
        live.texturePending = rhi_webgpu::webGpuNativePayloadFor(*live.texture)
                                  ->recordedState()
                                  .observePending(live.journal);
        assert(live.bufferPending && live.texturePending);
        is(live.graph->validate(), "RHI-ORIGINAL-BUFFER-STATE-PENDING");
        std::printf(
            "BINDLESS_RHI_ORIGINAL_L2_DEVICE_LOSS_COLD_OK %llu actual-captured-source sealed-domain before-registry exact-less-exhausted retry failure-suppression detach destruction graph-owner20 pending-resources retained\n",
            static_cast<unsigned long long>(peak));
#else
        (void)create;
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        assert(live_);
        auto& live = *live_;
        assert(WebGpuAsyncWaitFixture::until([&] {
            return live.ticket.diagnostic()
                && std::string_view(live.ticket.diagnostic())
                == "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST";
        }));
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalL2LossReceipts.every(function(count) {
                    return count === 1;
                });
            // clang-format on
        }) != 0);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(live.quotas, [&]() noexcept {
            is(live.device->originalDeviceNativeLossDiagnostic(),
                "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            is(live.lossObservation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            is(live.device->validateOriginalDeviceNativeLoss(*live.lossObservation),
                "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            const char* diagnostic = nullptr;
            assert(!live.device->observeOriginalDeviceNativeLoss(diagnostic));
            is(diagnostic, "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            for (const auto* ticket : {&live.bufferRecord, &live.textureRecord, &live.viewRecord})
                is(ticket->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            is(live.graph->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            is(live.bufferObservation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            is(live.textureObservation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            const auto buffer = rhi_webgpu::webGpuNativePayloadFor(*live.buffer);
            const auto texture = rhi_webgpu::webGpuNativePayloadFor(*live.texture);
            core::MutationJournal foreign;
            const auto closed = [&](const auto& payload, const auto& pending) noexcept {
                auto& control = [&]() -> auto& {
                    if constexpr (std::is_same_v<
                                      typename std::decay_t<decltype(payload)>::element_type,
                                      rhi_webgpu::WebGpuNativeTexturePayload>)
                        return const_cast<rhi::common::RecordedTextureStateControl&>(
                            payload->normalizedRecordedState());
                    else
                        return const_cast<rhi::common::RecordedResourceStateControl&>(
                            payload->recordedState());
                }();
                const auto& view = payload->recordedState();
                is(control.originalDomainDiagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                assert(!control.active() && !control.canPrepare(foreign)
                    && !control.prepare(foreign) && foreign.empty());
                assert(!view.observe()
                    && !view.matches({rhi::ResourceState::CopyDst, control.revision()})
                    && !view.observePending(live.journal)
                    && !view.matchesPending(*pending, live.journal));
                is(payload->recordState(rhi::ResourceState::CopySrc, &live.journal),
                    "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                is(payload->recordState(rhi::ResourceState::CopySrc, nullptr),
                    "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            };
            closed(buffer, live.bufferPending);
            closed(texture, live.texturePending);
            live.journal.rollback();
            closed(buffer, live.bufferPending);
            closed(texture, live.texturePending);
            assert(buffer->recordedState().state() == rhi::ResourceState::CopySrc
                && texture->recordedState().state() == rhi::ResourceState::CopySrc);
            is(live.buffer->originalCommittedStateDiagnostic(),
                "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            is(live.texture->originalCommittedStateDiagnostic(live.textureObservation->range()),
                "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            is(live.graph->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            return static_cast<const char*>(nullptr);
        }));
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 7> transactionQuotas{live.quotas[0],
            live.quotas[1],
            live.quotas[2],
            live.quotas[3],
            live.quotas[4],
            live.quotas[5],
            live.recordingQuota};
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(transactionQuotas, [&]() noexcept {
            is(live.canonicalClosure->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            is(live.transaction->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            is(live.transaction->commitOriginalPendingOwnership(),
                "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            assert(!live.transaction->journalEmpty());
            live.transaction->abandonOriginalPendingOwnership();
            is(live.transaction->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            return live.transaction->journalEmpty() ? nullptr
                                                    : "TEST-ORIGINAL-TRANSACTION-LOSS-ROLLBACK";
        }));
        is(live.canonicalClosure->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        std::puts(
            "BINDLESS_RHI_CANONICAL_NATIVE_CLOSURE_LOSS_OK actual-original-browser-lost same-root-owner18 original-native-buffer retained-pending commit-refused rollback-no-revival hot-allocation0 execution-closed");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_PLAN_NATIVE_LOSS_OK actual-browser-lost original-root applied-transition1 pending-retained commit-rejected rollback-no-revival allocation0 gates4-closed");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_NATIVE_LOSS_OBSERVATION_LOSS_OK actual-original-browser-lost original-domain-and-subscription exact-device preserved-terminal no-native-completion hot-allocation0");
        auto quotas = live.quotas;
        auto recording = live.recordingQuota;
        live_.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        assert(!recording->liveBytes());
        EM_ASM({
            // clang-format off
                delete Module.arpOriginalL2LossReceipts;
            // clang-format on
        });
        std::puts(
            "BINDLESS_RHI_ORIGINAL_L2_DEVICE_LOSS_NATIVE_OK actual-browser-lost exact-original-domain registrations-buffer-texture-view committed-pending-graph-revoked rollback-no-revival detached-destroyed-late-userdata-safe hot-allocation0 quotas6-zero full-gates-closed");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_STATE_DOMAIN_NATIVE_OK actual-browser-lost original-payloads scalar-uniform active-prepare-write-committed-pending-matches-closed rollback-no-revival hot-allocation0 quotas6-zero");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_TRANSACTION_OWNERSHIP_NATIVE_OK canonical-root-owner13 original-captured-device unrelated-loss-isolation pending-journal-retained commit-rejected terminal-rollback-no-revival hot-allocation0 quotas7-zero full-gates-closed");
#endif
    }
};
} // namespace arp::test
