#include "rhi/backends/vulkan/vulkan_render_pass_storage.h"

#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/backends/vulkan/vulkan_original_graphics_binding.h"
#include "rhi/backends/vulkan/vulkan_texture_descriptor.h"
#include "rhi/frontend/command_state.h"

namespace arp::rhi_vulkan {

namespace {
template <class Visitor>
const char* visitOrdinaryImmutableBuffers(
    VulkanRhiNativeCommandPayload::Impl& impl, Visitor&& visitor) noexcept
{
    if (!impl.ordinaryReadOnlyGraphics && !impl.ordinaryMixedPasses)
        return nullptr;
    const auto& sources = impl.recordingStorage->originalProgram->originalInputs()
                              ->originalScalars()
                              ->originalUsage()
                              ->originalSources()
                              ->sources();
    for (const auto& source : sources)
        if (const auto* buffer =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(&source))
            if (const auto* id = visitor(*(*buffer)->originalObject()))
                return id;
    return nullptr;
}

rhi::CommandRecordingUsage ordinaryIncrement(
    const rhi::OriginalCommandStreamProgram& program, std::size_t command) noexcept
{
    const auto& operation = program.operations()[command];
    const auto& scalars = *program.originalInputs()->originalScalars();
    const auto& args = *scalars.originalArguments();
    const auto& record = args.records()[command];
    rhi::CommandRecordingUsage increment;
    (void)increment.appendArguments({record.scalar,
        args.text(command),
        args.offsets(command),
        static_cast<std::uint32_t>(record.offsetCount),
        args.bytes(command),
        static_cast<std::uint32_t>(record.byteCount)});
    increment.resourceReferences = operation.referenceCount;
    if (operation.kind == rhi::CommandUsageKind::BindRenderDescriptor
        || operation.kind == rhi::CommandUsageKind::BindComputeDescriptor)
        if (const auto* set =
                rhi::OriginalReadOnlyGraphicsPassProjection::descriptor(program, command))
            increment.descriptorElements = set->frozenElements().size();
    return increment;
}

// Stack-only projection of one literal original pass. Its Program owns the
// canonical views; this scratch is neither an owner graph nor an executor.
struct PassInputs {
    std::array<VulkanRhiNativeClearStorePassFactory::Attachment,
        VulkanRhiNativeRenderPassResources::kMaxImageViews>
        attachments{};
    rhi::Extent3D extent{};
};

const char* projectPass(const rhi::OriginalCommandStreamProgram& program,
    const rhi::OriginalCommandStreamProgram::Operation& operation,
    const rhi::RenderPassDesc& descriptor,
    const VulkanRhiRuntime& runtime,
    PassInputs& output) noexcept
{
    using Contract = rhi::OriginalRenderPassPreparationContract;
    if (const auto* id = Contract::inspectDescriptor(descriptor))
        return id;
    const auto& limits = runtime.nativeLimits();
    if (operation.referenceCount > output.attachments.size()
        || descriptor.colorAttachments.size() > limits.maxColorAttachments
        || !limits.maxFramebufferLayers)
        return "RHI-VULKAN-COLD-RENDER-PASS-LIMIT";
    const auto getFormat = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
        runtime.getInstanceProcAddr()(runtime.instance(), "vkGetPhysicalDeviceFormatProperties"));
    if (!getFormat)
        return "RHI-VULKAN-COLD-RENDER-PASS-FORMAT-ENTRYPOINT";
    const auto& usage = *program.originalInputs()->originalScalars()->originalUsage();
    return rhi::RenderPassReferenceSequenceContract::visit(
        descriptor,
        operation.referenceCount,
        [&](std::size_t operand,
            rhi::GpuResourceId,
            rhi::TextureFormat,
            rhi::RenderPassReferenceSequenceContract::Role role,
            std::size_t) noexcept -> const char* {
            const auto sourceIndex =
                usage.references()[operation.firstReference + operand].sourceIndex;
            const auto& source =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    usage.originalSources()->sources()[sourceIndex]);
            const auto& parent = source->originalParentLease()->originalObject()->desc();
            const auto& view = source->originalObject()->desc();
            if (const auto* id = Contract::inspectView(parent, view))
                return id;
            const bool depth = role == rhi::RenderPassReferenceSequenceContract::Role::DepthStencil;
            if (view.aspect != (depth ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color))
                return "RHI-VULKAN-COLD-RENDER-PASS-ASPECT";
            const auto native =
                vulkanRhiNativePayload(*source->originalParentLease()->originalObject());
            auto& input = output.attachments[operand];
            input = {native->backendVulkanImageHandle(),
                native->backendVulkanImageFormat(),
                {depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                    view.baseMipLevel,
                    1u,
                    view.baseArrayLayer,
                    1u}};
            VkFormatProperties properties{};
            getFormat(runtime.physicalDevice(), input.format, &properties);
            const VkFormatFeatureFlags required = depth
                ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
            if ((properties.optimalTilingFeatures & required) != required)
                return "RHI-VULKAN-COLD-RENDER-PASS-FORMAT";
            const rhi::Extent3D extent{rhi::semantics::TextureTransferValidation::mipExtent(
                                           parent.extent.width, view.baseMipLevel),
                rhi::semantics::TextureTransferValidation::mipExtent(
                    parent.extent.height, view.baseMipLevel),
                1u};
            if (!operand)
                output.extent = extent;
            if (extent != output.extent || extent.width > limits.maxFramebufferWidth
                || extent.height > limits.maxFramebufferHeight)
                return "RHI-VULKAN-COLD-RENDER-PASS-EXTENT";
            for (std::size_t previous = 0u; previous < operand; ++previous)
                if (output.attachments[previous].image == input.image)
                    return "RHI-VULKAN-COLD-RENDER-PASS-ATTACHMENT-ALIAS";
            return nullptr;
        },
        "RHI-VULKAN-COLD-RENDER-PASS-OPERANDS");
}

template <class Visitor>
const char* visitOrdinaryAttachmentClaims(
    VulkanRhiNativeCommandPayload::Impl& impl, Visitor&& visitor) noexcept
{
    const auto& program = *impl.recordingStorage->originalProgram;
    const auto& usage = *program.originalInputs()->originalScalars()->originalUsage();
    for (std::size_t i = 0u; i < impl.retainedRenderPassCount; ++i) {
        auto& pass = *impl.retainedRenderPasses[i].originalContent;
        const auto& operation = program.operations()[pass.commandIndex];
        for (std::size_t operand = 0u; operand < operation.referenceCount; ++operand) {
            auto& claim = *pass.attachments[operand];
            if (!claim.claimOwner)
                continue;
            const auto index = usage.references()[operation.firstReference + operand].sourceIndex;
            const auto& source =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    usage.originalSources()->sources()[index]);
            if (const auto* id = visitor(*source->originalParentLease()->originalObject(), claim))
                return id;
        }
    }
    return nullptr;
}
} // namespace

