#include "rhi/backends/vulkan/vulkan_attachment_operations.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_graphics_pass.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_render_pass_storage.h"
#include "rhi/frontend/command_recorded_state.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>

#ifdef NDEBUG
#error Render pass storage tests require assertions
#endif

namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_vulkan;
constexpr std::uint64_t bytes = 65536u;

class Fixture final {
public:
    Fixture(const std::shared_ptr<VulkanRhiRuntime>& runtime,
        LoadOp load = LoadOp::Clear,
        StoreOp storeOp = StoreOp::Store,
        bool unsupportedSecond = false,
        bool originalContent = false,
        std::optional<LoadOp> secondLoad = {},
        bool recording = false,
        unsigned invalidRecording = 0u)
    {
        for (auto& quota : quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        VulkanRhiDeviceDesc desc;
        desc.runtime = runtime;
        desc.capabilityContract.backend = Backend::Vulkan;
        desc.memoryBudgetBytes = 1024u * 1024u;
        device = createVulkanRhiDevice(desc);
        assert(
            device && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, quotas[0]));
        if (originalContent)
            assert(!device->prepareOriginalDeviceNativeResultLoss(
                {1u, true, 1u, 2u, bytes}, quotas[6]));
        assert(!device->prepareOriginalResourceRegistration(
            {1u, true, 0u, 2u, 2u, 11u, bytes}, quotas[1]));
        using Sources = OriginalCommandResourceSources;
        std::array<Sources::Source, 4u> sources;
        std::array<GpuResourceId, 2u> ids;
        for (std::size_t i = 0u; i < 2u; ++i) {
            TextureDesc td;
            td.extent = {8u, 8u, 1u};
            td.mipLevelCount = 3u;
            td.arrayLayerCount = 3u;
            td.format = i ? TextureFormat::D32Float : TextureFormat::Rgba16Float;
            td.usage = TextureUsage::RenderAttachment;
            core::UniquePtr<GpuTexture> texture;
            if (originalContent) {
                const char* id = nullptr;
                std::uint64_t footprint = 0u;
                assert(!semantics::TextureUploadDestinationValidation::footprint(td, footprint));
                const auto allocation = device->queryOriginalContentTextureAllocation(td, id, 4u);
                assert(allocation && !id);
                OriginalTextureContentCreationBudget cb{
                    4u, true, 1u, 1u, footprint, 4u, bytes, 9u, allocation};
                core::CpuMemoryQuotaScope scope(quotas[2]);
                texture = device->createOriginalContentTexture(td, cb, quotas[5], id);
                if (id)
                    std::fprintf(stderr, "attachment content creation: %s\n", id);
                assert(!id);
            } else
                texture = device->createTexture(td);
            assert(texture);
            TextureViewDesc vd;
            vd.label = core::pmr::String(256u, 'v');
            vd.format = td.format;
            vd.usage = td.usage;
            vd.baseMipLevel = 1u;
            vd.baseArrayLayer = 2u;
            vd.aspect = i ? TextureAspect::Depth : TextureAspect::Color;
            auto view = device->createTextureView(*texture, vd);
            assert(view);
            ids[i] = view->id();
            const char* id = nullptr;
            textureObjects[i] = texture.get();
            auto parent = OriginalCommandTextureLease::prepareOriginal(
                texture, {1u, true, 2u, bytes}, quotas[2], id);
            auto lease = OriginalCommandTextureViewLease::prepareOriginal(
                view, {1u, true, 2u, bytes}, quotas[2], id);
            auto source = OriginalCommandTextureViewSource::prepareOriginal(
                lease, parent, {1u, true, 5u, bytes}, quotas[2], id);
            assert(source && !id);
            sources[i] = parent;
            sources[i + 2u] = source;
        }
        using K = CommandUsageKind;
        core::pmr::Vector<CommandUsage> commands;
        core::pmr::Vector<OriginalCommandUsage::Reference> references;
        if (recording) {
            commands.push_back({K::TextureBarrier, 0u, 1u});
            commands.push_back({K::TextureBarrier, 1u, 1u});
            references.push_back({0u});
            references.push_back({1u});
        }
        for (unsigned pass = 0u; pass < 2u; ++pass) {
            if (recording && pass && invalidRecording != 1u) {
                commands.push_back(
                    {K::TextureBarrier, static_cast<std::uint32_t>(references.size()), 1u});
                references.push_back({0u});
                commands.push_back(
                    {K::TextureBarrier, static_cast<std::uint32_t>(references.size()), 1u});
                references.push_back({1u});
            }
            commands.push_back(
                {K::BeginRenderPass, static_cast<std::uint32_t>(references.size()), 2u});
            references.push_back({2u});
            references.push_back({3u});
            commands.push_back(
                {K::EndRenderPass, static_cast<std::uint32_t>(references.size()), 0u});
        }
        commands.push_back({K::Finish, static_cast<std::uint32_t>(references.size()), 0u});
        OriginalCommandAdmissionBudget b;
        b.contractVersion = 1u;
        b.prototypeOptIn = true;
        b.maxCommands = commands.size();
        b.maxReferences = references.size();
        b.maxInputDescriptors = recording ? (invalidRecording == 1u ? 4u : 6u) : 2u;
        b.maxGraphOwners = 64u;
        b.maxPassColorAttachments = 2u;
        b.maxPassAttachmentReferences = 4u;
        b.maxLabelBytes = 640u;
        b.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Texture)] = 2u;
        b.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::TextureView)] = 2u;
        for (const auto domain : {OriginalCommandAdmissionBudget::CpuDomain::Source,
                 OriginalCommandAdmissionBudget::CpuDomain::Graph,
                 OriginalCommandAdmissionBudget::CpuDomain::Recording})
            b.maxCpuBytes[static_cast<std::size_t>(domain)] = bytes;
        const char* id = nullptr;
        auto source = Sources::prepareOriginal(sources.data(), sources.size(), b, quotas[2], id);
        assert(source && !id);
        auto usage = OriginalCommandUsage::prepareOriginal(source,
            commands.data(),
            commands.size(),
            references.data(),
            references.size(),
            quotas[3],
            id);
        assert(usage && !id);
        const auto argument = [](K kind) {
            return CommandVariableArguments::Input{
                *CommandScalarArguments::capture(kind, CommandScalarArguments::None{})};
        };
        core::pmr::Vector<CommandVariableArguments::Input> arguments;
        for (const auto& command : commands)
            arguments.push_back(argument(command.kind));
        auto store = CommandVariableArguments::prepareOriginal(arguments.data(),
            arguments.size(),
            {1u, true, static_cast<std::uint32_t>(commands.size()), 0u, 0u, 0u, bytes},
            quotas[3],
            id);
        assert(store && !id);
        auto scalars = OriginalCommandScalars::prepareOriginal(usage, store, quotas[3], id);
        assert(scalars && !id);
        RenderPassDesc pass;
        pass.label = core::pmr::String(256u, 'p');
        RenderPassColorAttachmentDesc color;
        color.label = core::pmr::String(64u, 'c');
        color.target = ids[0];
        color.format = TextureFormat::Rgba16Float;
        color.loadOp = load;
        color.storeOp = storeOp;
        color.clearColor = {0.125f, 0.375f, 0.75f, 0.625f};
        pass.colorAttachments.push_back(color);
        pass.hasDepthStencilAttachment = true;
        pass.depthStencilAttachment.target = ids[1];
        pass.depthStencilAttachment.format = TextureFormat::D32Float;
        pass.depthStencilAttachment.depthLoadOp = load;
        pass.depthStencilAttachment.depthClearValue = 0.25f;
        pass.depthStencilAttachment.depthStoreOp = storeOp;
        pass.depthStencilAttachment.stencilLoadOp = LoadOp::DontCare;
        pass.depthStencilAttachment.stencilStoreOp = StoreOp::DontCare;
        auto frozen = RenderPassArguments::prepareOriginal(pass, budget.arguments, quotas[3], id);
        assert(frozen && !id);
        auto second = frozen;
        if (unsupportedSecond || secondLoad) {
            pass.layeredRendering = unsupportedSecond;
            if (secondLoad) {
                pass.colorAttachments[0].loadOp = *secondLoad;
                pass.depthStencilAttachment.depthLoadOp = *secondLoad;
            }
            second = RenderPassArguments::prepareOriginal(pass, budget.arguments, quotas[3], id);
            assert(second && !id);
        }
        core::pmr::Vector<OriginalCommandInputs::OriginalDescriptor> descriptors;
        if (recording) {
            for (std::size_t i = 0u; i < 2u; ++i) {
                auto& barrier = barriers[i];
                const auto& parent =
                    *std::get<std::shared_ptr<const OriginalCommandTextureLease>>(sources[i])
                         ->originalObject();
                barrier.resource = parent.id();
                barrier.textureSubresources = {
                    1u, 1u, 2u, 1u, i ? TextureAspect::Depth : TextureAspect::Color};
                barrier.after = i ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
                barrier.sourceStages = PipelineStage::TopOfPipe;
                barrier.destinationStages =
                    i ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
                barrier.destinationAccess = i
                    ? ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite
                    : ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite;
                if (invalidRecording == 2u)
                    barrier.destinationAccess = i ? ResourceAccess::DepthStencilWrite
                                                  : ResourceAccess::ColorAttachmentWrite;
                if (invalidRecording == 3u)
                    barrier.before = ResourceState::CopySrc;
                if (invalidRecording == 4u)
                    barrier.sourceStages = PipelineStage::None;
                descriptors.push_back(
                    {i, *ResourceDependencyArguments::capture(K::TextureBarrier, barrier)});
            }
        }
        const std::size_t firstPass = recording ? 2u : 0u;
        descriptors.push_back({firstPass, frozen});
        if (recording && invalidRecording != 1u) {
            for (std::size_t i = 0u; i < 2u; ++i) {
                barriers[i + 2u] = barriers[i];
                barriers[i + 2u].before = barriers[i].after;
                barriers[i + 2u].sourceStages = barriers[i].destinationStages;
                barriers[i + 2u].sourceAccess = barriers[i].destinationAccess;
                if (invalidRecording == 5u)
                    barriers[i + 2u].sourceAccess = i ? ResourceAccess::DepthStencilWrite
                                                      : ResourceAccess::ColorAttachmentWrite;
                descriptors.push_back({4u + i,
                    *ResourceDependencyArguments::capture(K::TextureBarrier, barriers[i + 2u])});
            }
        }
        descriptors.push_back(
            {firstPass + (recording && invalidRecording != 1u ? 4u : 2u), second});
        auto inputs = OriginalCommandInputs::prepareOriginal(
            scalars, descriptors.data(), descriptors.size(), quotas[3], id);
        assert(inputs && !id);
        CommandRecordingBudget recordingBudget{1u, true, commands.size()};
        for (const auto& command : commands)
            recordingBudget.limits.append(command.kind);
        recordingBudget.limits.resourceReferences = references.size();
        OriginalCommandStreamBudget stream;
        stream.contractVersion = 1u;
        stream.prototypeOptIn = true;
        stream.maxCommands = commands.size();
        stream.maxDescriptors = descriptors.size();
        stream.maxReferences = references.size();
        stream.maxRenderPasses = 2u;
        stream.maxAttachmentReferences = 4u;
        stream.maxRetainedOwners = inputs->retainedOwnerCount() + 1u;
        stream.maxCpuMetadataBytes = bytes;
        program = OriginalCommandStreamProgram::prepareRecordingBoundOriginal(
            inputs, stream, recordingBudget, quotas[4], id);
        if (id)
            std::fprintf(stderr, "original render pass: %s\n", id);
        assert(program && !id);
    }

    ~Fixture()
    {
        program.reset();
        device.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
    }

    const OriginalRenderPassStorageContract::Budget budget{
        1u, true, 2u, 2u, 4u, {1u, true, 2u, 0u, 0u, 0u, 640u, bytes}};

    const GpuTexture& parentAt(std::size_t index) const
    {
        const auto& sources = program->originalInputs()
                                  ->originalScalars()
                                  ->originalUsage()
                                  ->originalSources()
                                  ->sources();
        return *std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
            sources[index + 2u])
                    ->originalParentLease()
                    ->originalObject();
    }

    std::array<GpuTexture*, 2u> textureObjects{};
    std::array<ResourceBarrierDesc, 4u> barriers{};
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 7u> quotas;
    core::UniquePtr<GpuDevice> device;
    std::shared_ptr<const OriginalCommandStreamProgram> program;
};

