#include "rhi/backends/metal/metal_original_texture_upload.h"

#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/frontend/transfer_upload_copy_mapping.h"

namespace arp::rhi_metal {
using namespace rhi;
using Contract = OriginalTextureUploadStagingContract;

MetalOriginalTextureUploadStaging::MetalOriginalTextureUploadStaging(ConstructionKey,
    std::shared_ptr<const OriginalCommandTextureUploadPackingSource> packing,
    std::shared_ptr<const OriginalCommandBufferLease> staging,
    std::shared_ptr<MetalRhiNativeBufferPayload> buffer,
    std::shared_ptr<MetalRhiNativeTexturePayload> image,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    const Budget& budget)
    : packing_(std::move(packing)), staging_(std::move(staging)), buffer_(std::move(buffer)),
      image_(std::move(image)), registry_(std::move(registry)),
      bufferObject_(buffer_->resource.nativeObject), imageObject_(image_->resource.nativeObject),
      device_(buffer_->resource.nativeDevice),
      bufferAllocation_(buffer_->resource.allocationReleaseToken),
      imageAllocation_(image_->resource.allocationReleaseToken), budget_(budget),
      bufferState_(*staging_->originalObject()->observeOriginalCommittedState()),
      textureState_(
          *packing_->originalDestinationLease()->originalObject()->observeOriginalCommittedState(
              defaultTextureSubresourceRange(
                  packing_->originalDestinationLease()->originalObject()->desc()))),
      content_(*packing_->originalDestinationLease()->originalObject()->observeOriginalContent())
{}

std::shared_ptr<const MetalOriginalTextureUploadStaging>
MetalOriginalTextureUploadStaging::prepareOriginal(
    std::shared_ptr<const OriginalCommandTextureUploadPackingSource> packing,
    GpuDevice& device,
    const Budget& b,
    const Quotas& q,
    const char*& diagnostic)
{
    const auto reject =
        [&](const char* id) -> std::shared_ptr<const MetalOriginalTextureUploadStaging> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = Contract::validateIntent(b))
        return reject(id);
    if (const auto* id = OriginalCommandTextureUploadPackingSource::validateOriginal(packing))
        return reject(id);
    const auto lowering = semantics::OriginalTextureUploadStagingLowering::lowering(
        device.backend(), b.contractVersion);
    if (!lowering.factoryAvailable || lowering.diagnostic)
        return reject(lowering.diagnostic);
    if (!q.metadata || !q.resource || !q.lease || q.metadata->limitBytes() != b.maxCpuMetadataBytes
        || q.resource->limitBytes() != b.maxCpuResourceBytes
        || q.lease->limitBytes() != b.maxCpuLeaseBytes)
        return reject("RHI-ORIGINAL-TEXTURE-STAGING-QUOTA");
    const std::array domains{q.metadata, q.resource, q.lease};
    if (!core::SharedOwnerProjection::hasIndependentOwners(domains.size(), [&](std::size_t i) {
            return domains[i];
        }))
        return reject("RHI-ORIGINAL-TEXTURE-STAGING-QUOTA-DOMAIN");
    // The adopted buffer lease is authored in the existing canonical Source
    // quota, exactly like the destination/upload Sources. Only staging metadata
    // and resource storage are independent from that already frozen closure.
    if (!core::SharedOwnerProjection::sameOwner(
            q.lease, packing->originalDestinationLease()->originalMetadataQuota()))
        return reject("RHI-ORIGINAL-TEXTURE-STAGING-SOURCE-QUOTA");
    for (const auto& qOwner : {q.metadata, q.resource})
        if (packing->overlapsCpuQuota(qOwner.get()) || device.overlapsOriginalDeviceCpuOwner(qOwner)
            || core::SharedOwnerProjection::sameOwner(qOwner,
                packing->originalDestinationLease()->originalObject()->originalContentCpuQuota()))
            return reject("RHI-ORIGINAL-TEXTURE-STAGING-QUOTA-DOMAIN");
    const auto* texture = packing->originalDestinationLease()->originalObject();
    if (const auto* id = Contract::validateDestination(texture->desc(), b.contractVersion))
        return reject(id);
    if (const auto* id = device.validateOriginalResourceRegistration(*texture))
        return reject(id);
    const auto state =
        texture->observeOriginalCommittedState(defaultTextureSubresourceRange(texture->desc()));
    const auto content = texture->observeOriginalContent();
    if (!texture->originalContentExclusive() || !state || state->state() != ResourceState::Undefined
        || !content
        || content->state() != common::OriginalTextureContentControl::State::Uninitialized
        || content->contentGeneration())
        return reject("RHI-ORIGINAL-TEXTURE-STAGING-FRESH-CONTENT");
    const auto& packed = packing->packing()->packedBytes();
    if (packed.size() > b.maxLogicalStagingBytes
        || packing->packing()->mipLayouts().size() > b.maxMipMappings)
        return reject("RHI-ORIGINAL-TEXTURE-STAGING-BYTE-CAPACITY");
    auto registry = metalRhiIssuingResourceRegistry(device);
    auto image = metalRhiNativePayloadFor(*texture);
    if (!registry || !image || !image->originalContentExclusive() || image->aliasHeap
        || !image->resource.allocationReleaseToken)
        return reject("RHI-METAL-TEXTURE-STAGING-DEVICE");
    if (const auto* id =
            validateMetalRhiTrackedNativeTextureObject(image->resource, texture->desc()))
        return reject(id);
    if (b.maxRetainedOwners < retainedOwnerBound)
        return reject("RHI-METAL-TEXTURE-STAGING-OWNER-CAPACITY");
    try {
        core::UniquePtr<GpuBuffer> object;
        {
            core::CpuMemoryQuotaScope scope(q.resource);
            BufferDesc d;
            d.sizeBytes = packed.size();
            d.memory = MemoryUsage::CpuToGpu;
            d.usage = ResourceUsage::CopySrc;
            d.initialState = ResourceState::CopySrc;
            object = device.createBuffer(d);
        }
        if (!object)
            return reject("RHI-METAL-TEXTURE-STAGING-CREATE");
        if (!object->allocation().estimatedSizeBytes
            || object->allocation().estimatedSizeBytes > b.maxNativeStagingBytes)
            return reject("RHI-ORIGINAL-TEXTURE-STAGING-BYTE-CAPACITY");
        if (const auto* id = device.validateOriginalResourceRegistration(*object))
            return reject(id);
        const auto buffer = metalRhiNativePayloadFor(*object);
        if (!buffer
            || !core::SharedOwnerProjection::sameOwner(
                buffer->resource.nativeDevice, image->resource.nativeDevice)
            || !buffer->resource.allocationReleaseToken
            || validateMetalRhiTrackedNativeBufferObject(
                buffer->resource, object->desc().sizeBytes))
            return reject("RHI-METAL-TEXTURE-STAGING-BUFFER");
        // writeBuffer performs the actual shared write or managed flush. No
        // source-created initialized flag or unflushed CPU shadow is admitted.
        const auto written = device.writeBuffer(*object, packed.data(), packed.size());
        if (!written.success)
            return reject("RHI-METAL-TEXTURE-STAGING-WRITE-FLUSH");
        object->sealWrites();
        if (const auto* id = Contract::validateBuffer(*object, packed.size(), b))
            return reject(id);
        if (!object->observeOriginalCommittedState())
            return reject("RHI-METAL-TEXTURE-STAGING-STATE");
        auto lease = OriginalCommandBufferLease::prepareOriginal(object,
            {1u, true, OriginalCommandBufferLease::retainedOwnerCount, b.maxCpuLeaseBytes},
            q.lease,
            diagnostic);
        if (!lease)
            return {};
        core::CpuMemoryQuotaScope scope(q.metadata);
        auto result =
            shareOriginal(core::makeUnique<MetalOriginalTextureUploadStaging>(ConstructionKey{},
                std::move(packing),
                std::move(lease),
                buffer,
                std::move(image),
                std::move(registry),
                b));
        diagnostic = result->validate();
        return diagnostic ? std::shared_ptr<const MetalOriginalTextureUploadStaging>{} : result;
    } catch (const std::bad_alloc&) {
        return reject("RHI-METAL-TEXTURE-STAGING-ALLOCATION");
    }
}