VulkanRhiRenderPassStorageProjection::Result
VulkanRhiRenderPassStorageProjection::inspectRegistered(
    const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& program,
    const rhi::OriginalRenderPassStorageContract::Budget& budget,
    std::uint64_t maxNativeObjects,
    const rhi::GpuDevice& device,
    const VulkanRhiRuntime& runtime) noexcept
{
    auto result = inspect(program, budget, maxNativeObjects);
    if (result.diagnostic)
        return result;
    if (device.backend() != rhi::Backend::Vulkan || !runtime.valid() || runtime.syntheticRuntime())
        return {"RHI-VULKAN-COLD-RENDER-PASS-DEVICE"};
    const auto& sources =
        program->originalInputs()->originalScalars()->originalUsage()->originalSources();
    if (const auto* id =
            rhi::OriginalCommandResourceSources::validateRegisteredLifetime(sources, device))
        return {id};
    const auto checked = rhi::OriginalRenderPassStorageContract::visitAttachments(program,
        budget,
        [&](const auto&,
            const auto&,
            std::size_t,
            auto,
            std::size_t,
            const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& source) noexcept
            -> const char* {
            const auto* viewObject = source->originalObject();
            const auto* textureObject = source->originalParentLease()->originalObject();
            const auto view = vulkanRhiNativePayload(*viewObject);
            const auto texture = vulkanRhiNativePayload(*textureObject);
            const auto parent = view ? view->backendVulkanTextureViewParentPayload() : nullptr;
            if (!view || !texture || parent != texture
                || !core::SharedOwnerProjection::sameOwner(parent, texture)
                || view->backendVulkanDeviceHandle() != runtime.logicalDevice()
                || texture->backendVulkanDeviceHandle() != runtime.logicalDevice()
                || !texture->backendVulkanImageHandle()
                || view->backendVulkanImageHandle() != texture->backendVulkanImageHandle())
                return "RHI-VULKAN-COLD-RENDER-PASS-VIEW-PARENT-PAYLOAD";
            const auto* nativeView = view->backendVulkanOriginalTextureViewDesc();
            const auto& shape = source->originalViewLease()->layout();
            const auto& parentShape = source->originalParentLease()->layout();
            if (!nativeView || !shape.matches(*nativeView)
                || texture->backendVulkanImageFormat()
                    != VulkanTextureDescriptor::format(parentShape.format)
                || texture->backendVulkanImageExtent() != parentShape.extent)
                return "RHI-VULKAN-COLD-RENDER-PASS-VIEW-PARENT-LAYOUT";
            return nullptr;
        });
    return checked.diagnostic ? Result{checked.diagnostic} : result;
}