// Every successful call delegates to the real driver. Failure injection proves
// prefix cleanup; it never stands in for a successful native payload.
class NativePassProbe final {
public:
    NativePassProbe(const Fixture& fixture,
        LoadOp load = LoadOp::Clear,
        StoreOp store = StoreOp::Store,
        unsigned fail = 0u)
        : load_(vulkanRhiAttachmentLoadOp(load)), store_(vulkanRhiAttachmentStoreOp(store)),
          fail_(fail)
    {
        assert(!active_);
        const auto& sources = fixture.program->originalInputs()
                                  ->originalScalars()
                                  ->originalUsage()
                                  ->originalSources()
                                  ->sources();
        for (std::size_t i = 0u; i < images_.size(); ++i) {
            const auto& source =
                std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(sources[i + 2u]);
            images_[i] = vulkanRhiNativePayload(*source->originalParentLease()->originalObject())
                             ->backendVulkanImageHandle();
        }
        active_ = this;
    }

    ~NativePassProbe()
    {
        assert(active_ == this && created == destroyed);
        active_ = nullptr;
    }

    unsigned attempts = 0u, created = 0u, destroyed = 0u, imageDestructions = 0u;
    unsigned queryPoolCreations = 0u, queryResets = 0u, timestampWrites = 0u;
    unsigned queueInvocations = 0u;
    bool failQueue = false;
    const char* missing = nullptr;

