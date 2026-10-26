#include "rhi/backends/vulkan/vulkan_original_texture_upload.h"

#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_bindless.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/frontend/transfer_upload_copy_mapping.h"

namespace arp::rhi_vulkan {
using namespace rhi;
using Contract = OriginalTextureUploadStagingContract;

const char* VulkanOriginalTextureUploadStaging::inspectDestination(
    const std::shared_ptr<const OriginalCommandTextureUploadPackingSource>& packing,
    GpuDevice& device,
    const VulkanRhiRuntime& runtime) noexcept
{
    if (const auto* id = OriginalCommandTextureUploadPackingSource::validateOriginal(packing))
        return id;
    const auto* texture = packing->originalDestinationLease()->originalObject();
    if (const auto* id = device.validateOriginalResourceRegistration(*texture))
        return id;
    auto* extension = dynamic_cast<VulkanBindlessCrossExtension*>(
        static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross)));
    if (device.backend() != Backend::Vulkan || !extension || !extension->nativeResources_
        || extension->runtime_ != &runtime
        || extension->nativeResources_->runtime().get() != &runtime || !runtime.valid()
        || runtime.syntheticRuntime())
        return "RHI-VULKAN-TEXTURE-STAGING-DEVICE";
    const auto image = vulkanRhiNativePayload(*texture);
    return image && image->backendVulkanImageHandle()
            && image->backendVulkanDeviceHandle() == runtime.logicalDevice()
        ? nullptr
        : "RHI-VULKAN-TEXTURE-STAGING-DEVICE";
}

VulkanOriginalTextureUploadStaging::VulkanOriginalTextureUploadStaging(ConstructionKey,
    std::shared_ptr<const OriginalCommandTextureUploadPackingSource> packing,
    std::shared_ptr<const OriginalCommandBufferLease> staging,
    std::shared_ptr<VulkanRhiNativeResourcePayload> buffer,
    std::shared_ptr<VulkanRhiNativeResourcePayload> image,
    std::shared_ptr<VulkanRhiRuntime> runtime,
    const Budget& budget)
    : packing_(std::move(packing)), staging_(std::move(staging)), buffer_(std::move(buffer)),
      image_(std::move(image)), runtime_(std::move(runtime)), budget_(budget),
      bufferState_(*staging_->originalObject()->observeOriginalCommittedState()),
      textureState_(
          *packing_->originalDestinationLease()->originalObject()->observeOriginalCommittedState(
              defaultTextureSubresourceRange(
                  packing_->originalDestinationLease()->originalObject()->desc()))),
      content_(*packing_->originalDestinationLease()->originalObject()->observeOriginalContent())
{}

std::shared_ptr<const VulkanOriginalTextureUploadStaging>
VulkanOriginalTextureUploadStaging::prepareOriginal(
    std::shared_ptr<const OriginalCommandTextureUploadPackingSource> packing,
    GpuDevice& device,
    const Budget& b,
    const Quotas& q,
    const char*& diagnostic)
{
    const auto reject =
        [&](const char* id) -> std::shared_ptr<const VulkanOriginalTextureUploadStaging> {
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
    auto* extension = dynamic_cast<VulkanBindlessCrossExtension*>(
        static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross)));
    if (!extension || !extension->runtime_ || !extension->nativeResources_)
        return reject("RHI-VULKAN-TEXTURE-STAGING-DEVICE");
    auto runtime = extension->nativeResources_->runtime();
    if (!runtime)
        return reject("RHI-VULKAN-TEXTURE-STAGING-DEVICE");
    if (const auto* id = inspectDestination(packing, device, *runtime))
        return reject(id);
    auto image = vulkanRhiNativePayload(*texture);
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
            return reject("RHI-VULKAN-TEXTURE-STAGING-CREATE");
        if (!object->allocation().estimatedSizeBytes
            || object->allocation().estimatedSizeBytes > b.maxNativeStagingBytes)
            return reject("RHI-ORIGINAL-TEXTURE-STAGING-BYTE-CAPACITY");
        if (const auto* id = device.validateOriginalResourceRegistration(*object))
            return reject(id);
        const auto buffer = vulkanRhiNativePayload(*object);
        if (!buffer || !buffer->backendVulkanBufferHandle()
            || buffer->backendVulkanDeviceHandle() != runtime->logicalDevice())
            return reject("RHI-VULKAN-TEXTURE-STAGING-BUFFER");
        // writeBuffer performs the actual mapped write and native flush. No
        // source-created initialized flag or unflushed CPU shadow is admitted.
        const auto written = device.writeBuffer(*object, packed.data(), packed.size());
        if (!written.success)
            return reject("RHI-VULKAN-TEXTURE-STAGING-WRITE-FLUSH");
        object->sealWrites();
        if (const auto* id = Contract::validateBuffer(*object, packed.size(), b))
            return reject(id);
        if (!object->observeOriginalCommittedState())
            return reject("RHI-VULKAN-TEXTURE-STAGING-STATE");
        auto lease = OriginalCommandBufferLease::prepareOriginal(object,
            {1u, true, OriginalCommandBufferLease::retainedOwnerCount, b.maxCpuLeaseBytes},
            q.lease,
            diagnostic);
        if (!lease)
            return {};
        core::CpuMemoryQuotaScope scope(q.metadata);
        auto result =
            shareOriginal(core::makeUnique<VulkanOriginalTextureUploadStaging>(ConstructionKey{},
                std::move(packing),
                std::move(lease),
                buffer,
                std::move(image),
                std::move(runtime),
                b));
        diagnostic = result->validate();
        return diagnostic ? std::shared_ptr<const VulkanOriginalTextureUploadStaging>{} : result;
    } catch (const std::bad_alloc&) {
        return reject("RHI-VULKAN-TEXTURE-STAGING-ALLOCATION");
    }
}