VulkanRhiRenderPassStorageProjection::Result
VulkanRhiRenderPassStorageProjection::inspectPreparation(
    const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& program,
    const rhi::OriginalRenderPassPreparationRequest& request,
    const rhi::GpuDevice& device,
    const VulkanRhiRuntime& runtime) noexcept
{
    const auto decision =
        rhi::OriginalRenderPassPreparationContract::lowering(device.backend(), request);
    if (!decision.privatePreparationAvailable || decision.diagnostic)
        return {decision.diagnostic};
    auto result =
        inspectRegistered(program, request.storage, request.maxNativeObjects, device, runtime);
    if (result.diagnostic)
        return result;
    if (const auto* id = rhi::OriginalRenderPassPreparationContract::inspectWrapperCapacity(
            request, result.renderPasses))
        return {id};
    if (request.content) {
        const auto content = rhi::OriginalRenderPassContentContract::inspect(
            program, request.storage, *request.content);
        if (content.diagnostic)
            return {content.diagnostic};
    }
    if (rhi::OriginalRenderPassPreparationContract::hasPassTiming(request)) {
        if (const auto* id = rhi::OriginalQueryRecordingStorageContract::inspectPassTiming(
                program->originalInputs(),
                *request.passTimingRetention,
                request.passTimingRecordingQuota)
                .diagnostic)
            return {id};
        if (const auto* id = runtime.timestampQueryDiagnostic())
            return {id};
        if (!runtime.getDeviceProcAddr("vkCmdWriteTimestamp"))
            return {"RHI-VULKAN-TIMESTAMP-ENTRYPOINTS"};
    }
    if (request.recording) {
        const rhi::OriginalComputeDispatchRecordingInput admission{device, request.dispatchDomain};
        const auto recording = rhi::OriginalRenderPassRecordingContract::inspect(program,
            request.storage,
            *request.content,
            *request.recording,
            request.dispatchDomain ? &admission : nullptr);
        if (recording.diagnostic)
            return {recording.diagnostic};
        for (const auto* name :
            {"vkCmdBeginRenderPass", "vkCmdEndRenderPass", "vkCmdPipelineBarrier"})
            if (!runtime.getDeviceProcAddr(name))
                return {"RHI-ORDINARY-ATTACHMENT-RECORDING-ENTRYPOINT"};
        if (request.contractVersion >= 5u) {
            for (const auto* name : {"vkCmdBindPipeline",
                     "vkCmdBindDescriptorSets",
                     "vkCmdSetViewport",
                     "vkCmdSetScissor",
                     "vkCmdDraw"})
                if (!runtime.getDeviceProcAddr(name))
                    return {"RHI-ORDINARY-GRAPHICS-RECORDING-ENTRYPOINT"};
            if (rhi::OriginalRenderPassPreparationContract::hasMixedPasses(request)) {
                for (const auto* name : {"vkCmdDispatch", "vkCmdCopyImageToBuffer"})
                    if (!runtime.getDeviceProcAddr(name))
                        return {"RHI-ORDINARY-MIXED-RECORDING-ENTRYPOINT"};
                if (!runtime.coreQueueCapabilities().compute)
                    return {"RHI-ORDINARY-MIXED-COMPUTE-QUEUE"};
                for (const auto& operation : program->operations())
                    if (operation.kind == rhi::CommandUsageKind::Dispatch) {
                        const auto& scalar = program->originalInputs()
                                                 ->originalScalars()
                                                 ->originalArguments()
                                                 ->records()[operation.commandIndex]
                                                 .scalar;
                        const auto& d = *scalar.get<rhi::CommandScalarArguments::Dispatch>();
                        const auto& limits = runtime.nativeLimits();
                        if (d.x > limits.maxComputeWorkGroupCount[0]
                            || d.y > limits.maxComputeWorkGroupCount[1]
                            || d.z > limits.maxComputeWorkGroupCount[2])
                            return {"RHI-ORDINARY-MIXED-DISPATCH-LIMIT"};
                    }
            }
            const auto& inventory = program->originalInputs()
                                        ->originalScalars()
                                        ->originalUsage()
                                        ->originalSources()
                                        ->sources();
            for (const auto& source : inventory) {
                if (const auto* buffer =
                        std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            &source)) {
                    const auto& object = *(*buffer)->originalObject();
                    const auto native = vulkanRhiNativePayload(object);
                    if (!native || native->backendVulkanDeviceHandle() != runtime.logicalDevice()
                        || (rhi::OriginalRenderPassPreparationContract::hasMixedPasses(request)
                                ? !native->originalHostDefinedBytesMatch(object)
                                : (!native->originalHostInitializationMatches(object)
                                      || object.state() != rhi::ResourceState::ShaderRead)))
                        return {"RHI-ORDINARY-GRAPHICS-IMMUTABLE-BUFFER-INITIALIZATION"};
                }
                if (const auto* pipeline = std::get_if<
                        std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                        &source)) {
                    const auto native = vulkanRhiNativePayload(*(*pipeline)->originalObject());
                    if (!native || native->backendVulkanDeviceHandle() != runtime.logicalDevice()
                        || !native->backendVulkanRenderPipelineHandle())
                        return {"RHI-ORDINARY-GRAPHICS-PIPELINE-NATIVE"};
                }
                if (const auto* pipeline = std::get_if<
                        std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(
                        &source)) {
                    const auto native = vulkanRhiNativePayload(*(*pipeline)->originalObject());
                    if (!rhi::OriginalRenderPassPreparationContract::hasMixedPasses(request)
                        || !native || native->backendVulkanDeviceHandle() != runtime.logicalDevice()
                        || !native->backendVulkanComputePipelineHandle())
                        return {"RHI-ORDINARY-MIXED-COMPUTE-NATIVE"};
                }
                if (const auto* set =
                        std::get_if<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                            &source)) {
                    const auto native =
                        rhi::OriginalRenderPassPreparationContract::hasMixedPasses(request)
                        ? vulkanOriginalSynchronousDescriptorPayload(*set)
                        : vulkanOriginalGraphicsDescriptorPayload(*set);
                    if (!native || native->backendVulkanDeviceHandle() != runtime.logicalDevice()
                        || !native->backendVulkanDescriptorSetHandle())
                        return {"RHI-ORDINARY-GRAPHICS-DESCRIPTOR-NATIVE"};
                }
            }
        }
    }
    if (!runtime.coreQueueCapabilities().graphics || !runtime.physicalDevice()
        || !runtime.getInstanceProcAddr())
        return {"RHI-VULKAN-COLD-RENDER-PASS-QUEUE"};
    if (result.renderPasses
        > std::tuple_size_v<decltype(VulkanRhiNativeCommandPayload::Impl::retainedRenderPasses)>)
        return {"RHI-VULKAN-COLD-RENDER-PASS-NATIVE-CAPACITY"};
    for (const auto* name : {"vkCreateRenderPass",
             "vkCreateImageView",
             "vkCreateFramebuffer",
             "vkDestroyRenderPass",
             "vkDestroyImageView",
             "vkDestroyFramebuffer"})
        if (!runtime.getDeviceProcAddr(name))
            return {"RHI-VULKAN-COLD-RENDER-PASS-PREPARATION-ENTRYPOINT"};
    const auto checked = rhi::OriginalRenderPassStorageContract::visit(program,
        request.storage,
        [&](const auto& operation, const auto& arguments) noexcept -> const char* {
            PassInputs scratch;
            return projectPass(*program, operation, arguments->desc(), runtime, scratch);
        });
    return checked.diagnostic ? Result{checked.diagnostic} : result;
}

