#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_command_inputs.h"
#include "webgpu_generic_command_usage_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuGenericCommandProjectionFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        WebGpuGenericCommandProjectionBudget budget;
        assert(std::string_view(WebGpuGenericCommandProjection::validateBudget(budget))
            == "RHI-WEBGPU-COMMAND-PROJECTION-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuGenericCommandProjection::validateBudget(budget))
            == "RHI-WEBGPU-COMMAND-PROJECTION-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(WebGpuGenericCommandProjection::validateBudget(budget))
            == "RHI-WEBGPU-COMMAND-PROJECTION-BUDGET");
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        budget.maxCpuMetadataBytes = quota->limitBytes();
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return WebGpuGenericCommandProjection::prepare(
                {}, nullptr, 0u, budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota->liveBytes());
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged() && !factory());
            assert(std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION");
        }
        --budget.maxCpuMetadataBytes;
        assert(!factory() && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-QUOTA");
        ++budget.maxCpuMetadataBytes;
        budget.maxPipelines = UINT32_MAX;
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION");
        budget.maxPipelines = 0u;
        auto empty = factory();
        assert(empty && !empty->validate() && !empty->retainedAllocationBytes());
        assert(std::string_view(empty->executionDiagnostic())
            == webGpuGenericCompletionResourceGraphDiagnostic);
        assert(empty->validateReference(0u, WebGpuCommandReferenceDomain::Buffer, empty.get()));
        empty.reset();
        budget.maxReferences = 1u;
        budget.maxDomainReferences.fill(1u);
        WebGpuCommandReference reference;
        reference.resource = {1u};
        const auto referenced = [&] {
            return WebGpuGenericCommandProjection::prepare(
                {}, &reference, 1u, budget, quota, diagnostic);
        };
        assert(!referenced()
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
        budget.maxDomainReferences[0] = 0u;
        assert(!referenced()
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-CAPACITY");
        budget.maxDomainReferences[0] = 1u;
        for (auto domain : {WebGpuCommandReferenceDomain::BindlessRoot,
                 WebGpuCommandReferenceDomain::Upload,
                 WebGpuCommandReferenceDomain::Readback,
                 WebGpuCommandReferenceDomain::Query,
                 WebGpuCommandReferenceDomain::RenderBundle,
                 WebGpuCommandReferenceDomain::IndirectProducer,
                 WebGpuCommandReferenceDomain::IndirectProcessor,
                 WebGpuCommandReferenceDomain::DescriptorTable,
                 WebGpuCommandReferenceDomain::SecondaryCommandBuffer}) {
            reference.domain = domain;
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!referenced()
                && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-UNPROVEN");
            assert(core::defaultCpuAllocator().stats().allocationCount == before);
        }
        reference.domain = static_cast<WebGpuCommandReferenceDomain>(UINT32_MAX);
        assert(!referenced()
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN");
        reference.domain = WebGpuCommandReferenceDomain::Upload;
        reference.source = WebGpuCommandReferenceSource::OriginalBufferUpload;
        budget.maxOriginalUploadSources = 1u;
        budget.maxOriginalUploadBytes = 16u;
        assert(!referenced()
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
        WebGpuGenericCommandProjection::Sources sources;
        sources.uploadCount = 1u;
        assert(!WebGpuGenericCommandProjection::prepare(
                   std::move(sources), nullptr, 0u, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY");
        const std::shared_ptr<const WebGpuOriginalBufferUploadSource> emptySource;
        sources.uploads = &emptySource;
        assert(!WebGpuGenericCommandProjection::prepare(
                   std::move(sources), nullptr, 0u, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-TRANSFER");
        assert(!quota->liveBytes());
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class Resources, class Transfers>
    static core::UniquePtr<rhi_webgpu::WebGpuGenericCommandProjection> verifyResources(
        Resources&& resources,
        Transfers&& transfers,
        const std::array<rhi::GpuResourceId, 4>& ids,
        std::uint64_t bytes,
        const void* buffer,
        const void* view,
        const void* secondView,
        const void* sampler)
    {
        using namespace rhi_webgpu;
        using Domain = WebGpuCommandReferenceDomain;
        using Source = WebGpuCommandReferenceSource;
        std::array<WebGpuCommandReference, 5> references{
            {{Domain::Buffer, Source::Transfer, ids[0]},
                {Domain::Buffer, Source::Resource, ids[0]},
                {Domain::TextureView, Source::Resource, ids[1]},
                {Domain::TextureView, Source::Resource, ids[2]},
                {Domain::Sampler, Source::Resource, ids[3]}}};
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        WebGpuGenericCommandProjectionBudget budget{
            1u, true, 1u, 1u, 0u, 0u, 5u, 5u, {}, quota->limitBytes(), bytes};
        budget.maxDomainReferences = {2u, 0u, 2u, 1u};
        const char* diagnostic = nullptr;
        std::size_t referenceCount = references.size();
        bool canonicalResources = true, canonicalTransfers = true, aliasResources = false;
        const auto factory = [&] {
            WebGpuGenericCommandProjection::Sources sources;
            auto originalResources = canonicalResources
                ? WebGpuOrdinaryResourceProjection::shareOriginal(resources())
                : core::shareUnique(resources());
            sources.resources = aliasResources
                ? std::shared_ptr<const WebGpuOrdinaryResourceProjection>(
                      quota, originalResources.get())
                : originalResources;
            sources.transfer = canonicalTransfers
                ? WebGpuOrdinaryTransferRetention::shareOriginal(transfers())
                : core::shareUnique(transfers());
            assert(sources.resources && sources.transfer);
            return WebGpuGenericCommandProjection::prepare(
                std::move(sources), references.data(), referenceCount, budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota->liveBytes());
        const auto expect = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes());
        };
        canonicalResources = false;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-RESOURCE-OWNER");
        canonicalResources = true;
        canonicalTransfers = false;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-RESOURCE-OWNER");
        canonicalTransfers = true;
        aliasResources = true;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-RESOURCE-OWNER");
        aliasResources = false;
        --budget.maxRetainedAllocationBytes;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-BYTES");
        ++budget.maxRetainedAllocationBytes;
        --budget.maxAllocationReferences;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-ALLOCATION-COUNT");
        ++budget.maxAllocationReferences;
        --budget.maxReferences;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY");
        ++budget.maxReferences;
        budget.maxTransferProjections = 0u;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY");
        budget.maxTransferProjections = 1u;
        budget.maxResourceProjections = 0u;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-CAPACITY");
        budget.maxResourceProjections = 1u;
        auto original = references[4];
        references[4].resource = {UINT64_MAX};
        expect("RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
        references[4] = original;
        references[4].domain = Domain::TextureView;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-CAPACITY");
        references[4] = original;
        references[4].source = static_cast<Source>(UINT32_MAX);
        expect("RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
        references[4] = original;
        references[4].sourceIndex = 1u;
        expect("RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
        references[4] = original;
        auto result = factory();
        assert(result && !diagnostic && result->retainedAllocationBytes() == bytes);
        references[0].resource = {UINT64_MAX}; // The original plan was copied cold.
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!result->validateReference(0u, Domain::Buffer, buffer));
        assert(!result->validateReference(1u, Domain::Buffer, buffer));
        assert(!result->validateReference(2u, Domain::TextureView, view));
        assert(!result->validateReference(4u, Domain::Sampler, sampler));
        assert(result->validateReference(4u, Domain::Sampler, buffer));
        assert(result->validateReference(0u, Domain::TextureView, buffer));
        assert(std::string_view(result->executionDiagnostic())
            == webGpuGenericCompletionResourceGraphDiagnostic);
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        std::printf("BINDLESS_RHI_GENERIC_RESOURCE_CLOSURE_OK %llu %llu 5\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(bytes));
        references[0].resource = ids[0];
        referenceCount = 0u;
        WebGpuOriginalCommandInputsFixture::verifyStateInventory(
            WebGpuGenericCommandProjection::shareOriginal(factory()));
        referenceCount = 4u;
        auto usageGraph = WebGpuGenericCommandProjection::shareOriginal(factory());
        assert(usageGraph);
        const std::array<WebGpuGenericCommandUsage::RawReference, 4> raw{{{Domain::Buffer, buffer},
            {Domain::Buffer, buffer},
            {Domain::TextureView, view},
            {Domain::TextureView, secondView}}};
        auto usage = WebGpuGenericCommandUsageFixture::verifyActual(usageGraph,
            {{{rhi::CommandUsageKind::BufferCopy, 0u, 2u},
                {rhi::CommandUsageKind::BeginRenderPass, 2u, 2u},
                {rhi::CommandUsageKind::Finish, 4u, 0u}}},
            raw.data(),
            raw.size(),
            "resources");
        // Ownership usage intentionally grants no scalar transfer/pass proof.
        assert(std::string_view(usage->executionDiagnostic())
            == webGpuGenericRecordingUsageDiagnostic);
        return result;
    }

    static void verifyGroups(
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryPhysicalGroups>& group,
        const char* name)
    {
        using namespace rhi_webgpu;
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        std::shared_ptr<const WebGpuOrdinaryPipelineObservation> pipeline = group->pipeline();
        auto inputGroup = group;
        std::array<WebGpuCommandReference, 2> references{
            {{WebGpuCommandReferenceDomain::Pipeline, WebGpuCommandReferenceSource::Pipeline},
                {WebGpuCommandReferenceDomain::Descriptor,
                    WebGpuCommandReferenceSource::PhysicalGroups}}};
        WebGpuGenericCommandProjectionBudget budget{1u,
            true,
            0u,
            0u,
            1u,
            1u,
            2u,
            1u,
            {},
            quota->limitBytes(),
            group->retainedAllocationBytes()};
        budget
            .maxDomainReferences[static_cast<std::size_t>(WebGpuCommandReferenceDomain::Pipeline)] =
            1u;
        budget.maxDomainReferences[static_cast<std::size_t>(
            WebGpuCommandReferenceDomain::Descriptor)] = 1u;
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            WebGpuGenericCommandProjection::Sources sources;
            sources.pipelines = &pipeline;
            sources.pipelineCount = 1u;
            sources.groups = &inputGroup;
            sources.groupCount = 1u;
            return WebGpuGenericCommandProjection::prepare(std::move(sources),
                references.data(),
                references.size(),
                budget,
                quota,
                diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        const auto originalGroup = inputGroup;
        inputGroup = std::shared_ptr<const WebGpuOrdinaryPhysicalGroups>(quota, group.get());
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-GROUP-OWNER");
        inputGroup = originalGroup;
        const auto originalPipeline = pipeline;
        pipeline = std::shared_ptr<const WebGpuOrdinaryPipelineObservation>(quota, pipeline.get());
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-PROJECTION-PIPELINE-OWNER");
        pipeline = originalPipeline;
        auto result = WebGpuGenericCommandProjection::shareOriginal(factory());
        assert(result && !result->validate()
            && result->retainedAllocationBytes() == group->retainedAllocationBytes());
        const void* nativePipeline = pipeline->compute()
            ? static_cast<const void*>(pipeline->compute().get())
            : pipeline->graphics().get();
        const std::array<WebGpuGenericCommandUsage::RawReference, 2> raw{
            {{WebGpuCommandReferenceDomain::Pipeline, nativePipeline},
                {WebGpuCommandReferenceDomain::Descriptor, group.get()}}};
        auto usage = WebGpuGenericCommandUsageFixture::verifyActual(result,
            {{{pipeline->compute() ? rhi::CommandUsageKind::BindComputePipeline
                                   : rhi::CommandUsageKind::BindGraphicsPipeline,
                  0u,
                  1u},
                {pipeline->compute() ? rhi::CommandUsageKind::BindComputeDescriptor
                                     : rhi::CommandUsageKind::BindRenderDescriptor,
                    1u,
                    1u},
                {rhi::CommandUsageKind::Finish, 2u, 0u}}},
            raw.data(),
            raw.size(),
            name);
        WebGpuFrozenCommandScalarsFixture::verifyActual(group, name);
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(
            !result->validateReference(0u, WebGpuCommandReferenceDomain::Pipeline, nativePipeline));
        assert(
            !result->validateReference(1u, WebGpuCommandReferenceDomain::Descriptor, group.get()));
        assert(std::string_view(result->executionDiagnostic())
            == webGpuGenericCompletionResourceGraphDiagnostic);
        const auto mutate = [&](auto& payload) {
            WebGpuGenericCommandUsage::Consumption recording(usage);
            const auto first = usage->commands()[0];
            const char* rejected = nullptr;
            const auto ticket = recording.preflight(first.kind, raw.data(), 1u, rejected);
            assert(ticket && !rejected);
            const auto hash = payload->shaderPackageHash;
            ++payload->shaderPackageHash;
            assert(result->validate() && usage->validate());
            assert(
                !recording.commit(ticket, rejected) && rejected && !recording.consumedCommands());
            payload->shaderPackageHash = hash;
            assert(!result->validate() && !usage->validate());
            assert(!recording.commit(ticket, rejected) && !recording.complete());
            assert(!recording.preflight(first.kind, raw.data(), 1u, rejected)
                && std::string_view(rejected) == "RHI-WEBGPU-COMMAND-USAGE-CLOSED");
        };
        if (pipeline->compute())
            mutate(pipeline->compute());
        else
            mutate(pipeline->graphics());
        pipeline.reset();
        inputGroup.reset();
        assert(
            !result->validateReference(0u, WebGpuCommandReferenceDomain::Pipeline, nativePipeline));
        assert(
            !result->validateReference(1u, WebGpuCommandReferenceDomain::Descriptor, group.get()));
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        result.reset();
        assert(quota->liveBytes() && !usage->validate());
        usage.reset();
        assert(!quota->liveBytes());
        std::printf("BINDLESS_RHI_GENERIC_BINDING_CLOSURE_OK %s %llu\n",
            name,
            static_cast<unsigned long long>(peak));
    }
#endif
};
} // namespace arp::test