const char* VulkanOriginalTextureUploadStaging::validateOriginal(
    const std::shared_ptr<const VulkanOriginalTextureUploadStaging>& self) noexcept
{
    return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-VULKAN-TEXTURE-STAGING-OWNER"
                                                          : self->validate();
}

const char* VulkanOriginalTextureUploadStaging::validateIdentity() const noexcept
{
    if (const auto* id = Contract::validateIntent(budget_))
        return id;
    if (!originalMetadataQuota()
        || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes)
        return "RHI-VULKAN-TEXTURE-STAGING-QUOTA";
    if (const auto* id = OriginalCommandTextureUploadPackingSource::validateOriginal(packing_))
        return id;
    if (const auto* id = OriginalCommandBufferLease::validateOriginal(staging_))
        return id;
    const auto* texture = packing_->originalDestinationLease()->originalObject();
    const auto* buffer = staging_->originalObject();
    if (const auto* id = texture->originalResourceRegistration().diagnostic())
        return id;
    if (const auto* id = buffer->originalResourceRegistration().diagnostic())
        return id;
    if (const auto* id =
            Contract::validateBuffer(*buffer, packing_->packing()->packedBytes().size(), budget_))
        return id;
    if (!runtime_->valid() || runtime_->syntheticRuntime()
        || !core::SharedOwnerProjection::sameOwner(buffer_, vulkanRhiNativePayload(*buffer))
        || !core::SharedOwnerProjection::sameOwner(image_, vulkanRhiNativePayload(*texture))
        || buffer_->backendVulkanDeviceHandle() != runtime_->logicalDevice()
        || image_->backendVulkanDeviceHandle() != runtime_->logicalDevice()
        || !buffer_->backendVulkanBufferHandle() || !image_->backendVulkanImageHandle())
        return "RHI-VULKAN-TEXTURE-STAGING-NATIVE-IDENTITY";
    return nullptr;
}

const char* VulkanOriginalTextureUploadStaging::validate() const noexcept
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

VulkanOriginalTextureUploadCommand::VulkanOriginalTextureUploadCommand(ConstructionKey,
    std::shared_ptr<const Reservation> reservation,
    std::shared_ptr<const Program> program,
    std::shared_ptr<const Staging> staging,
    std::size_t upload)
    : reservation_(std::move(reservation)), program_(std::move(program)),
      staging_(std::move(staging)), uploadCommand_(upload)
{}

VulkanOriginalTextureUploadCommand::~VulkanOriginalTextureUploadCommand()
{
    if (phase_ == Life::Phase::Claimed || phase_ == Life::Phase::RecordingPending
        || phase_ == Life::Phase::Recorded || phase_ == Life::Phase::SubmitReserved)
        reservation_->originalSeal()->rollbackOriginalRecordingPlan();
    else if (phase_ != Life::Phase::Completed)
        reservation_->originalSeal()->invalidateOriginalOwnership();
}

