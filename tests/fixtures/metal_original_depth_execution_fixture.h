#pragma once
#include "metal_ordinary_depth_closure_fixture.h"
#include "metal_original_bindless_graphics_content_fixture.h"
#include "original_mixed_depth_storage_plan_fixture.h"
#include "original_native_depth_root_preparation_fixture.h"
#include "rhi/backends/metal/metal_original_bindless_command.h"
#include "rhi/frontend/completed_depth_texture_source.h"

#include <cmath>

namespace arp::test {
// The native backends share shader, Root assembly, graph, Content12 and oracle.
// Metal supplies only native correspondence and its real queue completion.
struct MetalDepthRootBackendFixture {
    using Closure = rhi_metal::MetalRhiBindlessRootNativeClosure;
    // Capture submitted work only. The ordinary CTest independently exercises
    // all cancellation/retirement cases with Metal API Validation enabled.
    inline static bool captureSubmittedOnly = false;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Metal public depth6: %s\n", id);
        assert(!id);
    }

    static void verifyStandalone(rhi::GpuDevice& device,
        const std::shared_ptr<rhi::ComputePipeline>& pipeline,
        const std::shared_ptr<const rhi::BindlessPreparedComputeBinding>&,
        bool execute)
    {
        using namespace rhi;
        auto encoder = device.createCommandEncoder({"depth standalone refusal", 1u, 1u});
        assert(encoder);
        auto pass = encoder->beginComputePass({"depth standalone refusal"});
        assert(pass);
        const auto selected = pass->setPipeline(*pipeline);
        assert(!selected.success
            && selected.diagnostics
                == (execute ? "RHI-BINDLESS-ORIGINAL-EXECUTION-PUBLIC-ROOT-REQUIRED"
                            : "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE"));
        assert(pass->end().success);
    }

    template <class Root, class Snapshot, class Proof>
    static void verifyNativeProofs(const Root& root, const Snapshot& snapshot, const Proof& proof)
    {
        const auto prepared = std::get<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>>(
            root->originalPrepared());
        const auto native = rhi_metal::metalRhiPreparedComputePayloadFor(*prepared);
        assert(native && native->descriptors);
        assert(!rhi_metal::metalRhiNativePayloadFor(*snapshot->descriptorSet()));
        assert(native->descriptors->originalCompletedDepthSources.size() == 2u);
        std::size_t count = 0u;
        for (std::size_t i = 0u; i < root->elements().size(); ++i) {
            const auto source = root->originalCompletedDepthAt(i);
            if (!source)
                continue;
            assert(core::SharedOwnerProjection::sameOwner(source, proof));
            ++count;
        }
        for (const auto& source : native->descriptors->originalCompletedDepthSources)
            assert(core::SharedOwnerProjection::sameOwner(source, proof));
        assert(count == 2u);
    }

    static void reportCold()
    {
        std::puts(
            "BINDLESS_METAL_PUBLIC_DEPTH_ROOT6 Source2 Publication4 ReadInput2 Closure9 original-inventory exact-less hot0 cold-only");
    }