bool VulkanRhiNativeCommandPayload::prepareOrdinaryRenderPasses(const rhi::GpuDevice& device,
    const rhi::OriginalRenderPassPreparationRequest& request,
    core::pmr::String& diagnostic)
{
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    if (!impl_ || !impl_->recordingStorage || !impl_->recordingStorage->originalProgram
        || !impl_->ordinaryJournalAliased || !impl_->retainedResourceStates
        || !impl_->retainedResourceStates->empty() || !impl_->recording || impl_->ended
        || impl_->submitted || impl_->retainedRenderPassCount || impl_->activeRenderPass.renderPass)
        return reject("RHI-VULKAN-COLD-RENDER-PASS-PREPARATION-STATE");
    const auto& program = impl_->recordingStorage->originalProgram;
    const auto result = VulkanRhiRenderPassStorageProjection::inspectPreparation(
        program, request, device, impl_->device);
    if (result.diagnostic)
        return reject(result.diagnostic);
    if (result.renderPasses > impl_->retainedRenderPasses.size())
        return reject("RHI-VULKAN-COLD-RENDER-PASS-NATIVE-CAPACITY");
    if (request.content) {
        // This direct cold traversal may propagate allocation exceptions to the
        // ordinary factory. The subsequent noexcept projection only emplaces
        // fixed-size observations into those same per-pass native objects.
        std::size_t index = 0u;
        for (const auto& operation : program->operations())
            if (operation.kind == rhi::CommandUsageKind::BeginRenderPass)
                impl_->retainedRenderPasses[index++].originalContent =
                    core::makeShared<VulkanRhiOrdinaryRenderPassContent>(operation.commandIndex);
        const auto content = rhi::OriginalRenderPassContentContract::visit(program,
            request.storage,
            *request.content,
            [&](std::size_t command,
                std::size_t operand,
                const auto& initialContent,
                const auto& initialState,
                bool stored) noexcept -> const char* {
                for (std::size_t i = 0u; i < index; ++i) {
                    auto& pass = *impl_->retainedRenderPasses[i].originalContent;
                    if (pass.commandIndex != command)
                        continue;
                    if (operand >= pass.attachments.size() || pass.attachments[operand])
                        return "RHI-VULKAN-COLD-RENDER-PASS-CONTENT-POSITION";
                    pass.attachments[operand].emplace(
                        VulkanRhiOrdinaryRenderPassContent::Attachment{
                            initialContent, initialState, stored});
                    return nullptr;
                }
                return "RHI-VULKAN-COLD-RENDER-PASS-CONTENT-POSITION";
            });
        if (content.diagnostic)
            return reject(content.diagnostic);
    }
    const auto created = rhi::OriginalRenderPassStorageContract::visit(program,
        request.storage,
        [&](const auto& operation, const auto& arguments) noexcept -> const char* {
            PassInputs inputs;
            if (const auto* id =
                    projectPass(*program, operation, arguments->desc(), impl_->device, inputs))
                return id;
            // Include the active prefix in cleanup even when native creation
            // stops halfway through the current pass.
            auto& native = impl_->retainedRenderPasses[impl_->retainedRenderPassCount++];
            if (native.originalContent)
                native.originalContent->extent = inputs.extent;
            const bool success = VulkanRhiNativeClearStorePassFactory::create(
                impl_->device, inputs.attachments.data(), arguments->desc(), inputs.extent, native);
            if (impl_->stats) {
                impl_->stats->nativeRenderPassCreateCount += native.renderPass ? 1u : 0u;
                impl_->stats->nativeFramebufferCreateCount += native.framebuffer ? 1u : 0u;
                impl_->stats->nativeRenderAttachmentViewCreateCount += native.imageViewCount;
            }
            return success ? nullptr : "RHI-VULKAN-COLD-RENDER-PASS-NATIVE-CREATION";
        });
    if (created.diagnostic) {
        for (std::uint32_t i = 0u; i < impl_->retainedRenderPassCount; ++i)
            destroyVulkanRhiNativeRenderPassResources(
                impl_->device, impl_->retainedRenderPasses[i]);
        impl_->retainedRenderPassCount = 0u;
        return reject(created.diagnostic);
    }
    if (request.recording) {
        impl_->ordinaryReadOnlyGraphics = request.contractVersion >= 5u;
        impl_->ordinaryMixedPasses =
            rhi::OriginalRenderPassPreparationContract::hasMixedPasses(request);
        const char* id = nullptr;
        impl_->ordinaryAttachmentMeter =
            rhi::CommandRecordingMeter::prepare(*program->recordingBudget(), id);
        if (id)
            return reject(id);
        // Mark the first literal observation of each parent. The approved
        // profile has exactly one cell per parent; repeated passes reuse it.
        const auto& usage = *program->originalInputs()->originalScalars()->originalUsage();
        for (std::size_t i = 0u; i < impl_->retainedRenderPassCount; ++i) {
            auto& pass = *impl_->retainedRenderPasses[i].originalContent;
            const auto& op = program->operations()[pass.commandIndex];
            for (std::size_t a = 0u; a < op.referenceCount; ++a) {
                const auto source = usage.references()[op.firstReference + a].sourceIndex;
                const auto& object =
                    *std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                        usage.originalSources()->sources()[source])
                         ->originalParentLease()
                         ->originalObject();
                bool earlier = false;
                for (std::size_t p = 0u; p < i; ++p) {
                    const auto& previous = *impl_->retainedRenderPasses[p].originalContent;
                    const auto& previousOp = program->operations()[previous.commandIndex];
                    for (std::size_t b = 0u; b < previousOp.referenceCount; ++b) {
                        const auto index =
                            usage.references()[previousOp.firstReference + b].sourceIndex;
                        earlier |=
                            std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                                usage.originalSources()->sources()[index])
                                ->originalParentLease()
                                ->originalObject()
                            == &object;
                    }
                }
                pass.attachments[a]->claimOwner = !earlier;
            }
        }
    }
    diagnostic.clear();
    return true;
}

const char*
VulkanRhiNativeCommandPayload::preparedOrdinaryRenderPassContentDiagnostic() const noexcept
{
    if (!impl_ || !impl_->recordingStorage || !impl_->recordingStorage->originalProgram)
        return "RHI-VULKAN-COLD-RENDER-PASS-CONTENT-OWNER";
    const auto& program = *impl_->recordingStorage->originalProgram;
    if (impl_->retainedRenderPassCount != program.counts().renderPasses)
        return "RHI-VULKAN-COLD-RENDER-PASS-CONTENT-UNPREPARED";
    const auto& usage = *program.originalInputs()->originalScalars()->originalUsage();
    for (std::size_t i = 0u; i < impl_->retainedRenderPassCount; ++i) {
        const auto& metadata = impl_->retainedRenderPasses[i].originalContent;
        if (!metadata)
            return "RHI-VULKAN-COLD-RENDER-PASS-CONTENT-UNPREPARED";
        if (metadata->commandIndex >= program.operations().size())
            return "RHI-VULKAN-COLD-RENDER-PASS-CONTENT-POSITION";
        const auto& operation = program.operations()[metadata->commandIndex];
        if (operation.kind != rhi::CommandUsageKind::BeginRenderPass
            || operation.referenceCount > metadata->attachments.size())
            return "RHI-VULKAN-COLD-RENDER-PASS-CONTENT-POSITION";
        for (std::size_t operand = 0u; operand < operation.referenceCount; ++operand) {
            const auto& observation = metadata->attachments[operand];
            if (!observation)
                return "RHI-VULKAN-COLD-RENDER-PASS-CONTENT-POSITION";
            const auto sourceIndex =
                usage.references()[operation.firstReference + operand].sourceIndex;
            const auto& source =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    usage.originalSources()->sources()[sourceIndex]);
            const auto& object = *source->originalParentLease()->originalObject();
            if (const auto* id = object.validateOriginalCommittedState(observation->initialState))
                return id;
            if (const auto* id = object.validateOriginalContent(observation->initialContent))
                return id;
        }
    }
    return nullptr;
}

bool VulkanRhiNativeCommandPayload::hasOrdinaryAttachments() const noexcept
{
    return impl_ && bool(impl_->ordinaryAttachmentMeter);
}

const char* VulkanRhiNativeCommandPayload::preflightOrdinaryAttachmentOperation(
    rhi::CommandUsageKind kind, std::uint32_t command) const noexcept
{
    if (!hasOrdinaryAttachments())
        return nullptr;
    if (const auto* id = recordingStorageDiagnostic())
        return id;
    const auto& program = *impl_->recordingStorage->originalProgram;
    if (!impl_->recording || impl_->ordinaryAttachmentSealed
        || command != impl_->ordinaryAttachmentMeter->commandCount()
        || command >= program.operations().size() || program.operations()[command].kind != kind)
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-ORDINAL";
    const auto increment = ordinaryIncrement(program, command);
    return impl_->ordinaryAttachmentMeter->preflight(increment);
}

