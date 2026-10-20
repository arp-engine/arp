#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_command_descriptor_binding_fixture.h"
#include "original_command_native_closure_typed_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "original_descriptor_native_identity_fixture.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/descriptor_state_copy.h"
#include "rhi/semantics/command_input_admission_lowering.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
// Real pending/commit probes. No secondary source or native execution issuer.
class OriginalFiniteDescriptorWriteFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static constexpr std::uint64_t bytes = 64u * 1024u;
    using Contract = rhi::OriginalFiniteDescriptorWriteContract;
    std::shared_ptr<rhi::DescriptorArena> arena;
    core::UniquePtr<rhi::DescriptorSet> descriptor;
    std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource> retainedFiniteSource;
    std::shared_ptr<rhi::OriginalCommandResourceSources> retainedFiniteSources;
    std::shared_ptr<const rhi::OriginalCommandInputs> retainedFiniteInputs;
    std::shared_ptr<core::CpuMemoryQuota> positionalGraph;
    OriginalCommandDescriptorStateFixture::Retained retainedTypedState;
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> quotas;
    rhi::OriginalFiniteDescriptorWriteRequest request{1u,
        true,
        2u,
        static_cast<std::uint32_t>(Contract::ownerSlots(2u)),
        Contract::callbackCount,
        {bytes, bytes, bytes}};

    template <class Context> explicit OriginalFiniteDescriptorWriteFixture(Context& context)
    {
        for (auto& quota : quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        core::CpuMemoryQuotaScope scope(context.quotas[2]);
        rhi::DescriptorArenaDesc desc;
        desc.label = "original finite transaction test";
        desc.maxSets = 2u;
        desc.maxDescriptors = 4u;
        arena = core::shareUnique(context.device->createOriginalDescriptorArena(desc));
        assert(arena);
        allocateTarget(context);
    }

    template <class Context> void allocateTarget(Context& context)
    {
        core::CpuMemoryQuotaScope scope(context.quotas[2]);
        rhi::DescriptorSetDesc set;
        set.layout = rhi::DescriptorStateCopy::copy(context.layout->desc().descriptorLayouts[0]);
        descriptor = context.device->allocateOriginalDescriptorSet(arena, set);
        assert(descriptor);
    }

    template <class Context, class Pipeline>
    void recreateTarget(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u>& writes,
        bool report = true)
    {
        // The bounded arena is monotonic until reset. Numeric work has already
        // completed; retire both sets before starting the separate two-slot
        // negative transaction/caller-drop fixture under the original budget.
        retainedTypedState = {};
        retainedFiniteInputs.reset();
        positionalGraph.reset();
        retainedFiniteSource.reset();
        retainedFiniteSources.reset();
        descriptor.reset();
        arena->reset();
        allocateTarget(c);
        (void)commit(c, pipeline, writes, false, report);
    }

    static void drain(const rhi::DescriptorSet& descriptor)
    {
        for (std::uint32_t i = 0u; i < 2000u && !descriptor.originalFiniteWriteScopesDrained(); ++i)
            emscripten_sleep(1);
        assert(descriptor.originalFiniteWriteScopesDrained());
    }

    static void finishActual(rhi::DescriptorSet& object,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& domains)
    {
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                domains, [&]() noexcept -> const char* {
                    return object.finishOriginalFiniteWrite();
                }));
        });
    }

    template <bool targetMemory = false, class Context, class Pipeline, std::size_t Count>
    static const char* commitActual(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        rhi::DescriptorSet& object,
        std::uint32_t set,
        const std::array<rhi::OriginalFiniteDescriptorBufferWrite, Count>& writes,
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& domains)
    {
        for (auto& quota : domains)
            quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        using Request = std::conditional_t<targetMemory,
            rhi::OriginalFiniteTargetMemoryWriteRequest,
            rhi::OriginalFiniteDescriptorWriteRequest>;
        const Request intent{1u,
            true,
            Count,
            static_cast<std::uint32_t>(Contract::ownerSlots(Count)),
            Contract::callbackCount,
            {bytes, bytes, bytes}};
        const auto revision = object.contentRevision();
        const auto begin = [&](const auto& request) -> const char* {
            if constexpr (targetMemory)
                return c.device->beginOriginalFiniteTargetMemoryDescriptorWrite(
                    object, pipeline, set, writes.data(), writes.size(), request, domains);
            else
                return c.device->beginOriginalFiniteDescriptorWrite(
                    object, pipeline, set, writes.data(), writes.size(), request, domains);
        };
        if constexpr (targetMemory) {
            auto invalid = intent;
            invalid.contractVersion = 2u;
            assert(begin(invalid));
            invalid = intent;
            invalid.prototypeOptIn = false;
            assert(begin(invalid));
            for (const auto& quota : domains) {
                const auto limit = quota->limitBytes();
                quota->setLimitBytes(limit - 1u);
                assert(begin(intent));
                quota->setLimitBytes(limit);
                assert(!quota->liveBytes());
            }
            // Ready cancellation followed by retry uses the original candidate.
            if (const auto* id = begin(intent))
                return id;
            assert(object.finishOriginalFiniteWrite() && object.contentRevision() == revision);
            drain(object);
            assert(!object.cancelOriginalFiniteWrite());
            assert(object.contentRevision() == revision);
            for (const auto& quota : domains)
                assert(!quota->liveBytes());
        }
        if (const auto* id = begin(intent))
            return id;
        drain(object);
        finishActual(object, domains);
        assert(object.contentRevision() == revision + 1u && object.hasOriginalFiniteContent()
            && !object.originalScopedContentDiagnostic());
        if constexpr (targetMemory) {
            const auto& quota = writes[0].buffer->originalMetadataQuota();
            const auto diagnostic = [&](auto request) {
                return c.device->diagnoseOriginalFiniteTargetMemoryDescriptorSource(
                    request, object, pipeline, set, writes.data(), writes.size(), quota);
            };
            const rhi::OriginalFiniteTargetMemorySourceDiagnosticRequest request{
                1u, true, object.contentRevision(), quota->limitBytes()};
            assert(!diagnostic(request));
            auto bad = request;
            bad.contractVersion = 2u;
            assert(diagnostic(bad));
            bad = request;
            bad.prototypeOptIn = false;
            assert(diagnostic(bad));
            bad = request;
            ++bad.expectedContentRevision;
            assert(diagnostic(bad));
            const std::shared_ptr<const Pipeline> owner = pipeline;
            assert(core::StringView(object.diagnoseOriginalFiniteSource(
                       {1u, true, object.contentRevision(), quota->limitBytes()},
                       owner,
                       set,
                       writes.data(),
                       writes.size(),
                       quota))
                == "RHI-ORIGINAL-SCOPED-WRITE-PROFILE");
            const auto payload = rhi_webgpu::webGpuNativePayloadFor(object);
            using P = rhi::OriginalDescriptorNativeIdentityRequest::Profile;
            assert(!payload->diagnoseOriginalNativeIdentity({2u, true, P::FiniteTargetMemory1}));
            assert(core::StringView(
                       payload->diagnoseOriginalNativeIdentity({1u, true, P::FiniteWrite1}))
                == "RHI-ORIGINAL-SCOPED-WRITE-PROFILE");
            const rhi::OriginalFiniteDescriptorWriteRequest old{1u,
                true,
                Count,
                static_cast<std::uint32_t>(Contract::ownerSlots(Count)),
                3u,
                {bytes, bytes, bytes}};
            assert(core::StringView(c.device->beginOriginalFiniteDescriptorWrite(
                       object, pipeline, set, writes.data(), writes.size(), old, domains))
                == "RHI-ORIGINAL-SCOPED-WRITE-PROFILE");
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{domains[0], domains[1], domains[2], quota},
                    [&]() noexcept -> const char* {
                        if (const auto* id = diagnostic(request))
                            return id;
                        return payload->diagnoseOriginalNativeIdentity(
                            {2u, true, P::FiniteTargetMemory1});
                    }));
            });
        }
        return nullptr;
    }

    template <class Context, class Pipeline>
    const char* begin(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u>& writes)
    {
        return c.device->beginOriginalFiniteDescriptorWrite(
            *descriptor, pipeline, 0u, writes.data(), writes.size(), request, quotas);
    }

    template <class Context, class Pipeline>
    void budgets(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u>& writes)
    {
        const auto revision = descriptor->contentRevision();
        const auto factory = [&] {
            const auto* id = begin(c, pipeline, writes);
            if (id)
                return false;
            drain(*descriptor);
            assert(!descriptor->cancelOriginalFiniteWrite());
            assert(descriptor->contentRevision() == revision
                && !descriptor->hasOriginalFiniteContent());
            return true;
        };
        std::array<std::uint64_t, 3u> peaks{};
        for (std::size_t i = 0u; i < quotas.size(); ++i)
            peaks[i] = CpuQuotaFactoryFixture(quotas[i]).measure(factory);
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            CpuQuotaFactoryFixture(quotas[i]).verify(peaks[i], factory);
            {
                core::CpuMemoryQuotaLease pressure(quotas[i], quotas[i]->limitBytes());
                assert(!factory());
            }
            assert(!quotas[i]->liveBytes());
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(factory());
        std::pmr::set_default_resource(previous);
        const auto saved = request;
        request.prototypeOptIn = false;
        assert(begin(c, pipeline, writes));
        request = saved;
        auto hostile = writes;
        hostile[1].write.resource = {};
        assert(begin(c, pipeline, hostile));
        hostile = writes;
        hostile[1].buffer = hostile[0].buffer;
        if (hostile[1].write.resource != hostile[0].write.resource)
            assert(begin(c, pipeline, hostile));
        auto forged = std::shared_ptr<Pipeline>(quotas[0], pipeline.get());
        assert(begin(c, forged, writes));
        auto sharedQuota = quotas[1];
        quotas[1] = quotas[0];
        assert(begin(c, pipeline, writes));
        quotas[1] = sharedQuota;
        std::printf(
            "BINDLESS_RHI_ORIGINAL_FINITE_WRITE_BUDGETS_OK peaks%llu,%llu,%llu exact-one-less-exhausted hostile-pmr cleared-candidate\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]));
    }

    template <class Context, class Pipeline>
    const char* diagnoseSource(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u>& writes) const noexcept
    {
        const auto& quota = writes[0].buffer->originalMetadataQuota();
        return c.device->diagnoseOriginalFiniteDescriptorSource(
            {1u, true, descriptor->contentRevision(), quota->limitBytes()},
            *descriptor,
            pipeline,
            0u,
            writes.data(),
            writes.size(),
            quota);
    }

    template <class Context, class Pipeline>
    void sourceDiagnostic(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u>& writes,
        bool report = true)
    {
        using Source = rhi::OriginalCommandPipelineSource<Pipeline>;
        const auto& sourceQuota = writes[0].buffer->originalMetadataQuota();
        assert(core::SharedOwnerProjection::sameOwner(
            sourceQuota, writes[1].buffer->originalMetadataQuota()));
        const rhi::OriginalFiniteDescriptorSourceDiagnosticRequest request{
            1u, true, descriptor->contentRevision(), bytes};
        const std::shared_ptr<const Pipeline> original = pipeline;
        const char* id = nullptr;
        auto source = Source::prepareOriginal(original,
            {1u,
                true,
                Source::kind,
                pipeline->originalResourceRegistration().generation(),
                bytes,
                Source::exclusiveOwnerCount},
            sourceQuota,
            *c.device,
            id);
        assert(source && !id);
        const auto check = [&](auto intent,
                               const auto& owner,
                               std::uint32_t set,
                               const auto* declarations,
                               std::size_t count,
                               const auto& quota) noexcept {
            return c.device->diagnoseOriginalFiniteDescriptorSource(
                intent, *descriptor, owner, set, declarations, count, quota);
        };
        assert(!check(
            request, source->originalPipeline(), 0u, writes.data(), writes.size(), sourceQuota));
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> domains{
            quotas[0], quotas[1], quotas[2], sourceQuota};
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                domains, [&]() noexcept -> const char* {
                    return check(request,
                        source->originalPipeline(),
                        0u,
                        writes.data(),
                        writes.size(),
                        sourceQuota);
                }));
        });
        OriginalDescriptorNativeIdentityFixture::verifyActual(
            rhi_webgpu::webGpuNativePayloadFor(*descriptor),
            domains,
            [&]() noexcept {
                return check(request,
                    source->originalPipeline(),
                    0u,
                    writes.data(),
                    writes.size(),
                    sourceQuota);
            },
            report);
        auto invalid = request;
        invalid.contractVersion = 2u;
        assert(check(invalid, original, 0u, writes.data(), 2u, sourceQuota));
        invalid = request;
        invalid.prototypeOptIn = false;
        assert(check(invalid, original, 0u, writes.data(), 2u, sourceQuota));
        invalid = request;
        invalid.expectedContentRevision++;
        assert(check(invalid, original, 0u, writes.data(), 2u, sourceQuota));
        invalid = request;
        invalid.sourceCpuBytes--;
        assert(check(invalid, original, 0u, writes.data(), 2u, sourceQuota));
        assert(check(request, original, 1u, writes.data(), 2u, sourceQuota));
        assert(check(request,
            original,
            0u,
            static_cast<const rhi::OriginalFiniteDescriptorBufferWrite*>(nullptr),
            2u,
            sourceQuota));
        assert(check(request, original, 0u, writes.data(), 1u, sourceQuota));
        assert(
            check(request, std::shared_ptr<const Pipeline>{}, 0u, writes.data(), 2u, sourceQuota));
        auto forged = std::shared_ptr<const Pipeline>(sourceQuota, pipeline.get());
        assert(check(request, forged, 0u, writes.data(), 2u, sourceQuota));
        forged.reset();
        auto hostile = writes;
        std::swap(hostile[0], hostile[1]);
        assert(check(request, original, 0u, hostile.data(), 2u, sourceQuota));
        hostile = writes;
        hostile[1].buffer = {};
        assert(check(request, original, 0u, hostile.data(), 2u, sourceQuota));
        hostile = writes;
        hostile[0].buffer = std::shared_ptr<const rhi::OriginalCommandBufferLease>(
            quotas[0], writes[0].buffer.get());
        assert(check(request, original, 0u, hostile.data(), 2u, sourceQuota));
        hostile = writes;
        hostile[0].dynamicOffset++;
        assert(check(request, original, 0u, hostile.data(), 2u, sourceQuota));
        hostile = writes;
        hostile[1].write.bufferBindingRange.sizeBytes--;
        assert(check(request, original, 0u, hostile.data(), 2u, sourceQuota));
        hostile = writes;
        hostile[0].write.bufferBindingRange.offsetBytes++;
        assert(check(request, original, 0u, hostile.data(), 2u, sourceQuota));
        hostile = writes;
        hostile[0].write.resourceState = rhi::ResourceState::Undefined;
        if (hostile[0].write.resourceState == writes[0].write.resourceState)
            hostile[0].write.resourceState = rhi::ResourceState::CopySrc;
        assert(check(request, original, 0u, hostile.data(), 2u, sourceQuota));
        auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        assert(check(request, original, 0u, writes.data(), 2u, foreignQuota));
        auto forgedQuota = std::shared_ptr<core::CpuMemoryQuota>(foreignQuota, sourceQuota.get());
        assert(check(request, original, 0u, writes.data(), 2u, forgedQuota));
        forgedQuota.reset();
        for (const auto& quota : quotas)
            assert(check(request, original, 0u, writes.data(), 2u, quota));
        for (const auto& quota : c.quotas)
            assert(check(request, original, 0u, writes.data(), 2u, quota));
        const auto limit = sourceQuota->limitBytes();
        sourceQuota->setLimitBytes(limit - 1u);
        assert(check(request, original, 0u, writes.data(), 2u, sourceQuota));
        sourceQuota->setLimitBytes(limit);
        source.reset();
        assert(!diagnoseSource(c, pipeline, writes));
        if (report)
            std::puts(
                "BINDLESS_RHI_ORIGINAL_FINITE_SOURCE_DIAGNOSTIC_NATIVE_OK genuine-pipeline-source exact-committed-group full-logical-declarations shared-source-quota forged-revision-dynamic-range-reject hot0 query0 no-source3-authority");
    }

    template <class Context, class Pipeline>
    auto commit(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u>& writes,
        bool independentScopes = false,
        bool report = true)
    {
        const auto revision = descriptor->contentRevision();
        auto old = rhi_webgpu::webGpuNativePayloadFor(*descriptor);
        const char* id = nullptr;
        auto oldWrite = descriptor->observeSuccessfulBindingWrite(0u, 0u, id);
        if (independentScopes) {
            EM_ASM({
                // clang-format off
                    const d = Module.preinitializedWebGPUDevice;
                    Module.arpFinitePop = d.popErrorScope;
                    Module.arpFiniteDeferred = [];
                    d.popErrorScope = function() {
                        const actual = Module.arpFinitePop.call(this);
                        return new Promise(
                            resolve => actual.then(error => Module.arpFiniteDeferred.push(() => resolve(error))));
                    };
                // clang-format on
            });
        }
        const auto* error = begin(c, pipeline, writes);
        if (error)
            std::fprintf(stderr, "Finite begin: %s\n", error);
        assert(!error);
        assert(descriptor->contentRevision() == revision
            && rhi_webgpu::webGpuNativePayloadFor(*descriptor) == old);
        assert(descriptor->finishOriginalFiniteWrite() && descriptor->cancelOriginalFiniteWrite());
        assert(diagnoseSource(c, pipeline, writes)); // pending replacement never passes
        assert(begin(c, pipeline, writes));
        assert(!descriptor->write(writes[0].write).success
            && descriptor->contentRevision() == revision);
        if (independentScopes) {
            for (std::uint32_t spin = 0u; spin < 2000u
                && !EM_ASM_INT(
                    {
                        // clang-format off
                            return Module.arpFiniteDeferred.length === 3;
                        // clang-format on
                    });
                ++spin)
                emscripten_sleep(1);
            assert(EM_ASM_INT({
                // clang-format off
                    return Module.arpFiniteDeferred.length === 3;
                // clang-format on
            }));
            for (std::uint32_t participant = 0u; participant < 3u; ++participant) {
                EM_ASM(
                    {
                        // clang-format off
                        Module.arpFiniteDeferred[$0]();
                        // clang-format on
                    },
                    participant);
                for (std::uint32_t spin = 0u;
                    spin < 2000u && !descriptor->originalFiniteWriteScopeObserved(participant);
                    ++spin)
                    emscripten_sleep(1);
                assert(descriptor->originalFiniteWriteScopeObserved(participant));
                if (participant < 2u)
                    assert(descriptor->finishOriginalFiniteWrite());
                assert(descriptor->contentRevision() == revision
                    && rhi_webgpu::webGpuNativePayloadFor(*descriptor) == old);
            }
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.popErrorScope = Module.arpFinitePop;
                    delete Module.arpFinitePop;
                    delete Module.arpFiniteDeferred;
                // clang-format on
            });
        } else
            drain(*descriptor);
        finishActual(*descriptor, quotas);
        assert(descriptor->contentRevision() == revision + 1u
            && descriptor->hasOriginalFiniteContent());
        if (oldWrite)
            assert(oldWrite->diagnostic());
        assert(descriptor->finishOriginalFiniteWrite()); // exactly once
        auto payload = rhi_webgpu::webGpuNativePayloadFor(*descriptor);
        assert(payload && payload != old && payload->group);
        std::uint32_t group = 99u, count = 99u;
        const std::uint32_t* offsets = nullptr;
        assert(!payload->diagnoseOriginalFiniteGroup(group, offsets, count) && group == 0u
            && count == 1u && offsets[0] == writes[0].dynamicOffset);
        assert(core::StringView(payload->ordinaryTextureAccessDiagnostic())
            == "RHI-ORIGINAL-FINITE-WRITE-EMISSION-CLOSED");
        using Source = rhi::OriginalCommandDescriptorSource<rhi::DescriptorSet>;
        for (std::uint32_t version = 1u; version <= 2u; ++version) {
            rhi::OriginalCommandDescriptorSourceBudget budget{
                version, true, 2u, 2u, Source::exclusiveRetainedOwnerCount, bytes};
            assert(!Source::prepareOriginal(
                       descriptor, arena, {}, nullptr, 0u, budget, quotas[0], *c.device, id)
                && core::StringView(id)
                    == "RHI-ORIGINAL-DESCRIPTOR-SOURCE-FINITE-COMPOSITION-CLOSED"
                && descriptor);
        }
        for (const auto& item : writes) {
            auto receipt = descriptor->observeSuccessfulBindingWrite(item.write.binding, 0u, id);
            assert(receipt && !id && !receipt->diagnostic()
                && rhi::descriptorBindingWriteMatches(receipt->write(), item.write));
        }
        sourceDiagnostic(c, pipeline, writes, report);
        return payload;
    }

    template <class Context, class Pipeline>
    void sourceCustody(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u>& originalWrites,
        bool retain = false,
        const std::array<rhi::OriginalCommandResourceSources::Source, 2u>* attachments = nullptr,
        bool nativeClosure = false,
        bool dependencyLossCandidate = false)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Source = rhi::OriginalCommandDescriptorSetSource;
        using PipelineSource = rhi::OriginalCommandPipelineSource<Pipeline>;
        if (!retain) {
            retainedTypedState = {};
            retainedFiniteInputs.reset();
            positionalGraph.reset();
            retainedFiniteSource.reset();
            retainedFiniteSources.reset();
        }
        const auto& quota = originalWrites[0].buffer->originalMetadataQuota();
        const auto baseline = quota->liveBytes();
        const char* id = nullptr;
        auto pipelineSource = PipelineSource::prepareOriginal(pipeline,
            {1u,
                true,
                PipelineSource::kind,
                pipeline->originalResourceRegistration().generation(),
                bytes,
                PipelineSource::exclusiveOwnerCount},
            quota,
            *c.device,
            id);
        assert(pipelineSource);
        std::array<Sources::Source, 4u> prefix{originalWrites[0].buffer, originalWrites[1].buffer};
        const auto bufferCount = core::SharedOwnerProjection::sameOwner(
                                     originalWrites[0].buffer, originalWrites[1].buffer)
            ? 1u
            : 2u;
        if (attachments) {
            prefix[bufferCount] = (*attachments)[0];
            prefix[bufferCount + 1u] = (*attachments)[1];
        }
        const auto prefixCount = bufferCount + (attachments ? 2u : 0u);
        rhi::OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
            bufferCount;
        if (attachments) {
            admission
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] =
                1u;
            admission.maxSourceOwners[static_cast<std::size_t>(
                rhi::CommandReferenceDomain::TextureView)] = 1u;
            admission.maxPassColorAttachments = 2u;
            admission.maxPassAttachmentReferences = 2u;
        }
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
            1u;
        admission
            .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Descriptor)] =
            1u;
        // Three explicit dependencies per original buffer (before, between
        // passes, after), plus the late-repair negative, extend the14-command stream. These are
        // authored state-test ceilings, not inferred native descriptor capacity.
        admission.maxCommands = 14u + 4u * bufferCount + (attachments ? 2u : 0u);
        admission.maxReferences = (attachments ? 10u : 6u) + 4u * bufferCount;
        admission.maxGraphOwners = 64u;
        admission.maxInputDescriptors = (attachments ? 10u : 6u) + 4u * bufferCount;
        admission.maxDynamicOffsets = 6u;
        admission.maxStateObservations = bufferCount + (attachments ? 4u : 0u);
        admission.maxTextureRanges = attachments ? 4u : 0u;
        admission.maxStateTransactions = 1u;
        admission.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Source)] = bytes;
        admission.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Graph)] = bytes;
        admission.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording)] = bytes;
        if (nativeClosure || dependencyLossCandidate) {
            // Independent new native-inspection root; CPU fixtures retain zero
            // native ceilings. Explicit lifecycle/callback ceilings are authored.
            admission.maxNativePayloadObjects = prefixCount + 2u;
            admission.maxTypedNativeCommandCalls = 32u;
            admission.maxPendingSubmissions = 1u;
            admission.maxCallbackTickets = 4u;
            admission.maxCallbackRegistrations = 4u;
            admission.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)] = bytes;
            admission.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)] = bytes;
        }
        const auto makeRoot = [&](std::uint32_t version = 3u) {
            auto root = Sources::prepareColdOriginalTyped(
                prefix.data(), prefixCount, admission, {version, true, 2u}, quota, id);
            assert(root
                && !Sources::appendOriginalTyped(root, Sources::Source{pipelineSource}, *c.device));
            return root;
        };
        auto root = makeRoot();
        rhi::OriginalCommandDescriptorSourceBudget budget{
            3u, true, 2u, 2u, Source::finiteExclusiveRetainedOwnerCount, bytes};
        budget.finitePipelineSourceIndex = prefixCount;
        budget.finiteLogicalSet = 0u;
        budget.finiteFootprintContractVersion = 1u;
        budget.finiteFullBoundViewOptIn = true;
        budget.maxFrozenDynamicOffsets = 1u;
        auto authored = originalWrites;
        std::array<Source::Element, 2u> elements;
        for (std::size_t i = 0u; i < elements.size(); ++i) {
            auto& element = elements[i];
            const auto& binding = c.layout->desc().descriptorLayouts[0].bindings[i];
            authored[i].write.resourceState = binding.type == rhi::DescriptorType::StorageBuffer
                ? rhi::ResourceState::ShaderWrite
                : rhi::ResourceState::ShaderRead;
            element.write = authored[i].write;
            element.sourceIndex = bufferCount == 1u ? 0u : i;
            element.registrationGeneration =
                authored[i].buffer->originalObject()->originalResourceRegistration().generation();
            element.dynamicOffset = authored[i].dynamicOffset;
            element.stages = std::is_same_v<Pipeline, rhi::ComputePipeline>
                ? rhi::PipelineStage::ComputeShader
                : i ? rhi::PipelineStage::FragmentShader
                    : rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader;
            element.access = binding.type == rhi::DescriptorType::StorageBuffer
                ? rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite
                : rhi::ResourceAccess::ShaderRead;
            rhi::DescriptorBufferBindingRangeContract::Resolved range;
            assert(!rhi::DescriptorBufferBindingRangeContract::resolve(
                element.write.bufferBindingRange,
                authored[i].buffer->layout().sizeBytes,
                element.dynamicOffset,
                range));
            element.bufferAccessOffset = range.offsetBytes;
            element.bufferAccessBytes = range.sizeBytes;
        }
        core::UniquePtr<rhi::DescriptorSet> candidate;
        const auto fresh = [&] {
            candidate.reset();
            if (!retain)
                arena->reset();
            {
                core::CpuMemoryQuotaScope scope(c.quotas[2]);
                rhi::DescriptorSetDesc set;
                set.layout = rhi::DescriptorStateCopy::copy(c.layout->desc().descriptorLayouts[0]);
                candidate = c.device->allocateOriginalDescriptorSet(arena, set);
            }
            assert(candidate
                && !c.device->beginOriginalFiniteDescriptorWrite(
                    *candidate, pipeline, 0u, authored.data(), authored.size(), request, quotas));
            drain(*candidate);
            assert(!candidate->finishOriginalFiniteWrite());
        };
        fresh();
        const auto factory = [&] {
            const auto* original = candidate.get();
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            auto source = Source::prepareOriginal(candidate,
                arena,
                root,
                elements.data(),
                elements.size(),
                budget,
                quota,
                *c.device,
                id);
            std::pmr::set_default_resource(previous);
            assert(source ? !candidate : candidate.get() == original);
            if (!source && !id)
                std::fprintf(stderr, "missing finite Source diagnostic\n");
            return source;
        };
        if (!retain) {
            CpuQuotaFactoryFixture pressure(quota);
            const auto peak = pressure.measureSingleUse(fresh, factory);
            pressure.verifySingleUse(peak, fresh, factory);
            fresh();
            const auto rejectBudget = [&](auto change) {
                auto invalid = budget;
                change(invalid);
                assert(
                    !Source::prepareOriginal(
                        candidate, arena, root, elements.data(), 2u, invalid, quota, *c.device, id)
                    && candidate && id);
            };
            rejectBudget([](auto& b) {
                b.finitePipelineSourceIndex = 0u;
            });
            rejectBudget([](auto& b) {
                b.finitePipelineSourceIndex = SIZE_MAX;
            });
            rejectBudget([](auto& b) {
                b.finiteFootprintContractVersion = 2u;
            });
            rejectBudget([](auto& b) {
                b.finiteFullBoundViewOptIn = false;
            });
            rejectBudget([](auto& b) {
                b.finiteLogicalSet = 1u;
            });
            rejectBudget([](auto& b) {
                b.maxFrozenDynamicOffsets = 0u;
            });
            rejectBudget([](auto& b) {
                --b.maxExclusiveRetainedOwners;
            });
            const auto rejectElement = [&](auto change) {
                auto invalid = elements;
                change(invalid);
                assert(!Source::prepareOriginal(
                           candidate, arena, root, invalid.data(), 2u, budget, quota, *c.device, id)
                    && candidate && id);
            };
            rejectElement([](auto& e) {
                --e[0].bufferAccessBytes;
            });
            rejectElement([](auto& e) {
                ++e[0].bufferAccessOffset;
            });
            rejectElement([](auto& e) {
                ++e[0].dynamicOffset;
            });
            rejectElement([](auto& e) {
                ++e[0].write.bufferBindingRange.sizeBytes;
            });
            rejectElement([](auto& e) {
                e[0].stages = rhi::PipelineStage::None;
            });
            rejectElement([](auto& e) {
                e[1].access = rhi::ResourceAccess::None;
            });
            rejectElement([](auto& e) {
                ++e[0].registrationGeneration;
            });
            auto forgedArena = std::shared_ptr<const rhi::DescriptorArena>(quota, arena.get());
            assert(!Source::prepareOriginal(
                candidate, forgedArena, root, elements.data(), 2u, budget, quota, *c.device, id));
            forgedArena.reset();
            assert(!c.device->beginOriginalFiniteDescriptorWrite(
                *candidate, pipeline, 0u, authored.data(), 2u, request, quotas));
            assert(!factory() && candidate);
            drain(*candidate);
            assert(!candidate->cancelOriginalFiniteWrite());
            std::printf(
                "BINDLESS_RHI_ORIGINAL_FINITE_SOURCE_CUSTODY_BUDGETS_OK %s peak%llu exact-one-less-exhausted unique-preserved full-view-dynamic-quota-pending-reject\n",
                std::is_same_v<Pipeline, rhi::ComputePipeline> ? "compute" : "graphics",
                static_cast<unsigned long long>(peak));
        }
        auto* borrowed = candidate.get();
        auto source = factory();
        if (!source)
            std::fprintf(stderr, "Finite Source3 failed: %s\n", id ? id : "no diagnostic");
        assert(source && !candidate);
        assert(source->sourceContractVersion() == 3u
            && source->frozenPipelineSourceIndex() == prefixCount
            && source->frozenLogicalSet() == 0u && source->frozenFiniteDeclarations().size() == 2u
            && !source->nativeExecutionProven());
        assert(source->matchesCanonicalSources(*root));
        if (!retain) {
            auto other = makeRoot();
            assert(!source->matchesCanonicalSources(*other));
            assert(Sources::appendOriginalTyped(other, Sources::Source{source}, *c.device));
            other.reset();
            auto old = makeRoot(2u);
            assert(core::StringView(
                       Sources::appendOriginalTyped(old, Sources::Source{source}, *c.device))
                == "RHI-ORIGINAL-SOURCES-ASSEMBLY-DESCRIPTOR-VERSION");
            old.reset();
            auto forged = std::shared_ptr<const Source>(quota, source.get());
            assert(Source::validateOriginal(forged));
            forged.reset();
        }
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota, quotas[0], quotas[1], quotas[2]}, [&]() noexcept -> const char* {
                    if (const auto* error = Source::validateOriginal(source, *c.device))
                        return error;
                    return Sources::appendOriginalTyped(root, Sources::Source{source}, *c.device);
                }));
        });
        assert(!Sources::validateRegisteredLifetime(root, *c.device));
        std::uint32_t owners = 0u, registrations = 0u, total = 0u;
        assert(!root->cpuOwnerCount(2u, admission.maxGraphOwners, owners));
        assert(!root->cpuOwnerCount(2u, owners, total) && total == owners);
        assert(root->cpuOwnerCount(2u, owners - 1u, total));
        assert(!Sources::registeredOwnerUnion(
            root, owners, UINT32_MAX, UINT32_MAX, total, registrations));
        std::uint32_t projected = 0u, records = 0u;
        assert(
            !Sources::registeredOwnerUnion(root, owners, total, registrations, projected, records)
            && projected == total && records == registrations);
        assert(Sources::registeredOwnerUnion(
            root, owners, total - 1u, registrations, projected, records));
        assert(Sources::registeredOwnerUnion(
            root, owners, total, registrations - 1u, projected, records));
        assert(root->typedStateJoinDiagnostic()
            && !arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(c.device->backend())
                .executionAvailable);
        auto graph = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto positional = [&]() -> std::shared_ptr<const rhi::OriginalCommandInputs> {
            if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
                return OriginalCommandDescriptorBindingFixture::verify<Pipeline>(root,
                    prefixCount,
                    prefixCount + 1u,
                    graph,
                    *c.device,
                    SIZE_MAX,
                    !nativeClosure);
            else {
                assert(attachments);
                return OriginalCommandDescriptorBindingFixture::verify<Pipeline>(root,
                    prefixCount,
                    prefixCount + 1u,
                    graph,
                    *c.device,
                    bufferCount + 1u,
                    !nativeClosure);
            }
        }();
        auto typedState = OriginalCommandDescriptorStateFixture::verify<Pipeline>(
            positional, *c.device, true, !nativeClosure);
        if (nativeClosure || dependencyLossCandidate) {
            assert(retain && typedState.transaction);
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 15u> pressure{quotas[0],
                quotas[1],
                quotas[2],
                quota,
                graph,
                typedState.stateQuota,
                typedState.recordingQuota,
                typedState.stream.quota};
            for (std::size_t i = 0u; i < c.quotas.size(); ++i)
                pressure[8u + i] = c.quotas[i];
            std::uint64_t resourceBytes =
                originalWrites[0].buffer->originalObject()->allocation().estimatedSizeBytes;
            if (bufferCount == 2u)
                resourceBytes +=
                    originalWrites[1].buffer->originalObject()->allocation().estimatedSizeBytes;
            if (attachments)
                resourceBytes += std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                    (*attachments)[0])
                                     ->originalObject()
                                     ->allocation()
                                     .estimatedSizeBytes;
            if (dependencyLossCandidate) {
                assert(c.lossQuota);
                const auto loss = c.device->observeOriginalDeviceNativeLoss(id);
                assert(loss && !id);
                typedState.nativeClosure =
                    rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalTypedLossBound(
                        typedState.transaction,
                        typedState.stream.program,
                        *c.device,
                        {2u,
                            true,
                            prefixCount,
                            prefixCount + 2u,
                            admission.maxGraphOwners,
                            resourceBytes,
                            bytes,
                            1u,
                            1u},
                        *loss,
                        id);
                assert(typedState.nativeClosure && !id);
            } else {
                typedState.nativeClosure = OriginalCommandNativeClosureTypedFixture::verify(
                    typedState,
                    *c.device,
                    {2u, true, prefixCount, prefixCount + 2u, 128u, resourceBytes, bytes, 1u, 1u},
                    pressure,
                    std::is_same_v<Pipeline, rhi::ComputePipeline> ? "finite-compute"
                                                                   : "finite-graphics");
                if (c.lossQuota) {
                    const auto owners = typedState.nativeClosure->retainedOwnerCount();
                    typedState.nativeClosure.reset();
                    const auto loss = c.device->observeOriginalDeviceNativeLoss(id);
                    assert(loss && !id);
                    OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            std::array{c.lossQuota, quotas[0], quotas[1], quotas[2]},
                            [&]() noexcept -> const char* {
                                for (std::size_t i = 0u; i < quotas.size(); ++i) {
                                    auto overlap = quotas;
                                    overlap[i] = c.lossQuota;
                                    assert(
                                        core::StringView(
                                            c.device->beginOriginalFiniteDescriptorWrite(*borrowed,
                                                pipeline,
                                                0u,
                                                authored.data(),
                                                authored.size(),
                                                request,
                                                overlap))
                                        == "RHI-ORIGINAL-REGISTRATION-QUOTA-DOMAIN");
                                    assert(!Source::validateOriginal(source));
                                }
                                return nullptr;
                            }));
                    });
                    std::array<std::shared_ptr<core::CpuMemoryQuota>, 16u> boundPressure{};
                    std::copy(pressure.begin(), pressure.end(), boundPressure.begin());
                    boundPressure.back() = c.lossQuota;
                    typedState.nativeClosure =
                        OriginalCommandNativeClosureTypedFixture::verify(typedState,
                            *c.device,
                            {2u,
                                true,
                                prefixCount,
                                prefixCount + 2u,
                                owners + 1u,
                                resourceBytes,
                                bytes,
                                1u,
                                1u},
                            boundPressure,
                            std::is_same_v<Pipeline, rhi::ComputePipeline> ? "finite-compute"
                                                                           : "finite-graphics",
                            &*loss);
                    assert(typedState.nativeClosure->retainedOwnerCount() == owners + 1u);
                    const auto limit = c.lossQuota->limitBytes();
                    c.lossQuota->setLimitBytes(limit - 1u);
                    assert(typedState.nativeClosure->validate());
                    c.lossQuota->setLimitBytes(limit);
                    assert(!typedState.nativeClosure->validate());
                }
            }
        }
        if (!retain) {
            auto graph = core::makeShared<core::CpuMemoryQuota>(bytes),
                 state = core::makeShared<core::CpuMemoryQuota>(bytes);
            using Usage = rhi::OriginalCommandUsage;
            using K = rhi::CommandUsageKind;
            using Scalar = rhi::CommandScalarArguments;
            {
                const rhi::CommandUsage finish{K::Finish, 0u, 0u};
                auto usage = Usage::prepareOriginal(root, &finish, 1u, nullptr, 0u, graph, id);
                assert(usage && root->inventorySealed());
                const rhi::CommandVariableArguments::Input field{
                    *Scalar::capture(K::Finish, Scalar::None{}), {}};
                auto arguments = rhi::CommandVariableArguments::prepareOriginal(
                    &field, 1u, {1u, true, 1u, 0u, 0u, 0u, bytes}, graph, id);
                assert(arguments);
                auto scalars =
                    rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, graph, id);
                assert(scalars);
                auto inputs =
                    rhi::OriginalCommandInputs::prepareOriginal(scalars, nullptr, 0u, graph, id);
                assert(inputs);
                const rhi::OriginalCommandStateAdmissionBudget stateBudget{
                    1u, true, 1u, 8u, 2u, 0u, 64u, bytes};
                assert(!rhi::OriginalCommandResourceStates::prepareOriginal(
                    inputs, *c.device, stateBudget, state, id));
                assert(core::StringView(id) == "RHI-ORIGINAL-TYPED-PIPELINE-STATE-JOIN-MISSING");
                assert(!rhi::OriginalCommandBufferStates::prepareOriginal(
                    inputs, *c.device, stateBudget, state, id));
                assert(core::StringView(id) == "RHI-ORIGINAL-TYPED-PIPELINE-STATE-JOIN-MISSING");
                if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>) {
                    const std::array<rhi::CommandUsage, 5u> commands{{{K::BeginComputePass, 0u, 0u},
                        {K::BindComputePipeline, 0u, 1u},
                        {K::BindComputeDescriptor, 1u, 1u},
                        {K::EndComputePass, 2u, 0u},
                        {K::Finish, 2u, 0u}}};
                    const std::array<Usage::Reference, 2u> references{
                        {{prefixCount}, {prefixCount + 1u}}};
                    auto binds = Usage::prepareOriginal(
                        root, commands.data(), 5u, references.data(), 2u, graph, id);
                    assert(binds);
                    const auto displacement = source->frozenFiniteDeclarations()[0].dynamicOffset;
                    const std::array<rhi::CommandVariableArguments::Input, 5u> fields{
                        {{*Scalar::capture(K::BeginComputePass, Scalar::None{}), {}},
                            {*Scalar::capture(K::BindComputePipeline, Scalar::None{}), {}},
                            {*Scalar::capture(
                                 K::BindComputeDescriptor, Scalar::DescriptorBinding{0u}),
                                {},
                                &displacement,
                                1u},
                            {*Scalar::capture(K::EndComputePass, Scalar::None{}), {}},
                            {*Scalar::capture(K::Finish, Scalar::None{}), {}}}};
                    auto bindArguments = rhi::CommandVariableArguments::prepareOriginal(
                        fields.data(), 5u, {1u, true, 5u, 1u, 0u, 0u, bytes}, graph, id);
                    assert(bindArguments);
                    auto bindScalars = rhi::OriginalCommandScalars::prepareOriginal(
                        binds, bindArguments, graph, id);
                    assert(bindScalars);
                    const rhi::OriginalCommandInputs::OriginalDescriptor pipelineBind{1u,
                        rhi::OriginalCommandPipelineBinding{K::BindComputePipeline,
                            prefixCount,
                            0u,
                            pipeline->originalResourceRegistration().generation()}};
                    assert(!rhi::OriginalCommandInputs::prepareOriginal(
                        bindScalars, &pipelineBind, 1u, {2u, true, 1u}, graph, id));
                    assert(
                        core::StringView(id) == "RHI-ORIGINAL-TYPED-INPUT-POSITIONAL-JOIN-MISSING");
                }
            }
            assert(!graph->liveBytes() && !state->liveBytes());
        }
        prefix = {};
        pipelineSource.reset();
        authored = {};
        elements = {};
        assert(!Source::validateOriginal(
            source)); // caller declarations gone; original owners retained
        if (retain) {
            if (dependencyLossCandidate)
                OriginalTypedDependencyDecisionFixture::retainForActualLoss(typedState,
                    OriginalTypedUsageProjectionFixture::budgetFor(typedState),
                    "finite-physical-loss");
            retainedTypedState = std::move(typedState);
            retainedFiniteSource = source;
            retainedFiniteSources = root;
            retainedFiniteInputs = positional;
            positionalGraph = graph;
        } else {
            const bool typedValid = bool(typedState.graph);
            if (typedValid)
                OriginalCommandDescriptorStateFixture::verifyRetained(typedState, true);
            auto payload = rhi_webgpu::webGpuNativePayloadFor(*borrowed);
            assert(payload);
            const auto limit = quotas[0]->limitBytes();
            quotas[0]->setLimitBytes(limit - 1u);
            assert(Source::validateOriginal(source));
            if (typedValid) {
                OriginalCommandDescriptorStateFixture::verifyRetained(typedState, false);
                OriginalTypedUsageProjectionFixture::verifyUnavailable(
                    typedState, *c.device, "quota");
            }
            quotas[0]->setLimitBytes(limit);
            assert(!positional->validate());
            assert(!Source::validateOriginal(source));
            const auto& frozen = source->frozenFiniteDeclarations();
            assert(!c.device->beginOriginalFiniteDescriptorWrite(
                *borrowed, pipeline, 0u, frozen.data(), frozen.size(), request, quotas));
            assert(Source::validateOriginal(source) && positional->validate());
            if (typedValid) {
                OriginalCommandDescriptorStateFixture::verifyRetained(typedState, false);
                OriginalTypedUsageProjectionFixture::verifyUnavailable(
                    typedState, *c.device, "pending");
            }
            drain(*borrowed);
            assert(!borrowed->finishOriginalFiniteWrite());
            assert(core::StringView(Source::validateOriginal(source))
                == "RHI-DESCRIPTOR-CONTENT-CHANGED");
            if (typedValid) {
                OriginalCommandDescriptorStateFixture::verifyRetained(typedState, false);
                OriginalTypedUsageProjectionFixture::verifyUnavailable(
                    typedState, *c.device, "revision");
            }
            arena->reset();
            assert(Source::validateOriginal(source));
            if (typedValid) {
                OriginalCommandDescriptorStateFixture::verifyRetained(typedState, false);
                OriginalTypedUsageProjectionFixture::verifyUnavailable(
                    typedState, *c.device, "arena-epoch");
            }
            auto stateQuota = typedState.stateQuota, recordingQuota = typedState.recordingQuota,
                 streamQuota = typedState.stream.quota;
            typedState = {};
            if (typedValid)
                assert(!stateQuota->liveBytes() && !recordingQuota->liveBytes()
                    && !streamQuota->liveBytes());
            positional.reset();
            assert(!graph->liveBytes());
            std::weak_ptr<const Sources> observer = root;
            root.reset();
            assert(observer.expired());
            assert(Source::validateOriginal(source));
            source.reset();
            payload.reset();
            observer.reset();
            assert(quota->liveBytes() == baseline);
            if (typedValid)
                std::puts(
                    "BINDLESS_RHI_TYPED_STREAM_REVISION_EPOCH_OK caller-drop quota-mutation pending-write revision arena-epoch rejected quotas-reclaimed");
            if (typedValid)
                std::puts(
                    "BINDLESS_RHI_DESCRIPTOR_STATE_REVISION_EPOCH_OK caller-drop quota-mutation pending-write revision arena-epoch rejected prepared-journal-empty quotas-reclaimed");
        }
        if (!nativeClosure)
            std::printf(
                "BINDLESS_RHI_ORIGINAL_FINITE_SOURCE_CUSTODY_OK %s unique-adopted canonical-pipeline-index full-view shared-quota projection-exact caller-drop hot0 query0 state-native-closed\n",
                std::is_same_v<Pipeline, rhi::ComputePipeline> ? "compute" : "graphics");
    }

    template <class Context, class Pipeline>
    void ordinaryPositional(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u>& originalWrites,
        const std::array<rhi::OriginalCommandResourceSources::Source, 2u>* attachments = nullptr)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Source = rhi::OriginalCommandDescriptorSetSource;
        using PipelineSource = rhi::OriginalCommandPipelineSource<Pipeline>;
        const auto& quota = originalWrites[0u].buffer->originalMetadataQuota();
        const auto baseline = quota->liveBytes();
        auto graph = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* id = nullptr;
        {
            auto pipelineSource = PipelineSource::prepareOriginal(pipeline,
                {1u,
                    true,
                    PipelineSource::kind,
                    pipeline->originalResourceRegistration().generation(),
                    bytes,
                    PipelineSource::exclusiveOwnerCount},
                quota,
                *c.device,
                id);
            assert(pipelineSource);
            const auto bufferCount = core::SharedOwnerProjection::sameOwner(
                                         originalWrites[0].buffer, originalWrites[1].buffer)
                ? 1u
                : 2u;
            std::array<Sources::Source, 4u> prefix{
                originalWrites[0].buffer, originalWrites[1].buffer};
            if (attachments) {
                prefix[bufferCount] = (*attachments)[0u];
                prefix[bufferCount + 1u] = (*attachments)[1u];
            }
            const auto pipelineIndex = bufferCount + (attachments ? 2u : 0u);
            rhi::OriginalCommandAdmissionBudget admission;
            admission.contractVersion = 1u;
            admission.prototypeOptIn = true;
            admission
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
                bufferCount;
            admission
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
                1u;
            admission.maxSourceOwners[static_cast<std::size_t>(
                rhi::CommandReferenceDomain::Descriptor)] = 1u;
            admission.maxCommands = 14u + 4u * bufferCount + (attachments ? 2u : 0u);
            admission.maxReferences = (attachments ? 10u : 6u) + 4u * bufferCount;
            admission.maxInputDescriptors = (attachments ? 10u : 6u) + 4u * bufferCount;
            admission.maxStateObservations = bufferCount + (attachments ? 4u : 0u);
            admission.maxTextureRanges = attachments ? 4u : 0u;
            admission.maxStateTransactions = 1u;
            admission.maxDynamicOffsets = 6u;
            admission.maxGraphOwners = 64u;
            admission.maxCpuBytes.fill(bytes);
            if (attachments) {
                admission.maxSourceOwners[static_cast<std::size_t>(
                    rhi::CommandReferenceDomain::Texture)] = 1u;
                admission.maxSourceOwners[static_cast<std::size_t>(
                    rhi::CommandReferenceDomain::TextureView)] = 1u;
                admission.maxPassColorAttachments = 2u;
                admission.maxPassAttachmentReferences = 2u;
            }
            auto root = Sources::prepareColdOriginalTyped(
                prefix.data(), pipelineIndex, admission, {2u, true, 2u}, quota, id);
            assert(root);
            assert(!Sources::appendOriginalTyped(root, {pipelineSource}, *c.device));
            descriptor.reset();
            arena->reset();
            core::UniquePtr<rhi::DescriptorSet> candidate;
            {
                core::CpuMemoryQuotaScope scope(c.quotas[2u]);
                rhi::DescriptorSetDesc fields;
                fields.layout = rhi::DescriptorStateCopy::copy(
                    pipeline->originalLayoutOwner()->desc().descriptorLayouts[0u]);
                candidate = c.device->allocateOriginalDescriptorSet(arena, fields);
            }
            assert(candidate);
            std::array<Source::Element, 2u> elements;
            for (std::size_t i = 0u; i < elements.size(); ++i) {
                auto& e = elements[i];
                e.write = originalWrites[i].write;
                e.write.bufferBindingRange = {};
                e.sourceIndex = bufferCount == 1u ? 0u : i;
                e.registrationGeneration = originalWrites[i]
                                               .buffer->originalObject()
                                               ->originalResourceRegistration()
                                               .generation();
                const auto rw = e.write.type == rhi::DescriptorType::StorageBuffer;
                e.write.resourceState =
                    rw ? rhi::ResourceState::ShaderWrite : rhi::ResourceState::ShaderRead;
                e.stages = std::is_same_v<Pipeline, rhi::ComputePipeline>
                    ? rhi::PipelineStage::ComputeShader
                    : i ? rhi::PipelineStage::FragmentShader
                        : rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader;
                e.access = rw ? rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite
                              : rhi::ResourceAccess::ShaderRead;
                e.bufferAccessBytes = originalWrites[i].buffer->layout().sizeBytes;
                assert(candidate->write(e.write).success);
            }
            auto source = Source::prepareOriginal(candidate,
                arena,
                root,
                elements.data(),
                elements.size(),
                {2u, true, 2u, 2u, 5u, bytes},
                quota,
                *c.device,
                id);
            if (!source)
                std::fprintf(stderr, "ordinary source2 failed %s\n", id ? id : "no diagnostic");
            assert(source && !candidate);
            assert(!Sources::appendOriginalTyped(root, {source}, *c.device));
            auto inputs = OriginalCommandDescriptorBindingFixture::verify<Pipeline>(root,
                pipelineIndex,
                pipelineIndex + 1u,
                graph,
                *c.device,
                attachments ? bufferCount + 1u : SIZE_MAX);
            (void)OriginalCommandDescriptorStateFixture::verify<Pipeline>(inputs, *c.device);
            source.reset();
            root.reset();
            pipelineSource.reset();
            prefix = {};
            elements = {};
            assert(!inputs->validate());
            assert(!inputs->nativeExecutionProven());
            inputs.reset();
        }
        assert(!graph->liveBytes() && quota->liveBytes() == baseline);
        arena->reset();
    }

    template <class Context, class Pipeline>
    void nativeFailure(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u>& writes)
    {
        const auto revision = descriptor->contentRevision();
        const auto old = rhi_webgpu::webGpuNativePayloadFor(*descriptor);
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpFiniteGroup = d.createBindGroup;
                d.createBindGroup = function(desc) {
                    const entries = desc.entries.map(e => ({...e, resource : {...e.resource}}));
                    entries[0].resource.offset = 1;
                    return Module.arpFiniteGroup.call(this, {...desc, entries});
                };
            // clang-format on
        });
        assert(!begin(c, pipeline, writes));
        drain(*descriptor);
        assert(diagnoseSource(c, pipeline, writes));
        const auto* diagnostic = descriptor->finishOriginalFiniteWrite();
        assert(
            diagnostic && core::StringView(diagnostic) == "RHI-ORIGINAL-FINITE-WRITE-NATIVE-SCOPE");
        assert(descriptor->contentRevision() == revision
            && rhi_webgpu::webGpuNativePayloadFor(*descriptor) == old);
        assert(!descriptor->cancelOriginalFiniteWrite());
        assert(bool(diagnoseSource(c, pipeline, writes)) != descriptor->hasOriginalFiniteContent());
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createBindGroup = Module.arpFiniteGroup;
                delete Module.arpFiniteGroup;
            // clang-format on
        });
        std::puts(
            "BINDLESS_RHI_ORIGINAL_FINITE_WRITE_NATIVE_FAILURE_OK actual-invalid-native-offset old-content-revision-unchanged drained-retry");
    }

    template <class Context, class Pipeline>
    void negatives(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<rhi::OriginalFiniteDescriptorBufferWrite, 2u>& writes)
    {
        const auto revision = descriptor->contentRevision();
        auto old = rhi_webgpu::webGpuNativePayloadFor(*descriptor);
        assert(old);
        const auto checkClosed = [&] {
            assert(diagnoseSource(c, pipeline, writes));
            std::uint32_t group = 99u, count = 99u;
            const std::uint32_t* offsets =
                reinterpret_cast<const std::uint32_t*>(std::uintptr_t{1u});
            assert(old->diagnoseOriginalFiniteGroup(group, offsets, count) && !group && !count
                && !offsets);
        };
        for (const auto& quota : quotas) {
            const auto limit = quota->limitBytes();
            quota->setLimitBytes(limit - 1u);
            checkClosed();
            assert(begin(c, pipeline, writes));
            quota->setLimitBytes(limit);
        }
        auto hostile = writes;
        hostile[1].buffer = {};
        assert(begin(c, pipeline, hostile));
        hostile = writes;
        hostile[0].buffer = std::shared_ptr<const rhi::OriginalCommandBufferLease>(
            quotas[0], writes[0].buffer.get());
        assert(begin(c, pipeline, hostile));
        hostile = writes;
        hostile[0].write.bufferBindingRange.sizeBytes = writes[0].buffer->layout().sizeBytes;
        assert(begin(c, pipeline, hostile));
        assert(c.device->beginOriginalFiniteDescriptorWrite(
            *descriptor, pipeline, 1u, writes.data(), writes.size(), request, quotas));
        const auto saved = quotas[0];
        quotas[0] = writes[0].buffer->originalMetadataQuota();
        assert(begin(c, pipeline, writes));
        quotas[0] = saved;
        // Immutable original revalidation after native scopes become Ready.
        assert(!begin(c, pipeline, writes));
        drain(*descriptor);
        const auto limit = quotas[0]->limitBytes();
        quotas[0]->setLimitBytes(limit - 1u);
        assert(descriptor->finishOriginalFiniteWrite());
        quotas[0]->setLimitBytes(limit);
        assert(descriptor->contentRevision() == revision
            && rhi_webgpu::webGpuNativePayloadFor(*descriptor) == old);
        assert(!descriptor->cancelOriginalFiniteWrite());
        // A dropped unique wrapper is never dereferenced by its scope callback.
        std::array<std::uint64_t, 3u> baseline{};
        for (std::size_t i = 0u; i < quotas.size(); ++i)
            baseline[i] = quotas[i]->liveBytes();
        core::UniquePtr<rhi::DescriptorSet> dropped;
        {
            core::CpuMemoryQuotaScope scope(c.quotas[2]);
            rhi::DescriptorSetDesc set;
            set.layout = rhi::DescriptorStateCopy::copy(descriptor->layout());
            dropped = c.device->allocateOriginalDescriptorSet(arena, set);
        }
        assert(dropped
            && !c.device->beginOriginalFiniteDescriptorWrite(
                *dropped, pipeline, 0u, writes.data(), writes.size(), request, quotas));
        dropped.reset();
        for (std::uint32_t spin = 0u; spin < 2000u && quotas[2]->liveBytes() != baseline[2]; ++spin)
            emscripten_sleep(1);
        for (std::size_t i = 0u; i < quotas.size(); ++i)
            assert(quotas[i]->liveBytes() == baseline[i]);
        assert(descriptor->contentRevision() == revision);
        // Old committed native payload remains retained, but reset closes all
        // observations and cannot publish a candidate revision.
        assert(!begin(c, pipeline, writes));
        arena->reset();
        drain(*descriptor);
        assert(
            descriptor->finishOriginalFiniteWrite() && descriptor->contentRevision() == revision);
        checkClosed();
        assert(!descriptor->cancelOriginalFiniteWrite());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_FINITE_WRITE_NEGATIVES_OK forged-missing-binding-bounds-quota ready-revalidation caller-drop-reclaimed arena-reset old-revision source12-closed");
    }

    void release()
    {
        auto stateQuota = retainedTypedState.stateQuota,
             recordingQuota = retainedTypedState.recordingQuota,
             streamQuota = retainedTypedState.stream.quota;
        retainedTypedState = {};
        if (stateQuota)
            assert(!stateQuota->liveBytes());
        if (recordingQuota)
            assert(!recordingQuota->liveBytes());
        if (streamQuota)
            assert(!streamQuota->liveBytes());
        retainedFiniteInputs.reset();
        if (positionalGraph) {
            assert(!positionalGraph->liveBytes());
            positionalGraph.reset();
        }
        retainedFiniteSource.reset();
        retainedFiniteSources.reset();
        descriptor.reset();
        arena.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
    }
#endif
};
} // namespace arp::test