const char* VulkanOriginalTextureUploadCommand::advance(Life::Event event) noexcept
{
    const auto t = Life::inspectTransition(phase_, event);
    if (!t.accepted())
        return t.diagnostic;
    phase_ = t.next;
    return nullptr;
}

std::shared_ptr<VulkanRhiNativeCommandPayload> VulkanOriginalTextureUploadCommand::prepareOriginal(
    std::shared_ptr<const Reservation> reservation,
    std::shared_ptr<const Program> program,
    std::shared_ptr<const Staging> staging,
    GpuDevice& device,
    core::pmr::String& diagnostic)
{
    const auto reject = [&](const char* id) -> std::shared_ptr<VulkanRhiNativeCommandPayload> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = Reservation::validateOriginal(reservation))
        return reject(id);
    if (const auto* id = Program::validateOriginal(program))
        return reject(id);
    if (const auto* id = Staging::validateOriginal(staging))
        return reject(id);
    const auto& seal = reservation->originalSeal();
    const auto& root = seal->originalRoot();
    const auto& graph = root->originalGraph();
    const auto& inputs = program->originalInputs();
    const auto& usage = *inputs->originalScalars()->originalUsage();
    const auto& inventory = usage.originalSources();
    const auto& packing = staging->originalPacking();
    const auto lowering =
        semantics::OriginalCommandNativeLifecycleLowering::lowering(device.backend(), 7u);
    if (lowering.diagnostic || !lowering.sealedIssuerAvailable || !lowering.executionAvailable)
        return reject(lowering.diagnostic);
    if (seal->originalBudget().contractVersion != 7u
        || reservation->originalRequest().contractVersion != 3u
        || program->originalBudget().contractVersion != 1u
        || !program->originalBudget().bindlessRoots.empty() || seal->nativeColdCommandClaimed_
        || !core::SharedOwnerProjection::sameOwner(inputs, graph->originalInputs()))
        return reject("RHI-VULKAN-UPLOAD-FENCE-COLD-CORRESPONDENCE");
    if (const auto* id = root->compareOriginalGraph(graph, device))
        return reject(id);
    const auto stagingIndex = inventory->originalLeaseIndex(staging->originalStagingLease());
    const auto textureIndex = inventory->originalLeaseIndex(packing->originalDestinationLease());
    const auto uploadIndex = inventory->originalLeaseIndex(packing->originalSource());
    if (stagingIndex == SIZE_MAX || textureIndex == SIZE_MAX || uploadIndex == SIZE_MAX
        || inventory->sources().size() != 3u)
        return reject("RHI-VULKAN-UPLOAD-FENCE-INVENTORY");
    // Exact whole-upload profile. No uninspected operation is silently ignored.
    constexpr std::array kinds{CommandUsageKind::BufferMemoryBarrier,
        CommandUsageKind::TextureBarrier,
        CommandUsageKind::BeginTransferPass,
        CommandUsageKind::TextureUpload,
        CommandUsageKind::EndTransferPass,
        CommandUsageKind::Finish};
    if (program->operations().size() != kinds.size())
        return reject("RHI-VULKAN-UPLOAD-FENCE-OPERATIONS");
    for (std::size_t i = 0u; i < kinds.size(); ++i) {
        const auto& op = program->operations()[i];
        if (op.kind != kinds[i] || op.commandIndex != i)
            return reject("RHI-VULKAN-UPLOAD-FENCE-OPERATIONS");
        const auto expected = i < 2u ? 1u : i == 3u ? 2u : 0u;
        if (op.referenceCount != expected)
            return reject("RHI-VULKAN-UPLOAD-FENCE-REFERENCE");
        if (i < 2u || i == 3u) {
            if (usage.references()[op.firstReference].sourceIndex
                    != (i == 0u ? stagingIndex : textureIndex)
                || (i == 3u
                    && usage.references()[op.firstReference + 1u].sourceIndex != uploadIndex))
                return reject("RHI-VULKAN-UPLOAD-FENCE-REFERENCE");
        }
    }
    ResourceMemoryBarrierDesc dependency;
    if (!std::get<ResourceDependencyArguments>(*inputs->descriptorAt(0u))
            .write(CommandUsageKind::BufferMemoryBarrier, dependency)
        || dependency.resource != staging->originalStagingLease()->resource()
        || dependency.sourceStages != PipelineStage::Transfer
        || dependency.destinationStages != PipelineStage::Transfer
        || dependency.sourceAccess != ResourceAccess::TransferRead
        || dependency.destinationAccess != ResourceAccess::TransferRead)
        return reject("RHI-VULKAN-UPLOAD-FENCE-STAGING-DEPENDENCY");
    if (const auto* id = Contract::validateExecutionBudget(inventory->originalBudget(),
            packing->packing()->mipLayouts().size(),
            staging->nativeStagingBytes()))
        return reject(id);
    const auto& quota = seal->originalSubmissionQuota();
    const auto owners = std::uint64_t(reservation->retainedOwnerCount())
        + program->retainedOwnerCount() + staging->retainedOwnerCount() + 2u;
    if (owners > seal->originalBudget().maxSubmissionOwners)
        return reject("RHI-VULKAN-UPLOAD-FENCE-OWNER-CAPACITY");
    for (const auto& q : {quota, program->originalMetadataQuota()})
        if (packing->overlapsCpuQuota(q.get())
            || core::SharedOwnerProjection::sameOwner(q, staging->originalMetadataQuota())
            || core::SharedOwnerProjection::sameOwner(
                q, packing->originalDestinationLease()->originalObject()->originalContentCpuQuota())
            || core::SharedOwnerProjection::sameOwner(
                q, staging->originalStagingLease()->originalMetadataQuota())
            || core::inspectCpuAllocationCharge(staging->originalStagingLease()->originalObject())
                    .quota
                == q.get())
            return reject("RHI-VULKAN-UPLOAD-FENCE-QUOTA-DOMAIN");
    for (const auto& q : {inputs->originalMetadataQuota(),
             graph->originalMetadataQuota(),
             root->originalMetadataQuota()})
        if (core::SharedOwnerProjection::sameOwner(q, staging->originalMetadataQuota())
            || core::inspectCpuAllocationCharge(staging->originalStagingLease()->originalObject())
                    .quota
                == q.get())
            return reject("RHI-VULKAN-UPLOAD-FENCE-QUOTA-DOMAIN");
    auto* extension = dynamic_cast<VulkanBindlessCrossExtension*>(
        static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross)));
    if (!extension || extension->runtime_ != staging->runtime_.get()
        || !extension->nativeResources_)
        return reject("RHI-VULKAN-UPLOAD-FENCE-DEVICE");
    for (const char* name : {"vkCmdPipelineBarrier",
             "vkCmdCopyBufferToImage",
             "vkEndCommandBuffer",
             "vkQueueSubmit",
             "vkGetFenceStatus"})
        if (!staging->runtime_->getDeviceProcAddr(name))
            return reject("RHI-VULKAN-UPLOAD-FENCE-ENTRYPOINT");
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto command =
            createVulkanRhiNativeCommandPayload(*extension->nativeResources_, diagnostic);
        if (!command || !command->prepareOriginalSubmissionFence(diagnostic))
            return {};
        command->impl_->pendingBufferBarriers.reserve(1u);
        command->impl_->pendingMemoryBarriers.reserve(1u);
        command->impl_->pendingImageBarriers.reserve(1u);
        auto custody = shareOriginal(core::makeUnique<VulkanOriginalTextureUploadCommand>(
            ConstructionKey{}, reservation, std::move(program), std::move(staging), 3u));
        custody->command_ = command.get();
        seal->nativeColdCommandClaimed_ = true;
        command->impl_->originalBindlessCustody = custody;
        if (!custody->record(*command, diagnostic))
            return {};
        diagnostic.clear();
        return command;
    } catch (const std::bad_alloc&) {
        return reject("RHI-VULKAN-UPLOAD-FENCE-ALLOCATION");
    }
}