void VulkanRhiNativeCommandPayload::consumeOrdinaryAttachmentOperation() noexcept
{
    const auto increment = ordinaryIncrement(*impl_->recordingStorage->originalProgram,
        static_cast<std::size_t>(impl_->ordinaryAttachmentMeter->commandCount()));
    (void)impl_->ordinaryAttachmentMeter->consume(increment);
}

const char* VulkanRhiNativeCommandPayload::claimOrdinaryAttachments() noexcept
{
    if (!hasOrdinaryAttachments() || impl_->ordinaryAttachmentClaimed)
        return nullptr;
    // Prevalidate every observation before proposing the first parent. All
    // claims use the existing adopted journal; rollback is owned by its command.
    if (const auto* id = preparedOrdinaryRenderPassContentDiagnostic())
        return id;
    if (const auto* id =
            visitOrdinaryImmutableBuffers(*impl_, [&](const auto& object) noexcept -> const char* {
                const auto native = vulkanRhiNativePayload(object);
                if (!native
                    || (impl_->ordinaryMixedPasses
                            ? !native->originalHostDefinedBytesMatch(object)
                            : !native->originalHostInitializationMatches(object)))
                    return "RHI-ORDINARY-GRAPHICS-IMMUTABLE-BUFFER-INITIALIZATION";
                return impl_->ordinaryMixedPasses
                    ? rhi::OriginalCommandStateTransaction::preflightOrdinaryInitializedBuffer(
                          object, *impl_->retainedResourceStates)
                    : rhi::OriginalCommandStateTransaction::preflightOrdinaryImmutableBuffer(
                          object, *impl_->retainedResourceStates);
            }))
        return id;
    if (const auto *id = visitOrdinaryAttachmentClaims(
            *impl_, [&](const auto& object, const auto& claim) noexcept {
                return rhi::OriginalCommandStateTransaction::preflightOrdinaryAttachment(
                    object, claim.initialContent, *impl_->retainedResourceStates);
            }))
        return id;
    if (const auto* id = visitOrdinaryImmutableBuffers(*impl_, [&](const auto& object) noexcept {
            return impl_->ordinaryMixedPasses
                ? rhi::OriginalCommandStateTransaction::claimOrdinaryInitializedBuffer(
                      object, *impl_->retainedResourceStates)
                : rhi::OriginalCommandStateTransaction::claimOrdinaryImmutableBuffer(
                      object, *impl_->retainedResourceStates);
        }))
        return id;
    if (const auto *id =
            visitOrdinaryAttachmentClaims(*impl_, [&](const auto& object, auto& claim) noexcept {
                return rhi::OriginalCommandStateTransaction::proposeOrdinaryAttachment(object,
                    claim.initialContent,
                    claim.finalStored,
                    *impl_->retainedResourceStates,
                    claim.proposed,
                    claim.pending);
            }))
        return id;
    impl_->ordinaryAttachmentClaimed = true;
    return nullptr;
}

const char* VulkanRhiNativeCommandPayload::validateOrdinaryAttachmentsPending() const noexcept
{
    if (!hasOrdinaryAttachments())
        return nullptr;
    if (!impl_->ordinaryAttachmentClaimed)
        return "RHI-ORDINARY-ATTACHMENT-CONTENT-UNCLAIMED";
    if (const auto* id =
            visitOrdinaryImmutableBuffers(*impl_, [&](const auto& object) noexcept -> const char* {
                const bool pending = impl_->ordinaryMixedPasses
                    ? rhi::OriginalCommandStateTransaction::ordinaryInitializedBufferPending(
                          object, *impl_->retainedResourceStates)
                    : rhi::OriginalCommandStateTransaction::ordinaryImmutableBufferPending(
                          object, *impl_->retainedResourceStates);
                return pending ? nullptr : "RHI-ORDINARY-GRAPHICS-IMMUTABLE-BUFFER-PENDING";
            }))
        return id;
    return visitOrdinaryAttachmentClaims(
        *impl_, [&](const auto& object, const auto& claim) noexcept -> const char* {
            return claim.pending
                ? rhi::OriginalCommandStateTransaction::validateOrdinaryAttachmentPending(object,
                      claim.initialContent,
                      *impl_->retainedResourceStates,
                      claim.proposed,
                      *claim.pending)
                : "RHI-ORDINARY-ATTACHMENT-CONTENT-PENDING";
        });
}

const char* VulkanRhiNativeCommandPayload::completeOrdinaryAttachments() noexcept
{
    if (!hasOrdinaryAttachments())
        return nullptr;
    if (!impl_->ordinaryAttachmentSealed)
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-INCOMPLETE";
    if (const auto* id = validateOrdinaryAttachmentsPending())
        return id;
    return visitOrdinaryAttachmentClaims(
        *impl_, [&](const auto& object, const auto& claim) noexcept {
            return rhi::OriginalCommandStateTransaction::completeOrdinaryAttachment(object,
                claim.initialContent,
                *impl_->retainedResourceStates,
                claim.proposed,
                *claim.pending);
        });
}

const char* VulkanRhiNativeCommandPayload::preflightOrdinaryAttachmentTransition(
    const rhi::GpuTexture& object,
    const rhi::ResourceBarrierDesc& descriptor,
    std::uint32_t command) const noexcept
{
    if (const auto* id =
            preflightOrdinaryAttachmentOperation(rhi::CommandUsageKind::TextureBarrier, command))
        return id;
    if (impl_->activeRenderPass.renderPass)
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-BARRIER-IN-PASS";
    const auto& program = *impl_->recordingStorage->originalProgram;
    const auto& operation = program.operations()[command];
    const auto* input = program.originalInputs()->descriptorAt(command);
    const auto* expected = input ? std::get_if<rhi::ResourceDependencyArguments>(input) : nullptr;
    if (!expected || !expected->matches(rhi::CommandUsageKind::TextureBarrier, descriptor)
        || operation.referenceCount != 1u)
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-BARRIER-JOIN";
    const auto& usage = *program.originalInputs()->originalScalars()->originalUsage();
    const auto index = usage.references()[operation.firstReference].sourceIndex;
    const auto* lease = std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
        &usage.originalSources()->sources()[index]);
    if (!lease || (*lease)->originalObject() != &object)
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-BARRIER-SOURCE";
    return rhi::OriginalCommandStateTransaction::ordinaryAttachmentState(object,
               descriptor.textureSubresources,
               descriptor.before,
               *impl_->retainedResourceStates)
        ? nullptr
        : "RHI-ORDINARY-ATTACHMENT-RECORDING-STATE";
}

