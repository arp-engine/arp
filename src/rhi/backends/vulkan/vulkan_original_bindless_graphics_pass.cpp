#include "rhi/backends/vulkan/vulkan_original_bindless_graphics_pass.h"

#include "core/ownership/shared_identity_projection.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"

namespace arp::rhi_vulkan {
using namespace rhi;

VulkanOriginalBindlessGraphicsPass::VulkanOriginalBindlessGraphicsPass(ConstructionKey,
    std::shared_ptr<const Closure> closure,
    std::shared_ptr<const Transaction> transaction,
    std::shared_ptr<const Stream> stream,
    Budget budget,
    std::shared_ptr<core::AtomicCapacityBudget> capacity)
    : closure_(std::move(closure)), transaction_(std::move(transaction)),
      stream_(std::move(stream)), budget_(budget), capacity_(std::move(capacity))
{}

VulkanOriginalBindlessGraphicsPass::~VulkanOriginalBindlessGraphicsPass()
{
    // Native objects retire before their capacity charge or original owners.
    destroyVulkanRhiNativeRenderPassResources(*closure_->runtime_, native_);
}

const RenderPassDesc* VulkanOriginalBindlessGraphicsPass::descriptor() const noexcept
{
    const auto* input = stream_->originalInputs()->descriptorAt(commandIndex_);
    const auto* pass =
        input ? std::get_if<std::shared_ptr<const RenderPassArguments>>(input) : nullptr;
    return pass && *pass ? &(*pass)->desc() : nullptr;
}

const char* VulkanOriginalBindlessGraphicsPass::captureAttachments() noexcept
{
    if (const auto* id =
            Contract::selectPass(*stream_, closure_->originalRoot(), budget_, commandIndex_))
        return id;
    const auto* desc = descriptor();
    const auto& prepared = std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
        closure_->root_->originalPrepared());
    const auto* definition =
        vulkanRhiBindlessGraphicsPassPipelineState(*prepared->owners()->desc().pipeline);
    if (!desc || !definition)
        return "RHI-BINDLESS-GRAPHICS-PASS-DEFINITION";
    if (const auto* id = Contract::inspectPass(*desc, *definition))
        return id;
    attachmentCount_ = desc->colorAttachments.size() + (desc->hasDepthStencilAttachment ? 1u : 0u);
    if (const auto* id = Contract::validateCounts(budget_, attachmentCount_, 0u))
        return id;
    const auto& usage = *stream_->originalInputs()->originalScalars()->originalUsage();
    const auto& command = usage.commands()[commandIndex_];
    if (command.referenceCount != attachmentCount_)
        return "RHI-BINDLESS-GRAPHICS-PASS-OPERANDS";
    const auto& limits = closure_->runtime_->descriptorIndexing().limits;
    if (desc->colorAttachments.size() > limits.maxColorAttachments)
        return "RHI-BINDLESS-GRAPHICS-PASS-DEVICE-LIMIT";
    for (std::uint32_t i = 0u; i < attachmentCount_; ++i) {
        auto& a = attachments_[i];
        const auto index = usage.references()[command.firstReference + i].sourceIndex;
        const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
            &closure_->inventory_->sources()[index]);
        if (!view)
            return "RHI-BINDLESS-GRAPHICS-PASS-VIEW-SOURCE";
        a.source = *view;
        const bool depth = i == desc->colorAttachments.size();
        const auto target =
            depth ? desc->depthStencilAttachment.target : desc->colorAttachments[i].target;
        const auto format =
            depth ? desc->depthStencilAttachment.format : desc->colorAttachments[i].format;
        const auto& object = *a.source->originalParentLease()->originalObject();
        const auto& d = a.source->originalObject()->desc();
        const auto content =
            object.observeOriginalContent(defaultTextureSubresourceRange(object.desc()));
        if (a.source->resource() != target || object.desc().format != format || d.format != format
            || !content
            || OriginalBindlessAttachmentClearContract::validateObservation(budget_.attachmentClear,
                content->controlVersion(),
                content->state() == common::OriginalTextureContentControl::State::Uninitialized,
                content->state() == common::OriginalTextureContentControl::State::Completed,
                content->contentGeneration())
            || d.dimension != TextureViewDimension::Texture2D
            || d.usage != TextureUsage::RenderAttachment
            || d.aspect != (depth ? TextureAspect::Depth : TextureAspect::Color)
            || object.desc().extent.depth != 1u || object.desc().mipLevelCount != 1u
            || object.desc().arrayLayerCount != 1u || d.baseMipLevel || d.mipLevelCount != 1u
            || d.baseArrayLayer || d.arrayLayerCount != 1u)
            return "RHI-BINDLESS-GRAPHICS-PASS-VIEW-CORRESPONDENCE";
        a.parent = vulkanRhiNativePayload(object);
        const auto wrapper = vulkanRhiNativePayload(*a.source->originalObject());
        if (!a.parent || !wrapper || !a.parent->backendVulkanOriginalContentExclusive()
            || a.parent->backendVulkanDeviceHandle() != closure_->runtime_->logicalDevice()
            || !core::SharedOwnerProjection::sameOwner(
                a.parent, wrapper->backendVulkanTextureViewParentPayload())
            || a.parent->backendVulkanSwapchainFrameSyncAvailable())
            return "RHI-BINDLESS-GRAPHICS-PASS-NATIVE-CORRESPONDENCE";
        a.image = a.parent->backendVulkanImageHandle();
        a.format = a.parent->backendVulkanImageFormat();
        const auto expected = format == TextureFormat::D32Float ? VK_FORMAT_D32_SFLOAT
            : format == TextureFormat::Rgba8Unorm               ? VK_FORMAT_R8G8B8A8_UNORM
                                                                : VK_FORMAT_R16G16B16A16_SFLOAT;
        const auto extent = a.parent->backendVulkanImageExtent();
        if (!a.image || a.format != expected || extent.width != object.desc().extent.width
            || extent.height != object.desc().extent.height || extent.depth != 1u)
            return "RHI-BINDLESS-GRAPHICS-PASS-NATIVE-SHAPE";
        if (!i)
            extent_ = extent;
        if (!extent.width || !extent.height || extent.width != extent_.width
            || extent.height != extent_.height || extent.width > limits.maxFramebufferWidth
            || extent.height > limits.maxFramebufferHeight)
            return "RHI-BINDLESS-GRAPHICS-PASS-EXTENT";
        for (std::uint32_t j = 0u; j < i; ++j)
            if (attachments_[j].image == a.image)
                return "RHI-BINDLESS-GRAPHICS-PASS-ATTACHMENT-ALIAS";
        a.generation = object.originalResourceRegistration().generation();
    }
    const auto* counts = transaction_->originalMixedContentCounts();
    return counts
            && (budget_.contractVersion == 2u ? counts->attachmentControls >= attachmentCount_
                                              : counts->attachmentControls == attachmentCount_)
        ? nullptr
        : "RHI-BINDLESS-GRAPHICS-PASS-CONTENT-CORRESPONDENCE";
}

