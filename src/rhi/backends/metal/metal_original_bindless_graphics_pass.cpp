#include "rhi/backends/metal/metal_original_bindless_graphics_pass.h"

#include "core/ownership/shared_identity_projection.h"
#include "rhi/backends/metal/metal_device.h"

namespace arp::rhi_metal {
using namespace rhi;

MetalOriginalBindlessGraphicsPass::MetalOriginalBindlessGraphicsPass(ConstructionKey,
    std::shared_ptr<const Closure> closure,
    std::shared_ptr<const Transaction> transaction,
    std::shared_ptr<const Stream> stream,
    Budget budget,
    std::shared_ptr<core::AtomicCapacityBudget> capacity,
    std::shared_ptr<MetalRhiResourceRegistry> registry)
    : closure_(std::move(closure)), transaction_(std::move(transaction)),
      stream_(std::move(stream)), budget_(budget), capacity_(std::move(capacity)),
      registry_(std::move(registry))
{}

const RenderPassDesc* MetalOriginalBindlessGraphicsPass::descriptor() const noexcept
{
    const auto* input = stream_->originalInputs()->descriptorAt(commandIndex_);
    const auto* pass =
        input ? std::get_if<std::shared_ptr<const RenderPassArguments>>(input) : nullptr;
    return pass && *pass ? &(*pass)->desc() : nullptr;
}

const char* MetalOriginalBindlessGraphicsPass::captureAttachments() noexcept
{
    if (const auto* id =
            Contract::selectPass(*stream_, closure_->originalRoot(), budget_, commandIndex_))
        return id;
    const auto* desc = descriptor();
    const auto& prepared = std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
        closure_->root_->originalPrepared());
    const auto definition =
        metalRhiOriginalGraphicsPassPipelineState(*prepared->owners()->desc().pipeline);
    if (!desc || !definition)
        return "RHI-BINDLESS-GRAPHICS-PASS-DEFINITION";
    if (const auto* id = Contract::inspectPass(*desc, *definition))
        return id;
    attachmentCount_ = desc->colorAttachments.size() + (desc->hasDepthStencilAttachment ? 1u : 0u);
    if (const auto* id =
            Contract::validateCounts(budget_, attachmentCount_, nativeObjectCount(), 0u))
        return id;
    const auto& usage = *stream_->originalInputs()->originalScalars()->originalUsage();
    const auto& command = usage.commands()[commandIndex_];
    if (command.referenceCount != attachmentCount_)
        return "RHI-BINDLESS-GRAPHICS-PASS-OPERANDS";
    for (std::uint32_t i = 0u; i < attachmentCount_; ++i) {
        auto& a = attachments_[i];
        const auto index = usage.references()[command.firstReference + i].sourceIndex;
        const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
            &closure_->inventory_->sources()[index]);
        if (!view || !*view)
            return "RHI-BINDLESS-GRAPHICS-PASS-VIEW-SOURCE";
        a.source = *view;
        a.parent = metalRhiNativePayloadFor(*a.source->originalParentLease()->originalObject());
        a.view = metalRhiNativePayloadFor(*a.source->originalObject());
        if (!a.parent || !a.view)
            return "RHI-BINDLESS-GRAPHICS-PASS-NATIVE-CORRESPONDENCE";
        a.parentIdentity = MetalRhiNativeResourceObservation(a.parent->resource);
        a.viewIdentity = MetalRhiNativeResourceObservation(a.view->resource);
        a.generation = a.source->originalParentLease()
                           ->originalObject()
                           ->originalResourceRegistration()
                           .generation();
    }
    const auto* counts = transaction_->originalMixedContentCounts();
    if (const auto* depth = transaction_->originalGraphicsDepthContentCounts())
        counts = &depth->graphics;
    if (!counts
        || (budget_.contractVersion == 2u ? counts->attachmentControls < attachmentCount_
                                          : counts->attachmentControls != attachmentCount_))
        return "RHI-BINDLESS-GRAPHICS-PASS-CONTENT-CORRESPONDENCE";
    return validateAttachments();
}

