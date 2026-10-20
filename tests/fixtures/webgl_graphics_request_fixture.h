#pragma once

#include "rhi/semantics/webgl_graphics_request.h"
#include "webgl_profile_probe_fixture.h"

#include <cassert>
#include <cstring>

namespace arp::test {

class WebGlGraphicsRequestFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using namespace rhi::semantics;
        using Contract = WebGlGraphicsRequestContract;
        static_assert(core::unsignedBitWidth(0u) == 0u);
        static_assert(core::unsignedBitWidth(1u) == 1u);
        static_assert(core::unsignedBitWidth(3u) == 2u);
        static_assert(core::unsignedBitWidth(4u) == 3u);
        static_assert(core::unsignedBitWidth(std::uint64_t{1} << 63u) == 64u);
        static_assert(core::unsignedBitWidth(std::numeric_limits<std::uint64_t>::max()) == 64u);
        const auto baseline = WebGlProfileProbeFixture::baseline();
        const auto profile = WebGlRuntimeProfile::fromProbe(baseline);
        static_assert(static_cast<int>(Backend::Null) == 0);
        static_assert(static_cast<int>(Backend::Vulkan) == 1);
        static_assert(static_cast<int>(Backend::Metal) == 2);
        static_assert(static_cast<int>(Backend::WebGPU) == 3);
        static_assert(static_cast<int>(Backend::WebGL) == 4);
        for (const auto operation : {WebGlGraphicsOperation::Draw,
                 WebGlGraphicsOperation::DrawIndexed,
                 WebGlGraphicsOperation::Uniform,
                 WebGlGraphicsOperation::Texture2D,
                 WebGlGraphicsOperation::ColorAttachment,
                 WebGlGraphicsOperation::Canvas}) {
            accepted(Contract::operation(profile, 1u, operation));
            rejected(
                Contract::operation(profile, 0u, operation), "RHI-WEBGL1-GRAPHICS-REQUEST-VERSION");
        }
        for (const auto operation : {WebGlGraphicsOperation::Compute,
                 WebGlGraphicsOperation::StorageBuffer,
                 WebGlGraphicsOperation::StorageImage,
                 WebGlGraphicsOperation::Indirect,
                 WebGlGraphicsOperation::Bindless,
                 WebGlGraphicsOperation::AliasHeap,
                 WebGlGraphicsOperation::Readback,
                 WebGlGraphicsOperation::MultipleColorAttachments,
                 WebGlGraphicsOperation::Multisample})
            rejected(Contract::operation(profile, 1u, operation),
                "RHI-WEBGL1-GRAPHICS-FEATURE-UNSUPPORTED");
        rejected(Contract::operation(profile, 1u, WebGlGraphicsOperation::GpuCompletion),
            "RHI-WEBGL1-GPU-COMPLETION-UNAVAILABLE");
        rejected(Contract::operation(profile, 1u, static_cast<WebGlGraphicsOperation>(99)),
            "RHI-WEBGL1-GRAPHICS-OPERATION-UNKNOWN");
        const std::array extensions{WebGlGraphicsOperation::Uint32Indices,
            WebGlGraphicsOperation::Instancing,
            WebGlGraphicsOperation::VertexArrayObjects,
            WebGlGraphicsOperation::DepthTexture,
            WebGlGraphicsOperation::Derivatives};
        // Availability, enablement and opt-in are independent for every extension.
        for (std::size_t i = 0u; i < extensions.size(); ++i) {
            for (unsigned bits = 0u; bits < 8u; ++bits) {
                auto probe = baseline;
                WebGlExtensionOptIn optIn;
                optIn.contractVersion = 1u;
                set(probe.extensions, i, bits & 1u);
                set(probe.enabledExtensions, i, bits & 2u);
                set(optIn.extensions, i, bits & 4u);
                const auto candidate = WebGlRuntimeProfile::fromProbe(probe);
                const auto result = Contract::operation(candidate, 1u, extensions[i], optIn);
                assert(result.compatible == (bits == 7u));
                assert(!result.executable && result.diagnostic);
            }
        }
        textureAndBindings(profile);
        drawsAndCanvas(profile);
        // Invalid profile takes precedence and cannot be repaired by request fields.
        const auto invalid = WebGlRuntimeProfile::fromProbe({});
        rejected(
            Contract::operation(invalid, 1u, WebGlGraphicsOperation::Draw), invalid.diagnostic());
    }