    static void verifyValues(
        const rhi::OriginalCommandBindlessRootSource& root, bool fault, unsigned terminal)
    {
        using namespace rhi;
        using namespace rhi_metal;
        std::size_t outputs = 0u;
        core::pmr::String diagnostic;
        for (const auto& element : root.elements()) {
            if (element.declaration.write.binding != 4u)
                continue;
            const auto& lease =
                std::get<std::shared_ptr<const OriginalCommandBufferLease>>(element.owner);
            std::array<std::uint32_t, 4u> words{};
            assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*lease->originalObject()),
                words.data(),
                sizeof(words),
                0u,
                lease->originalObject()->desc().sizeBytes,
                diagnostic));
            if (!fault) {
                float loaded = 0.f, filtered = 0.f;
                std::memcpy(&loaded, &words[0], 4u);
                std::memcpy(&filtered, &words[1], 4u);
                if (std::abs(loaded - .01125f) >= 1e-7f || std::abs(filtered - .01375f) >= 1e-7f)
                    std::fprintf(stderr,
                        "Depth oracle[%zu] load=%.9f sample=%.9f\n",
                        outputs,
                        loaded,
                        filtered);
                assert(std::abs(loaded - .01125f) < 1e-7f && std::abs(filtered - .01375f) < 1e-7f
                    && words[2] == 2u && words[3] == 2u);
            }
            char name[128u];
            std::snprintf(name,
                sizeof(name),
                "metal-public-depth6-fault%d-terminal%u-result%zu.bin",
                fault,
                terminal,
                outputs);
            auto* file = std::fopen(name, "wb");
            assert(file);
            assert(std::fwrite(words.data(), 1u, sizeof(words), file) == sizeof(words));
            assert(!std::fclose(file));
            ++outputs;
        }
        assert(outputs == 2u);
    }

    // terminal: 0 complete, 1 caller drop, 2 retire in-flight, 3 cancel,
    // 4 retire before submit. No CPU completion or synthetic GPU outputs.
    static void execute(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        const rhi::OriginalBindlessRootNativeClosureBudget& closureBudget,
        rhi::GpuDevice& device,
        bool fault,
        unsigned terminal = 0u)
    {
        using namespace rhi;
        using namespace rhi_metal;
        using Execution = OriginalBindlessRootExecution;
        using Tx = OriginalCommandStateTransaction;
        assert(terminal <= 4u);
        const auto bytes = inventory->originalMetadataQuota()->limitBytes();
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(extension);
        {
            const auto prepared = std::get<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                root->originalPrepared());
            // Test-only corruption of the frozen payload, restored before use.
            const auto descriptors = std::const_pointer_cast<MetalRhiNativeDescriptorGroupPayload>(
                metalRhiPreparedComputePayloadFor(*prepared)->descriptors);
            auto metadata = core::makeShared<core::CpuMemoryQuota>(bytes);
            const char* id = nullptr;
            auto closure =
                Closure::prepareOriginal(root, inventory, device, closureBudget, metadata, id);
            require(id);
            assert(closure);
            auto saved = descriptors->originalCompletedDepthSources.front();
            descriptors->originalCompletedDepthSources.front() =
                std::shared_ptr<const OriginalCompletedDepthTextureSource>(metadata, saved.get());
            assert(closure->validate());
            assert(!Closure::prepareOriginal(root, inventory, device, closureBudget, metadata, id)
                && id);
            descriptors->originalCompletedDepthSources.front() = saved;
            require(closure->validate());
            closure.reset();
            assert(!metadata->liveBytes());
        }
        for (unsigned attempt = captureSubmittedOnly ? 1u : 0u; attempt < 2u; ++attempt) {
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> q;
            for (auto& quota : q)
                quota = core::makeShared<core::CpuMemoryQuota>(bytes);
            OriginalBindlessDepthStreamFixture::execute(
                root,
                inventory,
                device,
                [&](const auto&, const auto&, const auto& binding, const auto& tx, GpuDevice&) {
                    OriginalBindlessRootFenceExecutionInputs inputs{
                        root, inventory, tx, binding, q[0], q[1], q[2]};
                    OriginalBindlessRootFenceExecutionBudget budget{{6u, true},
                        closureBudget,
                        {14u,
                            true,
                            1u,
                            0u,
                            0u,
                            tx->retainedOwnerCount() + 2u,
                            1024u,
                            0u,
                            bytes,
                            bytes,
                            0u},
                        {7u, true, 1u, 0u, 0u},
                        bytes,
                        512u};
                    budget.requireWorkloadRetention = true;
                    core::pmr::String diagnostic;
                    diagnostic.reserve(512u);
                    const auto& result = binding->originalResult();
                    const auto refuse = [&](const auto& bad) {
                        assert(!extension->prepareOriginalRootExecution(
                                   device, inputs, bad, diagnostic)
                            && !diagnostic.empty());
                        assert(tx->status() == Tx::Status::Prepared && tx->journalEmpty()
                            && !result->publishedBytes());
                        for (const auto& quota : q)
                            assert(!quota->liveBytes());
                    };
                    auto bad = budget;
                    bad.lifecycle.contractVersion = 13u;
                    refuse(bad);
                    bad = budget;
                    bad.capacity.contractVersion = 6u;
                    refuse(bad);
                    bad = budget;
                    bad.lifecycle.maxCallbackTickets = 1u;
                    refuse(bad);
                    bad = budget;
                    --bad.closure.maxNativeOwners;
                    refuse(bad);
                    {
                        core::CpuMemoryQuotaLease pressure(q[2], bytes);
                        assert(pressure.charged());
                        assert(!extension->prepareOriginalRootExecution(
                            device, inputs, budget, diagnostic));
                        assert(tx->status() == Tx::Status::Prepared && tx->journalEmpty());
                    }
                    auto execution =
                        extension->prepareOriginalRootExecution(device, inputs, budget, diagnostic);
                    if (!execution)
                        std::fprintf(stderr, "Metal depth prepare: %s\n", diagnostic.c_str());
                    assert(execution);
                    assert(Execution::record(execution, {5u, true}));
                    require(Execution::record(execution, {6u, true}));
                    assert(execution->recordingProven() && tx->status() == Tx::Status::Pending
                        && !result->publishedBytes());
                    if (!attempt || terminal == 3u || terminal == 4u) {
                        if (attempt && terminal == 4u) {
                            device.retireOriginalDeviceLifetime();
                            assert(Execution::submit(execution, {6u, true}));
                        }
                        require(execution->cancelExecution());
                        execution.reset();
                        assert(!result->publishedBytes());
                        if (!attempt || terminal == 3u)
                            assert(tx->status() == Tx::Status::Abandoned);
                        return;
                    }
                    auto gate = createMetalRhiNativeSharedEventPayload("public-depth-gate");
                    auto blocker = createMetalRhiNativeCommandPayload("public-depth-blocker", {});
                    assert(gate && blocker);
                    auto blocked = blocker->preparedCompletionState();
                    MetalRhiNativeQueueStats queue;
                    assert(blocker->finish(diagnostic)
                        && blocker->submit(queue, blocked, gate, 1u, {}, 0u, diagnostic));
                    assert(Execution::submit(execution, {5u, true}));
                    require(CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept {
                        return Execution::submit(execution, {6u, true});
                    }));
                    assert(execution->pendingRecords() == 1u && !result->publishedBytes()
                        && q[0]->liveBytes() && q[1]->liveBytes());
                    assert(root->originalSnapshot()->originalSubmissionBudgetOwner()->used() == 1u);
                    if (terminal == 1u)
                        execution.reset();
                    else
                        assert(execution->cancelExecution());
                    if (terminal == 2u)
                        device.retireOriginalDeviceLifetime();
                    const auto residual = q[0]->limitBytes() - q[0]->liveBytes();
                    require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        q, [&]() noexcept -> const char* {
                            signalMetalRhiNativeSharedEvent(gate, 1u);
                            const auto deadline =
                                std::chrono::steady_clock::now() + std::chrono::seconds(10);
                            while (q[0]->liveBytes() != residual) {
                                if (std::chrono::steady_clock::now() > deadline)
                                    return "METAL-DEPTH-COMPLETION-TIMEOUT";
                                std::this_thread::yield();
                            }
                            return nullptr;
                        }));
                    blocked->wait();
                    assert(tx->status()
                        == (terminal == 2u ? Tx::Status::Invalidated : Tx::Status::Committed));
                    assert(bool(result->publishedBytes()) == (terminal != 2u));
                    if (terminal != 2u) {
                        std::uint32_t bits = ~0u;
                        std::memcpy(&bits, result->publishedBytes(), 4u);
                        assert(bits == std::uint32_t(fault));
                        verifyValues(*root, fault, terminal);
                    }
                    if (execution) {
                        const auto deadline =
                            std::chrono::steady_clock::now() + std::chrono::seconds(10);
                        while (execution->pendingRecords()) {
                            assert(std::chrono::steady_clock::now() < deadline);
                            std::this_thread::yield();
                        }
                        assert(execution->submissionCompleted() == (terminal != 2u));
                    }
                    execution.reset();
                },
                12u,
                1u);
            for (const auto& quota : q)
                assert(!quota->liveBytes());
            assert(!root->originalSnapshot()->originalSubmissionBudgetOwner()->used());
        }
        std::printf(
            "BINDLESS_METAL_PUBLIC_DEPTH_EXECUTION6 fault=%d terminal=%u same-journal Content12 real-completion nonuniform2 load sampleLevel query hot-submit0 hot-completion0 retirement0\n",
            fault,
            terminal);
    }
};