const char* MetalOriginalBindlessGraphicsPass::validateAttachments(bool pending) const noexcept
{
    const auto* desc = descriptor();
    if (!desc || !registry_)
        return "RHI-BINDLESS-GRAPHICS-PASS-DEFINITION";
    Extent3D extent;
    for (std::uint32_t i = 0u; i < attachmentCount_; ++i) {
        const auto& a = attachments_[i];
        const bool depth = i == desc->colorAttachments.size();
        const auto target =
            depth ? desc->depthStencilAttachment.target : desc->colorAttachments[i].target;
        const auto format =
            depth ? desc->depthStencilAttachment.format : desc->colorAttachments[i].format;
        const auto& object = *a.source->originalParentLease()->originalObject();
        const auto& d = a.source->originalObject()->desc();
        const auto content = pending
            ? std::optional<GpuTexture::ContentRangeExpectation>{}
            : object.observeOriginalContent(defaultTextureSubresourceRange(object.desc()));
        if (a.source->resource() != target || object.desc().format != format || d.format != format
            || (!pending
                && (!content
                    || OriginalBindlessAttachmentClearContract::validateObservation(
                        budget_.attachmentClear,
                        content->controlVersion(),
                        content->state()
                            == common::OriginalTextureContentControl::State::Uninitialized,
                        content->state() == common::OriginalTextureContentControl::State::Completed,
                        content->contentGeneration())))
            || d.dimension != TextureViewDimension::Texture2D
            || d.usage != TextureUsage::RenderAttachment
            || d.aspect != (depth ? TextureAspect::Depth : TextureAspect::Color)
            || object.desc().extent.depth != 1u || object.desc().mipLevelCount != 1u
            || object.desc().arrayLayerCount != 1u || d.baseMipLevel || d.mipLevelCount != 1u
            || d.baseArrayLayer || d.arrayLayerCount != 1u)
            return "RHI-BINDLESS-GRAPHICS-PASS-VIEW-CORRESPONDENCE";
        const auto actual = registry_->find(target);
        const auto parent = registry_->find(object.id());
        if (!actual || !actual->parentTextureLive || actual->parentTexture != object.id() || !parent
            || !core::SharedOwnerProjection::sameOwner(parent->texture, a.parent)
            || !core::SharedOwnerProjection::sameOwner(actual->textureView, a.view)
            || actual->textureViewWrapper.get() != a.source->originalObject()
            || parent->textureWrapper.get() != &object || actual->gpuWritesSealed
            || actual->gpuTransferWritesSealed || !a.parent->originalContentExclusive()
            || !a.view->originalContentExclusive() || a.parent->aliasHeap
            || a.parent->immutableRevision() || a.view->immutableRevision()
            || !a.parentIdentity.matches(a.parent->resource)
            || !a.viewIdentity.matches(a.view->resource)
            || !core::SharedOwnerProjection::sameOwner(a.parent, metalRhiNativePayloadFor(object))
            || !core::SharedOwnerProjection::sameOwner(
                a.view, metalRhiNativePayloadFor(*a.source->originalObject()))
            || !core::SharedOwnerProjection::sameOwner(
                a.parent->resource.nativeDevice, closure_->pipeline_->nativeDevice)
            || !core::SharedOwnerProjection::sameOwner(
                a.view->resource.nativeDevice, closure_->pipeline_->nativeDevice)
            || a.generation != object.originalResourceRegistration().generation())
            return "RHI-BINDLESS-GRAPHICS-PASS-NATIVE-CORRESPONDENCE";
        if (const auto* id =
                validateMetalRhiTrackedNativeTextureObject(a.parent->resource, object.desc()))
            return id;
        if (!i)
            extent = object.desc().extent;
        if (!extent.width || !extent.height || extent.width != object.desc().extent.width
            || extent.height != object.desc().extent.height)
            return "RHI-BINDLESS-GRAPHICS-PASS-EXTENT";
        for (std::uint32_t j = 0u; j < i; ++j)
            if (attachments_[j].parent->resource.nativeObject == a.parent->resource.nativeObject)
                return "RHI-BINDLESS-GRAPHICS-PASS-ATTACHMENT-ALIAS";
    }
    return nullptr;
}

