#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {
// Adopts the producer's already-published ordinary set. No additional GPU
// resource, native handle, content issuer or execution profile is created.
class OrdinaryCompletedDepthSourceFixture final {
public:
    template <class Invalidate>
    static void verify(rhi::GpuDevice& device,
        core::UniquePtr<rhi::DescriptorSet>& object,
        const std::shared_ptr<rhi::DescriptorArena>& arena,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& proof,
        Invalidate&& invalidate)
    {
        using namespace rhi;
        using Source = OriginalCommandDescriptorSetSource;
        using Sources = OriginalCommandResourceSources;
        const auto& view = proof->originalViewSource();
        const auto& quota = view->originalMetadataQuota();
        const auto bytes = quota->limitBytes();
        const auto revision = object->contentRevision();
        const auto* original = object.get();
        const std::array<Sources::Source, 2u> leases{view->originalParentLease(), view};
        OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = 1u;
        admission.maxGraphOwners = Source::canonicalExclusiveRetainedOwnerCount
            + OriginalCompletedDepthTextureSource::completeRetainedOwnerCount;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Texture)] = 1u;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::TextureView)] =
            1u;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Descriptor)] =
            1u;
        admission.maxCpuBytes.fill(bytes);
        const char* id = nullptr;
        auto inventory = Sources::prepareColdOriginalTyped(
            leases.data(), leases.size(), admission, {6u, true, 1u}, quota, id);
        if (!inventory)
            std::fprintf(stderr, "ordinary depth inventory: %s\n", id ? id : "missing");
        assert(inventory && !id);
        Source::Element element;
        const auto observation = object->observeSuccessfulBindingWrite(0u, 0u, id);
        assert(observation && !id);
        element.write = observation->write();
        element.sourceIndex = 1u;
        element.registrationGeneration =
            view->originalObject()->originalResourceRegistration().generation();
        element.stages = PipelineStage::FragmentShader;
        element.access = ResourceAccess::ShaderRead;
        OriginalCommandDescriptorSourceBudget budget{
            6u, true, 1u, 1u, Source::canonicalExclusiveRetainedOwnerCount, bytes};
        std::uint64_t texelBytes = 0u;
        assert(!semantics::TextureUploadDestinationValidation::footprint(
            view->originalParentLease()->originalObject()->desc(), texelBytes));
        budget.completedDepthInputs = {3u,
            true,
            proof->cells().size(),
            texelBytes,
            OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        std::array<std::shared_ptr<const OriginalCompletedDepthTextureSource>, 1u> depths{proof};
        const auto freeze = [&](const auto& limits, const auto* inputs, std::size_t count) {
            return Source::prepareOriginal(
                object, arena, inventory, &element, 1u, limits, quota, device, id, inputs, count);
        };
        const auto baseline = quota->liveBytes();
        {
            auto equivalent = OriginalCompletedDepthTextureSource::prepareOriginal(
                view, proof->budget(), quota, id);
            assert(equivalent && !id && equivalent.get() != proof.get());
            assert(!object->diagnoseOriginalCompletedDepthPublication(*observation, proof));
            const auto* mismatch =
                object->diagnoseOriginalCompletedDepthPublication(*observation, equivalent);
            assert(mismatch
                && std::strcmp(mismatch, "RHI-ORIGINAL-COMPLETED-DEPTH-PUBLICATION-OWNER") == 0);
            depths[0] = equivalent;
            const auto retainedBytes = quota->liveBytes();
            assert(!freeze(budget, depths.data(), 1u) && id
                && std::strcmp(id, "RHI-ORIGINAL-COMPLETED-DEPTH-PUBLICATION-OWNER") == 0);
            assert(object.get() == original && object->contentRevision() == revision);
            assert(quota->liveBytes() == retainedBytes);
            depths[0] = proof;
        }
        assert(quota->liveBytes() == baseline);
        const auto refuse = [&](const auto& limits, const auto* inputs, std::size_t count) {
            assert(!freeze(limits, inputs, count) && id);
            assert(object.get() == original && object->contentRevision() == revision);
            assert(quota->liveBytes() == baseline);
        };
        refuse(budget, depths.data(), 0u);
        depths[0].reset();
        refuse(budget, depths.data(), 1u);
        auto foreign = core::makeShared<core::CpuMemoryQuota>(bytes);
        depths[0] =
            std::shared_ptr<const OriginalCompletedDepthTextureSource>(foreign, proof.get());
        refuse(budget, depths.data(), 1u);
        depths[0] = proof;
        auto wrong = budget;
        --wrong.completedDepthInputs.maxTextureTexelBytes;
        refuse(wrong, depths.data(), 1u);
        wrong = budget;
        wrong.completedDepthInputs.contractVersion = 2u;
        refuse(wrong, depths.data(), 1u); // Old native input remains Compute-only.
        wrong = budget;
        --wrong.completedDepthInputs.maxCells;
        refuse(wrong, depths.data(), 1u);
        wrong = budget;
        --wrong.completedDepthInputs.maxRetainedOwners;
        refuse(wrong, depths.data(), 1u);
        wrong = budget;
        wrong.completedDepthInputs.prototypeOptIn = false;
        refuse(wrong, depths.data(), 1u);
        const auto authored = element;
        ++element.registrationGeneration;
        refuse(budget, depths.data(), 1u);
        element = authored;
        element.sourceIndex = 0u; // Parent texture is not the canonical sampled view.
        refuse(budget, depths.data(), 1u);
        element = authored;
        element.stages = PipelineStage::VertexShader;
        refuse(budget, depths.data(), 1u);
        element = authored;
        element.access = ResourceAccess::ShaderWrite;
        refuse(budget, depths.data(), 1u);
        element = authored;
        element.write.resourceState = ResourceState::CopySrc;
        refuse(budget, depths.data(), 1u);
        element = authored;
        wrong = budget;
        wrong.contractVersion = 2u;
        wrong.completedDepthInputs = {};
        refuse(wrong, depths.data(), 1u);
        std::uint32_t before = 0u;
        assert(!inventory->cpuOwnerCount(0u, UINT32_MAX, before));
        auto frozen = freeze(budget, depths.data(), 1u);
        if (!frozen)
            std::fprintf(stderr, "ordinary depth Source6: %s\n", id ? id : "missing");
        assert(frozen && !id && !object && frozen->originalObject() == original);
        assert(core::SharedOwnerProjection::sameOwner(
            frozen->frozenElements()[0].completedDepth, proof));
        assert(!Sources::appendOriginalTyped(inventory, frozen, device));
        std::uint32_t exact = 0u, refused = 0u;
        assert(!inventory->cpuOwnerCount(0u, UINT32_MAX, exact));
        assert(exact == before + Source::retainedOwnerCount + 2u); // Arena + typed proof.
        assert(!inventory->cpuOwnerCount(0u, exact, refused) && refused == exact);
        assert(inventory->cpuOwnerCount(0u, exact - 1u, refused));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
            return Source::validateOriginal(frozen);
        }));
        invalidate();
        assert(proof->validate() && Source::validateOriginal(frozen));
        assert(original->diagnoseOriginalCompletedDepthPublication(*observation, proof));
        std::puts(
            "BINDLESS_ORDINARY_DEPTH_SOURCE6 genuine-publication fragment canonical-view typed-owner exact-publication-owner equivalent-proof-refusal exact-less-owner hot0 stale-refusal no-execution-grant");
    }
};
} // namespace arp::test