const char* VulkanOriginalBindlessGraphicsPass::inspectOwners(std::uint32_t& count) const noexcept
{
    // Existing recursive owners are charged by the same transaction/stream/
    // closure budgets. Project only this cold object's direct shared closure.
    const auto references = 5u + attachmentCount_ * 2u;
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
            else {
                const auto& a = attachments_[(i - 5u) / 2u];
                owner = (i - 5u) % 2u ? std::shared_ptr<const void>(a.parent)
                                      : std::shared_ptr<const void>(a.source);
            }
            return {static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(owner.get())),
                1u,
                owner,
                owner.get(),
                1u};
        });
    if (projection.status != core::SharedIdentityProjection::Status::Complete)
        return "RHI-BINDLESS-GRAPHICS-PASS-OWNER-IDENTITY";
    count = projection.identities + 2u; // This object and its exclusive lease.
    return Contract::validateCounts(budget_, attachmentCount_, count);
}

bool VulkanRhiNativeClearStorePassFactory::create(const VulkanRhiRuntime& runtime,
    const Attachment* attachments,
    std::uint32_t colorCount,
    bool hasDepth,
    rhi::Extent3D extent,
    VulkanRhiNativeRenderPassResources& native) noexcept
{
    const auto attachmentCount = colorCount + (hasDepth ? 1u : 0u);
    if (!attachments || !attachmentCount || attachmentCount > native.kMaxImageViews
        || colorCount > OriginalBindlessGraphicsPassContract::maxColorAttachments || !extent.width
        || !extent.height || extent.depth != 1u || native.renderPass || native.framebuffer
        || native.imageViewCount)
        return false;
    const auto createPass =
        reinterpret_cast<PFN_vkCreateRenderPass>(runtime.getDeviceProcAddr("vkCreateRenderPass"));
    const auto createView =
        reinterpret_cast<PFN_vkCreateImageView>(runtime.getDeviceProcAddr("vkCreateImageView"));
    const auto createFrame =
        reinterpret_cast<PFN_vkCreateFramebuffer>(runtime.getDeviceProcAddr("vkCreateFramebuffer"));
    if (!createPass || !createView || !createFrame || runtime.syntheticRuntime())
        return false;
    std::array<VkAttachmentDescription, OriginalBindlessGraphicsPassContract::maxAttachments>
        descriptions{};
    std::array<VkAttachmentReference, OriginalBindlessGraphicsPassContract::maxColorAttachments>
        colors{};
    VkAttachmentReference depth{};
    for (std::uint32_t i = 0u; i < attachmentCount; ++i) {
        const bool isDepth = i == colorCount;
        const auto layout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                    : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        auto& d = descriptions[i];
        d.format = attachments[i].format;
        d.samples = VK_SAMPLE_COUNT_1_BIT;
        d.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        d.initialLayout = d.finalLayout = layout;
        if (isDepth)
            depth = {i, layout};
        else
            colors[i] = {i, layout};
    }
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = colorCount;
    subpass.pColorAttachments = subpass.colorAttachmentCount ? colors.data() : nullptr;
    subpass.pDepthStencilAttachment = hasDepth ? &depth : nullptr;
    VkRenderPassCreateInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass.attachmentCount = attachmentCount;
    pass.pAttachments = descriptions.data();
    pass.subpassCount = 1u;
    pass.pSubpasses = &subpass;
    if (createPass(runtime.logicalDevice(), &pass, nullptr, &native.renderPass) != VK_SUCCESS)
        return false;
    for (std::uint32_t i = 0u; i < attachmentCount; ++i) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = attachments[i].image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = attachments[i].format;
        view.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY};
        view.subresourceRange = attachments[i].range;
        if (createView(runtime.logicalDevice(), &view, nullptr, &native.imageViews[i])
            != VK_SUCCESS)
            return false;
        ++native.imageViewCount;
    }
    VkFramebufferCreateInfo framebuffer{};
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.renderPass = native.renderPass;
    framebuffer.attachmentCount = attachmentCount;
    framebuffer.pAttachments = native.imageViews.data();
    framebuffer.width = extent.width;
    framebuffer.height = extent.height;
    framebuffer.layers = 1u;
    return createFrame(runtime.logicalDevice(), &framebuffer, nullptr, &native.framebuffer)
        == VK_SUCCESS;
}