const char* MetalOriginalBindlessGraphicsPass::inspectOwners(std::uint32_t& count) const noexcept
{
    const auto references = 7u + attachmentCount_ * 9u;
    const auto projection = core::SharedIdentityProjection::inspect(references,
        references,
        references,
        [&](std::size_t i) -> core::SharedIdentityProjection::Record {
            std::shared_ptr<const void> owner;
            if (i == 0u)
                owner = closure_;
            else if (i == 1u)
                owner = transaction_;
            else if (i == 2u)
                owner = stream_;
            else if (i == 3u)
                owner = originalMetadataQuota();
            else if (i == 4u)
                owner = capacity_;
            else if (i == 5u)
                owner = registry_;
            else if (i == 6u)
                owner = native_.descriptor;
            else {
                const auto& a = attachments_[(i - 7u) / 9u];
                switch ((i - 7u) % 9u) {
                case 0u:
                    owner = a.source;
                    break;
                case 1u:
                    owner = a.parent;
                    break;
                case 2u:
                    owner = a.view;
                    break;
                case 3u:
                    owner = a.view->resource.nativeDevice;
                    break;
                case 4u:
                    owner = a.view->resource.nativeObject;
                    break;
                case 5u:
                    owner = a.view->resource.allocationReleaseToken;
                    break;
                case 6u:
                    owner = a.parent->resource.nativeDevice;
                    break;
                case 7u:
                    owner = a.parent->resource.nativeObject;
                    break;
                case 8u:
                    owner = a.parent->resource.allocationReleaseToken;
                    break;
                }
            }
            if (!owner)
                return {};
            return {static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(owner.get())),
                1u,
                owner,
                owner.get(),
                1u};
        });
    if (projection.status != core::SharedIdentityProjection::Status::Complete)
        return "RHI-BINDLESS-GRAPHICS-PASS-OWNER-IDENTITY";
    // Reserve the one additional native descriptor owner before allocating it.
    count = projection.identities + 2u + (native_.descriptor ? 0u : 1u);
    return Contract::validateCounts(budget_, attachmentCount_, nativeObjectCount(), count);
}