    static PFN_vkVoidFunction VKAPI_CALL resolver(VkDevice device, const char* name)
    {
        const std::string_view entry(name);
        if (active_ && active_->missing && entry == active_->missing)
            return nullptr;
        if (entry == "vkCreateRenderPass")
            return reinterpret_cast<PFN_vkVoidFunction>(createPass);
        if (entry == "vkCreateImageView")
            return reinterpret_cast<PFN_vkVoidFunction>(createView);
        if (entry == "vkCreateFramebuffer")
            return reinterpret_cast<PFN_vkVoidFunction>(createFrame);
        if (entry == "vkDestroyRenderPass")
            return reinterpret_cast<PFN_vkVoidFunction>(destroyPass);
        if (entry == "vkDestroyImageView")
            return reinterpret_cast<PFN_vkVoidFunction>(destroyView);
        if (entry == "vkDestroyFramebuffer")
            return reinterpret_cast<PFN_vkVoidFunction>(destroyFrame);
        if (entry == "vkDestroyImage")
            return reinterpret_cast<PFN_vkVoidFunction>(destroyImage);
        if (entry == "vkCreateQueryPool")
            return reinterpret_cast<PFN_vkVoidFunction>(createQueryPool);
        if (entry == "vkCmdResetQueryPool")
            return reinterpret_cast<PFN_vkVoidFunction>(resetQueryPool);
        if (entry == "vkCmdWriteTimestamp")
            return reinterpret_cast<PFN_vkVoidFunction>(writeTimestamp);
        if (entry == "vkQueueSubmit")
            return reinterpret_cast<PFN_vkVoidFunction>(queueSubmit);
        return vkGetDeviceProcAddr(device, name);
    }

private:
    static VkResult VKAPI_CALL queueSubmit(
        VkQueue queue, std::uint32_t count, const VkSubmitInfo* info, VkFence fence)
    {
        if (active_) {
            ++active_->queueInvocations;
            if (active_->failQueue) {
                active_->failQueue = false;
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
        }
        return vkQueueSubmit(queue, count, info, fence);
    }

    static VkResult VKAPI_CALL createQueryPool(VkDevice device,
        const VkQueryPoolCreateInfo* info,
        const VkAllocationCallbacks* allocation,
        VkQueryPool* output)
    {
        if (active_)
            ++active_->queryPoolCreations;
        return vkCreateQueryPool(device, info, allocation, output);
    }

    static void VKAPI_CALL resetQueryPool(
        VkCommandBuffer command, VkQueryPool pool, std::uint32_t first, std::uint32_t count)
    {
        if (active_)
            ++active_->queryResets;
        vkCmdResetQueryPool(command, pool, first, count);
    }

    static void VKAPI_CALL writeTimestamp(VkCommandBuffer command,
        VkPipelineStageFlagBits stage,
        VkQueryPool pool,
        std::uint32_t query)
    {
        if (active_)
            ++active_->timestampWrites;
        vkCmdWriteTimestamp(command, stage, pool, query);
    }

    bool fails() { return ++attempts == fail_; }

    template <class T, std::size_t N> void release(std::array<T, N>& handles, T handle)
    {
        const auto found = std::find(handles.begin(), handles.end(), handle);
        if (found != handles.end()) {
            *found = VK_NULL_HANDLE;
            ++destroyed;
        }
    }

    static VkResult VKAPI_CALL createPass(VkDevice device,
        const VkRenderPassCreateInfo* info,
        const VkAllocationCallbacks* allocation,
        VkRenderPass* output)
    {
        auto* p = active_;
        if (!p)
            return vkCreateRenderPass(device, info, allocation, output);
        assert(info->attachmentCount == 2u && info->subpassCount == 1u && !info->dependencyCount);
        assert(info->pSubpasses[0].colorAttachmentCount == 1u
            && info->pSubpasses[0].pColorAttachments[0].attachment == 0u
            && info->pSubpasses[0].pDepthStencilAttachment->attachment == 1u);
        for (unsigned i = 0u; i < 2u; ++i) {
            const auto& a = info->pAttachments[i];
            const auto layout = i ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                  : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            assert(a.samples == VK_SAMPLE_COUNT_1_BIT && a.loadOp == p->load_
                && a.storeOp == p->store_ && a.initialLayout == layout && a.finalLayout == layout
                && a.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE
                && a.stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE
                && a.format == (i ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT));
        }
        if (p->fails())
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        const auto result = vkCreateRenderPass(device, info, allocation, output);
        assert(result == VK_SUCCESS && p->passCount_ < p->passes_.size());
        p->passes_[p->passCount_++] = *output;
        ++p->created;
        return result;
    }

    static VkResult VKAPI_CALL createView(VkDevice device,
        const VkImageViewCreateInfo* info,
        const VkAllocationCallbacks* allocation,
        VkImageView* output)
    {
        auto* p = active_;
        if (!p)
            return vkCreateImageView(device, info, allocation, output);
        const auto index = p->viewCount_ % 2u;
        const auto& range = info->subresourceRange;
        assert(info->image == p->images_[index] && info->viewType == VK_IMAGE_VIEW_TYPE_2D
            && range.baseMipLevel == 1u && range.levelCount == 1u && range.baseArrayLayer == 2u
            && range.layerCount == 1u
            && range.aspectMask == (index ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT));
        assert(info->components.r == VK_COMPONENT_SWIZZLE_IDENTITY
            && info->components.g == VK_COMPONENT_SWIZZLE_IDENTITY
            && info->components.b == VK_COMPONENT_SWIZZLE_IDENTITY
            && info->components.a == VK_COMPONENT_SWIZZLE_IDENTITY);
        if (p->fails())
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        const auto result = vkCreateImageView(device, info, allocation, output);
        assert(result == VK_SUCCESS && p->viewCount_ < p->views_.size());
        p->views_[p->viewCount_++] = *output;
        ++p->created;
        return result;
    }

    static VkResult VKAPI_CALL createFrame(VkDevice device,
        const VkFramebufferCreateInfo* info,
        const VkAllocationCallbacks* allocation,
        VkFramebuffer* output)
    {
        auto* p = active_;
        if (!p)
            return vkCreateFramebuffer(device, info, allocation, output);
        assert(info->width == 4u && info->height == 4u && info->layers == 1u
            && info->attachmentCount == 2u && info->renderPass == p->passes_[p->passCount_ - 1u]);
        assert(info->pAttachments[0] == p->views_[p->viewCount_ - 2u]
            && info->pAttachments[1] == p->views_[p->viewCount_ - 1u]);
        if (p->fails())
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        const auto result = vkCreateFramebuffer(device, info, allocation, output);
        assert(result == VK_SUCCESS && p->frameCount_ < p->frames_.size());
        p->frames_[p->frameCount_++] = *output;
        ++p->created;
        return result;
    }

    static void VKAPI_CALL destroyFrame(
        VkDevice device, VkFramebuffer handle, const VkAllocationCallbacks* allocation)
    {
        if (active_)
            active_->release(active_->frames_, handle);
        vkDestroyFramebuffer(device, handle, allocation);
    }

    static void VKAPI_CALL destroyView(
        VkDevice device, VkImageView handle, const VkAllocationCallbacks* allocation)
    {
        if (active_)
            active_->release(active_->views_, handle);
        vkDestroyImageView(device, handle, allocation);
    }

    static void VKAPI_CALL destroyPass(
        VkDevice device, VkRenderPass handle, const VkAllocationCallbacks* allocation)
    {
        if (active_)
            active_->release(active_->passes_, handle);
        vkDestroyRenderPass(device, handle, allocation);
    }

    static void VKAPI_CALL destroyImage(
        VkDevice device, VkImage handle, const VkAllocationCallbacks* allocation)
    {
        if (active_
            && std::find(active_->images_.begin(), active_->images_.end(), handle)
                != active_->images_.end()) {
            assert(active_->created == active_->destroyed);
            ++active_->imageDestructions;
        }
        vkDestroyImage(device, handle, allocation);
    }

    inline static NativePassProbe* active_ = nullptr;
    VkAttachmentLoadOp load_;
    VkAttachmentStoreOp store_;
    unsigned fail_ = 0u, passCount_ = 0u, frameCount_ = 0u, viewCount_ = 0u;
    std::array<VkImage, 2u> images_{};
    std::array<VkRenderPass, 2u> passes_{};
    std::array<VkFramebuffer, 2u> frames_{};
    std::array<VkImageView, 4u> views_{};
};

void verifyNativePreparation(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    const VulkanRhiNativeResourceAllocator resources(runtime);
    rhi::semantics::NativeBackendCapabilityContract capabilities;
    capabilities.backend = Backend::Vulkan;
    assert(vulkanRhiAttachmentLoadOp(static_cast<LoadOp>(255u)) == VK_ATTACHMENT_LOAD_OP_MAX_ENUM);
    assert(
        vulkanRhiAttachmentStoreOp(static_cast<StoreOp>(255u)) == VK_ATTACHMENT_STORE_OP_MAX_ENUM);
    const auto create = [&](Fixture& f,
                            const OriginalRenderPassPreparationRequest& request,
                            core::pmr::String& diagnostic) {
        return createVulkanRhiCommandEncoderWithColdRenderPasses(
            resources, {}, nullptr, capabilities, f.program, *f.device, request, diagnostic);
    };
    for (const auto version : {1u, 2u}) {
        for (const auto load : {LoadOp::Load, LoadOp::Clear, LoadOp::DontCare})
            for (const auto store : {StoreOp::Store, StoreOp::DontCare}) {
                Fixture f(runtime, load, store);
                NativePassProbe probe(f, load, store);
                const OriginalRenderPassPreparationRequest request{
                    version, true, f.budget, 8u, version == 2u ? 2u : 0u};
                assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    f.quotas, [&]() noexcept -> const char* {
                        return VulkanRhiRenderPassStorageProjection::inspectPreparation(
                            f.program, request, *f.device, *runtime)
                            .diagnostic;
                    }));
                core::pmr::String diagnostic;
                auto encoder = create(f, request, diagnostic);
                if (!encoder)
                    std::fprintf(stderr, "native pass preparation: %s\n", diagnostic.c_str());
                assert(encoder && diagnostic.empty() && probe.attempts == 8u && probe.created == 8u
                    && !probe.destroyed);
                assert(!probe.queryPoolCreations && !probe.queryResets && !probe.timestampWrites);
                const auto stats = resources.sharedStats();
                assert(!stats->nativeRenderPassBeginRecordCount
                    && !stats->nativeRenderPassEndRecordCount && !stats->nativeQueueSubmitCount);
                const auto& pass = std::get<std::shared_ptr<const RenderPassArguments>>(
                    *f.program->originalInputs()->descriptorAt(0u));
                if (version == 2u) {
                    // An alias with the wrong original control block must not gain
                    // admission merely by pointing at the authentic object's bytes.
                    const std::shared_ptr<const RenderPassArguments> foreign(f.program, pass.get());
                    std::uint32_t commands = 0u;
                    bool active = false;
                    BindlessIndirectDeclarations indirect(0u);
                    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        f.quotas, [&]() noexcept -> const char* {
                            assert(!createVulkanRhiColdRenderPassEncoder(
                                commands, active, 1u, indirect, {}));
                            assert(!createVulkanRhiColdRenderPassEncoder(
                                commands, active, 1u, indirect, foreign));
                            assert(!commands && !active);
                            return nullptr;
                        }));
                }
                const auto count = encoder->lastValidation().recordedCommandCount;
                assert(!count);
                assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    f.quotas, [&]() noexcept -> const char* {
                        const auto first = inspectVulkanRhiColdRenderPass(*encoder, 0u);
                        const auto second = inspectVulkanRhiColdRenderPass(*encoder, 1u);
                        if (version == 2u) {
                            assert(
                                first.wrapper && second.wrapper && first.wrapper != second.wrapper);
                            assert(first.original == pass.get() && second.original == pass.get());
                            assert(first.descriptor == &pass->desc()
                                && second.descriptor == &pass->desc());
                            assert(first.descriptor->label.data() == pass->desc().label.data()
                                && first.descriptor->label.size() == 256u
                                && first.descriptor->colorAttachments.data()
                                    == pass->desc().colorAttachments.data()
                                && first.descriptor->colorAttachments[0].label.data()
                                    == pass->desc().colorAttachments[0].label.data());
                        } else {
                            assert(!first.wrapper && !first.original && !first.descriptor
                                && !second.wrapper && !second.original && !second.descriptor);
                        }
                        const auto absent = inspectVulkanRhiColdRenderPass(*encoder, 2u);
                        assert(!absent.wrapper && !absent.original && !absent.descriptor);
                        assert(encoder->lastValidation().recordedCommandCount == count);
                        return nullptr;
                    }));
                assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    f.quotas, [&]() noexcept -> const char* {
                        assert(!encoder->beginRenderPass(pass->desc()));
                        assert(!encoder->beginRenderPass(pass->desc()));
                        return nullptr;
                    }));
                assert(!stats->nativeRenderPassBeginRecordCount && !stats->nativeQueueSubmitCount);
                auto observer = vulkanRhiCommandPayload(*encoder);
                assert(std::string_view(observer->preparedOrdinaryRenderPassContentDiagnostic())
                    == "RHI-VULKAN-COLD-RENDER-PASS-CONTENT-UNPREPARED");
                std::weak_ptr<const OriginalCommandStreamProgram> original = f.program;
                encoder.reset();
                f.program.reset();
                assert(!original.expired() && !probe.destroyed);
                observer.reset();
                assert(original.expired() && probe.created == probe.destroyed
                    && probe.imageDestructions == 2u);
            }
    }
    for (const auto version : {1u, 2u}) {
        for (unsigned fail = 1u; fail <= 8u; ++fail) {
            Fixture f(runtime);
            NativePassProbe probe(f, LoadOp::Clear, StoreOp::Store, fail);
            core::pmr::String diagnostic;
            assert(!create(f, {version, true, f.budget, 8u, version == 2u ? 2u : 0u}, diagnostic));
            assert(diagnostic == "RHI-VULKAN-COLD-RENDER-PASS-NATIVE-CREATION"
                && probe.attempts == fail && probe.created == fail - 1u
                && probe.destroyed == probe.created);
        }
        for (const auto* missing : {"vkCreateRenderPass",
                 "vkCreateImageView",
                 "vkCreateFramebuffer",
                 "vkDestroyRenderPass",
                 "vkDestroyImageView",
                 "vkDestroyFramebuffer"}) {
            Fixture f(runtime);
            NativePassProbe probe(f);
            probe.missing = missing;
            core::pmr::String diagnostic;
            assert(!create(f, {version, true, f.budget, 8u, version == 2u ? 2u : 0u}, diagnostic)
                && !probe.attempts
                && diagnostic == "RHI-VULKAN-COLD-RENDER-PASS-PREPARATION-ENTRYPOINT");
            probe.missing = nullptr;
        }
    }
    {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, true);
        NativePassProbe probe(f);
        core::pmr::String diagnostic;
        assert(!create(f, {1u, true, f.budget, 8u}, diagnostic) && !probe.attempts
            && diagnostic == "RHI-RENDER-PASS-PREPARATION-STRUCTURE");
    }
    for (const auto version : {1u, 2u}) {
        Fixture f(runtime);
        NativePassProbe probe(f);
        core::pmr::String diagnostic;
        assert(!create(f, {version, true, f.budget, 7u, version == 2u ? 2u : 0u}, diagnostic)
            && !probe.attempts);
        core::CpuMemoryQuotaLease pressure(
            f.quotas[4], f.quotas[4]->limitBytes() - f.quotas[4]->liveBytes());
        assert(pressure.charged());
        assert(!create(f, {version, true, f.budget, 8u, version == 2u ? 2u : 0u}, diagnostic)
            && !probe.attempts);
    }
    {
        Fixture f(runtime);
        NativePassProbe probe(f);
        core::pmr::String diagnostic;
        assert(!create(f, {2u, true, f.budget, 8u, 1u}, diagnostic) && !probe.attempts
            && diagnostic == "RHI-RENDER-PASS-PREPARATION-WRAPPER-CAPACITY");
        assert(!create(f, {2u, true, f.budget, 7u, 2u}, diagnostic) && !probe.attempts
            && diagnostic == "RHI-VULKAN-COLD-RENDER-PASS-OBJECT-CAPACITY");
    }
    for (const auto version : {1u, 2u}) {
        Fixture f(runtime);
        core::pmr::String diagnostic;
        const auto factory = [&] {
            return create(f, {version, true, f.budget, 8u, version == 2u ? 2u : 0u}, diagnostic);
        };
        test::CpuQuotaFactoryFixture quota(f.quotas[4]);
        const auto required = quota.measureSingleUse([] {}, factory);
        quota.verifySingleUse(required, [] {}, factory);
        std::printf("COLD_RENDER_PASS_RECORDING_CPU_EXACT_BYTES version%u %llu\n",
            version,
            static_cast<unsigned long long>(required));
    }
    for (const auto version : {1u, 2u}) {
        Fixture f(runtime);
        NativePassProbe probe(f);
        core::pmr::String diagnostic;
        auto encoder =
            create(f, {version, true, f.budget, 8u, version == 2u ? 2u : 0u}, diagnostic);
        assert(encoder);
        auto observer = vulkanRhiCommandPayload(*encoder);
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
        assert(observer && gate && observer->end(diagnostic));
        assert(!probe.queryPoolCreations && !probe.queryResets && !probe.timestampWrites);
        // A real gated command with no Graphics work tests native object retirement only.
        // It does not execute any original Graphics operation or grant it authority.
        assert(observer->submit({gate, 1u}, diagnostic));
        encoder.reset();
        f.program.reset();
        assert(!observer->submittedFenceCompleted() && !probe.destroyed);
        assert(gate->hostSignal(1u) == VK_SUCCESS);
        assert(observer->waitSubmittedFenceUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        assert(observer->submittedFenceCompleted() && probe.created == probe.destroyed
            && !probe.imageDestructions);
        retireCompletedVulkanRhiSubmittedCommands();
        observer.reset();
        assert(probe.imageDestructions == 2u);
        assert(!probe.queryPoolCreations && !probe.queryResets && !probe.timestampWrites);
    }
    std::puts(
        "ORIGINAL_RENDER_PASS_COLD_NATIVE_OK versions1-2 real-native-objects8 wrappers0-2 original-descriptor-borrowed exact-mip1-layer2 framebuffer4x4 load-store6 failure-prefix8 entrypoint6 original-owner-retained gated-non-graphics-command-retirement unbudgeted-query-pool-reset-write0 no-graphics-recording");
}