bool VulkanOriginalBindlessGraphicsPass::createNativeObjects() noexcept
{
    std::array<VulkanRhiNativeClearStorePassFactory::Attachment, Contract::maxAttachments> inputs;
    const auto* desc = descriptor();
    for (std::uint32_t i = 0u; i < attachmentCount_; ++i)
        inputs[i] = {attachments_[i].image,
            attachments_[i].format,
            {i == desc->colorAttachments.size() ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                : VK_IMAGE_ASPECT_COLOR_BIT,
                0u,
                1u,
                0u,
                1u}};
    return VulkanRhiNativeClearStorePassFactory::create(*closure_->runtime_,
        inputs.data(),
        static_cast<std::uint32_t>(desc->colorAttachments.size()),
        desc->hasDepthStencilAttachment,
        extent_,
        native_);
}

std::shared_ptr<const VulkanOriginalBindlessGraphicsPass>
VulkanOriginalBindlessGraphicsPass::prepareOriginal(std::shared_ptr<const Closure> closure,
    std::shared_ptr<const Transaction> transaction,
    std::shared_ptr<const Stream> stream,
    GpuDevice& device,
    const Budget& budget,
    std::shared_ptr<core::AtomicCapacityBudget> capacity,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    const auto reject =
        [&](const char* id) -> std::shared_ptr<const VulkanOriginalBindlessGraphicsPass> {
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
    if (budget.attachmentClear.contractVersion) {
        const auto clear = OriginalBindlessAttachmentClearContract::lowering(
            device.backend(), budget.attachmentClear.contractVersion);
        if (!clear.preparationAvailable || clear.diagnostic)
            return reject(clear.diagnostic);
    }
    const auto* intent = transaction->originalBindlessAttachmentClearIntent();
    if (!intent || !OriginalBindlessAttachmentClearContract::same(*intent, budget.attachmentClear))
        return reject("RHI-BINDLESS-GRAPHICS-PASS-ATTACHMENT-INTENT");
    if (device.backend() != Backend::Vulkan
        || !Contract::lowering(device.backend(), budget.contractVersion).preparationAvailable)
        return reject("RHI-BINDLESS-GRAPHICS-PASS-BACKEND");
    if ((transaction->originalContentPlanContractVersion() != 9u
            && !(transaction->originalContentPlanContractVersion() == 18u
                && closure->closureContractVersion() == 16u))
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
    for (const char* entry : {"vkCreateRenderPass",
             "vkCreateImageView",
             "vkCreateFramebuffer",
             "vkDestroyRenderPass",
             "vkDestroyImageView",
             "vkDestroyFramebuffer"})
        if (!closure->runtime_->getDeviceProcAddr(entry))
            return reject("RHI-BINDLESS-GRAPHICS-PASS-ENTRYPOINT");
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result =
            shareOriginal(core::makeUnique<VulkanOriginalBindlessGraphicsPass>(ConstructionKey{},
                std::move(closure),
                std::move(transaction),
                std::move(stream),
                budget,
                std::move(capacity)));
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
        if (!result->createNativeObjects())
            return reject("RHI-BINDLESS-GRAPHICS-PASS-NATIVE-CREATION");
        if ((diagnostic = result->validate()))
            return {};
        diagnostic = nullptr;
        return result;
    } catch (const std::bad_alloc&) {
        return reject("RHI-BINDLESS-GRAPHICS-PASS-ALLOCATION");
    }
}

const char* VulkanOriginalBindlessGraphicsPass::validateOriginal(
    const std::shared_ptr<const VulkanOriginalBindlessGraphicsPass>& self) noexcept
{
    return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-BINDLESS-GRAPHICS-PASS-OWNER"
                                                          : self->validate();
}

const char* VulkanOriginalBindlessGraphicsPass::validate() const noexcept
{
    if (const auto* id = Closure::validateOriginal(closure_))
        return id;
    if (const auto* id = transaction_->compareOriginalBindlessGraphicsStream(stream_))
        return id;
    return validateNativeOwners();
}

const char* VulkanOriginalBindlessGraphicsPass::validateNativeOwners(
    const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& seal) const noexcept
{
    if (const auto* id = seal ? Closure::validateOriginalPending(closure_, seal)
                              : Closure::validateOriginal(closure_))
        return id;
    if (!lease_ || lease_->units() != nativeObjectCount() || !native_.renderPass
        || !native_.framebuffer || native_.imageViewCount != attachmentCount_)
        return "RHI-BINDLESS-GRAPHICS-PASS-NATIVE-OWNER";
    for (std::uint32_t i = 0u; i < attachmentCount_; ++i) {
        const auto& a = attachments_[i];
        if (!native_.imageViews[i]
            || a.source->originalParentLease()
                    ->originalObject()
                    ->originalResourceRegistration()
                    .generation()
                != a.generation
            || !core::SharedOwnerProjection::sameOwner(a.parent,
                vulkanRhiNativePayload(*a.source->originalParentLease()->originalObject()))
            || a.image != a.parent->backendVulkanImageHandle()
            || a.format != a.parent->backendVulkanImageFormat())
            return "RHI-BINDLESS-GRAPHICS-PASS-NATIVE-CORRESPONDENCE";
    }
    std::uint32_t owners = 0u;
    if (const auto* id = inspectOwners(owners))
        return id;
    return owners == owners_ ? nullptr : "RHI-BINDLESS-GRAPHICS-PASS-OWNER-CAPACITY";
}
} // namespace arp::rhi_vulkan