std::shared_ptr<const MetalOriginalBindlessGraphicsPass>
MetalOriginalBindlessGraphicsPass::prepareOriginal(std::shared_ptr<const Closure> closure,
    std::shared_ptr<const Transaction> transaction,
    std::shared_ptr<const Stream> stream,
    GpuDevice& device,
    const Budget& budget,
    std::shared_ptr<core::AtomicCapacityBudget> capacity,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    const auto reject =
        [&](const char* id) -> std::shared_ptr<const MetalOriginalBindlessGraphicsPass> {
        diagnostic = id;
        return {};
    };
    diagnostic = Contract::validateBudget(budget);
    if (diagnostic)
        return {};
    if ((diagnostic = Closure::validateOriginal(closure))
        || (diagnostic = Transaction::validateOriginal(transaction))
        || (diagnostic = Stream::validateOriginal(stream)))
        return {};
    if (device.backend() != Backend::Metal
        || !Contract::lowering(device.backend(), budget.contractVersion).preparationAvailable)
        return reject("RHI-BINDLESS-GRAPHICS-PASS-BACKEND");
    if (budget.attachmentClear.contractVersion)
        return reject("RHI-BINDLESS-GRAPHICS-PASS-METAL-ATTACHMENT-CLEAR-UNAVAILABLE");
    const auto* intent = transaction->originalBindlessAttachmentClearIntent();
    if (!intent || !OriginalBindlessAttachmentClearContract::same(*intent, budget.attachmentClear))
        return reject("RHI-BINDLESS-GRAPHICS-PASS-ATTACHMENT-INTENT");
    const bool ordinaryDepth = closure->closureContractVersion() == 16u;
    if (transaction->originalContentPlanContractVersion() != (ordinaryDepth ? 18u : 9u)
        || (budget.contractVersion == 2u
                ? !transaction->hasOriginalBindlessProducedGeometryProfile()
                : transaction->hasOriginalBindlessProducedGeometryProfile()
                    || transaction->hasOriginalBindlessGraphicsGeometryProfile()
                        != (closure->closureContractVersion() == 3u))
        || !std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            closure->root_->originalPrepared())
        || !core::SharedOwnerProjection::sameOwner(closure->inventory_,
            stream->originalInputs()->originalScalars()->originalUsage()->originalSources()))
        return reject("RHI-BINDLESS-GRAPHICS-PASS-ROOT-CORRESPONDENCE");
    if ((diagnostic = transaction->compareOriginalGraph(transaction->originalGraph(), device))
        || (diagnostic = transaction->compareOriginalBindlessGraphicsStream(stream))
        || (diagnostic = stream->compareOriginalStates(transaction->originalGraph())))
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
        return reject("RHI-BINDLESS-GRAPHICS-PASS-QUOTA");
    if (transaction->overlapsCpuOwner(quota)
        || core::SharedOwnerProjection::overlapsOwner(quota, stream->originalMetadataQuota())
        || core::SharedOwnerProjection::overlapsOwner(quota, closure->originalMetadataQuota()))
        return reject("RHI-BINDLESS-GRAPHICS-PASS-QUOTA-DOMAIN");
    if (!capacity || capacity->capacity() != budget.maxNativeObjects)
        return reject("RHI-BINDLESS-GRAPHICS-PASS-PHYSICAL-BUDGET");
    auto registry = metalRhiIssuingResourceRegistry(device);
    if (!registry)
        return reject("RHI-BINDLESS-GRAPHICS-PASS-NATIVE-REGISTRY");
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result =
            shareOriginal(core::makeUnique<MetalOriginalBindlessGraphicsPass>(ConstructionKey{},
                std::move(closure),
                std::move(transaction),
                std::move(stream),
                budget,
                std::move(capacity),
                std::move(registry)));
        if ((diagnostic = result->captureAttachments())
            || (diagnostic = result->inspectOwners(result->owners_)))
            return {};
        if (result->nativeObjectCount()
            > result->closure_->inventory_->originalBudget().maxNativePayloadObjects)
            return reject("RHI-BINDLESS-GRAPHICS-PASS-SOURCE-NATIVE-CAPACITY");
        result->lease_ = core::makeUnique<core::AtomicCapacityLease>(
            result->capacity_, result->nativeObjectCount());
        if (!result->lease_->charged())
            return reject("RHI-BINDLESS-GRAPHICS-PASS-PHYSICAL-CAPACITY");
        core::pmr::String message;
        if (!result->createNativeObjects(message))
            return reject("RHI-BINDLESS-GRAPHICS-PASS-NATIVE-CREATION");
        if ((diagnostic = result->validate()))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        return reject("RHI-BINDLESS-GRAPHICS-PASS-ALLOCATION");
    }
}

const char* MetalOriginalBindlessGraphicsPass::validateOriginal(
    const std::shared_ptr<const MetalOriginalBindlessGraphicsPass>& self) noexcept
{
    return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-BINDLESS-GRAPHICS-PASS-OWNER"
                                                          : self->validate();
}

const char* MetalOriginalBindlessGraphicsPass::validate() const noexcept
{
    if (const auto* id = Closure::validateOriginal(closure_))
        return id;
    if (const auto* id = transaction_->compareOriginalBindlessGraphicsStream(stream_))
        return id;
    if (const auto* id = validateAttachments())
        return id;
    return validateNativeOwners();
}

const char* MetalOriginalBindlessGraphicsPass::validateNativeOwners(
    const std::shared_ptr<const OriginalCommandRecordingSeal>& seal) const noexcept
{
    if (const auto* id = seal ? Closure::validateOriginalPending(closure_, seal)
                              : Closure::validateOriginal(closure_))
        return id;
    if (const auto* id = validateAttachments(true))
        return id;
    if (!lease_ || !lease_->charged() || lease_->units() != nativeObjectCount()
        || native_.attachmentCount != attachmentCount_)
        return "RHI-BINDLESS-GRAPHICS-PASS-NATIVE-OWNER";
    if (const auto* id = validateMetalRhiNativeRenderPassResources(
            native_, registry_, closure_->pipeline_->nativeDevice))
        return id;
    std::uint32_t owners = 0u;
    if (const auto* id = inspectOwners(owners))
        return id;
    return owners == owners_ ? nullptr : "RHI-BINDLESS-GRAPHICS-PASS-OWNER-CAPACITY";
}
} // namespace arp::rhi_metal