void verifyContentObservations(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    using Contract = OriginalRenderPassContentContract;
    const OriginalRenderPassContentBudget content{1u, true, 4u, 9u, 16u, 20u, 32u};
    const VulkanRhiNativeResourceAllocator resources(runtime);
    semantics::NativeBackendCapabilityContract capabilities;
    capabilities.backend = Backend::Vulkan;
    const auto requestFor = [&](const Fixture& f) {
        return OriginalRenderPassPreparationRequest{3u, true, f.budget, 8u, 2u, content};
    };
    const auto create = [&](Fixture& f,
                            const OriginalRenderPassPreparationRequest& request,
                            core::pmr::String& diagnostic) {
        return createVulkanRhiCommandEncoderWithColdRenderPasses(
            resources, {}, nullptr, capabilities, f.program, *f.device, request, diagnostic);
    };
    const auto reject = [&](Fixture& f, const OriginalRenderPassContentBudget& budget) {
        std::size_t callbacks = 0u;
        const auto result = Contract::visit(f.program,
            f.budget,
            budget,
            [&](auto, auto, const auto&, const auto&, bool) noexcept -> const char* {
                ++callbacks;
                return nullptr;
            });
        assert(result.diagnostic && !callbacks && !result.counts.observations
            && !result.counts.contentSteps && !result.counts.commandVisits
            && !result.counts.projectionVisits);
        return result.diagnostic;
    };
    for (auto load : {LoadOp::Load, LoadOp::Clear, LoadOp::DontCare})
        for (auto store : {StoreOp::Store, StoreOp::DontCare}) {
            Fixture f(runtime, load, store, false, true);
            NativePassProbe probe(f, load, store);
            const auto request = requestFor(f);
            core::pmr::String diagnostic;
            if (load == LoadOp::Load) {
                assert(std::string_view(reject(f, content))
                    == "RHI-ORIGINAL-CONTENT-SEQUENCE-LOAD-UNINITIALIZED");
                assert(!create(f, request, diagnostic) && !probe.attempts
                    && diagnostic == "RHI-ORIGINAL-CONTENT-SEQUENCE-LOAD-UNINITIALIZED");
                continue;
            }
            const auto inspect = [&]() noexcept -> const char* {
                std::size_t callbacks = 0u;
                const auto result = Contract::visit(f.program,
                    f.budget,
                    content,
                    [&](std::size_t command,
                        std::size_t operand,
                        const auto& initial,
                        const auto& state,
                        bool stored) noexcept -> const char* {
                        assert(command == (callbacks / 2u) * 2u && operand == callbacks % 2u);
                        assert(initial.range().baseMipLevel == 1u
                            && initial.range().baseArrayLayer == 2u
                            && initial.range().mipLevelCount == 1u
                            && initial.range().arrayLayerCount == 1u
                            && initial.range() == state.range());
                        assert(initial.controlVersion() == 4u
                            && initial.state()
                                == common::OriginalTextureContentControl::State::Uninitialized
                            && state.state() == ResourceState::Undefined
                            && !initial.nativeExecutionProven() && !state.nativeExecutionProven());
                        assert(stored == (load == LoadOp::Clear && store == StoreOp::Store));
                        const auto& texture = f.parentAt(operand);
                        assert(!texture.validateOriginalContent(initial)
                            && !texture.validateOriginalCommittedState(state));
                        const auto after = texture.observeOriginalContent(initial.range());
                        const auto afterState =
                            texture.observeOriginalCommittedState(state.range());
                        assert(after && afterState && after->revision() == initial.revision()
                            && after->contentGeneration() == initial.contentGeneration()
                            && afterState->revision() == state.revision());
                        ++callbacks;
                        return nullptr;
                    });
                if (result.diagnostic)
                    return result.diagnostic;
                assert(callbacks == 4u && result.counts.observations == 4u
                    && result.counts.contentSteps == 16u && result.counts.commandVisits == 20u
                    && result.counts.projectionVisits == 32u);
                return nullptr;
            };
            assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(f.quotas, inspect));
            for (auto field : {&OriginalRenderPassContentBudget::maxAttachmentObservations,
                     &OriginalRenderPassContentBudget::maxParentCells,
                     &OriginalRenderPassContentBudget::maxContentSteps,
                     &OriginalRenderPassContentBudget::maxCommandVisits,
                     &OriginalRenderPassContentBudget::maxProjectionVisits}) {
                auto shortBudget = content;
                --(shortBudget.*field);
                assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    f.quotas, [&]() noexcept -> const char* {
                        assert(reject(f, shortBudget));
                        return nullptr;
                    }));
                auto shortRequest = request;
                shortRequest.content = shortBudget;
                assert(!create(f, shortRequest, diagnostic) && !probe.attempts);
            }
            auto encoder = create(f, request, diagnostic);
            if (!encoder)
                std::fprintf(stderr, "content preparation: %s\n", diagnostic.c_str());
            assert(encoder && diagnostic.empty() && probe.created == 8u);
            auto observer = vulkanRhiCommandPayload(*encoder);
            assert(observer);
            assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                f.quotas, [&]() noexcept -> const char* {
                    assert(inspectVulkanRhiColdRenderPass(*encoder, 0u).wrapper
                        && inspectVulkanRhiColdRenderPass(*encoder, 1u).wrapper);
                    assert(!encoder->beginRenderPass(
                        std::get<std::shared_ptr<const RenderPassArguments>>(
                            *f.program->originalInputs()->descriptorAt(0u))
                            ->desc()));
                    return observer->preparedOrdinaryRenderPassContentDiagnostic();
                }));
            assert(!inspect());
            const auto stats = resources.sharedStats();
            assert(!stats->nativeRenderPassBeginRecordCount
                && !stats->nativeRenderPassEndRecordCount && !stats->nativeQueueSubmitCount);
            std::weak_ptr<const OriginalCommandStreamProgram> original = f.program;
            encoder.reset();
            f.program.reset();
            assert(!original.expired() && !probe.destroyed
                && !observer->preparedOrdinaryRenderPassContentDiagnostic());
            observer.reset();
            assert(original.expired() && probe.created == probe.destroyed
                && probe.imageDestructions == 2u);
        }
    // A later Load consumes the original timeline's Clear/Store result, while
    // Clear/Discard and DontCare/Store never manufacture initialized contents.
    for (auto load : {LoadOp::Clear, LoadOp::DontCare})
        for (auto store : {StoreOp::Store, StoreOp::DontCare}) {
            Fixture f(runtime, load, store, false, true, LoadOp::Load);
            if (load == LoadOp::Clear && store == StoreOp::Store)
                assert(!Contract::inspect(f.program, f.budget, content).diagnostic);
            else
                assert(std::string_view(reject(f, content))
                    == "RHI-ORIGINAL-CONTENT-SEQUENCE-LOAD-UNINITIALIZED");
        }
    {
        Fixture legacy(runtime);
        assert(std::string_view(reject(legacy, content))
            == "RHI-RENDER-PASS-CONTENT-ORIGINAL-CONTROL");
    }
    {
        Fixture unsupported(runtime, LoadOp::Clear, StoreOp::Store, true, true);
        assert(std::string_view(reject(unsupported, content))
            == "RHI-RENDER-PASS-PREPARATION-STRUCTURE");
    }
    for (const char* missing : {"vkCreateRenderPass",
             "vkCreateImageView",
             "vkCreateFramebuffer",
             "vkDestroyRenderPass",
             "vkDestroyImageView",
             "vkDestroyFramebuffer"}) {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, false, true);
        NativePassProbe probe(f);
        probe.missing = missing;
        core::pmr::String diagnostic;
        assert(!create(f, requestFor(f), diagnostic) && !probe.attempts
            && diagnostic == "RHI-VULKAN-COLD-RENDER-PASS-PREPARATION-ENTRYPOINT");
        probe.missing = nullptr;
    }
    for (unsigned fail = 1u; fail <= 8u; ++fail) {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, false, true);
        NativePassProbe probe(f, LoadOp::Clear, StoreOp::Store, fail);
        core::pmr::String diagnostic;
        const auto baseline = f.quotas[4]->liveBytes();
        auto rejected = create(f, requestFor(f), diagnostic);
        const bool expectedDiagnostic = diagnostic == "RHI-VULKAN-COLD-RENDER-PASS-NATIVE-CREATION";
        // This explicit caller output was charged while the cold factory's
        // Recording quota was active. Release it before checking internal
        // failed-owner cleanup; retaining a diagnostic is not an owner leak.
        {
            core::pmr::String released(diagnostic.get_allocator());
            released.swap(diagnostic);
        }
        if (rejected || !expectedDiagnostic || probe.attempts != fail
            || probe.created != probe.destroyed || f.quotas[4]->liveBytes() != baseline)
            std::fprintf(stderr,
                "content native failure %u: result=%d id=%s attempts=%u created=%u destroyed=%u cpu=%llu baseline=%llu\n",
                fail,
                bool(rejected),
                diagnostic.c_str(),
                probe.attempts,
                probe.created,
                probe.destroyed,
                static_cast<unsigned long long>(f.quotas[4]->liveBytes()),
                static_cast<unsigned long long>(baseline));
        assert(!rejected && expectedDiagnostic && probe.attempts == fail
            && probe.created == probe.destroyed && f.quotas[4]->liveBytes() == baseline);
    }
    {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, false, true);
        core::pmr::String diagnostic;
        const auto factory = [&] {
            return create(f, requestFor(f), diagnostic);
        };
        test::CpuQuotaFactoryFixture quota(f.quotas[4]);
        const auto required = quota.measureSingleUse([] {}, factory);
        quota.verifySingleUse(required, [] {}, factory);
        std::printf("COLD_RENDER_PASS_RECORDING_CPU_EXACT_BYTES version3 %llu\n",
            static_cast<unsigned long long>(required));
    }
    {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, false, true);
        NativePassProbe probe(f);
        core::pmr::String diagnostic;
        auto encoder = create(f, requestFor(f), diagnostic);
        assert(encoder);
        auto observer = vulkanRhiCommandPayload(*encoder);
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
        assert(observer && gate && !observer->preparedOrdinaryRenderPassContentDiagnostic()
            && observer->end(diagnostic) && observer->submit({gate, 1u}, diagnostic));
        encoder.reset();
        f.program.reset();
        assert(!observer->submittedFenceCompleted() && !probe.destroyed
            && !observer->preparedOrdinaryRenderPassContentDiagnostic());
        assert(gate->hostSignal(1u) == VK_SUCCESS);
        assert(observer->waitSubmittedFenceUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        assert(observer->submittedFenceCompleted() && probe.created == probe.destroyed
            && !probe.imageDestructions
            && std::string_view(observer->preparedOrdinaryRenderPassContentDiagnostic())
                == "RHI-VULKAN-COLD-RENDER-PASS-CONTENT-UNPREPARED");
        retireCompletedVulkanRhiSubmittedCommands();
        observer.reset();
        assert(probe.imageDestructions == 2u);
        assert(!probe.queryPoolCreations && !probe.queryResets && !probe.timestampWrites);
    }
    std::puts("ORIGINAL_RENDER_PASS_CONTENT_OBSERVATIONS_OK Creation4 exact-mip1-layer2 "
              "observations4 steps16 visits20 projections32 budgets5 load-store6 "
              "late-load-preflight-atomic zero-mutation native-prefix-cleanup8 "
              "original-owner-retained timeline-gated-no-graphics-retirement recording-closed");
}

