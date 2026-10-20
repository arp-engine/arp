#include "renderer/frame/executable_render_feature.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <iostream>

namespace {

class RecordingFeature final : public arp::renderer::IExecutableRenderFeature {
public:
    RecordingFeature(const char* featureId,
        arp::renderer::RenderFeatureStage featureStage,
        arp::core::pmr::Vector<arp::core::pmr::String>& trace,
        bool failCreate = false)
        : id_(featureId), stage_(featureStage), trace_(trace), failCreate_(failCreate)
    {}

    const char* id() const noexcept override { return id_; }

    arp::renderer::RenderFeatureStage stage() const noexcept override { return stage_; }

    arp::renderer::RenderFeatureResult create(
        const arp::renderer::RenderFeatureCreateContext&) override
    {
        trace_.push_back(arp::core::pmr::String("create:") + id_);
        return failCreate_
            ? arp::renderer::RenderFeatureResult{false, "TEST-CREATE-FAILURE", "requested failure"}
            : arp::renderer::RenderFeatureResult{true, {}, {}};
    }

    arp::renderer::RenderFeatureResult resize(arp::rhi::Extent2D) override
    {
        trace_.push_back(arp::core::pmr::String("resize:") + id_);
        return {true, {}, {}};
    }

    arp::renderer::RenderFeatureResult contributeGraph(
        const arp::renderer::RenderFeatureGraphContext&) override
    {
        trace_.push_back(arp::core::pmr::String("graph:") + id_);
        return {true, {}, {}};
    }

    void release() noexcept override { trace_.push_back(arp::core::pmr::String("release:") + id_); }

private:
    const char* id_;
    arp::renderer::RenderFeatureStage stage_;
    arp::core::pmr::Vector<arp::core::pmr::String>& trace_;
    bool failCreate_ = false;
};

void feature_set_orders_lifecycle_by_production_stage()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);
    arp::core::pmr::Vector<arp::core::pmr::String> trace;
    RecordingFeature post("post", arp::renderer::RenderFeatureStage::PostProcess, trace);
    RecordingFeature depth("depth", arp::renderer::RenderFeatureStage::ExactDepth, trace);
    RecordingFeature cull("cull", arp::renderer::RenderFeatureStage::LightingCull, trace);
    arp::renderer::ExecutableRenderFeatureSet features;
    assert(features.registerFeature(post).success);
    assert(features.registerFeature(depth).success);
    assert(features.registerFeature(cull).success);
    assert(features.create({*device, {64u, 64u}}).success);
    assert(trace[0] == "create:depth");
    assert(trace[1] == "create:cull");
    assert(trace[2] == "create:post");

    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = graph.createTransientTexture({
        "FeatureTestBackbuffer",
        {64u, 64u, 1u},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::Transient,
    });
    assert(features.contributeGraph({graph, backbuffer, 0u}).success);
    assert(trace[3] == "graph:depth");
    assert(trace[4] == "graph:cull");
    assert(trace[5] == "graph:post");
    assert(features.resize({128u, 72u}).success);
    features.release();
    assert(trace[9] == "release:post");
    assert(trace[10] == "release:cull");
    assert(trace[11] == "release:depth");
}

void feature_set_rejects_duplicates_and_rolls_back_create_failure()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);
    arp::core::pmr::Vector<arp::core::pmr::String> trace;
    RecordingFeature depth("same", arp::renderer::RenderFeatureStage::ExactDepth, trace);
    RecordingFeature duplicate("same", arp::renderer::RenderFeatureStage::PostProcess, trace);
    RecordingFeature failure(
        "failure", arp::renderer::RenderFeatureStage::LightingCull, trace, true);
    arp::renderer::ExecutableRenderFeatureSet features;
    assert(features.registerFeature(depth).success);
    const auto duplicateResult = features.registerFeature(duplicate);
    assert(!duplicateResult.success);
    assert(duplicateResult.diagnosticId == "ARP-RENDER-FEATURE-ID-DUPLICATE");
    assert(features.registerFeature(failure).success);
    const auto created = features.create({*device, {64u, 64u}});
    assert(!created.success);
    assert(created.diagnosticId == "TEST-CREATE-FAILURE");
    assert(trace.back() == "release:same");
    assert(!features.created());
}

} // namespace

int main()
{
    feature_set_orders_lifecycle_by_production_stage();
    feature_set_rejects_duplicates_and_rolls_back_create_failure();
    std::cout << "executable_render_feature_tests: all assertions passed\n";
    return 0;
}