const char* MetalOriginalTextureUploadStaging::validateOriginal(
    const std::shared_ptr<const MetalOriginalTextureUploadStaging>& self) noexcept
{
    return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-METAL-TEXTURE-STAGING-OWNER"
                                                          : self->validate();
}

const char* MetalOriginalTextureUploadStaging::validateIdentity() const noexcept
{
    if (const auto* id = Contract::validateIntent(budget_))
        return id;
    if (!originalMetadataQuota()
        || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes)
        return "RHI-METAL-TEXTURE-STAGING-QUOTA";
    if (const auto* id = OriginalCommandTextureUploadPackingSource::validateOriginal(packing_))
        return id;
    if (const auto* id = OriginalCommandBufferLease::validateOriginal(staging_))
        return id;
    const auto* texture = packing_->originalDestinationLease()->originalObject();
    const auto* buffer = staging_->originalObject();
    if (const auto* id = Contract::validateDestination(texture->desc(), budget_.contractVersion))
        return id;
    if (const auto* id = texture->originalResourceRegistration().diagnostic())
        return id;
    if (const auto* id = buffer->originalResourceRegistration().diagnostic())
        return id;
    if (const auto* id =
            Contract::validateBuffer(*buffer, packing_->packing()->packedBytes().size(), budget_))
        return id;
    const auto registeredBuffer = registry_->find(buffer->id());
    const auto registeredImage = registry_->find(texture->id());
    if (!registeredBuffer || !registeredImage
        || !core::SharedOwnerProjection::sameOwner(registeredBuffer->buffer, buffer_)
        || !core::SharedOwnerProjection::sameOwner(registeredImage->texture, image_)
        || !core::SharedOwnerProjection::sameOwner(buffer_, metalRhiNativePayloadFor(*buffer))
        || !core::SharedOwnerProjection::sameOwner(image_, metalRhiNativePayloadFor(*texture))
        || !core::SharedOwnerProjection::sameOwner(buffer_->resource.nativeObject, bufferObject_)
        || !core::SharedOwnerProjection::sameOwner(image_->resource.nativeObject, imageObject_)
        || !core::SharedOwnerProjection::sameOwner(buffer_->resource.nativeDevice, device_)
        || !core::SharedOwnerProjection::sameOwner(image_->resource.nativeDevice, device_)
        || !core::SharedOwnerProjection::sameOwner(
            buffer_->resource.allocationReleaseToken, bufferAllocation_)
        || !core::SharedOwnerProjection::sameOwner(
            image_->resource.allocationReleaseToken, imageAllocation_)
        || !image_->originalContentExclusive() || image_->aliasHeap)
        return "RHI-METAL-TEXTURE-STAGING-NATIVE-IDENTITY";
    if (const auto* id =
            validateMetalRhiTrackedNativeBufferObject(buffer_->resource, buffer->desc().sizeBytes))
        return id;
    if (const auto* id =
            validateMetalRhiTrackedNativeTextureObject(image_->resource, texture->desc()))
        return id;
    return nullptr;
}