void verifyOrdinaryAttachmentRecording(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    const VulkanRhiNativeResourceAllocator resources(runtime);
    rhi::semantics::NativeBackendCapabilityContract capabilities;
    capabilities.backend = Backend::Vulkan;
    const auto requestFor = [](const Fixture& f) {
        return OriginalRenderPassPreparationRequest{4u,
            true,
            f.budget,
            8u,
            2u,
            OriginalRenderPassContentBudget{1u, true, 4u, 9u, 16u, 36u, 32u},
            OriginalRenderPassRecordingBudget{1u, true, 36u, 84u}};
    };
    const auto create = [&](Fixture& f, const auto& request, core::pmr::String& diagnostic) {
        return createVulkanRhiCommandEncoderWithColdRenderPasses(
            resources, {}, nullptr, capabilities, f.program, *f.device, request, diagnostic);
    };
    const auto passAt = [](Fixture& f, std::size_t command) -> const RenderPassDesc& {
        return std::get<std::shared_ptr<const RenderPassArguments>>(
            *f.program->originalInputs()->descriptorAt(command))
            ->desc();
    };
    const auto record = [&](Fixture& f, CommandEncoder& encoder) {
        for (std::size_t p = 0u; p < 2u; ++p) {
            for (std::size_t i = 0u; i < 2u; ++i)
                assert(encoder.transition(*f.textureObjects[i], f.barriers[p * 2u + i]).success);
            const auto cold = inspectVulkanRhiColdRenderPass(encoder, p);
            auto pass = encoder.beginRenderPass(passAt(f, p ? 6u : 2u));
            assert(pass && pass.get() == cold.wrapper && pass->end().success);
        }
    };
    for (unsigned invalid = 1u; invalid <= 5u; ++invalid) {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, false, true, {}, true, invalid);
        NativePassProbe probe(f);
        const auto request = requestFor(f);
        const auto rejected = OriginalRenderPassRecordingContract::inspect(
            f.program, request.storage, *request.content, *request.recording);
        const std::array<const char*, 5u> expected{"RHI-RECORDED-RESOURCE-DEPENDENCY-MISSING",
            "RHI-RECORDED-RESOURCE-DEPENDENCY-DESTINATION",
            "RHI-ORDINARY-ATTACHMENT-RECORDING-BARRIER-BEFORE",
            "RHI-STATE-SEQUENCE-DISCARD-SCOPE",
            "RHI-RECORDED-RESOURCE-DEPENDENCY-SOURCE"};
        assert(rejected.diagnostic
            && std::string_view(rejected.diagnostic) == expected[invalid - 1u]
            && !rejected.commandVisits && !rejected.attachmentVisits);
        core::pmr::String diagnostic;
        assert(!create(f, request, diagnostic) && !probe.attempts
            && diagnostic == expected[invalid - 1u]);
    }
    for (const char* missing :
        {"vkCmdBeginRenderPass", "vkCmdEndRenderPass", "vkCmdPipelineBarrier"}) {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, false, true, {}, true);
        NativePassProbe probe(f);
        probe.missing = missing;
        core::pmr::String diagnostic;
        assert(!create(f, requestFor(f), diagnostic) && !probe.attempts
            && diagnostic == "RHI-ORDINARY-ATTACHMENT-RECORDING-ENTRYPOINT");
        probe.missing = nullptr;
    }
    {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, false, true, {}, true);
        const auto request = requestFor(f);
        const auto checked = OriginalRenderPassRecordingContract::inspect(
            f.program, request.storage, *request.content, *request.recording);
        if (checked.diagnostic)
            std::fprintf(stderr, "ordinary attachment admission: %s\n", checked.diagnostic);
        assert(
            !checked.diagnostic && checked.commandVisits == 36u && checked.attachmentVisits == 84u);
        {
            NativePassProbe probe(f);
            for (unsigned field = 0u; field < 2u; ++field) {
                auto shortRequest = request;
                if (!field)
                    --shortRequest.recording->maxCommandVisits;
                else
                    --shortRequest.recording->maxAttachmentVisits;
                const auto rejected = OriginalRenderPassRecordingContract::inspect(f.program,
                    shortRequest.storage,
                    *shortRequest.content,
                    *shortRequest.recording);
                assert(
                    rejected.diagnostic && !rejected.commandVisits && !rejected.attachmentVisits);
                core::pmr::String diagnostic;
                assert(!create(f, shortRequest, diagnostic) && !probe.attempts);
            }
        }
        core::pmr::String diagnostic;
        const auto factory = [&] {
            return create(f, request, diagnostic);
        };
        test::CpuQuotaFactoryFixture quota(f.quotas[4]);
        const auto required = quota.measureSingleUse([] {}, factory);
        quota.verifySingleUse(required, [] {}, factory);
        std::printf("COLD_RENDER_PASS_RECORDING_CPU_EXACT_BYTES version4 %llu\n",
            static_cast<unsigned long long>(required));
    }
    for (unsigned mode = 0u; mode < 6u; ++mode) {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, false, true, {}, true);
        NativePassProbe probe(f);
        core::pmr::String diagnostic;
        auto encoder = create(f, requestFor(f), diagnostic);
        if (!encoder)
            std::fprintf(stderr, "ordinary attachment cold factory: %s\n", diagnostic.c_str());
        assert(encoder);
        auto native = vulkanRhiCommandPayload(*encoder);
        const auto range = f.barriers[0].textureSubresources;
        const auto initial = f.parentAt(0u).observeOriginalContent(range);
        assert(initial && initial->contentGeneration() == 0u);
        core::UniquePtr<CommandBuffer> finished;
        std::optional<tests::RenderDocCaptureScope> capture;
        if (mode == 5u)
            capture.emplace();
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            f.quotas, [&]() noexcept -> const char* {
                if (mode == 0u) {
                    assert(encoder->transition(*f.textureObjects[0], f.barriers[0]).success);
                    assert(!f.parentAt(0u).observeOriginalContent(range));
                    return nullptr;
                }
                if (mode == 2u) {
                    auto wrong = f.barriers[0];
                    wrong.textureSubresources.baseMipLevel = 0u;
                    assert(!encoder->transition(*f.textureObjects[0], wrong).success
                        && !encoder->finish());
                    return nullptr;
                }
                if (mode == 3u) {
                    assert(!encoder->beginRenderPass(passAt(f, 2u)) && !encoder->finish());
                    return nullptr;
                }
                record(f, *encoder);
                finished = encoder->finish();
                assert(finished && !acceptRecordedCommandOwnership(*finished));
                return nullptr;
            }));
        if (mode < 4u) {
            finished.reset();
            encoder.reset();
            const auto restored = f.parentAt(0u).observeOriginalContent(range);
            assert(restored
                && restored->state() == common::OriginalTextureContentControl::State::Uninitialized
                && !restored->contentGeneration());
            const auto state = f.parentAt(0u).observeOriginalCommittedState(range);
            assert(state && state->state() == ResourceState::Undefined);
            assert(!resources.sharedStats()->nativeQueueSubmitCount);
        } else {
            auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
            assert(gate);
            if (mode == 4u) {
                probe.missing = "vkQueueSubmit";
                assert(!native->submit({gate, 1u}, diagnostic) && !probe.queueInvocations
                    && !f.parentAt(0u).observeOriginalContent(range));
                probe.missing = nullptr;
            }
            assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                f.quotas, [&]() noexcept -> const char* {
                    assert(native->submit({gate, 1u}, diagnostic));
                    assert(acceptRecordedCommandOwnership(*finished));
                    assert(!native->submittedFenceCompleted()
                        && !f.parentAt(0u).observeOriginalContent(range)
                        && !f.parentAt(0u).observeOriginalCommittedState(range));
                    return nullptr;
                }));
            if (mode == 5u) {
                finished.reset();
                encoder.reset();
            }
            assert(gate->hostSignal(1u) == VK_SUCCESS);
            assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                f.quotas, [&]() noexcept -> const char* {
                    assert(native->waitSubmittedFenceUntil(
                        std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
                    assert(native->submittedFenceCompleted());
                    retireCompletedVulkanRhiSubmittedCommands();
                    return nullptr;
                }));
            if (capture)
                capture->end();
            for (std::size_t i = 0u; i < 2u; ++i) {
                const auto& object = f.parentAt(i);
                const auto completed =
                    object.observeOriginalContent(f.barriers[i].textureSubresources);
                const auto state =
                    object.observeOriginalCommittedState(f.barriers[i].textureSubresources);
                assert(completed
                    && completed->state() == common::OriginalTextureContentControl::State::Completed
                    && completed->contentGeneration() == 1u && state
                    && state->state() == f.barriers[i].after);
                const TextureSubresourceRange untouched{
                    0u, 1u, 0u, 1u, i ? TextureAspect::Depth : TextureAspect::Color};
                const auto other = object.observeOriginalContent(untouched);
                const auto otherState = object.observeOriginalCommittedState(untouched);
                assert(other && !other->contentGeneration()
                    && other->state() == common::OriginalTextureContentControl::State::Uninitialized
                    && otherState && otherState->state() == ResourceState::Undefined);
            }
            assert(probe.created == probe.destroyed && !probe.queryPoolCreations
                && !probe.queryResets && !probe.timestampWrites);
            finished.reset();
            encoder.reset();
        }
        native.reset();
        assert(probe.created == probe.destroyed);
    }
    for (unsigned rejection = 0u; rejection < 2u; ++rejection) {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, false, true, {}, true);
        NativePassProbe probe(f);
        core::pmr::String diagnostic;
        auto encoder = create(f, requestFor(f), diagnostic);
        assert(encoder);
        auto native = vulkanRhiCommandPayload(*encoder);
        auto foreignDescriptor = passAt(f, 2u);
        foreignDescriptor.colorAttachments[0].clearColor.r += 0.25f;
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            f.quotas, [&]() noexcept -> const char* {
                for (std::size_t i = 0u; i < 2u; ++i)
                    assert(encoder->transition(*f.textureObjects[i], f.barriers[i]).success);
                if (!rejection)
                    assert(!encoder->beginRenderPass(foreignDescriptor));
                else {
                    auto pass = encoder->beginRenderPass(passAt(f, 2u));
                    assert(pass && !pass->draw(3u, 1u, 0u, 0u).success && !pass->end().success);
                }
                assert(!probe.queueInvocations);
                return nullptr;
            }));
        // This public observer returns an owning string copy; observe the
        // already-published failure outside the recording allocation probe.
        if (rejection) {
            const auto failure = encoder->lastValidation();
            assert(!failure.success
                && failure.diagnostics == "RHI-ORDINARY-ATTACHMENT-RECORDING-COMMAND-PROFILE");
        }
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            f.quotas, [&]() noexcept -> const char* {
                assert(!encoder->finish() && !probe.queueInvocations);
                return nullptr;
            }));
        encoder.reset();
        const auto content =
            f.parentAt(0u).observeOriginalContent(f.barriers[0].textureSubresources);
        assert(content && !content->contentGeneration());
    }
    for (const auto load : {LoadOp::Clear, LoadOp::DontCare})
        for (const auto store : {StoreOp::Store, StoreOp::DontCare}) {
            Fixture f(runtime, load, store, false, true, {}, true);
            NativePassProbe probe(f, load, store);
            core::pmr::String diagnostic;
            auto encoder = create(f, requestFor(f), diagnostic);
            assert(encoder);
            auto native = vulkanRhiCommandPayload(*encoder);
            core::UniquePtr<CommandBuffer> finished;
            assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                f.quotas, [&]() noexcept -> const char* {
                    record(f, *encoder);
                    finished = encoder->finish();
                    assert(finished && native->submit(diagnostic));
                    assert(native->waitSubmittedFenceUntil(
                        std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
                    retireCompletedVulkanRhiSubmittedCommands();
                    return nullptr;
                }));
            const auto content =
                f.parentAt(0u).observeOriginalContent(f.barriers[0].textureSubresources);
            const auto expected = load == LoadOp::Clear && store == StoreOp::Store
                ? common::OriginalTextureContentControl::State::Completed
                : common::OriginalTextureContentControl::State::Uninitialized;
            assert(content && content->state() == expected && content->contentGeneration() == 1u
                && probe.created == probe.destroyed);
        }
    {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, false, true, LoadOp::Load, true);
        core::pmr::String diagnostic;
        auto encoder = create(f, requestFor(f), diagnostic);
        assert(encoder);
        auto native = vulkanRhiCommandPayload(*encoder);
        record(f, *encoder);
        auto finished = encoder->finish();
        assert(finished && native->submit(diagnostic)
            && native->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        retireCompletedVulkanRhiSubmittedCommands();
        const auto content =
            f.parentAt(0u).observeOriginalContent(f.barriers[0].textureSubresources);
        assert(content
            && content->state() == common::OriginalTextureContentControl::State::Completed
            && content->contentGeneration() == 1u);
    }
    {
        Fixture f(runtime, LoadOp::Clear, StoreOp::Store, false, true, {}, true);
        NativePassProbe probe(f);
        core::pmr::String diagnostic;
        auto encoder = create(f, requestFor(f), diagnostic);
        assert(encoder);
        auto native = vulkanRhiCommandPayload(*encoder);
        record(f, *encoder);
        auto finished = encoder->finish();
        assert(finished);
        probe.failQueue = true;
        assert(!native->submit(diagnostic) && probe.queueInvocations == 1u && !probe.destroyed);
        finished.reset();
        encoder.reset();
        assert(!f.parentAt(0u).observeOriginalContent(f.barriers[0].textureSubresources)
            && !native->submittedFenceCompleted());
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        assert(probe.created == probe.destroyed
            && !f.parentAt(0u).observeOriginalContent(f.barriers[0].textureSubresources));
    }
    std::puts("ORIGINAL_ORDINARY_ATTACHMENT_RECORDING_OK exact-program9 references8 same-wrapper2 "
              "real-barrier4 begin2 end2 same-journal exact-cell mip1-layer2 "
              "cancel-before-after-finish ordinal-refusal zero-hot-allocation "
              "timeline-delayed-completion-generation1 untouched-cells-preserved");
}
} // namespace

