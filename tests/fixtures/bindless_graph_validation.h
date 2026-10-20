#pragma once
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/gpu_extensions.h"

#include <array>
#include <cassert>
#include <stdexcept>

namespace arp::tests {

inline void validateGraphSnapshot(rhi::GpuDevice& device,
    const rhi::BindlessCrossExtension& extension,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
    const std::array<std::shared_ptr<rhi::GpuBuffer>, 4>& buffers,
    std::shared_ptr<rhi::GpuTexture> sampled,
    std::shared_ptr<rhi::GpuTexture> storage)
{
    using namespace rhi;
    using namespace render_graph;
    using ResourceAccess = rhi::ResourceAccess;
    core::pmr::String diagnostic;
    auto pipeline = extension.createComputePipeline(authorization, "computeMain", diagnostic);
    assert(pipeline);
    RenderPipelineDesc graphicsDesc;
    graphicsDesc.colorFormat = TextureFormat::Rgba8Unorm;
    graphicsDesc.dynamicViewportScissor = true;
    auto graphics = extension.createRenderPipeline(
        authorization, graphicsDesc, "vertexMain", "fragmentMain", diagnostic);
    assert(graphics);
    enum class Case {
        Complete,
        MissingMetadata,
        ShortBuffer,
        MissingStage,
        MissingWrite,
        MissingTexture,
        StaleGeneration,
        NoRetention,
        NoGraphScope,
        ForeignEncoder
    };
    for (bool render : {false, true})
        for (auto test : {Case::Complete,
                 Case::MissingMetadata,
                 Case::ShortBuffer,
                 Case::MissingStage,
                 Case::MissingWrite,
                 Case::MissingTexture,
                 Case::StaleGeneration,
                 Case::NoRetention,
                 Case::NoGraphScope,
                 Case::ForeignEncoder}) {
            if ((render && (test == Case::MissingWrite || test == Case::ForeignEncoder))
                || (!render && test == Case::MissingTexture))
                continue;
            FrameGraph graph(device);
            if (test != Case::NoRetention)
                graph.setCommandEncoderDesc({"IndexedGraph", 1, 1});
            const auto metadata = graph.importSnapshotMetadata(snapshot);
            assert(graph.importSnapshotMetadata(snapshot).id == metadata.id);
            // An ordinary import of the same pointer must not erase its restriction.
            assert(graph
                       .importBufferRef(
                           std::shared_ptr<GpuBuffer>(snapshot, snapshot->metadataBuffer()))
                       .id
                == metadata.id);
            auto pass =
                graph.addPass("IndexedGraphFixture").kind(PassKind::Compute).externalSideEffects();
            for (unsigned mode = 0; mode < 3; ++mode) {
                bool rejected = false;
                try {
                    if (mode == 0)
                        pass.write(metadata, ResourceState::ShaderWrite);
                    else if (mode == 1)
                        pass.read(metadata, ResourceState::ShaderWrite);
                    else
                        pass.access(metadata,
                            {ResourceState::ShaderWrite,
                                PipelineStage::ComputeShader,
                                ResourceAccess::ShaderWrite},
                            0,
                            160);
                } catch (const std::runtime_error& error) {
                    rejected = std::string_view(error.what()) == "RG-BINDLESS-METADATA-READ-ONLY";
                }
                assert(rejected);
            }
            if (test != Case::MissingMetadata)
                pass.access(metadata,
                    {ResourceState::ShaderRead,
                        render ? PipelineStage::VertexShader | PipelineStage::FragmentShader
                               : PipelineStage::ComputeShader,
                        ResourceAccess::ShaderRead},
                    0,
                    160);
            for (std::size_t i = 0; i < buffers.size(); ++i) {
                if (render && i != 1)
                    continue;
                const auto handle = graph.importBufferRef(buffers[i]);
                const auto stages = i == 1
                    ? (test == Case::MissingStage ? (render ? PipelineStage::ComputeShader
                                                            : PipelineStage::FragmentShader)
                                                  : (render ? PipelineStage::VertexShader
                                                            : PipelineStage::ComputeShader))
                    : PipelineStage::ComputeShader;
                const auto writes = i >= 2;
                pass.access(handle,
                    {writes ? ResourceState::ShaderWrite : ResourceState::ShaderRead,
                        stages,
                        ResourceAccess::ShaderRead
                            | (writes ? ResourceAccess::ShaderWrite : ResourceAccess::None)},
                    0,
                    i == 1 && test == Case::ShortBuffer ? 8 : 16);
            }
            if (render && test != Case::MissingTexture)
                pass.access(graph.importTextureRef(sampled),
                    {ResourceState::ShaderRead,
                        PipelineStage::FragmentShader,
                        ResourceAccess::ShaderRead},
                    defaultTextureSubresourceRange(sampled->desc()));
            if (!render)
                pass.access(graph.importTextureRef(storage),
                    {ResourceState::ShaderWrite,
                        PipelineStage::ComputeShader,
                        ResourceAccess::ShaderRead
                            | (test == Case::MissingWrite ? ResourceAccess::None
                                                          : ResourceAccess::ShaderWrite)},
                    defaultTextureSubresourceRange(storage->desc()));
            unsigned records = 0;
            if (!render)
                pass.execute([&](RenderContext& context) {
                    auto encoder = context.commands().beginComputePass({"IndexedGraphFixture"});
                    assert(encoder);
                    const auto selected = encoder->setPipeline(*pipeline);
                    if (test == Case::NoRetention) {
                        assert(!selected.success
                            && selected.diagnostics == "RHI-BINDLESS-COMMAND-PIPELINE-BUDGET");
                        assert(encoder->end().success);
                        return;
                    }
                    assert(selected.success);
                    auto* indexed = static_cast<BindlessPassExtension*>(
                        encoder->queryInterface(kExtensionBindlessCross));
                    assert(indexed);
                    if (test == Case::ForeignEncoder) {
                        auto other = device.createCommandEncoder({"ForeignIndexedEncoder", 1, 1});
                        auto otherPass = other->beginComputePass({"ForeignIndexedPass"});
                        assert(otherPass && otherPass->setPipeline(*pipeline).success);
                        auto* foreign = static_cast<BindlessPassExtension*>(
                            otherPass->queryInterface(kExtensionBindlessCross));
                        assert(foreign
                            && context
                                    .bindSnapshot(*foreign,
                                        snapshot,
                                        snapshot->plan(),
                                        snapshot->generation())
                                    .diagnostics
                                == "RG-BINDLESS-RECORDING-SCOPE-MISMATCH");
                        assert(!otherPass->dispatch(1, 1, 1).success && otherPass->end().success);
                        assert(encoder->end().success);
                        return;
                    }
                    RenderContext standalone(device, context.commands());
                    const auto& active = test == Case::NoGraphScope ? standalone : context;
                    const auto bound = active.bindSnapshot(*indexed,
                        snapshot,
                        snapshot->plan(),
                        snapshot->generation() + (test == Case::StaleGeneration ? 1u : 0u));
                    if (test == Case::Complete) {
                        assert(bound.success && encoder->dispatch(1, 1, 1).success);
                        const auto allocations =
                            core::defaultCpuAllocator().stats().allocationCount;
                        for (unsigned i = 0; i < 32; ++i)
                            assert(context
                                    .bindSnapshot(*indexed,
                                        snapshot,
                                        snapshot->plan(),
                                        snapshot->generation())
                                    .success);
                        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
                        ++records;
                    } else {
                        assert(!bound.success && !encoder->dispatch(1, 1, 1).success);
                        assert(bound.diagnostics
                            == (test == Case::StaleGeneration ? "RHI-BINDLESS-RECORD-PLAN-MISMATCH"
                                    : test == Case::NoGraphScope
                                    ? "RHI-BINDLESS-ACCESS-SET-INVALID"
                                    : "RHI-BINDLESS-ACCESS-SET-INCOMPLETE"));
                    }
                    assert(encoder->end().success);
                });
            else {
                const auto target = graph.createTexture({"IndexedGraphTarget",
                    {2, 2, 1},
                    TextureFormat::Rgba8Unorm,
                    TextureUsage::RenderAttachment,
                    MemoryUsage::GpuOnly,
                    ResourceState::Undefined});
                pass.kind(PassKind::Graphics)
                    .writeAttachment(
                        target, ResourceState::ColorAttachment, LoadOp::Clear, StoreOp::Store)
                    .executeRenderPass([&](RenderPassContext& context) {
                        auto& encoder = context.renderPass();
                        const auto selected = encoder.setPipeline(*graphics);
                        if (test == Case::NoRetention) {
                            assert(!selected.success
                                && selected.diagnostics == "RHI-BINDLESS-COMMAND-PIPELINE-BUDGET");
                            return;
                        }
                        assert(selected.success && encoder.setViewport({0, 0, 2, 2, 0, 1}).success
                            && encoder.setScissor({0, 0, 2, 2}).success);
                        auto* indexed = static_cast<BindlessPassExtension*>(
                            encoder.queryInterface(kExtensionBindlessCross));
                        assert(indexed);
                        RenderContext standalone(device, context.commands());
                        const auto generation =
                            snapshot->generation() + (test == Case::StaleGeneration ? 1u : 0u);
                        const auto bound = test == Case::NoGraphScope
                            ? standalone.bindSnapshot(
                                  *indexed, snapshot, snapshot->plan(), generation)
                            : context.bindSnapshot(snapshot, snapshot->plan(), generation);
                        if (test == Case::Complete) {
                            assert(bound.success && encoder.draw(3, 1, 0, 0).success);
                            ++records;
                            const auto allocations =
                                core::defaultCpuAllocator().stats().allocationCount;
                            for (unsigned i = 0; i < 32; ++i)
                                assert(context
                                        .bindSnapshot(
                                            snapshot, snapshot->plan(), snapshot->generation())
                                        .success);
                            assert(
                                core::defaultCpuAllocator().stats().allocationCount == allocations);
                        } else {
                            assert(!bound.success && !encoder.draw(3, 1, 0, 0).success);
                            assert(bound.diagnostics
                                == (test == Case::StaleGeneration
                                        ? "RHI-BINDLESS-RECORD-PLAN-MISMATCH"
                                        : test == Case::NoGraphScope
                                        ? "RHI-BINDLESS-ACCESS-SET-INVALID"
                                        : "RHI-BINDLESS-ACCESS-SET-INCOMPLETE"));
                        }
                    });
            }
            const auto compiled = graph.compile();
            // A later authoring setting cannot enlarge/shrink a compiled reservation.
            graph.setCommandEncoderDesc({"ChangedAfterCompile", 0, 0});
            graph.execute(compiled);
            graph.execute(compiled);
            assert(records == (test == Case::Complete ? 2u : 0u));
        }
    {
        FrameGraph graph(device);
        const auto raw =
            graph.importBufferRef(std::shared_ptr<GpuBuffer>(snapshot, snapshot->metadataBuffer()));
        graph.addPass("PreexistingInvalidWrite").write(raw, ResourceState::ShaderWrite);
        graph.importSnapshotMetadata(snapshot);
        bool rejected = false;
        try {
            graph.compile();
        } catch (const std::runtime_error& e) {
            rejected = std::string_view(e.what()) == "RG-BINDLESS-METADATA-READ-ONLY";
        }
        assert(rejected);
    }
}

} // namespace arp::tests