using MetalOriginalDepthRootPreparationFixture =
    OriginalNativeDepthRootPreparationFixture<MetalDepthRootBackendFixture>;

class MetalDepthProducerDataFixture final : public MetalOriginalGraphicsBufferDataFixture {
public:
    bool mixedContent = false;
    bool ordinaryDepth = false;

    void configureAdditionalRegistration(rhi::OriginalResourceRegistrationBudget& b) const
    {
        if (mixedContent)
            OriginalMixedDepthStoragePlanFixture::configureRegistration(b);
        if (ordinaryDepth)
            MetalOrdinaryDepthClosureFixture::configureRegistration(b);
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table&, std::uint32_t index) const
    {
        const std::uint32_t value = 10u * (index + 1u);
        core::pmr::String diagnostic;
        assert(rhi_metal::writeMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
            &value,
            sizeof(value),
            0u,
            buffer.desc().sizeBytes,
            diagnostic));
    }
};

class MetalOriginalCompletedDepthFixture final {
public:
    std::shared_ptr<const rhi::BindlessShaderPackage> package;
    bool fault = false;
    unsigned terminal = 0u;
    bool mixedContent = false;
    std::shared_ptr<const rhi::BindlessShaderPackage> ordinaryPackage;

    void operator()(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device) const
    {
        using namespace rhi;
        using Proof = OriginalCompletedDepthTextureSource;
        if (!device) {
            assert(!root && !inventory);
            return;
        }
        std::shared_ptr<const OriginalCommandTextureViewSource> target;
        for (const auto& source : inventory->sources())
            if (const auto* view =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(&source))
                if ((*view)->originalObject()->desc().usage == TextureUsage::RenderAttachment) {
                    assert(!target);
                    target = *view;
                }
        assert(target && target->originalObject()->desc().format == TextureFormat::D32Float);
        const auto& quota = target->originalMetadataQuota();
        const auto baseline = quota->liveBytes();
        {
            const char* id = nullptr;
            auto vd = target->originalObject()->desc();
            vd.usage = TextureUsage::Sampled;
            core::UniquePtr<GpuTextureView> object;
            {
                core::CpuMemoryQuotaScope allocation(quota);
                object =
                    device->createTextureView(*target->originalParentLease()->originalObject(), vd);
            }
            assert(object);
            auto lease = OriginalCommandTextureViewLease::prepareOriginal(
                object, {1u, true, 2u, quota->limitBytes()}, quota, id);
            MetalDepthRootBackendFixture::require(id);
            assert(lease);
            auto source = OriginalCommandTextureViewSource::prepareOriginal(lease,
                target->originalParentLease(),
                {1u,
                    true,
                    OriginalCommandTextureViewSource::completeRetainedOwnerCount,
                    quota->limitBytes()},
                quota,
                id);
            MetalDepthRootBackendFixture::require(id);
            assert(source);
            const Proof::Budget budget{
                2u, true, 1u, quota->limitBytes(), Proof::completeRetainedOwnerCount};
            assert(!Proof::prepareOriginal(source, budget, quota, id) && id);
            MetalOriginalBindlessGraphicsContentFixture producer;
            producer.execute = true;
            producer(root, inventory, device);
            auto proof = Proof::prepareOriginal(source, budget, quota, id);
            MetalDepthRootBackendFixture::require(id);
            assert(proof);
            assert(proof->cells().size() == 1u && proof->cells().front().controlVersion() == 4u);
            const auto deviceLifetime = device->originalDeviceLifetimeTicket();
            auto closure = MetalOriginalDepthRootPreparationFixture::prepare(proof,
                package,
                *device,
                quota,
                true,
                fault,
                terminal,
                mixedContent ? &OriginalMixedDepthStoragePlanFixture::verify<
                                   rhi_metal::MetalRhiBindlessRootNativeClosure>
                             : nullptr);
            auto metadata = closure->originalMetadataQuota();
            closure.reset();
            assert(!metadata->liveBytes());
            if (ordinaryPackage)
                MetalOrdinaryDepthClosureFixture::verify(
                    *device, ordinaryPackage, proof, inventory);
            if (device->validateOriginalDeviceLifetime(deviceLifetime)) {
                assert(proof->validate()
                    && std::string_view(proof->validate()) == "RHI-COMPLETED-DEPTH-SOURCE-PARENT");
            } else {
                if (terminal != 2u && terminal != 4u)
                    MetalDepthRootBackendFixture::require(proof->validate());
                else
                    assert(proof->validate());
            }
        }
        assert(quota->liveBytes() == baseline);
    }
};
} // namespace arp::test
