#pragma once
#include "original_bindless_graphics_stream_fixture.h"
#include "webgpu_original_completed_preparation_fixture.h"

namespace arp::test {
struct WebGpuOriginalCompletedContentRequest : WebGpuOriginalCompletedPreparationRequest {
    static constexpr bool canonicalCompletion = true;

    // The original Root fixture already prepares this exact native loss source.
    void prepareOriginalDevice(rhi::GpuDevice&) const {}

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& budget) const
    {
        // Two authored sampled whole-range transitions beside the attachment.
        budget.maxStateObservations += 2u;
        budget.maxTextureRanges += 2u;
        budget.maxCommands += 2u;
        budget.maxReferences += 2u;
        budget.maxInputDescriptors += 2u;
    }
};

// This tests cold Content9 planning with real original uploads. It grants no
// recording, submission, completion or mutation of the captured CopyDst state.
class WebGpuOriginalCompletedContentFixture final {
public:
    static constexpr bool completedTextures = true;
    std::uint32_t readProfile = 1u;

    template <class Root, class Inventory>
    void operator()(const Root& root, const Inventory& inventory, rhi::GpuDevice* device) const
    {
        if (!device) {
            assert(!root && !inventory);
            return;
        }
        for (const auto profile : {1u, 3u}) {
            auto selected = *this;
            selected.readProfile = profile;
            OriginalBindlessGraphicsStreamFixture::execute(
                root, inventory, *device, selected, nullptr, profile);
        }
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(const Root& root,
        const Inventory& inventory,
        const Binding& binding,
        const Transaction& transaction,
        rhi::GpuDevice&) const
    {
        using namespace rhi;
        assert(root->matchesCanonicalSources(*inventory)
            && core::SharedOwnerProjection::sameOwner(
                binding->originalGraph(), transaction->originalGraph())
            && transaction->status() == OriginalCommandStateTransaction::Status::Prepared
            && transaction->journalEmpty()
            && transaction->originalBindlessCompletedReadContractVersion() == readProfile);
        const auto* counts = transaction->originalMixedContentCounts();
        assert(counts && counts->completedControls == 2u && counts->drawReads == 2u
            && counts->attachmentControls == 1u && counts->attachmentCells == 1u
            && counts->attachmentSteps == 2u);
        for (const auto& element : root->elements()) {
            const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                &element.owner);
            if (!view)
                continue;
            const auto* object = (*view)->originalParentLease()->originalObject();
            const auto content = object->observeOriginalContent();
            assert(content && content->controlVersion() == 1u && content->contentGeneration() == 2u
                && object->state() == ResourceState::CopyDst);
        }
        assert(!binding->originalResult()->publishedBytes());
        assert(transaction->executionDiagnostic(Backend::WebGPU));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{transaction->originalMetadataQuota()}, [&]() noexcept -> const char* {
                return transaction->validate();
            }));
        if (readProfile == 1u)
            std::puts(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPLETED_CONTENT_OK Content9 read10 controls2 drawReads2 attachment1 same-root-inventory-journal canonical-control1 CopyDst-retained hot0 no-execution");
        else {
            assert(readProfile == 3u);
            std::puts(
                "BINDLESS_RHI_WEBGPU_CANONICAL_COLOR_READ18_OK Content9 profile3 controls2 drawReads2 attachment1 same-root-inventory-journal canonical-control1 CopyDst-retained hot0 no-execution");
        }
    }
};
} // namespace arp::test