const char* MetalOriginalTextureUploadStaging::validate() const noexcept
{
    if (const auto* id = validateIdentity())
        return id;
    if (const auto* id = staging_->originalObject()->validateOriginalCommittedState(bufferState_))
        return id;
    const auto* texture = packing_->originalDestinationLease()->originalObject();
    if (const auto* id = texture->validateOriginalCommittedState(textureState_))
        return id;
    return texture->validateOriginalContent(content_);
}

MetalOriginalTextureUploadCommand::MetalOriginalTextureUploadCommand(ConstructionKey,
    std::shared_ptr<const Reservation> reservation,
    std::shared_ptr<const Program> program,
    std::shared_ptr<const Staging> staging)
    : reservation_(std::move(reservation)), program_(std::move(program)),
      staging_(std::move(staging))
{}

MetalOriginalTextureUploadCommand::~MetalOriginalTextureUploadCommand()
{
    if (phase_ == Life::Phase::Claimed || phase_ == Life::Phase::RecordingPending
        || phase_ == Life::Phase::Recorded || phase_ == Life::Phase::SubmitReserved)
        reservation_->originalSeal()->rollbackOriginalRecordingPlan();
    else if (phase_ != Life::Phase::Completed && phase_ != Life::Phase::Abandoned)
        reservation_->originalSeal()->invalidateOriginalOwnership();
}

const char* MetalOriginalTextureUploadCommand::advance(Life::Event event) noexcept
{
    const auto transition = Life::inspectTransition(phase_, event);
    if (!transition.accepted())
        return transition.diagnostic;
    phase_ = transition.next;
    return nullptr;
}

const char* MetalOriginalTextureUploadCommand::validatePending() const noexcept
{
    if (const auto* id = reservation_->validateRetainedOwnership())
        return id;
    if (const auto* id = staging_->validateIdentity())
        return id;
    const auto& seal = reservation_->originalSeal();
    if (const auto* id = seal->validateInitializedBufferPending(
            *staging_->staging_->originalObject(), staging_->bufferState_))
        return id;
    return seal->validateInitializedTexturePending(3u,
        *staging_->packing_->originalDestinationLease()->originalObject(),
        staging_->textureState_,
        staging_->content_);
}

bool MetalOriginalTextureUploadCommand::beforeSubmit(
    const MetalRhiNativeCommandPayload& command, core::pmr::String& diagnostic) noexcept
{
    const char* id = command_ == &command ? validatePending() : "RHI-METAL-UPLOAD-COMMAND-IDENTITY";
    if (!id)
        id = advance(Life::Event::ReserveSubmission);
    if (id) {
        diagnostic = id;
        return false;
    }
    return true;
}

void MetalOriginalTextureUploadCommand::abandonUnsubmitted() noexcept
{
    if (phase_ == Life::Phase::SubmitPending || phase_ == Life::Phase::Completed
        || phase_ == Life::Phase::QuarantinedAfterSubmit)
        return;
    (void)advance(Life::Event::Cancel);
    if (phase_ == Life::Phase::QuarantinedBeforeSubmit)
        (void)advance(Life::Event::BeforeSubmitScopesDrained);
    reservation_->originalSeal()->rollbackOriginalRecordingPlan();
}

bool MetalOriginalTextureUploadCommand::nativeCompleted(bool success) noexcept
{
    if (phase_ != Life::Phase::SubmitPending)
        return false;
    const auto& seal = reservation_->originalSeal();
    if (success && !validatePending() && !seal->commitOriginalSubmission(4u)
        && !advance(Life::Event::CompletionProven))
        return true;
    (void)advance(Life::Event::CompletionUnproven);
    seal->invalidateOriginalOwnership();
    (void)advance(Life::Event::SubmittedQueueAndCallbacksDrained);
    return false;
}
} // namespace arp::rhi_metal
