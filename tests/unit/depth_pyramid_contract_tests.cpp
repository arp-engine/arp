#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/visibility/depth_pyramid_contract.h"
#include "renderer/visibility/depth_pyramid_renderer.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <cstdio>
#include <filesystem>

using arp::renderer::visibility::DepthPyramidExtent;
using arp::renderer::visibility::depthPyramidMipExtent;
using arp::renderer::visibility::depthPyramidMipLevelCount;
using arp::renderer::visibility::isConservativelyOccludedByMaxDepth;
using arp::renderer::visibility::validDepthPyramidExtent;

int main()
{
    assert(!validDepthPyramidExtent({0u, 720u}));
    assert(!validDepthPyramidExtent({1280u, 0u}));
    assert(depthPyramidMipLevelCount({0u, 0u}) == 0u);
    assert(depthPyramidMipLevelCount({1u, 1u}) == 1u);
    assert(depthPyramidMipLevelCount({1280u, 720u}) == 11u);
    assert(depthPyramidMipLevelCount({3u, 5u}) == 3u);

    assert((depthPyramidMipExtent({3u, 5u}, 0u) == DepthPyramidExtent{3u, 5u}));
    assert((depthPyramidMipExtent({3u, 5u}, 1u) == DepthPyramidExtent{1u, 2u}));
    assert((depthPyramidMipExtent({3u, 5u}, 2u) == DepthPyramidExtent{1u, 1u}));
    assert((depthPyramidMipExtent({3u, 5u}, 3u) == DepthPyramidExtent{}));

    assert(isConservativelyOccludedByMaxDepth(0.8f, 0.7f));
    assert(!isConservativelyOccludedByMaxDepth(0.7f, 0.7f));
    assert(!isConservativelyOccludedByMaxDepth(0.2f, 0.7f));

    auto device = arp::rhi::createNullDevice();
    assert(device);
    arp::renderer::visibility::DepthPyramidRenderer renderer;
    const auto shaderPath = std::filesystem::path(ARP_SHADER_DIR) / "depth_pyramid.slang";
    const auto created = renderer.create({
        *device,
        {33u, 17u},
        shaderPath.string().c_str(),
    });
    assert(created.success);
    assert(renderer.mipLevelCount() == 6u);

    arp::render_graph::FrameGraph graph(*device);
    arp::rhi::TextureDesc depthDesc;
    depthDesc.label = "DepthPyramidContract.SourceDepth";
    depthDesc.extent = {33u, 17u, 1u};
    depthDesc.format = arp::rhi::TextureFormat::D32Float;
    depthDesc.usage = arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled;
    depthDesc.memory = arp::rhi::MemoryUsage::Transient;
    const auto sourceDepth = graph.createTransientTexture(depthDesc);
    graph.addPass("DepthPyramidContract.DepthPrepass")
        .writeAttachment(sourceDepth,
            arp::rhi::ResourceState::DepthAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});
    arp::renderer::visibility::DepthPyramidGraphOutputs outputs;
    const auto contribution = renderer.contributeGraph({graph, sourceDepth, 0u}, outputs);
    assert(contribution.success);
    assert(outputs.mipLevelCount == 6u);
    graph.addPass("DepthPyramidContract.Consumer")
        .read(outputs.texture, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});
    const auto compiled = graph.compile();
    bool depthToCopy = false;
    bool copyToReduce = false;
    bool reduceToConsumer = false;
    std::uint32_t reductionCount = 0u;
    for (std::size_t passIndex = 0u; passIndex < compiled.passes().size(); ++passIndex) {
        if (compiled.passes()[passIndex].name != "DepthPyramid.Reduce")
            continue;
        const auto& uses = compiled.passResourceUses()[passIndex];
        assert(uses.size() == 2u);
        assert(uses[0].state == arp::rhi::ResourceState::ShaderWrite);
        assert(uses[1].state == arp::rhi::ResourceState::ShaderWrite);
        assert(uses[0].textureRange.baseMipLevel == reductionCount);
        assert(uses[1].textureRange.baseMipLevel == reductionCount + 1u);
        assert(uses[0].resourceAccess == arp::rhi::ResourceAccess::ShaderRead);
        assert(uses[1].resourceAccess == arp::rhi::ResourceAccess::ShaderWrite);
        ++reductionCount;
    }
    assert(reductionCount == renderer.mipLevelCount() - 1u);
    for (const auto& dependency : compiled.dependencies()) {
        const auto& from = compiled.passes()[dependency.fromPass].name;
        const auto& to = compiled.passes()[dependency.toPass].name;
        depthToCopy = depthToCopy
            || (from == "DepthPyramidContract.DepthPrepass" && to == "DepthPyramid.Copy");
        copyToReduce = copyToReduce || (from == "DepthPyramid.Copy" && to == "DepthPyramid.Reduce");
        reduceToConsumer = reduceToConsumer
            || (from == "DepthPyramid.Reduce" && to == "DepthPyramidContract.Consumer");
    }
    assert(depthToCopy);
    assert(copyToReduce);
    assert(reduceToConsumer);
    graph.execute();
    assert(renderer.resize({64u, 32u}).success);
    assert(renderer.mipLevelCount() == 7u);
    renderer.release();
    assert(!renderer.created());

    std::printf("depth_pyramid_contract_tests: all assertions passed\n");
    return 0;
}