const char* VulkanRhiNativeCommandPayload::recordOrdinaryRenderPassBegin(
    std::uint32_t command, const rhi::RenderPassDesc& descriptor) noexcept
{
    if (!hasOrdinaryAttachments())
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-OPT-IN";
    if (const auto* id =
            preflightOrdinaryAttachmentOperation(rhi::CommandUsageKind::BeginRenderPass, command))
        return id;
    const auto& program = *impl_->recordingStorage->originalProgram;
    const auto& arguments = std::get<std::shared_ptr<const rhi::RenderPassArguments>>(
        *program.originalInputs()->descriptorAt(command));
    if (!arguments->matches(descriptor) || impl_->activeRenderPass.renderPass)
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-PASS-JOIN";
    if (const auto* id = claimOrdinaryAttachments())
        return id;
    if (const auto* id = validateOrdinaryAttachmentsPending())
        return id;
    for (std::size_t i = 0u; i < impl_->retainedRenderPassCount; ++i) {
        auto& native = impl_->retainedRenderPasses[i];
        auto& pass = *native.originalContent;
        if (pass.commandIndex != command)
            continue;
        if (pass.began || !native.renderPass || !native.framebuffer)
            return "RHI-ORDINARY-ATTACHMENT-RECORDING-NATIVE-PASS";
        const auto& operation = program.operations()[command];
        const auto& usage = *program.originalInputs()->originalScalars()->originalUsage();
        for (std::size_t a = 0u; a < operation.referenceCount; ++a) {
            const auto index = usage.references()[operation.firstReference + a].sourceIndex;
            const auto& source =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    usage.originalSources()->sources()[index]);
            const auto required = a < descriptor.colorAttachments.size()
                ? rhi::ResourceState::ColorAttachment
                : rhi::ResourceState::DepthAttachment;
            if (!rhi::OriginalCommandStateTransaction::ordinaryAttachmentState(
                    *source->originalParentLease()->originalObject(),
                    pass.attachments[a]->initialContent.range(),
                    required,
                    *impl_->retainedResourceStates))
                return "RHI-ORDINARY-ATTACHMENT-RECORDING-PASS-STATE";
        }
        const auto begin = reinterpret_cast<PFN_vkCmdBeginRenderPass>(
            impl_->device.getDeviceProcAddr("vkCmdBeginRenderPass"));
        if (!begin)
            return "RHI-ORDINARY-ATTACHMENT-RECORDING-ENTRYPOINT";
        core::pmr::String diagnostic;
        if (!flushPendingBarriers(diagnostic))
            return "RHI-ORDINARY-ATTACHMENT-RECORDING-BARRIER-FLUSH";
        std::array<VkClearValue, VulkanRhiNativeRenderPassResources::kMaxImageViews> clears{};
        for (std::size_t a = 0u; a < descriptor.colorAttachments.size(); ++a) {
            const auto& c = descriptor.colorAttachments[a].clearColor;
            clears[a].color = {{c.r, c.g, c.b, c.a}};
        }
        if (descriptor.hasDepthStencilAttachment)
            clears[descriptor.colorAttachments.size()].depthStencil = {
                descriptor.depthStencilAttachment.depthClearValue,
                descriptor.depthStencilAttachment.stencilClearValue};
        VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        info.renderPass = native.renderPass;
        info.framebuffer = native.framebuffer;
        info.renderArea.extent = {pass.extent.width, pass.extent.height};
        info.clearValueCount = static_cast<std::uint32_t>(operation.referenceCount);
        info.pClearValues = clears.data();
        begin(impl_->commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        impl_->activeRenderPass = native;
        impl_->originalRenderPassBorrowed = true;
        impl_->ordinaryGraphicsPipeline = nullptr;
        impl_->ordinaryGraphicsDescriptor = nullptr;
        impl_->ordinaryGraphicsViewport = impl_->ordinaryGraphicsScissor = false;
        pass.began = true;
        consumeOrdinaryAttachmentOperation();
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeRenderPassBeginRecordCount;
        }
        return nullptr;
    }
    return "RHI-ORDINARY-ATTACHMENT-RECORDING-NATIVE-PASS";
}

const char* VulkanRhiNativeCommandPayload::recordOrdinaryRenderPassEnd(
    std::uint32_t command) noexcept
{
    if (!hasOrdinaryAttachments())
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-OPT-IN";
    if (const auto* id =
            preflightOrdinaryAttachmentOperation(rhi::CommandUsageKind::EndRenderPass, command))
        return id;
    auto& active = impl_->activeRenderPass;
    if (!impl_->originalRenderPassBorrowed || !active.originalContent
        || !active.originalContent->began || active.originalContent->ended)
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-PASS-ORDER";
    core::pmr::String diagnostic;
    const auto metadata = active.originalContent;
    if (!recordRenderPassEnd(diagnostic))
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-NATIVE-END";
    metadata->ended = true;
    consumeOrdinaryAttachmentOperation();
    return nullptr;
}