const char* VulkanOriginalTextureUploadCommand::validatePending() const noexcept
{
    if (const auto* id = reservation_->validateRetainedOwnership())
        return id;
    if (const auto* id = staging_->validateIdentity())
        return id;
    const auto& seal = reservation_->originalSeal();
    if (const auto* id = seal->validateInitializedBufferPending(
            *staging_->staging_->originalObject(), staging_->bufferState_))
        return id;
    return seal->validateInitializedTexturePending(uploadCommand_,
        *staging_->packing_->originalDestinationLease()->originalObject(),
        staging_->textureState_,
        staging_->content_);
}

bool VulkanOriginalTextureUploadCommand::record(
    VulkanRhiNativeCommandPayload& command, core::pmr::String& diagnostic)
{
    const auto fail = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    if (const auto* id = reservation_->originalSeal()->applyOriginalRecordingPlan())
        return fail(id);
    if (const auto* id = advance(Life::Event::BeginRecording))
        return fail(id);
    if (const auto* id = validatePending())
        return fail(id);
    const auto& inputs = program_->originalInputs();
    ResourceDependencyArguments bufferDep =
        std::get<ResourceDependencyArguments>(*inputs->descriptorAt(0u));
    ResourceMemoryBarrierDesc bufferBarrier;
    ResourceBarrierDesc imageBarrier;
    if (!bufferDep.write(CommandUsageKind::BufferMemoryBarrier, bufferBarrier)
        || !std::get<ResourceDependencyArguments>(*inputs->descriptorAt(1u))
            .write(CommandUsageKind::TextureBarrier, imageBarrier))
        return fail("RHI-VULKAN-UPLOAD-FENCE-DESCRIPTOR");
    if (!command.recordResourceMemoryBarrier(bufferBarrier, diagnostic)
        || !command.recordValidatedOriginalImageBarrier(*staging_->image_, imageBarrier, diagnostic)
        || !command.flushPendingBarriers(diagnostic))
        return false;
    const auto& packing = staging_->packing_->packing();
    for (std::size_t i = 0u; i < packing->mipLayouts().size(); ++i) {
        semantics::TextureUploadPackedCopy copy;
        if (const auto* id = semantics::TextureUploadPackedCopyMapping::mapOriginal(
                1u, true, packing, staging_->staging_->resource(), i, copy))
            return fail(id);
        UploadTextureDesc desc;
        desc.destinationTexture = copy.destinationTexture;
        desc.extent = copy.extent;
        desc.destinationOffset = copy.origin;
        desc.format = copy.format;
        desc.subresources = copy.subresources;
        desc.bytesPerRow = copy.bytesPerRow;
        desc.rowsPerImage = copy.rowsPerImage;
        desc.pixelDataSize = copy.packedFootprintBytes;
        if (!command.recordValidatedOriginalTextureCopy(
                staging_->buffer_->backendVulkanBufferHandle(),
                copy.bufferOffsetBytes,
                *staging_->image_,
                desc,
                diagnostic))
            return false;
    }
    if (!command.end(diagnostic))
        return false;
    if (const auto* id = advance(Life::Event::RecordingValidatedAndDrained))
        return fail(id);
    return true;
}

