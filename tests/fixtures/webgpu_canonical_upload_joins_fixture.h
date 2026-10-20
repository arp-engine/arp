#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_upload_joins.h"
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class WebGpuCanonicalUploadJoinsFixture final {
    using Joins = rhi_webgpu::WebGpuCanonicalUploadJoins;
    using Root = rhi::OriginalCommandStateTransaction;
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "upload joins actual=%s expected=%s\n",
                actual ? actual : "<none>",
                expected);
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static std::shared_ptr<Root> prepareRoot(
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& stateQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& recordingQuota,
        bool texture,
        const char*& diagnostic)
    {
        return prepareRoot(inputs,
            device,
            stateQuota,
            recordingQuota,
            texture,
            2u,
            texture ? 3u : 2u,
            texture ? 2u : 0u,
            {1u, true, 5u, 0u, texture ? 5u : 2u, 4u},
            diagnostic);
    }

    static std::shared_ptr<Root> prepareRoot(
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& stateQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& recordingQuota,
        bool texture,
        std::uint32_t resources,
        std::uint32_t observations,
        std::uint32_t ranges,
        const rhi::OriginalCommandStateSequenceBudget& sequence,
        const char*& diagnostic,
        const rhi::OriginalTextureContentPlanBudget* content = nullptr)
    {
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        std::uint32_t owners = 0u, registrations = 0u;
        if ((diagnostic = rhi::OriginalCommandResourceSources::registeredOwnerUnion(sources,
                 inputs->retainedOwnerCount() + 1u,
                 UINT32_MAX,
                 resources,
                 owners,
                 registrations)))
            return {};
        rhi::OriginalCommandStateAdmissionBudget b{
            1u, true, 1u, resources, observations, ranges, owners, bytes};
        auto graph = rhi::OriginalCommandResourceStates::prepareOriginal(
            inputs, device, b, stateQuota, diagnostic);
        if (!graph)
            return {};
        b.maxRetainedOwners += 1u + (content ? content->maxContentControls : texture ? 1u : 0u);
        auto root = Root::prepareOriginalOwnership(graph, device, b, recordingQuota, diagnostic);
        if (!root)
            return {};
        diagnostic = content ? root->materializeOriginalPrototypePlan(sequence, *content)
            : texture        ? root->materializeOriginalPrototypePlan(sequence, {1u, true, 1u, 2u})
                             : root->materializeOriginalPrototypePlan(sequence);
        return diagnostic ? std::shared_ptr<Root>{} : root;
    }

    static Joins::Budget budget(const Root& root, bool texture)
    {
        const auto closure = texture
            ? rhi_webgpu::WebGpuCanonicalTextureContentUploadSource::retainedOwnerBound
            : rhi::OriginalCommandInitializedBufferStagingContract::retainedOwnerBound;
        return {1u,
            true,
            2u,
            1u,
            1u,
            root.retainedOwnerCount() + 1u + 2u * closure,
            texture ? 768u : 16u,
            texture ? 20u : 16u,
            bytes};
    }

    static std::shared_ptr<const Joins> verify(std::shared_ptr<Root> root,
        rhi::GpuDevice& device,
        Joins::Source source,
        bool texture,
        bool retainForLoss = false)
    {
        const char* diagnostic = nullptr;
        const std::array<Joins::Input, 2> entries{{{1u, source}, {2u, source}}};
        const auto limits = budget(*root, texture);
        const auto make = [&] {
            return Joins::prepareOriginal(
                root, device, entries.data(), entries.size(), limits, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(root->originalMetadataQuota());
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        {
            core::CpuMemoryQuotaLease full(
                root->originalMetadataQuota(), bytes - root->originalMetadataQuota()->liveBytes());
            assert(full.charged());
            assert(!make());
            is(diagnostic, "RHI-WEBGPU-UPLOAD-JOIN-ALLOCATION");
        }
        auto joined = make();
        assert(joined && !diagnostic && !joined->validate() && joined->uploadCount() == 2u
            && joined->retainedOwnerCount() == limits.maxRetainedOwners
            && joined->stagingResourceBytes() == limits.maxStagingResourceBytes
            && joined->destinationResourceBytes() == limits.maxDestinationResourceBytes
            && !joined->nativeExecutionProven());
        for (unsigned limit = 0u; limit < 4u; ++limit) {
            auto bad = limits;
            if (!limit)
                --bad.maxUploads;
            if (limit == 1u)
                --bad.maxRetainedOwners;
            if (limit == 2u)
                --bad.maxStagingResourceBytes;
            if (limit == 3u)
                --bad.maxDestinationResourceBytes;
            assert(!Joins::prepareOriginal(
                root, device, entries.data(), entries.size(), bad, diagnostic));
            is(diagnostic,
                !limit            ? "RHI-WEBGPU-UPLOAD-JOIN-COUNT"
                    : limit == 1u ? "RHI-WEBGPU-UPLOAD-JOIN-OWNER-CAPACITY"
                                  : "RHI-WEBGPU-UPLOAD-JOIN-NATIVE-BYTE-CAPACITY");
        }
        auto changed = entries;
        std::swap(changed[0], changed[1]);
        assert(!Joins::prepareOriginal(
            root, device, changed.data(), changed.size(), limits, diagnostic));
        is(diagnostic, "RHI-WEBGPU-UPLOAD-JOIN-ORDER-COVERAGE");
        assert(!Joins::prepareOriginal(root, device, entries.data(), 1u, limits, diagnostic));
        is(diagnostic, "RHI-WEBGPU-UPLOAD-JOIN-ORDER-COVERAGE");
        assert(!Joins::prepareOriginal(root, device, nullptr, 2u, limits, diagnostic));
        is(diagnostic, "RHI-WEBGPU-UPLOAD-JOIN-COUNT");
        auto forged = std::shared_ptr<const Joins>(root->originalMetadataQuota(), joined.get());
        is(Joins::validateOriginal(forged), "RHI-WEBGPU-UPLOAD-JOIN-OWNER");
        forged.reset();
        std::visit(
            [&](const auto& original) {
                using Pointer = std::decay_t<decltype(original)>;
                Pointer fake(root->originalMetadataQuota(), original.get());
                changed = entries;
                changed[0].source = fake;
                assert(!Joins::prepareOriginal(
                    root, device, changed.data(), changed.size(), limits, diagnostic));
                is(diagnostic,
                    texture ? "RHI-CANONICAL-CONTENT-STAGING-OWNER"
                            : "RHI-WEBGPU-UPLOAD-JOIN-BUFFER-OWNER");
            },
            source);
        changed = entries;
        if (texture)
            changed[0].source =
                std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>{};
        else
            changed[0].source =
                std::shared_ptr<const rhi_webgpu::WebGpuCanonicalTextureContentUploadSource>{};
        assert(!Joins::prepareOriginal(
            root, device, changed.data(), changed.size(), limits, diagnostic));
        is(diagnostic,
            texture ? "RHI-WEBGPU-UPLOAD-JOIN-TEXTURE-OWNER"
                    : "RHI-WEBGPU-UPLOAD-JOIN-BUFFER-OWNER");
        changed = entries;
        assert(root->status() == Root::Status::Prepared && root->journalEmpty());
        std::visit(
            [&](const auto& proof) {
                const auto lease = [&] {
                    if constexpr (std::is_same_v<std::decay_t<decltype(proof)>,
                                      std::shared_ptr<
                                          const rhi_webgpu::WebGpuCanonicalBufferUploadSource>>)
                        return proof->originalLease();
                    else
                        return proof->originalStaging()->initializedBuffer()->originalLease();
                }();
                auto native = rhi_webgpu::webGpuNativePayloadFor(*lease->originalObject());
                assert(native);
                const auto frozenSize = native->sizeBytes;
                --native->sizeBytes;
                assert(joined->validate());
                native->sizeBytes = frozenSize;
                assert(!joined->validate());
            },
            source);
        const auto& sources = root->originalGraph()
                                  ->originalInputs()
                                  ->originalScalars()
                                  ->originalUsage()
                                  ->originalSources();
        const auto hotQuotas = std::array{root->originalMetadataQuota(),
            root->originalGraph()->originalMetadataQuota(),
            root->originalGraph()->originalInputs()->originalMetadataQuota(),
            sources->originalMetadataQuota()};
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            hotQuotas, [&]() noexcept -> const char* {
                if (const auto* id = joined->validate())
                    return id;
                is(joined->executionDiagnostic(),
                    "RHI-ORIGINAL-UPLOAD-JOIN-ORDERED-EXECUTION-MISSING");
                return nullptr;
            }));
        std::printf(
            "BINDLESS_RHI_CANONICAL_UPLOAD_JOIN_OK %s peak%llu uploads2 exact-original-ordinals1-2 unique-native-source1-target1 staging%llu target%llu cold-exact-less-exhausted forged-order-count-byte-owner-variant-refused hot-allocation0 execution-closed\n",
            texture ? "texture" : "buffer",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(joined->stagingResourceBytes()),
            static_cast<unsigned long long>(joined->destinationResourceBytes()));
        if (retainForLoss)
            return joined;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            hotQuotas, [&]() noexcept -> const char* {
                if (const auto* id = root->applyOriginalPrototypePlan())
                    return id;
                assert(joined->validate());
                if (const auto* id = root->abandonOriginalPendingOwnership())
                    return id;
                assert(joined->validate());
                return nullptr;
            }));
        std::weak_ptr<Root> observed = root;
        root.reset();
        assert(!observed.expired());
        joined.reset();
        observed.reset();
        std::printf(
            "BINDLESS_RHI_CANONICAL_UPLOAD_JOIN_LIFECYCLE_OK %s same-root-journal pending-rollback-stale-refused caller-root-drop retained-until-join-drop hot-allocation0\n",
            texture ? "texture" : "buffer");
        return {};
    }
};
} // namespace arp::test
