#pragma once

#include "core/diagnostics/diagnostic_error.h"
#include "rhi/frontend/ordered_graphics_execution.h"
#include "rhi/gpu_device.h"

#include <algorithm>
#include <array>

namespace arp::tests {

// Cold, explicit browser validation using the actual published editor pipelines.
// Recording is cancelled: negative checks never submit malformed GPU work.
class OrderedGraphicsCatalogFixture final {
public:
    static void run(rhi::GpuDevice& device,
        rhi::OriginalOrderedGraphicsExecution& execution,
        core::ArrayView<std::shared_ptr<rhi::RenderPipeline>> pipelines)
    {
        using namespace rhi;
        auto require = [](bool passed, const char* diagnostic) {
            if (!passed)
                throw core::DiagnosticError(diagnostic);
        };
        require(pipelines.size() == 4, "CATALOG-FIXTURE-PIPELINES");
        auto color = device.createTexture({"CatalogFixtureColor",
            {4, 4, 1},
            TextureFormat::Rgba8Unorm,
            TextureUsage::RenderAttachment,
            MemoryUsage::GpuOnly,
            ResourceState::ColorAttachment});
        auto depth = device.createTexture({"CatalogFixtureDepth",
            {4, 4, 1},
            TextureFormat::Depth24Stencil8,
            TextureUsage::RenderAttachment,
            MemoryUsage::GpuOnly,
            ResourceState::DepthAttachment});
        auto overlay = device.createTexture({"CatalogFixtureOverlay",
            {4, 4, 1},
            TextureFormat::Rgba8Unorm,
            TextureUsage::RenderAttachment,
            MemoryUsage::GpuOnly,
            ResourceState::ColorAttachment});
        require(color && depth && overlay, "CATALOG-FIXTURE-TARGETS");
        const std::array<OrderedGraphicsTarget, 2> targets{
            {{color.get(), depth.get()}, {overlay.get(), nullptr}}};
        semantics::PreparedGraphicsCommandsRequest budget{5,
            true,
            4,
            4 * (256 + 176 + 64),
            24,
            {1, true, 12, 32, 4096, 4u << 20, 64},
            4,
            2,
            4,
            8,
            2,
            2,
            5};
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.issue.maxCpuMetadataBytes);
        const char* diagnostic = nullptr;
        auto prepare = [&]() {
            return execution.prepare(pipelines, targets, budget, quota, diagnostic);
        };
        budget.contractVersion = 4;
        budget.maxVertexStreams = budget.maxDescriptorSets = budget.maxSampledTextures = 0;
        require(!prepare() && diagnostic
                && std::string_view(diagnostic) == "RHI-ORDERED-GRAPHICS-CATALOG-TOPOLOGY"
                && !quota->liveBytes(),
            "CATALOG-FIXTURE-LEGACY-TOPOLOGY");
        budget.contractVersion = 5;
        for (const auto& pipeline : pipelines) {
            const auto* definition = execution.pipelineDefinition(*pipeline, diagnostic);
            require(bool(definition), "CATALOG-FIXTURE-ORIGINAL-DEFINITION");
            budget.maxVertexStreams = std::max(budget.maxVertexStreams,
                static_cast<std::uint32_t>(definition->vertexBuffers.size()));
            budget.maxDescriptorSets = std::max(budget.maxDescriptorSets,
                static_cast<std::uint32_t>(pipeline->layout()->desc().descriptorSets.size()));
        }
        budget.maxSampledTextures = 5;
        for (auto* capacity :
            {&budget.maxVertexStreams, &budget.maxDescriptorSets, &budget.maxSampledTextures}) {
            --*capacity;
            auto rejected = prepare();
            require(!rejected, "CATALOG-FIXTURE-CAPACITY-ACCEPTED");
            require(diagnostic
                    && std::string_view(diagnostic)
                        == (*capacity ? "RHI-ORDERED-GRAPHICS-CATALOG-CAPACITY"
                                      : "RHI-ORDERED-GRAPHICS-CATALOG-BUDGET"),
                diagnostic ? diagnostic : "CATALOG-FIXTURE-CAPACITY-DIAGNOSTIC");
            require(!quota->liveBytes(), "CATALOG-FIXTURE-CAPACITY-REFUND");
            ++*capacity;
        }
        {
            auto encoder = prepare();
            require(bool(encoder), diagnostic ? diagnostic : "CATALOG-FIXTURE-PREPARE");
            auto vertex = device.createBuffer({"CatalogFixtureVertices",
                256,
                ResourceUsage::VertexBuffer | ResourceUsage::CopyDst,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            const std::array<float, 64> bytes{};
            require(vertex && device.writeBuffer(*vertex, bytes.data(), sizeof(bytes)).success,
                "CATALOG-FIXTURE-VERTICES");
            const auto& guiLayout = pipelines[3]->layout()->desc().descriptorLayouts.front();
            auto arena = device.createDescriptorArena({"CatalogFixtureDescriptors", 1, 2});
            auto set = arena ? arena->allocateDescriptorSet({"CatalogFixtureSet", guiLayout})
                             : core::UniquePtr<DescriptorSet>{};
            require(bool(set), "CATALOG-FIXTURE-SET");
            auto sampled = device.createTexture({"CatalogFixtureSample",
                {1, 1, 1},
                TextureFormat::Rgba8Unorm,
                TextureUsage::Sampled | TextureUsage::CopyDst,
                MemoryUsage::GpuOnly,
                ResourceState::ShaderRead});
            const std::array<std::uint8_t, 4> texel{255, 255, 255, 255};
            require(sampled
                    && execution.uploadTexture(*sampled, {1, 0, {0, 0, 0}, {1, 1, 1}, 4, texel})
                        .success,
                "CATALOG-FIXTURE-SAMPLE-UPLOAD");
            TextureViewDesc viewDesc;
            viewDesc.format = TextureFormat::Rgba8Unorm;
            viewDesc.usage = TextureUsage::Sampled;
            auto view = device.createTextureView(*sampled, viewDesc);
            SamplerDesc samplerDesc;
            samplerDesc.mipFilter = SamplerMipFilter::None;
            auto sampler = device.createSampler(samplerDesc);
            require(view && sampler
                    && set
                        ->write({0,
                            DescriptorType::SampledTexture,
                            view->id(),
                            DescriptorResourceKind::TextureView})
                        .success
                    && set
                        ->write({1,
                            DescriptorType::Sampler,
                            sampler->id(),
                            DescriptorResourceKind::Sampler})
                        .success,
                "CATALOG-FIXTURE-SAMPLE-BINDINGS");
            auto begin = [&](bool withDepth, const RenderPipeline& pipeline) {
                RenderPassDesc pass;
                pass.colorAttachments.push_back({{},
                    withDepth ? color->id() : overlay->id(),
                    TextureFormat::Rgba8Unorm,
                    LoadOp::Clear,
                    StoreOp::Store});
                pass.hasDepthStencilAttachment = withDepth;
                if (withDepth) {
                    pass.depthStencilAttachment.target = depth->id();
                    pass.depthStencilAttachment.format = TextureFormat::Depth24Stencil8;
                    pass.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
                    pass.depthStencilAttachment.stencilLoadOp = LoadOp::Clear;
                }
                const auto before = core::defaultCpuAllocator().stats().allocationCount;
                auto active = encoder->beginRenderPass(pass);
                require(active && active->setPipeline(pipeline).success
                        && active->setViewport({0, 0, 4, 4, 0, 1}).success
                        && active->setScissor({0, 0, 4, 4}).success,
                    "CATALOG-FIXTURE-PASS");
                require(core::defaultCpuAllocator().stats().allocationCount == before,
                    "CATALOG-FIXTURE-SWITCH-ALLOCATION");
                return active;
            };
            // Install a set0 owner, then change both the shader ABI and stream count.
            auto active = begin(false, *pipelines[3]);
            require(active->bindDescriptorSet(0, *set).success
                    && active->bindVertexBuffer(0, *vertex).success && active->end().success,
                "CATALOG-FIXTURE-FIRST-SET");
            active = begin(true, *pipelines[1]);
            auto result = active->draw(6, 1, 0, 0);
            require(!result.success && result.diagnostics == "RHI-WEBGL1-DRAW-VERTEX-MISSING",
                "CATALOG-FIXTURE-OLD-VERTEX-REJECTED");
            require(!active->bindVertexBuffer(2, *vertex).success
                    && active->bindVertexBuffer(0, *vertex).success
                    && active->bindVertexBuffer(1, *vertex).success,
                "CATALOG-FIXTURE-REAL-STREAM-SLOTS");
            result = active->draw(6, 1, 0, 0);
            require(!result.success && result.diagnostics == "RHI-WEBGL1-DRAW-SAMPLED-SOURCE",
                "CATALOG-FIXTURE-OLD-SET-REJECTED");
            result = active->setPipeline(*pipelines[0]);
            require(!result.success
                    && result.diagnostics == "RHI-ORDERED-GRAPHICS-ABI-PASS-BOUNDARY-REQUIRED"
                    && active->end().success,
                "CATALOG-FIXTURE-IN-PASS-GATE");
            // Switching back to a single stream must drop the second stream.
            active = begin(false, *pipelines[0]);
            result = active->draw(3, 1, 0, 0);
            require(!result.success
                    && result.diagnostics == "RHI-WEBGL1-PREPARED-PIPELINE-ATTACHMENT-MISMATCH"
                    && active->end().success,
                "CATALOG-FIXTURE-ATTACHMENT-GATE");
            active = begin(true, *pipelines[0]);
            require(!active->bindDescriptorSet(0, *set).success
                    && !active->bindVertexBuffer(1, *vertex).success,
                "CATALOG-FIXTURE-PBR-SLOT-IDENTITIES");
            const std::array<float, 64> push{};
            require(
                active
                    ->pushConstants(
                        ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(push), push.data())
                    .success,
                "CATALOG-FIXTURE-PBR-PUSH");
            result = active->pushConstants(
                ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(push), push.data());
            require(!result.success && result.diagnostics == "[PC-E013] RHI-WEBGL1-PUSH-UNCONSUMED"
                    && active->end().success,
                "CATALOG-FIXTURE-UNCONSUMED-PUSH");
        }
        require(!quota->liveBytes(), "CATALOG-FIXTURE-CANCELLATION-REFUND");
    }
};
} // namespace arp::tests
