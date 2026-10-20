#pragma once
#include "rhi/backends/webgpu/webgpu_command_inputs.h"
#include "rhi/backends/webgpu/webgpu_original_command_inputs.h"
#include "webgpu_original_texture_readback_pair_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackProjectionFixture final {
public:
    using Graph = rhi_webgpu::WebGpuGenericCommandProjection;
    using Pair = WebGpuOriginalTextureReadbackPairFixture::Pair;
    using Budget = rhi_webgpu::WebGpuGenericCommandProjectionBudget;
    using Ref = rhi_webgpu::WebGpuCommandReference;
    using Domain = rhi_webgpu::WebGpuCommandReferenceDomain;
    using Source = rhi_webgpu::WebGpuCommandReferenceSource;
    static constexpr std::uint64_t cpuBytes = 65536u;

    static Budget budget()
    {
        Budget b;
        b.contractVersion = 1u;
        b.prototypeOptIn = true;
        b.maxReferences = 2u;
        b.maxAllocationReferences = 3u;
        b.maxDomainReferences[static_cast<std::size_t>(Domain::Texture)] = 1u;
        b.maxDomainReferences[static_cast<std::size_t>(Domain::Readback)] = 1u;
        b.maxCpuMetadataBytes = cpuBytes;
        b.maxRetainedAllocationBytes = 4432u;
        b.maxOriginalReadbackPairs = 1u;
        b.maxReadbackMipCopies = 2u;
        b.maxReadbackRetainedContentBytes = 1696u;
        b.maxReadbackUnpublishedResultBytes = 1536u;
        return b;
    }

    static std::array<Ref, 2> references(const std::shared_ptr<const Pair>& pair)
    {
        return {{{Domain::Texture,
                     Source::OriginalTextureReadback,
                     pair->destination()->source()->resource().id},
            {Domain::Readback,
                Source::OriginalTextureReadback,
                pair->destination()->resourceId()}}};
    }

    static auto prepare(const std::shared_ptr<const Pair>& pair,
        const Budget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        auto refs = references(pair);
        Graph::Sources sources;
        sources.readbacks = &pair;
        sources.readbackCount = 1u;
        return Graph::prepareOriginalShared(
            std::move(sources), refs.data(), refs.size(), b, std::move(quota), diagnostic);
    }

    static void verifyContract()
    {
        static_assert(static_cast<std::uint32_t>(Source::OriginalTextureUpload) == 5u);
        static_assert(static_cast<std::uint32_t>(Source::OriginalTextureReadback) == 6u);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto result = Graph::originalReadbackLowering(backend);
            assert(result.diagnostic && result.validatorOnly == (backend == rhi::Backend::Null)
                && result.nativeFactBindingImplemented == (backend == rhi::Backend::WebGPU));
        }
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto b = budget();
        const char* diagnostic = nullptr;
        Graph::Sources sources;
        sources.readbackCount = 1u;
        assert(!Graph::prepareOriginalShared(std::move(sources), nullptr, 0u, b, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY");
        std::shared_ptr<const Pair> empty;
        sources.readbacks = &empty;
        assert(!Graph::prepareOriginalShared(std::move(sources), nullptr, 0u, b, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-OWNER");
        Ref reference{Domain::Readback, Source::OriginalTextureReadback, {1u}};
        assert(!Graph::prepareOriginalShared({}, &reference, 1u, b, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-UNPROVEN"
            && !quota->liveBytes());
        assert(
            rhi_webgpu::WebGpuOriginalCommandInputs::family(rhi::CommandUsageKind::TextureReadback)
            == rhi_webgpu::WebGpuOriginalCommandInputs::Family::TextureReadback);
        std::puts(
            "BINDLESS_RHI_READBACK_PROJECTION_CONTRACT_OK backends4 source-enum-appended original-pair-only recording-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static auto verifyActual(const std::shared_ptr<const Pair>& pair)
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto b = budget();
        const char* diagnostic = nullptr;
        auto original = pair;
        auto refs = references(pair);
        std::size_t count = refs.size();
        const auto factory = [&] {
            Graph::Sources s;
            s.readbacks = &original;
            s.readbackCount = 1u;
            return Graph::prepareOriginalShared(
                std::move(s), refs.data(), count, b, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(quota, cpuBytes);
            assert(full.charged() && !factory());
        }
        assert(!quota->liveBytes());
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes()
                && !pair->validate());
        };
        --b.maxCpuMetadataBytes;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-QUOTA");
        b = budget();
        --b.maxOriginalReadbackPairs;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY");
        b = budget();
        --b.maxReadbackMipCopies;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-READBACK-CAPACITY");
        b = budget();
        --b.maxReadbackRetainedContentBytes;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-READBACK-CAPACITY");
        b = budget();
        --b.maxReadbackUnpublishedResultBytes;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-READBACK-CAPACITY");
        b = budget();
        --b.maxAllocationReferences;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-COUNT");
        b = budget();
        --b.maxRetainedAllocationBytes;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-BYTES");
        b = budget();
        --b.maxReferences;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY");
        b = budget();
        b.maxDomainReferences[static_cast<std::size_t>(Domain::Readback)] = 0u;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-CAPACITY");
        b = budget();
        original = std::shared_ptr<const Pair>(quota, pair.get());
        reject("RHI-WEBGPU-COMMAND-PROJECTION-READBACK-OWNER");
        original = pair;
        for (std::size_t i = 0u; i < refs.size(); ++i) {
            const auto ref = refs[i];
            refs[i].sourceIndex = 1u;
            reject("RHI-WEBGPU-COMMAND-PROJECTION-READBACK-REFERENCES");
            refs[i] = ref;
            refs[i].resource = {UINT64_MAX};
            reject("RHI-WEBGPU-COMMAND-PROJECTION-READBACK-REFERENCES");
            refs[i] = ref;
        }
        std::swap(refs[0], refs[1]);
        reject("RHI-WEBGPU-COMMAND-PROJECTION-READBACK-REFERENCES");
        refs = references(pair);
        refs[1].domain = Domain::Buffer;
        b.maxDomainReferences[static_cast<std::size_t>(Domain::Buffer)] = 1u;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-READBACK-REFERENCES");
        refs = references(pair);
        b = budget();
        refs[1].source = Source::Transfer;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-UNPROVEN");
        refs = references(pair);
        count = 1u;
        reject("RHI-WEBGPU-COMMAND-PROJECTION-READBACK-REFERENCES");
        count = 2u;
        {
            std::array<std::shared_ptr<const Pair>, 2> pairs{{pair, pair}};
            auto multi = b;
            multi.maxOriginalReadbackPairs = 2u;
            Graph::Sources sources;
            sources.readbacks = pairs.data();
            sources.readbackCount = 2u;
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Graph::prepareOriginalShared(
                       std::move(sources), nullptr, 0u, multi, quota, diagnostic)
                && std::string_view(diagnostic)
                    == "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-MULTI-PAIR-UNPROVEN"
                && core::defaultCpuAllocator().stats().allocationCount == before);
        }
        {
            std::array<Ref, 4> duplicate{{refs[0], refs[1], refs[0], refs[1]}};
            auto duplicateBudget = b;
            duplicateBudget.maxReferences = 4u;
            duplicateBudget.maxDomainReferences.fill(2u);
            Graph::Sources sources;
            sources.readbacks = &pair;
            sources.readbackCount = 1u;
            assert(!Graph::prepareOriginalShared(std::move(sources),
                       duplicate.data(),
                       duplicate.size(),
                       duplicateBudget,
                       quota,
                       diagnostic)
                && std::string_view(diagnostic)
                    == "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-REFERENCES");
        }
        auto graph = factory();
        assert(graph && !graph->validate() && graph->originalReadbacks().size() == 1u
            && graph->originalReadbacks()[0] == pair && graph->retainedAllocationBytes() == 4432u);
        refs[0].resource = {UINT64_MAX};
        refs[1].sourceIndex = 99u;
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!graph->validateReference(
            0u, Domain::Texture, pair->destination()->source()->resource().texture.get()));
        assert(!graph->validateReference(1u, Domain::Readback, pair.get()));
        assert(graph->validateReference(
                   1u, Domain::Buffer, pair->destination()->nativeResource().get())
            && graph->validateReference(
                1u, Domain::Readback, pair->destination()->nativeResource().get()));
        auto& texture = *pair->destination()->source()->resource().texture;
        const auto extent = texture.extent;
        ++texture.extent.width;
        assert(graph->validate());
        texture.extent = extent;
        auto& buffer = *pair->destination()->nativeResource();
        const auto bytes = buffer.sizeBytes;
        ++buffer.sizeBytes;
        assert(graph->validate());
        buffer.sizeBytes = bytes;
        assert(!graph->validate()
            && std::string_view(graph->executionDiagnostic())
                == rhi_webgpu::webGpuGenericCompletionResourceGraphDiagnostic);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        std::printf(
            "BINDLESS_RHI_READBACK_PROJECTION_NATIVE_OK %llu pair1 references2 allocations3 native4432 mips2 original-owner private-buffer-closed allocation0 recording-closed\n",
            static_cast<unsigned long long>(peak));
        return graph;
    }
#endif
};
} // namespace arp::test
