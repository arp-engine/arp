#pragma once
#include "webgpu_ordinary_compute_recording_fixture.h"

#include <limits>

namespace arp::test {
class WebGpuOrdinaryGraphicsRecordingFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using Contract = semantics::SingleColor2DRendering;
        WebGpuOrdinaryGraphicsRecordingCommonFixture::verifyContract();
        WebGpuOrdinaryGraphicsAttachmentBudget budget;
        assert(std::string_view(WebGpuOrdinaryGraphicsAttachments::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-ATTACHMENT-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuOrdinaryGraphicsAttachments::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-ATTACHMENT-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(WebGpuOrdinaryGraphicsAttachments::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-ATTACHMENT-CAPACITY");
        RenderPassDesc pass;
        pass.colorAttachments.resize(1u);
        auto& color = pass.colorAttachments.front();
        color.target = allocateGpuResourceId();
        color.format = TextureFormat::Rgba8Unorm;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        color.clearColor = {0.1f, 0.2f, 0.3f, 1.0f};
        assert(!Contract::validatePass(pass));
        color.hasResolveTarget = true;
        assert(Contract::validatePass(pass));
        color.hasResolveTarget = false;
        color.resolveTarget = color.target;
        assert(Contract::validatePass(pass));
        color.resolveTarget = {};
        color.format = TextureFormat::Bgra8Unorm;
        assert(Contract::validatePass(pass));
        color.format = TextureFormat::Rgba8Unorm;
        for (const auto load : {LoadOp::Load, LoadOp::DontCare, static_cast<LoadOp>(UINT32_MAX)}) {
            color.loadOp = load;
            assert(Contract::validatePass(pass));
        }
        color.loadOp = LoadOp::Clear;
        for (const auto store : {StoreOp::DontCare, static_cast<StoreOp>(UINT32_MAX)}) {
            color.storeOp = store;
            assert(Contract::validatePass(pass));
        }
        color.storeOp = StoreOp::Store;
        for (auto* component :
            {&color.clearColor.r, &color.clearColor.g, &color.clearColor.b, &color.clearColor.a}) {
            const auto original = *component;
            for (const auto invalid :
                {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
                *component = invalid;
                assert(Contract::validatePass(pass));
            }
            *component = original;
        }
        pass.hasDepthStencilAttachment = true;
        assert(Contract::validatePass(pass));
        pass.hasDepthStencilAttachment = false;
        pass.layeredRendering = true;
        assert(Contract::validatePass(pass));
        pass.layeredRendering = false;
        pass.colorTargetName = "ambiguous";
        assert(Contract::validatePass(pass));
        pass.colorTargetName.clear();
        pass.subpasses.resize(1u);
        assert(Contract::validatePass(pass));
        pass.subpasses.clear();
        pass.subpassDependencies.resize(1u);
        assert(Contract::validatePass(pass));
        pass.subpassDependencies.clear();
        RenderPassDesc wrongCount;
        assert(Contract::validatePass(wrongCount));
        wrongCount.colorAttachments.resize(2u);
        assert(Contract::validatePass(wrongCount));
        TextureDesc parent{
            "", {8u, 4u, 1u}, TextureFormat::Rgba8Unorm, TextureUsage::RenderAttachment};
        parent.mipLevelCount = 3u;
        parent.arrayLayerCount = 2u;
        TextureViewDesc view{"", TextureFormat::Rgba8Unorm, TextureUsage::RenderAttachment};
        view.baseMipLevel = 1u;
        view.baseArrayLayer = 1u;
        Extent3D extent;
        assert(!Contract::resolveTarget(parent, view, color.format, extent) && extent.width == 4u
            && extent.height == 2u);
        const auto viewField = [&](auto& field, auto invalid) {
            const auto original = field;
            field = invalid;
            assert(Contract::resolveTarget(parent, view, color.format, extent));
            field = original;
        };
        viewField(view.baseMipLevel, 3u);
        viewField(view.baseArrayLayer, 2u);
        viewField(view.mipLevelCount, 2u);
        viewField(view.arrayLayerCount, 2u);
        viewField(view.aspect, TextureAspect::Depth);
        viewField(view.dimension, TextureViewDimension::Texture2DArray);
        viewField(view.usage, TextureUsage::Sampled);
        viewField(view.format, TextureFormat::Bgra8Unorm);
        viewField(parent.sampleCount, 4u);
        viewField(parent.format, TextureFormat::Bgra8Unorm);
        viewField(parent.usage, TextureUsage::Sampled);
        viewField(parent.extent.depth, 2u);
        viewField(parent.cubeCompatible, true);
        assert(!Contract::resolveTarget(parent, view, color.format, extent));
        WebGpuNativePipelineGraphicsCompatibility graphics;
        graphics.graphics = true;
        graphics.colorFormats.push_back(color.format);
        graphics.hasViewport = true;
        graphics.hasScissor = true;
        graphics.viewport = {0.0f, 0.0f, 4.0f, 2.0f, 0.0f, 1.0f};
        graphics.scissor = {0, 0, 4u, 2u};
        auto check = [&] {
            return Contract::validatePipeline(graphics, color.format, extent);
        };
        assert(!check());
        const auto field = [&](auto& value, auto invalid) {
            const auto original = value;
            value = invalid;
            assert(check());
            value = original;
        };
        field(graphics.graphics, false);
        field(graphics.hasViewport, false);
        field(graphics.hasScissor, false);
        field(graphics.vertexBufferCount, 1u);
        field(graphics.sampleCount, 4u);
        field(graphics.topology, PrimitiveTopology::LineList);
        field(graphics.depthStencilFormat, TextureFormat::D32Float);
        field(graphics.colorFormats.front(), TextureFormat::Bgra8Unorm);
        field(graphics.viewport.x, -1.0f);
        field(graphics.viewport.y, 1.0f);
        field(graphics.viewport.width, 5.0f);
        field(graphics.viewport.height, 0.0f);
        field(graphics.viewport.minDepth, -1.0f);
        field(graphics.viewport.maxDepth, 2.0f);
        for (auto* component : {&graphics.viewport.x,
                 &graphics.viewport.y,
                 &graphics.viewport.width,
                 &graphics.viewport.height,
                 &graphics.viewport.minDepth,
                 &graphics.viewport.maxDepth}) {
            field(*component, std::numeric_limits<float>::quiet_NaN());
            field(*component, std::numeric_limits<float>::infinity());
        }
        field(graphics.scissor.x, -1);
        field(graphics.scissor.y, 1);
        field(graphics.scissor.width, UINT32_MAX);
        field(graphics.scissor.height, 0u);
        // Cold construction is complete before the hostile-default/exhausted
        // check; classification of valid and invalid intent is allocation-free.
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, 1u);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!check() && !Contract::validatePass(pass));
        assert(!Contract::resolveTarget(parent, view, color.format, extent));
        field(graphics.scissor.width, UINT32_MAX);
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        std::pmr::set_default_resource(previous);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(
        std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryGraphicsTriangleRange> range)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using Attachments = WebGpuOrdinaryGraphicsAttachments;
        auto registry = core::makeShared<WebGpuRhiResourceRegistry>();
        TextureDesc desc{"original bounded graphics target",
            {4u, 4u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::RenderAttachment};
        desc.mipLevelCount = 2u;
        const auto bytes = estimateWebGpuRhiTextureBytes(desc);
        WebGpuRhiAllocator allocator({"original graphics allocation", bytes, false});
        auto allocation = allocator.allocate({desc.label, bytes, desc.memory});
        assert(allocation.success);
        auto texture = createWebGpuRhiTexture(
            allocateGpuResourceId(), desc, allocation, {true, true}, registry);
        assert(texture);
        registry->registerTexture(*texture);
        TextureViewDesc viewDesc{
            "original attachment mip1", TextureFormat::Rgba8Unorm, TextureUsage::RenderAttachment};
        viewDesc.baseMipLevel = 1u;
        auto view = createWebGpuRhiTextureView(
            allocateGpuResourceId(), texture->id(), *texture, viewDesc, {true, true}, registry);
        assert(view);
        registry->registerTextureView(*view);
        RenderPassDesc pass;
        pass.colorAttachments.resize(1u);
        auto& color = pass.colorAttachments.front();
        color.target = view->id();
        color.format = desc.format;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        color.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        WebGpuOrdinaryGraphicsAttachmentBudget budget{1u,
            true,
            1u,
            {1u,
                true,
                {kCommandRetentionBudgetContractVersion, true, 3u, 0u, 0u, 0u, metadataBytes},
                1u,
                1u,
                0u,
                bytes}};
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        auto factory = [&] {
            return Attachments::prepare(range, registry, pass, budget, quota, diagnostic);
        };
        auto expect = [&](const char* id) {
            const auto live = quota->liveBytes();
            auto result = factory();
            if (result || !diagnostic || std::string_view(diagnostic) != id)
                std::printf("GRAPHICS_ATTACHMENT_EXPECTED %s GOT %s\n",
                    id,
                    diagnostic ? diagnostic : "Success");
            assert(!result && std::string_view(diagnostic) == id && quota->liveBytes() == live);
        };
        auto originalRange = range;
        range = std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange>(quota, range.get());
        expect("RHI-WEBGPU-ORDINARY-RECORDING-RANGE-OWNER");
        range = originalRange;
        originalRange.reset();
        auto originalRegistry = registry;
        registry = std::shared_ptr<WebGpuRhiResourceRegistry>(quota, registry.get());
        expect("RHI-WEBGPU-ORDINARY-ATTACHMENT-REGISTRY");
        registry = originalRegistry;
        originalRegistry.reset();
        --budget.maxColorAttachments;
        expect("RHI-WEBGPU-ORDINARY-ATTACHMENT-CAPACITY");
        ++budget.maxColorAttachments;
        --budget.resources.maxResources;
        expect("RHI-WEBGPU-ORDINARY-RESOURCE-MANIFEST-CAPACITY");
        ++budget.resources.maxResources;
        --budget.resources.maxTextureViews;
        expect("RHI-WEBGPU-ORDINARY-RESOURCE-NATIVE-OBJECT-CAPACITY");
        ++budget.resources.maxTextureViews;
        --budget.resources.retention.maxResourceOwners;
        expect("RHI-COMMAND-RETENTION-OWNER-CAPACITY");
        ++budget.resources.retention.maxResourceOwners;
        --budget.resources.maxRetainedAllocationBytes;
        expect("RHI-WEBGPU-ORDINARY-RESOURCE-ALLOCATION-BYTE-CAPACITY");
        ++budget.resources.maxRetainedAllocationBytes;
        --budget.resources.retention.maxCpuMetadataBytes;
        expect("RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
        ++budget.resources.retention.maxCpuMetadataBytes;
        const auto originalTarget = color.target;
        color.target = texture->id();
        expect("RHI-WEBGPU-ORDINARY-RESOURCE-KIND-UNSUPPORTED");
        color.target = originalTarget;
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged());
            expect("RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED");
        }
        auto attachments = factory();
        assert(attachments && !attachments->validate());
        assert(attachments->extent().width == 2u && attachments->extent().height == 2u);
        assert(attachments->target().viewDesc().label.empty()
            && attachments->target().backing().textureDesc.label.empty());
        color.clearColor.r = 1.0f;
        color.target = texture->id();
        assert(!attachments->clear().r && attachments->target().id() == originalTarget);
        WebGpuOrdinaryGraphicsRecordingCommonFixture::verifyActual(attachments);
        auto nativeView = webGpuNativePayloadFor(*view);
        auto nativeTexture = webGpuNativePayloadFor(*texture);
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            ++nativeView->creationKey.baseMipLevel;
            assert(attachments->validate());
            WebGpuOrdinaryGraphicsRecordingCommonFixture::verifyRetainedDiagnostic(
                "RHI-WEBGPU-ORDINARY-RESOURCE-REGISTERED-IDENTITY-CHANGED");
            --nativeView->creationKey.baseMipLevel;
            const auto device = nativeTexture->ownerDevice;
            nativeTexture->ownerDevice = nullptr;
            assert(attachments->validate());
            nativeTexture->ownerDevice = device;
            registry->unregisterResource(view->id());
            WebGpuOrdinaryGraphicsRecordingCommonFixture::verifyRetainedDiagnostic(
                "RHI-WEBGPU-ORDINARY-RESOURCE-MISSING");
            assert(core::defaultCpuAllocator().stats().allocationCount == before);
            std::pmr::set_default_resource(previous);
        }
        registry->registerTextureView(*view);
        assert(!attachments->validate());
        registry->unregisterResource(texture->id());
        assert(attachments->validate());
        registry->registerTexture(*texture);
        assert(!attachments->validate());
        std::weak_ptr<WebGpuNativeTextureViewPayload> viewOwner = nativeView;
        std::weak_ptr<WebGpuNativeTexturePayload> textureOwner = nativeTexture;
        nativeView.reset();
        nativeTexture.reset();
        allocation.releaseToken.reset();
        originalView_ = std::move(view);
        originalTexture_ = std::move(texture);
        assert(!viewOwner.expired() && !textureOwner.expired());
        assert(allocator.memoryStats().allocatedBytes == bytes);
        retained_ = std::move(attachments);
        quota_ = quota;
        std::printf("BINDLESS_RHI_ORDINARY_GRAPHICS_ATTACHMENT_OK %llu %llu 3 1\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(bytes));
    }

    static void verifySubmissionActual()
    {
        WebGpuOrdinaryGraphicsRecordingCommonFixture::verifySubmissionActual(retained_);
        std::weak_ptr<rhi_webgpu::WebGpuNativeTextureViewPayload> view = retained_->target().view();
        std::weak_ptr<rhi_webgpu::WebGpuNativeTexturePayload> texture =
            retained_->target().backing().texture;
        originalView_.reset();
        originalTexture_.reset();
        assert(!view.expired() && !texture.expired());
    }

    static void verifyRetainedDiagnostic(const char* id)
    {
        WebGpuOrdinaryGraphicsRecordingCommonFixture::verifyRetainedDiagnostic(id);
    }

    static void verifyActualDeviceLoss()
    {
        WebGpuOrdinaryGraphicsRecordingCommonFixture::verifyActualDeviceLoss();
        assert(retained_
            && std::string_view(retained_->validate())
                == "RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST");
        retained_.reset();
        assert(!quota_->liveBytes());
        quota_.reset();
        std::puts("BINDLESS_RHI_ORDINARY_GRAPHICS_ATTACHMENT_LOSS_OK");
    }
#endif
private:
    static constexpr std::uint64_t metadataBytes = 65536u;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static inline std::shared_ptr<rhi_webgpu::WebGpuOrdinaryGraphicsAttachments> retained_;
    static inline std::shared_ptr<core::CpuMemoryQuota> quota_;
    static inline core::UniquePtr<rhi::GpuTexture> originalTexture_;
    static inline core::UniquePtr<rhi::GpuTextureView> originalView_;
#endif
};
} // namespace arp::test