bool VulkanOriginalTextureUploadCommand::beforeSubmit(
    const VulkanRhiNativeCommandPayload& command, core::pmr::String& diagnostic) noexcept
{
    const char* id =
        command_ == &command ? validatePending() : "RHI-VULKAN-UPLOAD-FENCE-COMMAND-IDENTITY";
    if (!id)
        id = advance(Life::Event::ReserveSubmission);
    if (id) {
        diagnostic = id;
        return false;
    }
    return true;
}

void VulkanOriginalTextureUploadCommand::submissionUnproven(
    const VulkanRhiNativeCommandPayload& command) noexcept
{
    if (command_ != &command)
        return;
    advance(Life::Event::CompletionUnproven);
    reservation_->originalSeal()->invalidateOriginalOwnership();
}

void VulkanOriginalTextureUploadCommand::fenceCompleted(
    const VulkanRhiNativeCommandPayload& command) noexcept
{
    if (command_ != &command || command.impl_->device.syntheticRuntime()
        || !command.impl_->submitted || !command.impl_->submitFenceCompleted)
        return;
    if (phase_ == Life::Phase::QuarantinedAfterSubmit) {
        advance(Life::Event::SubmittedQueueAndCallbacksDrained);
        return;
    }
    if (phase_ != Life::Phase::SubmitPending)
        return;
    if (validatePending() || reservation_->originalSeal()->commitOriginalSubmission(4u)
        || advance(Life::Event::CompletionProven))
        submissionUnproven(command);
}
} // namespace arp::rhi_vulkan