const char* VulkanRhiNativeCommandPayload::recordOrdinaryGraphicsCommand(std::uint32_t command,
    const rhi::CommandScalarArguments& scalar,
    const rhi::RenderPipeline* incomingPipeline,
    const rhi::DescriptorSet* incomingSet) noexcept
{
    using K = rhi::CommandUsageKind;
    if (hasOrdinaryAttachments() && !impl_->ordinaryReadOnlyGraphics)
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-COMMAND-PROFILE";
    if (!hasOrdinaryAttachments())
        return "RHI-ORDINARY-GRAPHICS-RECORDING-OPT-IN";
    if (const auto* id = preflightOrdinaryAttachmentOperation(scalar.kind(), command))
        return id;
    if (!impl_->originalRenderPassBorrowed || !impl_->activeRenderPass.renderPass)
        return "RHI-ORDINARY-GRAPHICS-PASS-ORDER";
    const auto& program = *impl_->recordingStorage->originalProgram;
    const auto& scalars = *program.originalInputs()->originalScalars();
    const auto& expected = scalars.originalArguments()->records()[command];
    if (!expected.scalar.matches(scalar) || expected.offsetCount || expected.byteCount)
        return "RHI-ORDINARY-GRAPHICS-ORIGINAL-ARGUMENTS";
    if (const auto* id = validateOrdinaryAttachmentsPending())
        return id;
    core::pmr::String diagnostic;
    switch (scalar.kind()) {
    case K::BindGraphicsPipeline: {
        const auto& binding = std::get<rhi::OriginalCommandPipelineBinding>(
            *program.originalInputs()->descriptorAt(command));
        const auto& source =
            std::get<std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                scalars.originalUsage()->originalSources()->sources()[binding.sourceIndex]);
        if (source->originalObject() != incomingPipeline
            || rhi::OriginalCommandGraphicsPipelineSource::validateOriginal(source))
            return "RHI-ORDINARY-GRAPHICS-ORIGINAL-PIPELINE";
        const auto native = vulkanRhiNativePayload(*incomingPipeline);
        if (!native || !recordGraphicsPipelineBind(*native, diagnostic))
            return "RHI-ORDINARY-GRAPHICS-NATIVE-PIPELINE";
        impl_->ordinaryGraphicsPipeline = incomingPipeline;
        impl_->ordinaryGraphicsDescriptor = nullptr;
        impl_->ordinaryGraphicsViewport = impl_->ordinaryGraphicsScissor = false;
        break;
    }
    case K::BindRenderDescriptor: {
        const auto& binding = std::get<rhi::OriginalCommandDescriptorBinding>(
            *program.originalInputs()->descriptorAt(command));
        const auto& sources = scalars.originalUsage()->originalSources()->sources();
        const auto& pipeline =
            std::get<std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                sources[binding.pipelineSourceIndex]);
        const auto& source =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                sources[binding.sourceIndex]);
        if (source->originalObject() != incomingSet
            || pipeline->originalObject() != impl_->ordinaryGraphicsPipeline)
            return "RHI-ORDINARY-GRAPHICS-ORIGINAL-DESCRIPTOR";
        const auto native = vulkanOriginalGraphicsDescriptorPayload(source);
        if (!native
            || !recordDescriptorSetBind(
                *native, source->frozenLogicalSet(), nullptr, 0u, diagnostic))
            return "RHI-ORDINARY-GRAPHICS-NATIVE-DESCRIPTOR";
        impl_->ordinaryGraphicsDescriptor = incomingSet;
        break;
    }
    case K::SetViewport:
        if (!impl_->ordinaryGraphicsPipeline
            || !recordSetViewport(*scalar.get<rhi::ViewportState>(), diagnostic))
            return "RHI-ORDINARY-GRAPHICS-NATIVE-VIEWPORT";
        impl_->ordinaryGraphicsViewport = true;
        break;
    case K::SetScissor:
        if (!impl_->ordinaryGraphicsPipeline
            || !recordSetScissor(*scalar.get<rhi::ScissorState>(), diagnostic))
            return "RHI-ORDINARY-GRAPHICS-NATIVE-SCISSOR";
        impl_->ordinaryGraphicsScissor = true;
        break;
    case K::Draw: {
        const auto& draw = *scalar.get<rhi::CommandScalarArguments::Draw>();
        if (!impl_->ordinaryGraphicsPipeline || !impl_->ordinaryGraphicsDescriptor
            || !impl_->ordinaryGraphicsViewport || !impl_->ordinaryGraphicsScissor)
            return "RHI-ORDINARY-GRAPHICS-DRAW-STATE";
        if (!recordDraw(
                draw.vertices, draw.instances, draw.firstVertex, draw.firstInstance, diagnostic))
            return "RHI-ORDINARY-GRAPHICS-NATIVE-DRAW";
        break;
    }
    default:
        return "RHI-ORDINARY-GRAPHICS-COMMAND-PROFILE";
    }
    consumeOrdinaryAttachmentOperation();
    return nullptr;
}

const char* VulkanRhiNativeCommandPayload::sealOrdinaryAttachments(std::uint32_t command) noexcept
{
    if (!hasOrdinaryAttachments())
        return nullptr;
    if (const auto* id =
            preflightOrdinaryAttachmentOperation(rhi::CommandUsageKind::Finish, command))
        return id;
    if (impl_->activeRenderPass.renderPass || impl_->ordinaryComputeActive
        || impl_->ordinaryTransferActive
        || command + 1u != impl_->recordingStorage->originalProgram->operations().size())
        return "RHI-ORDINARY-ATTACHMENT-RECORDING-INCOMPLETE";
    for (std::size_t i = 0u; i < impl_->retainedRenderPassCount; ++i)
        if (!impl_->retainedRenderPasses[i].originalContent->ended)
            return "RHI-ORDINARY-ATTACHMENT-RECORDING-INCOMPLETE";
    if (const auto* id = validateOrdinaryAttachmentsPending())
        return id;
    consumeOrdinaryAttachmentOperation();
    impl_->ordinaryAttachmentMeter->seal();
    impl_->ordinaryAttachmentSealed = true;
    return nullptr;
}

bool VulkanRhiNativeCommandPayload::hasOrdinaryMixedPasses() const noexcept
{
    return hasOrdinaryAttachments() && impl_->ordinaryMixedPasses;
}

const char* VulkanRhiNativeCommandPayload::preflightOrdinaryBufferTransition(
    const rhi::GpuBuffer& object,
    const rhi::ResourceBarrierDesc& descriptor,
    std::uint32_t command) const noexcept
{
    using K = rhi::CommandUsageKind;
    if (!hasOrdinaryMixedPasses())
        return "RHI-ORDINARY-MIXED-RECORDING-OPT-IN";
    if (const auto* id = preflightOrdinaryAttachmentOperation(K::BufferBarrier, command))
        return id;
    if (impl_->activeRenderPass.renderPass || impl_->ordinaryComputeActive
        || impl_->ordinaryTransferActive)
        return "RHI-ORDINARY-MIXED-BARRIER-IN-PASS";
    const auto& program = *impl_->recordingStorage->originalProgram;
    const auto& operation = program.operations()[command];
    const auto* input = program.originalInputs()->descriptorAt(command);
    const auto* expected = input ? std::get_if<rhi::ResourceDependencyArguments>(input) : nullptr;
    if (!expected || !expected->matches(K::BufferBarrier, descriptor)
        || operation.referenceCount != 1u)
        return "RHI-ORDINARY-MIXED-BUFFER-BARRIER-JOIN";
    const auto& usage = *program.originalInputs()->originalScalars()->originalUsage();
    const auto index = usage.references()[operation.firstReference].sourceIndex;
    const auto* lease = std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
        &usage.originalSources()->sources()[index]);
    return lease && (*lease)->originalObject() == &object
        ? nullptr
        : "RHI-ORDINARY-MIXED-BUFFER-BARRIER-SOURCE";
}