int main()
{
    tests::VulkanHeadlessRuntime native(false, NativePassProbe::resolver);
    if (!native.runtime() || !native.runtime()->valid())
        return 77;
    using Projection = VulkanRhiRenderPassStorageProjection;
    Fixture f(native.runtime());
    const auto before = inspectVulkanRhiNativeResourceStats(*f.device);
    const auto inspect = [&] {
        return Projection::inspectRegistered(f.program, f.budget, 8u, *f.device, *native.runtime());
    };
    const auto good = inspect();
    if (good.diagnostic)
        std::fprintf(stderr, "registered pass: %s\n", good.diagnostic);
    assert(!good.diagnostic && good.nativeObjects == 8u && good.imageViews == 4u);
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        f.quotas, [&]() noexcept -> const char* {
            return inspect().diagnostic;
        }));
    auto shortBudget = f.budget;
    --shortBudget.maxAttachmentReferences;
    assert(Projection::inspectRegistered(f.program, shortBudget, 8u, *f.device, *native.runtime())
            .diagnostic);
    assert(Projection::inspectRegistered(f.program, f.budget, 7u, *f.device, *native.runtime())
            .diagnostic);
    Fixture foreign(native.runtime());
    assert(
        Projection::inspectRegistered(f.program, f.budget, 8u, *foreign.device, *native.runtime())
            .diagnostic);
    tests::VulkanHeadlessRuntime other;
    assert(other.runtime() && other.runtime()->valid()
        && Projection::inspectRegistered(f.program, f.budget, 8u, *f.device, *other.runtime())
            .diagnostic);
    const auto& sources = f.program->originalInputs()
                              ->originalScalars()
                              ->originalUsage()
                              ->originalSources()
                              ->sources();
    const auto& last =
        std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(sources[3u]);
    const auto payload = vulkanRhiNativePayload(*last->originalObject());
    auto& physical = *const_cast<TextureViewDesc*>(payload->backendVulkanOriginalTextureViewDesc());
    for (unsigned field = 0u; field < 8u; ++field) {
        const auto saved = TextureViewResourceLayout::capture(physical);
        if (field == 0u)
            physical.format = TextureFormat::Unknown;
        if (field == 1u)
            physical.usage = TextureUsage::Sampled;
        if (field == 2u)
            physical.dimension = TextureViewDimension::Texture2DArray;
        if (field == 3u)
            --physical.baseMipLevel;
        if (field == 4u)
            ++physical.mipLevelCount;
        if (field == 5u)
            --physical.baseArrayLayer;
        if (field == 6u)
            ++physical.arrayLayerCount;
        if (field == 7u)
            physical.aspect = TextureAspect::Color;
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            f.quotas, [&]() noexcept -> const char* {
                const auto result = inspect();
                assert(result.diagnostic
                    && std::string_view(result.diagnostic)
                        == "RHI-VULKAN-COLD-RENDER-PASS-VIEW-PARENT-LAYOUT"
                    && !result.nativeObjects && result.inputs.matches({}));
                return nullptr;
            }));
        physical.format = saved.format;
        physical.usage = saved.usage;
        physical.dimension = saved.dimension;
        physical.baseMipLevel = saved.baseMipLevel;
        physical.mipLevelCount = saved.mipLevelCount;
        physical.baseArrayLayer = saved.baseArrayLayer;
        physical.arrayLayerCount = saved.arrayLayerCount;
        physical.aspect = saved.aspect;
    }
    assert(!inspect().diagnostic);
    const auto after = inspectVulkanRhiNativeResourceStats(*f.device);
    assert(before.nativeRenderPassCreateCount == after.nativeRenderPassCreateCount
        && before.nativeFramebufferCreateCount == after.nativeFramebufferCreateCount
        && before.nativeRenderAttachmentViewCreateCount
            == after.nativeRenderAttachmentViewCreateCount
        && before.nativeCommandBufferRecordCount == after.nativeCommandBufferRecordCount
        && before.nativeQueueSubmitCount == after.nativeQueueSubmitCount);
    std::puts(
        "ORIGINAL_RENDER_PASS_REGISTERED_VIEWS_OK pass2 refs4 objects8 mip1 layer2 exact-device-parent-native-layout8 allocation0 no-native-pass-creation no-recording-authority");
    verifyNativePreparation(native.runtime());
    verifyContentObservations(native.runtime());
    verifyOrdinaryAttachmentRecording(native.runtime());
}
