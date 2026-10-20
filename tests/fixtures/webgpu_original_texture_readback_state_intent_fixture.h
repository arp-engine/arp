#pragma once
#include "webgpu_original_texture_readback_inputs_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackStateIntentFixture final {
public:
    using Base = WebGpuOriginalTextureReadbackInputsFixture;
    using Pair = WebGpuOriginalTextureReadbackPairFixture::Pair;
    using States = Base::States;
    using Quotas = std::array<std::shared_ptr<core::CpuMemoryQuota>, 6>;
    static constexpr auto cpuBytes = Base::cpuBytes;

    static void verifyContract()
    {
        using Source = rhi_webgpu::WebGpuCommandReferenceSource;
        static_assert(static_cast<std::uint32_t>(Source::OriginalTextureReadback) == 6u);
        static_assert(static_cast<std::uint32_t>(Source::OriginalTextureReadbackState) == 7u);
        rhi_webgpu::WebGpuGenericCommandProjectionBudget b;
        assert(!b.maxReadbackStateReferences);
        rhi::ResourceBarrierDesc barrier;
        barrier.resource = {1u};
        barrier.before = rhi::ResourceState::CopyDst;
        barrier.after = rhi::ResourceState::CopySrc;
        barrier.sourceStages = rhi::PipelineStage::Transfer;
        barrier.destinationStages = rhi::PipelineStage::Transfer;
        barrier.sourceAccess = rhi::ResourceAccess::TransferWrite;
        barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
        assert(!rhi::semantics::RecordedResourceStateSequence::validateBarrier(
            barrier, barrier.before));
        assert(std::string_view(rhi::semantics::RecordedResourceStateSequence::validateBarrier(
                   barrier, rhi::ResourceState::CopySrc))
            == "RHI-STATE-SEQUENCE-BEFORE-MISMATCH");
        std::puts(
            "BINDLESS_RHI_READBACK_STATE_INTENT_CONTRACT_OK source-ref7 default-closed explicit-barrier no-current-proof");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static rhi::ResourceBarrierDesc barrier(const std::shared_ptr<const Pair>& pair)
    {
        rhi::ResourceBarrierDesc result;
        result.resource = pair->destination()->source()->resource().id;
        result.textureSubresources = rhi::defaultTextureSubresourceRange(
            pair->destination()->source()->resource().textureDesc);
        result.before = pair->destination()->source()->stateObservation().value;
        result.after = rhi::ResourceState::CopySrc;
        result.sourceStages = rhi::PipelineStage::Transfer;
        result.destinationStages = rhi::PipelineStage::Transfer;
        assert(result.before == rhi::ResourceState::CopyDst
            || result.before == rhi::ResourceState::CopySrc);
        result.sourceAccess = result.before == rhi::ResourceState::CopyDst
            ? rhi::ResourceAccess::TransferWrite
            : rhi::ResourceAccess::TransferRead;
        result.destinationAccess = rhi::ResourceAccess::TransferRead;
        return result;
    }

    static auto prepare(const std::shared_ptr<const Pair>& pair,
        const rhi::ResourceBarrierDesc& barrier,
        const Quotas& quotas,
        const char*& diagnostic,
        std::uint64_t metadataCpuBytes = cpuBytes)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using A = CommandScalarArguments;
        using Store = CommandVariableArguments;
        auto b = WebGpuOriginalTextureReadbackProjectionFixture::budget();
        b.maxReferences = 3u;
        b.maxReadbackStateReferences = 1u;
        b.maxCpuMetadataBytes = metadataCpuBytes;
        b.maxDomainReferences[static_cast<std::size_t>(WebGpuCommandReferenceDomain::Texture)] = 2u;
        auto graph = prepareGraph(pair,
            {WebGpuCommandReferenceDomain::Texture,
                WebGpuCommandReferenceSource::OriginalTextureReadbackState,
                barrier.resource},
            b,
            quotas[0],
            diagnostic);
        if (!graph)
            return std::shared_ptr<const States>{};
        const std::array<CommandUsage, 5> commands{{{K::TextureBarrier, 0u, 1u},
            {K::BeginTransferPass, 1u, 0u},
            {K::TextureReadback, 1u, 2u},
            {K::EndTransferPass, 3u, 0u},
            {K::Finish, 3u, 0u}}};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            commands.data(),
            commands.size(),
            {1u, true, 5u, 3u, metadataCpuBytes},
            quotas[1],
            diagnostic);
        if (!usage)
            return std::shared_ptr<const States>{};
        const auto& snapshot = pair->layout()->inputOwner();
        const std::array<Store::Input, 5> raw{{{*A::capture(K::TextureBarrier, A::None{})},
            {*A::capture(K::BeginTransferPass, A::None{}), "原始读回"},
            {*A::capture(K::TextureReadback, A::None{}), snapshot->label()},
            {*A::capture(K::EndTransferPass, A::None{})},
            {*A::capture(K::Finish, A::None{})}}};
        auto arguments = Store::prepareOriginal(raw.data(),
            raw.size(),
            {1u, true, 5u, 0u, raw[1].text.size() + raw[2].text.size(), 0u, metadataCpuBytes},
            quotas[2],
            diagnostic);
        if (!arguments)
            return std::shared_ptr<const States>{};
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(
            usage, arguments, {1u, true, 5u, 0u, metadataCpuBytes}, quotas[3], diagnostic);
        if (!scalars)
            return std::shared_ptr<const States>{};
        const std::array<WebGpuOriginalCommandInputs::OriginalDescriptor, 2> descriptors{
            {{0u, *ResourceDependencyArguments::capture(K::TextureBarrier, barrier)},
                {2u, *TextureReadbackOriginalInputArguments::captureOriginal(snapshot)}}};
        WebGpuOriginalCommandInputsBudget ib{1u, true, 2u, metadataCpuBytes};
        ib.maxReadbackDescriptors = 1u;
        ib.maxReadbackMipMappings = 2u;
        ib.maxReadbackUnpublishedResultBytes = 1536u;
        auto inputs = WebGpuOriginalCommandInputs::prepareOriginal(
            scalars, {}, descriptors.data(), descriptors.size(), ib, quotas[4], diagnostic);
        if (!inputs)
            return std::shared_ptr<const States>{};
        return States::prepareOriginal(
            inputs, {1u, true, 3u, 3u, metadataCpuBytes}, quotas[5], diagnostic);
    }

    static std::shared_ptr<const rhi_webgpu::WebGpuGenericCommandProjection> prepareGraph(
        const std::shared_ptr<const Pair>& pair,
        const rhi_webgpu::WebGpuCommandReference& stateReference,
        const rhi_webgpu::WebGpuGenericCommandProjectionBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        auto refs = WebGpuOriginalTextureReadbackProjectionFixture::references(pair);
        const std::array<rhi_webgpu::WebGpuCommandReference, 3> references{
            {stateReference, refs[0], refs[1]}};
        rhi_webgpu::WebGpuGenericCommandProjection::Sources sources;
        sources.readbacks = &pair;
        sources.readbackCount = 1u;
        return rhi_webgpu::WebGpuGenericCommandProjection::prepareOriginalShared(std::move(sources),
            references.data(),
            references.size(),
            b,
            std::move(quota),
            diagnostic);
    }

    static auto verifyActual(const std::shared_ptr<const Pair>& pair, const Quotas& quotas)
    {
        const char* diagnostic = nullptr;
        auto desc = barrier(pair);
        const auto factory = [&] {
            return prepare(pair, desc, quotas, diagnostic);
        };
        assert(desc.before == rhi::ResourceState::CopyDst);
        using namespace rhi_webgpu;
        auto budget = WebGpuOriginalTextureReadbackProjectionFixture::budget();
        budget.maxReferences = 3u;
        budget
            .maxDomainReferences[static_cast<std::size_t>(WebGpuCommandReferenceDomain::Texture)] =
            2u;
        WebGpuCommandReference ref{WebGpuCommandReferenceDomain::Texture,
            WebGpuCommandReferenceSource::OriginalTextureReadbackState,
            desc.resource};
        assert(!prepareGraph(pair, ref, budget, quotas[0], diagnostic)
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-STATE-CAPACITY");
        budget.maxReadbackStateReferences = 1u;
        ref.sourceIndex = 1u;
        assert(!prepareGraph(pair, ref, budget, quotas[0], diagnostic)
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-STATE-REFERENCE");
        ref.sourceIndex = 0u;
        ref.domain = WebGpuCommandReferenceDomain::Buffer;
        budget.maxDomainReferences[static_cast<std::size_t>(ref.domain)] = 1u;
        assert(!prepareGraph(pair, ref, budget, quotas[0], diagnostic)
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-STATE-REFERENCE");
        ref.domain = WebGpuCommandReferenceDomain::Texture;
        --budget.maxReferences;
        assert(!prepareGraph(pair, ref, budget, quotas[0], diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY");
        ++budget.maxReferences;
        --budget
              .maxDomainReferences[static_cast<std::size_t>(WebGpuCommandReferenceDomain::Texture)];
        assert(!prepareGraph(pair, ref, budget, quotas[0], diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-CAPACITY");
        std::array<std::uint64_t, 6> peaks;
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            CpuQuotaFactoryFixture pressure(quotas[i]);
            peaks[i] = pressure.measure(factory);
            pressure.verify(peaks[i], factory);
            {
                core::CpuMemoryQuotaLease full(quotas[i], cpuBytes);
                assert(full.charged() && !factory());
            }
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }
        auto states = factory();
        assert(states && !states->validate());
        assert(!rhi_webgpu::WebGpuOriginalTransferStateValidation::validate(states, 0u).diagnostic);
        rhi::ResourceState before = rhi::ResourceState::Undefined;
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!rhi_webgpu::WebGpuOriginalTransferStateValidation::stateBefore(
                   *states, desc.resource, 0u, before)
            && before == desc.before);
        assert(!rhi_webgpu::WebGpuOriginalTransferStateValidation::stateBefore(
                   *states, desc.resource, 2u, before)
            && before == rhi::ResourceState::CopySrc);
        assert(!rhi_webgpu::WebGpuOriginalTransferStateValidation::validate(states, 0u).diagnostic
            && pair->destination()->source()->resource().texture->recordedState().state()
                == desc.before);
        assert(states->executionDiagnostic() && !pair->destination()->resultPublished());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        const auto rejectAccess = [&](const char* expected) {
            auto invalid = factory();
            assert(invalid
                && std::string_view(
                       rhi_webgpu::WebGpuOriginalTransferStateValidation::validate(invalid, 0u)
                           .diagnostic)
                    == expected);
        };
        auto original = desc;
        desc.before = rhi::ResourceState::CopySrc;
        rejectAccess("RHI-STATE-SEQUENCE-BEFORE-MISMATCH");
        desc = original;
        desc.after = rhi::ResourceState::CopyDst;
        desc.destinationAccess = rhi::ResourceAccess::TransferWrite;
        rejectAccess("RHI-WEBGPU-ORIGINAL-READBACK-STATE-MISMATCH");
        desc = original;
        desc.sourceStages = rhi::PipelineStage::None;
        auto invalid = factory();
        assert(invalid
            && rhi_webgpu::WebGpuOriginalTransferStateValidation::validate(invalid, 0u).diagnostic);
        invalid.reset();
        desc = original;
        desc.textureSubresources.mipLevelCount = 1u;
        rejectAccess("RHI-WEBGPU-BARRIER-SUBRESOURCE-STATE-UNPROVEN");
        desc = original;
        desc.resource = pair->destination()->resourceId();
        assert(!factory()
            && std::string_view(diagnostic)
                == "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-STATE-REFERENCE");
        desc = original;
        std::printf(
            "BINDLESS_RHI_READBACK_STATE_INTENT_NATIVE_OK %llu %llu %llu %llu %llu %llu stream5 refs3 controls3 explicit-CopyDst-CopySrc immutable-initial allocation0 no-proof\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]),
            static_cast<unsigned long long>(peaks[3]),
            static_cast<unsigned long long>(peaks[4]),
            static_cast<unsigned long long>(peaks[5]));
        return states;
    }
#endif
};
} // namespace arp::test