private:
    static void accepted(const rhi::semantics::WebGlProfileAdmission& result)
    {
        assert(result.compatible && !result.executable && result.diagnostic);
        assert(std::strcmp(result.diagnostic, "RHI-WEBGL1-EXECUTION-NOT-IMPLEMENTED") == 0);
    }

    static void rejected(
        const rhi::semantics::WebGlProfileAdmission& result, const char* diagnostic)
    {
        assert(!result.compatible && !result.executable && result.diagnostic);
        assert(std::strcmp(result.diagnostic, diagnostic) == 0);
    }

    static void set(
        rhi::semantics::WebGlRuntimeExtensions& extensions, std::size_t index, bool value)
    {
        switch (index) {
        case 0:
            extensions.uint32Indices = value;
            break;
        case 1:
            extensions.instancing = value;
            break;
        case 2:
            extensions.vertexArrayObjects = value;
            break;
        case 3:
            extensions.depthTextures = value;
            break;
        case 4:
            extensions.derivatives = value;
            break;
        default:
            assert(false);
        }
    }

    static void textureAndBindings(const rhi::semantics::WebGlRuntimeProfile& profile)
    {
        using namespace rhi;
        using namespace rhi::semantics;
        using Contract = WebGlGraphicsRequestContract;
        TextureDesc desc;
        desc.extent = {4u, 4u, 1u};
        desc.format = TextureFormat::Rgba8Unorm;
        desc.usage = TextureUsage::Sampled | TextureUsage::RenderAttachment;
        SamplerDesc sampler;
        sampler.mipFilter = SamplerMipFilter::None;
        std::array<WebGlSampledBindingRequest, 2> bindings{
            {{{1u}, &desc, &sampler}, {{1u}, &desc, &sampler}}};
        SamplerDesc other = sampler;
        TextureDesc different = desc;
        different.extent.width = 2u;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        accepted(Contract::texture(profile, 1u, desc));
        accepted(Contract::sampledBindings(profile, 1u, bindings));
        bindings[1].texture = &different;
        rejected(Contract::sampledBindings(profile, 1u, bindings),
            "RHI-WEBGL1-SAMPLED-BINDING-IDENTITY");
        bindings[1].texture = &desc;
        bindings[1].sampler = &other;
        other.minFilter = SamplerFilter::Nearest;
        rejected(Contract::sampledBindings(profile, 1u, bindings), "RHI-WEBGL1-SAMPLER-CONFLICT");
        bindings[1].textureIdentity = {2u};
        accepted(Contract::sampledBindings(profile, 1u, bindings));
        rejected(Contract::sampledBindings(profile, 1u, {nullptr, 1u}),
            "RHI-WEBGL1-SAMPLED-BINDING-MISSING");
        rejected(Contract::sampledBindings(
                     profile, 1u, {nullptr, std::numeric_limits<std::size_t>::max()}),
            "RHI-WEBGL1-WORKLOAD-LIMIT");
        bindings[1].textureIdentity = {};
        rejected(
            Contract::sampledBindings(profile, 1u, bindings), "RHI-WEBGL1-SAMPLED-BINDING-MISSING");
        bindings[1].textureIdentity = {2u};
        desc.extent.width = 3u;
        accepted(Contract::sampledBindings(profile, 1u, bindings));
        other.addressU = SamplerAddressMode::Repeat;
        rejected(Contract::sampledBindings(profile, 1u, bindings), "RHI-WEBGL1-INVALID-NPOT");
        other = sampler;
        other.mipFilter = SamplerMipFilter::Linear;
        rejected(Contract::sampledBindings(profile, 1u, bindings), "RHI-WEBGL1-INVALID-NPOT");
        desc.extent.width = 4u;
        rejected(Contract::sampledBindings(profile, 1u, bindings), "RHI-WEBGL1-TEXTURE-MIP-CHAIN");
        desc.mipLevelCount = 3u;
        accepted(Contract::sampledBindings(profile, 1u, bindings));
        other.maxAnisotropy = std::numeric_limits<float>::quiet_NaN();
        rejected(
            Contract::sampledBindings(profile, 1u, bindings), "RHI-WEBGL1-SAMPLER-UNSUPPORTED");
        other = sampler;
        other.compareOp = CompareOp::Less;
        rejected(
            Contract::sampledBindings(profile, 1u, bindings), "RHI-WEBGL1-SAMPLER-UNSUPPORTED");
        other = sampler;
        other.magFilter = static_cast<SamplerFilter>(99);
        rejected(
            Contract::sampledBindings(profile, 1u, bindings), "RHI-WEBGL1-SAMPLER-UNSUPPORTED");
        desc.mipLevelCount = 4u;
        rejected(Contract::texture(profile, 1u, desc), "RHI-WEBGL1-TEXTURE-MIP-CHAIN");
        desc.mipLevelCount = 1u;
        for (const auto usage : {TextureUsage::None,
                 TextureUsage::Storage,
                 TextureUsage::CopySrc,
                 TextureUsage::CopyDst,
                 static_cast<TextureUsage>(128)}) {
            desc.usage = usage;
            rejected(Contract::texture(profile, 1u, desc), "RHI-WEBGL1-TEXTURE-USAGE");
        }
        desc.usage = TextureUsage::Sampled;
        desc.format = TextureFormat::Rgba8UnormSrgb;
        rejected(Contract::texture(profile, 1u, desc), "RHI-WEBGL1-FORMAT-UNSUPPORTED");
        desc.format = TextureFormat::Rgba8Unorm;
        desc.extent.depth = 2u;
        rejected(Contract::texture(profile, 1u, desc), "RHI-WEBGL1-TEXTURE-SHAPE");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }

    static void drawsAndCanvas(const rhi::semantics::WebGlRuntimeProfile& profile)
    {
        using namespace rhi;
        using namespace rhi::semantics;
        using Contract = WebGlGraphicsRequestContract;
        WebGlDrawRequest draw;
        draw.contractVersion = 1u;
        draw.count = 3u;
        draw.instanceCount = 1u;
        accepted(Contract::draw(profile, draw));
        draw.indexed = true;
        draw.indexBufferSize = 6u;
        accepted(Contract::draw(profile, draw));
        draw.indexBufferSize = 5u;
        rejected(Contract::draw(profile, draw), "RHI-WEBGL1-DRAW-RANGE");
        draw.indexBufferSize = std::numeric_limits<std::uint64_t>::max();
        draw.indexOffset = std::numeric_limits<std::uint64_t>::max() - 1u;
        rejected(Contract::draw(profile, draw), "RHI-WEBGL1-DRAW-RANGE");
        draw.indexOffset = 1u;
        rejected(Contract::draw(profile, draw), "RHI-WEBGL1-DRAW-RANGE");
        draw.indexOffset = 0u;
        draw.indexFormat = static_cast<IndexFormat>(99);
        rejected(Contract::draw(profile, draw), "RHI-WEBGL1-DRAW-INDEX-FORMAT");
        draw.indexFormat = IndexFormat::Uint32;
        rejected(Contract::draw(profile, draw), "RHI-WEBGL1-EXTENSION-UNAVAILABLE");
        auto enabledProbe = WebGlProfileProbeFixture::baseline();
        enabledProbe.extensions.uint32Indices = enabledProbe.enabledExtensions.uint32Indices = true;
        enabledProbe.extensions.instancing = enabledProbe.enabledExtensions.instancing = true;
        const auto enabled = WebGlRuntimeProfile::fromProbe(enabledProbe);
        WebGlExtensionOptIn explicitExtensions{1u, enabledProbe.extensions};
        rejected(Contract::draw(enabled, draw), "RHI-WEBGL1-EXTENSION-OPT-IN-REQUIRED");
        accepted(Contract::draw(enabled, draw, explicitExtensions));
        draw.instanceCount = 2u;
        accepted(Contract::draw(enabled, draw, explicitExtensions));
        draw.indexFormat = IndexFormat::Uint16;
        draw.instanceCount = 2u;
        rejected(Contract::draw(profile, draw), "RHI-WEBGL1-EXTENSION-UNAVAILABLE");
        draw.instanceCount = 1u;
        draw.baseVertex = 1;
        rejected(Contract::draw(profile, draw), "RHI-WEBGL1-DRAW-SEMANTICS");
        draw.baseVertex = 0;
        draw.firstInstance = 1u;
        rejected(Contract::draw(profile, draw), "RHI-WEBGL1-DRAW-SEMANTICS");
        draw.firstInstance = 0u;
        draw.primitiveRestart = true;
        rejected(Contract::draw(profile, draw), "RHI-WEBGL1-DRAW-SEMANTICS");
        draw.primitiveRestart = false;
        draw.count = std::numeric_limits<std::uint32_t>::max();
        rejected(Contract::draw(profile, draw), "RHI-WEBGL1-DRAW-RANGE");
        WebGlCanvasRequest canvas;
        canvas.contractVersion = 1u;
        accepted(Contract::canvas(profile, canvas));
        canvas.contractVersion = 2u;
        rejected(Contract::canvas(profile, canvas), "RHI-WEBGL1-GRAPHICS-REQUEST-VERSION");
        canvas.contractVersion = 1u;
        for (const auto mode : {PresentationMode::VSync,
                 PresentationMode::LowLatency,
                 PresentationMode::Immediate,
                 static_cast<PresentationMode>(99)}) {
            canvas.mode = mode;
            rejected(Contract::canvas(profile, canvas), "RHI-WEBGL1-PRESENTATION-POLICY");
        }
        canvas.mode = PresentationMode::Auto;
        canvas.nativeImageCount = 2u;
        rejected(Contract::canvas(profile, canvas), "RHI-WEBGL1-PRESENTATION-POLICY");
        canvas.nativeImageCount = 0u;
        canvas.loadPreviousDrawingBuffer = true;
        rejected(Contract::canvas(profile, canvas), "RHI-WEBGL1-CANVAS-CONTENT-UNAVAILABLE");
    }
};

} // namespace arp::test