const char* VulkanRhiNativeCommandPayload::recordOrdinaryMixedCommand(std::uint32_t command,
    const rhi::CommandScalarArguments& scalar,
    const rhi::ComputePipeline* incomingPipeline,
    const rhi::DescriptorSet* incomingSet,
    const rhi::TextureToBufferCopyDesc* incomingCopy) noexcept
{
    using K = rhi::CommandUsageKind;
    if (!hasOrdinaryMixedPasses())
        return "RHI-ORDINARY-MIXED-RECORDING-OPT-IN";
    if (const auto* id = preflightOrdinaryAttachmentOperation(scalar.kind(), command))
        return id;
    if (impl_->activeRenderPass.renderPass)
        return "RHI-ORDINARY-MIXED-PASS-ORDER";
    const auto& program = *impl_->recordingStorage->originalProgram;
    const auto& scalars = *program.originalInputs()->originalScalars();
    const auto& expected = scalars.originalArguments()->records()[command];
    if (!expected.scalar.matches(scalar) || expected.offsetCount || expected.byteCount)
        return "RHI-ORDINARY-MIXED-ORIGINAL-ARGUMENTS";
    if (const auto* id = claimOrdinaryAttachments())
        return id;
    if (const auto* id = validateOrdinaryAttachmentsPending())
        return id;
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    const auto& sources = scalars.originalUsage()->originalSources()->sources();
    switch (scalar.kind()) {
    case K::BeginComputePass:
    case K::BeginTransferPass:
        if (impl_->ordinaryComputeActive || impl_->ordinaryTransferActive)
            return "RHI-ORDINARY-MIXED-PASS-ORDER";
        if (!flushPendingBarriers(diagnostic))
            return "RHI-ORDINARY-MIXED-BARRIER-FLUSH";
        impl_->ordinaryComputeActive = scalar.kind() == K::BeginComputePass;
        impl_->ordinaryTransferActive = scalar.kind() == K::BeginTransferPass;
        impl_->ordinaryComputePipeline = nullptr;
        impl_->ordinaryComputeDescriptor = nullptr;
        break;
    case K::EndComputePass:
        if (!impl_->ordinaryComputeActive)
            return "RHI-ORDINARY-MIXED-PASS-ORDER";
        impl_->ordinaryComputeActive = false;
        impl_->ordinaryComputePipeline = nullptr;
        impl_->ordinaryComputeDescriptor = nullptr;
        break;
    case K::EndTransferPass:
        if (!impl_->ordinaryTransferActive)
            return "RHI-ORDINARY-MIXED-PASS-ORDER";
        impl_->ordinaryTransferActive = false;
        break;
    case K::BindComputePipeline: {
        const auto& binding = std::get<rhi::OriginalCommandPipelineBinding>(
            *program.originalInputs()->descriptorAt(command));
        const auto& source =
            std::get<std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(
                sources[binding.sourceIndex]);
        if (!impl_->ordinaryComputeActive || source->originalObject() != incomingPipeline
            || rhi::OriginalCommandComputePipelineSource::validateOriginal(source))
            return "RHI-ORDINARY-MIXED-ORIGINAL-PIPELINE";
        const auto native = vulkanRhiNativePayload(*incomingPipeline);
        if (!native || !recordComputePipelineBind(*native, diagnostic))
            return "RHI-ORDINARY-MIXED-NATIVE-PIPELINE";
        impl_->ordinaryComputePipeline = incomingPipeline;
        impl_->ordinaryComputeDescriptor = nullptr;
        break;
    }
    case K::BindComputeDescriptor: {
        const auto& binding = std::get<rhi::OriginalCommandDescriptorBinding>(
            *program.originalInputs()->descriptorAt(command));
        const auto& pipeline =
            std::get<std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(
                sources[binding.pipelineSourceIndex]);
        const auto& source =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                sources[binding.sourceIndex]);
        if (!impl_->ordinaryComputeActive || source->sourceContractVersion() != 7u
            || source->originalObject() != incomingSet
            || pipeline->originalObject() != impl_->ordinaryComputePipeline)
            return "RHI-ORDINARY-MIXED-ORIGINAL-DESCRIPTOR";
        const auto native = vulkanOriginalSynchronousDescriptorPayload(source);
        if (!native
            || !recordDescriptorSetBind(
                *native, source->frozenLogicalSet(), nullptr, 0u, diagnostic))
            return "RHI-ORDINARY-MIXED-NATIVE-DESCRIPTOR";
        impl_->ordinaryComputeDescriptor = incomingSet;
        break;
    }
    case K::Dispatch: {
        if (!impl_->ordinaryComputeActive || !impl_->ordinaryComputePipeline
            || !impl_->ordinaryComputeDescriptor)
            return "RHI-ORDINARY-MIXED-DISPATCH-STATE";
        const auto& dispatch = *scalar.get<rhi::CommandScalarArguments::Dispatch>();
        if (!recordDispatch(dispatch.x, dispatch.y, dispatch.z, diagnostic))
            return "RHI-ORDINARY-MIXED-NATIVE-DISPATCH";
        break;
    }
    case K::TextureToBufferCopy: {
        const auto* copy = std::get_if<rhi::TransferCopyArguments>(
            program.originalInputs()->descriptorAt(command));
        if (!impl_->ordinaryTransferActive || !copy || !incomingCopy
            || !copy->matches(*incomingCopy))
            return "RHI-ORDINARY-MIXED-ORIGINAL-COPY";
        const auto& operation = program.operations()[command];
        const auto& usage = *scalars.originalUsage();
        const auto& source = std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
            sources[usage.references()[operation.firstReference].sourceIndex]);
        const auto& destination = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
            sources[usage.references()[operation.firstReference + 1u].sourceIndex]);
        if (destination->originalObject()->gpuTransferWritesSealed()
            || !rhi::OriginalCommandStateTransaction::ordinaryAttachmentState(
                *source->originalObject(),
                incomingCopy->textureSubresources,
                rhi::ResourceState::CopySrc,
                *impl_->retainedResourceStates)
            || !rhi::OriginalCommandStateTransaction::ordinaryBufferState(
                *destination->originalObject(),
                rhi::ResourceState::CopyDst,
                *impl_->retainedResourceStates))
            return "RHI-ORDINARY-MIXED-COPY-STATE";
        const auto nativeSource = vulkanRhiNativePayload(*source->originalObject());
        const auto nativeDestination = vulkanRhiNativePayload(*destination->originalObject());
        if (!nativeSource || !nativeDestination
            || !recordImageToBufferCopyImpl(
                *nativeSource, *nativeDestination, *incomingCopy, diagnostic, true))
            return "RHI-ORDINARY-MIXED-NATIVE-COPY";
        break;
    }
    default:
        return "RHI-ORDINARY-MIXED-COMMAND-PROFILE";
    }
    consumeOrdinaryAttachmentOperation();
    return nullptr;
}

} // namespace arp::rhi_vulkan
