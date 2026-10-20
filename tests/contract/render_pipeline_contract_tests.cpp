#include "core/cpu_allocator.h"
#include "renderer/pipeline/render_pipeline.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <string>

class CountingFeature final : public arp::renderer::RenderFeature {
public:
    const char* name() const override { return "CountingFeature"; }

    void registerPasses(arp::renderer::RenderPipelineBuilder& builder) override
    {
        auto color = builder.createTransientColor("GenericIntermediate");
        builder.addGraphicsPass("GenericOpaque",
            {},
            {arp::renderer::RenderTargetWrite{color, arp::rhi::ResourceState::ColorAttachment}},
            [&](arp::render_graph::RenderContext&) {
                ++executed_;
            });
        builder.addGraphicsPass("GenericComposite",
            {arp::renderer::RenderTargetRead{color, arp::rhi::ResourceState::ShaderRead}},
            {arp::renderer::RenderTargetWrite{
                builder.backbuffer(), arp::rhi::ResourceState::Present}},
            [&](arp::render_graph::RenderContext&) {
                ++executed_;
            });
    }

    int executed() const { return executed_; }

private:
    int executed_ = 0;
};

int main()
{
    arp::renderer::RenderPipelineDesc desc;
    desc.name = "GenericPipeline";
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.extent = {320, 180};
    assert(desc.name.find("Stylized") == std::pmr::string::npos);

    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    auto backbuffer = graph.createTexture(arp::rhi::TextureDesc{
        "Backbuffer",
        {desc.extent.width, desc.extent.height, 1u},
        desc.colorFormat,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Present,
        1,
    });

    arp::renderer::RenderPipelineBuilder builder(graph, backbuffer, desc);
    CountingFeature feature;
    feature.registerPasses(builder);
    graph.execute();

    assert(feature.executed() == 2);
    assert(graph.executionTrace().size() == 2);
    assert(graph.executionTrace()[0] == std::pmr::string("GenericOpaque"));
    assert(graph.executionTrace()[1] == std::pmr::string("GenericComposite"));
    for (const auto& pass : graph.executionTrace()) {
        assert(pass.find("Stylized") == std::pmr::string::npos);
        assert(pass.find("Npr") == std::pmr::string::npos);
    }
    return 0;
}
