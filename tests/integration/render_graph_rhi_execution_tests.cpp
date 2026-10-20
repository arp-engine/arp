#include "fixtures/capability_override_device.h"
#include "fixtures/submission_outcome_queue.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/frame_graph_resource_pool.h"
#include "renderer/render_graph/frame_graph_uniform_ring_buffer.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/render_graph/rhi_frame_plan.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "rhi/frontend/resource_access_usage.h"
#include "rhi/gpu_descriptors.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace {

void test_whole_packed_depth_stencil_graph()
{
    using namespace arp;
    using namespace rhi;
    auto device = createNullDevice();
    render_graph::FrameGraph graph(*device);
    const auto depth = graph.createTransientTexture({"WholePackedDepth",
        {8, 8, 1},
        TextureFormat::Depth24Stencil8,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    int recorded = 0;
    for (auto load : {LoadOp::Clear, LoadOp::Load})
        graph.addPass(load == LoadOp::Clear ? "PackedClear" : "PackedLoad")
            .writeAttachment(depth, ResourceState::DepthAttachment, load, StoreOp::Store)
            .externalSideEffects()
            .executeRenderPass([&](render_graph::RenderPassContext&) {
                ++recorded;
            });
    graph.execute();
    assert(recorded == 2 && graph.executionTrace().size() == 2);
}

class BindlessAccessCapture final : public arp::rhi::BindlessPassExtension {
public:
    explicit BindlessAccessCapture(std::uint64_t recordingIdentity)
        : recordingIdentity_(recordingIdentity)
    {}

    std::uint64_t recordingIdentity() const noexcept override { return recordingIdentity_; }

    arp::rhi::CommandValidationResult bindSnapshot(
        std::shared_ptr<const arp::rhi::BindlessSnapshot>,
        const arp::rhi::BindlessLoweringPlan&,
        std::uint64_t,
        const arp::rhi::BindlessAccessDeclarations& declarations) override
    {
        if (!declarations.valid())
            return {false, "TEST-ACCESS-DECLARATIONS-INVALID", 0u};
        accesses.clear();
        for (std::size_t index = 0; index < declarations.size(); ++index) {
            arp::rhi::BindlessResourceAccess access;
            if (!declarations.read(index, access))
                return {false, "TEST-ACCESS-DECLARATION-READ-FAILED", 0u};
            accesses.push_back(access);
        }
        return {true, {}, static_cast<std::uint32_t>(accesses.size())};
    }

    std::vector<arp::rhi::BindlessResourceAccess> accesses;

private:
    std::uint64_t recordingIdentity_ = 0u;
};

void test_render_graph_forwards_precise_bindless_access_declarations()
{
    using namespace arp;
    using namespace arp::render_graph;
    using namespace arp::rhi;

    auto device = createNullDevice();
    auto texture = std::shared_ptr<GpuTexture>(device->createTexture({"BindlessGraph.Texture",
        {32u, 32u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled | TextureUsage::Storage,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
        4u}));
    auto buffer = std::shared_ptr<GpuBuffer>(device->createBuffer({"BindlessGraph.Buffer",
        128u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined}));
    auto readback = std::shared_ptr<GpuBuffer>(device->createBuffer({"BindlessGraph.Readback",
        4u,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst}));
    assert(texture && buffer && readback);

    FrameGraph graph(*device);
    const auto textureHandle = graph.importTextureRef(texture);
    const auto bufferHandle = graph.importBufferRef(buffer);
    const TextureSubresourceRange range{1u, 2u, 0u, 1u, TextureAspect::Color};
    const semantics::ResourceAccessScope textureRead{
        ResourceState::ShaderRead, PipelineStage::ComputeShader, rhi::ResourceAccess::ShaderRead};
    const semantics::ResourceAccessScope bufferWrite{
        ResourceState::ShaderWrite, PipelineStage::ComputeShader, rhi::ResourceAccess::ShaderWrite};
    const semantics::ResourceAccessScope readbackWrite{
        ResourceState::CopyDst, PipelineStage::Transfer, rhi::ResourceAccess::TransferWrite};

    std::vector<BindlessResourceAccess> captured;
    graph.addPass("BindlessGraphAccessSet")
        .access(textureHandle, textureRead, range)
        .access(bufferHandle, bufferWrite, 16u, 32u)
        .access(graph.importBufferRef(readback), readbackWrite, 0u, 4u)
        .externalSideEffects()
        .execute([&](RenderContext& context) {
            BindlessAccessCapture pass(context.commands().recordingIdentity());
            const auto result = context.bindSnapshot(pass, {}, {}, 0u);
            assert(result.success);
            captured = pass.accesses;
        });
    graph.execute();

    assert(captured.size() == 3u);
    assert(captured[0].resource == texture->id());
    assert(captured[0].kind == BindlessAccessKind::Texture);
    assert(captured[0].stages == ShaderStage::Compute);
    assert(captured[0].pipelineStages == PipelineStage::ComputeShader);
    assert(captured[0].access == rhi::ResourceAccess::ShaderRead);
    assert(captured[0].state == ResourceState::ShaderRead);
    assert(captured[0].textureRange == range);
    assert(captured[1].resource == buffer->id());
    assert(captured[1].kind == BindlessAccessKind::Buffer);
    assert(captured[1].stages == ShaderStage::Compute);
    assert(captured[1].pipelineStages == PipelineStage::ComputeShader);
    assert(captured[1].access == rhi::ResourceAccess::ShaderWrite);
    assert(captured[1].state == ResourceState::ShaderWrite);
    assert(captured[1].bufferOffset == 16u);
    assert(captured[1].bufferSize == 32u);
    assert(captured[2].resource == readback->id());
    assert(captured[2].kind == BindlessAccessKind::Buffer);
    assert(captured[2].stages == ShaderStage::None);
    assert(captured[2].pipelineStages == PipelineStage::Transfer);
    assert(captured[2].access == rhi::ResourceAccess::TransferWrite);
    assert(captured[2].state == ResourceState::CopyDst);
    assert(captured[2].bufferOffset == 0u);
    assert(captured[2].bufferSize == 4u);
}

class SubmissionObserver final : public arp::render_graph::PassSubmissionObserver {
public:
    explicit SubmissionObserver(arp::render_graph::FrameGraph& graph) : graph_(graph) {}

    void onSubmitted(arp::render_graph::PassSubmissionToken,
        const std::shared_ptr<const arp::rhi::GpuFence>& completion) noexcept override
    {
        assert(graph_.lastExecutionSubmitted());
        assert(completion);
        lastCompletion = completion;
        ++commits;
    }

    unsigned commits = 0u;
    std::shared_ptr<const arp::rhi::GpuFence> lastCompletion;

private:
    arp::render_graph::FrameGraph& graph_;
};

void test_submission_tokens_bind_the_original_graph()
{
    using namespace arp;
    using Token = render_graph::PassSubmissionToken;

    struct BoundObserver final : render_graph::PassSubmissionObserver {
        Token prepared{7u, 1u};
        unsigned commits = 0u;

        const char* validateSubmission(Token token) const noexcept override
        {
            return token.ownerId == prepared.ownerId && token.generation != 0u
                    && token.generation == prepared.generation
                ? nullptr
                : "TEST-STALE-RECEIPT";
        }

        void onSubmitted(
            Token token, const std::shared_ptr<const rhi::GpuFence>& completion) noexcept override
        {
            assert(validateSubmission(token) == nullptr);
            assert(completion);
            prepared.generation = 0u;
            ++commits;
        }
    } observer;

    auto device = rhi::createNullDevice();
    render_graph::FrameGraph oldGraph(*device), newGraph(*device);
    unsigned records = 0u;
    auto oldBinding = oldGraph.addPass("OldHistory").externalSideEffects();
    oldBinding.onSubmitted(observer, observer.prepared).execute([&](render_graph::RenderContext&) {
        ++records;
    });
    auto oldCompiled = oldGraph.compile();
    observer.prepared.generation = 2u;
    bool invalidateDuringRecord = false;
    auto newBinding = newGraph.addPass("NewHistory").externalSideEffects();
    newBinding.onSubmitted(observer, observer.prepared).execute([&](render_graph::RenderContext&) {
        ++records;
        if (invalidateDuringRecord)
            ++observer.prepared.generation;
    });
    auto newCompiled = newGraph.compile();
    const auto reject = [&](auto& graph, const auto& compiled) {
        const auto submits = device->queue().submitCount();
        bool failed = false;
        try {
            graph.execute(compiled);
        } catch (const std::exception& error) {
            failed = std::string_view(error.what()) == "TEST-STALE-RECEIPT";
        }
        assert(failed && !graph.lastExecutionSubmitted());
        assert(device->queue().submitCount() == submits);
    };
    reject(oldGraph, oldCompiled);
    assert(records == 0u && observer.commits == 0u);
    newGraph.execute(newCompiled);
    assert(records == 1u && observer.commits == 1u);
    reject(newGraph, newCompiled); // Once-only consumed identity.
    assert(records == 1u);
    observer.prepared.generation = 3u;
    newBinding.onSubmitted(observer, observer.prepared);
    invalidateDuringRecord = true;
    reject(newGraph, newCompiled); // Pre-submit recheck, after real recording.
    assert(records == 2u && observer.commits == 1u);
    invalidateDuringRecord = false;
    for (const auto invalid : {Token{}, Token{8u, 4u}}) {
        newBinding.onSubmitted(observer, invalid);
        reject(newGraph, newCompiled);
        assert(records == 2u);
    }
    newBinding.onSubmitted(observer, observer.prepared);
    newGraph.execute(newCompiled);
    assert(records == 3u && observer.commits == 2u);
    reject(oldGraph, oldCompiled); // Rebinding one graph never refreshes another.
    newGraph.reset();
    observer.prepared.generation = 5u;
    for (const auto* name : {"First", "Duplicate"}) {
        newGraph.addPass(name)
            .externalSideEffects()
            .onSubmitted(observer, observer.prepared)
            .execute([](render_graph::RenderContext&) {
                assert(false);
            });
    }
    bool duplicateRejected = false;
    const auto submits = device->queue().submitCount();
    try {
        newGraph.execute();
    } catch (const std::exception& error) {
        duplicateRejected = std::string_view(error.what()) == "RG-SUBMISSION-TOKEN-DUPLICATE";
    }
    assert(duplicateRejected && observer.commits == 2u);
    assert(device->queue().submitCount() == submits);
    newGraph.reset();
    SubmissionObserver stateless(newGraph);
    newGraph.addPass("UnsupportedIdentity")
        .externalSideEffects()
        .onSubmitted(stateless, {1u, 1u})
        .execute([](render_graph::RenderContext&) {
            assert(false);
        });
    bool unsupportedRejected = false;
    try {
        newGraph.execute();
    } catch (const std::exception& error) {
        unsupportedRejected = std::string_view(error.what()) == "RG-SUBMISSION-TOKEN-UNSUPPORTED";
    }
    assert(unsupportedRejected && stateless.commits == 0u);
    assert(device->queue().submitCount() == submits);
    newGraph.reset();
    newGraph.setPassCullingEnabled(true);
    auto unused = newGraph.createTransientTexture({"CulledToken",
        {1u, 1u, 1u},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::RenderAttachment});
    newGraph.addPass("Culled")
        .write(unused, rhi::ResourceState::ColorAttachment)
        .onSubmitted(observer, {})
        .execute([](render_graph::RenderContext&) {
            assert(false);
        });
    newGraph.addPass("Live").externalSideEffects().execute([](render_graph::RenderContext&) {});
    newGraph.execute();
    assert(observer.commits == 2u);
}

using arp::tests::SubmissionOutcomeQueue;

void test_segment_failure_preserves_only_accepted_resource_states()
{
    using namespace arp;
    using namespace arp::render_graph;
    for (bool acceptedError : {false, true}) {
        auto native = rhi::createNullDevice();
        tests::CapabilityOverrideDevice device(*native, nullptr);
        SubmissionOutcomeQueue graphics(native->queue());
        graphics.reject = !acceptedError;
        graphics.failAfterAcceptance = acceptedError;
        device.overrideQueue(&graphics);
        FrameGraph graph(device);
        auto texture = graph.createTexture({"SegmentFailure.State",
            {4u, 4u, 1u},
            rhi::TextureFormat::Rgba16Float,
            rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::Undefined});
        graph.addPass("AcceptedCompute")
            .write(texture, rhi::ResourceState::ShaderWrite)
            .queue(QueueRole::Compute)
            .asyncComputeEligible(true)
            .execute([](RenderContext&) {});
        graph.addPass("FailingGraphics")
            .read(texture, rhi::ResourceState::ShaderRead)
            .execute([](RenderContext&) {});
        graph.addPass("UnreachedCompute")
            .write(texture, rhi::ResourceState::ShaderWrite)
            .queue(QueueRole::Compute)
            .asyncComputeEligible(true)
            .execute([](RenderContext&) {});
        const auto compiled = graph.compile();
        FrameGraphRhiShaderPackageCatalog resources;
        resources.resources.push_back({texture.id, &graph.texture(texture), nullptr});
        FrameGraphRhiCommandExecutor executor({2, 16, 16, 16, nullptr, &resources});
        const auto result = executor.execute(compiled, 1, device);
        assert(!result.success && !result.multiQueueExecuted);
        assert(result.submitSegmentCount == 3u && result.executedPassCount == 2u);
        assert(result.commandBufferCount == (acceptedError ? 2u : 1u));
        assert(result.diagnostics.find(
                   acceptedError ? "test-post-submit-report-failed" : "test-submit-rejected")
            != std::pmr::string::npos);
        assert(graph.texture(texture).state()
            == (acceptedError ? rhi::ResourceState::ShaderRead : rhi::ResourceState::ShaderWrite));
        assert(native->computeQueue()->submitCount() == 1u);
        assert(graphics.submitCount() == (acceptedError ? 1u : 0u));
    }
}

void test_submission_observers_follow_acceptance_not_recording()
{
    using namespace arp;
    auto native = rhi::createNullDevice();
    tests::CapabilityOverrideDevice device(*native, nullptr);
    SubmissionOutcomeQueue queue(native->queue());
    device.overrideQueue(&queue);
    render_graph::FrameGraph graph(device);
    SubmissionObserver history(graph);
    bool failRecord = false;
    bool leavePassOpen = false;
    unsigned records = 0u;
    auto stateBuffer = native->createBuffer({"SubmissionOutcome.State",
        64u,
        rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::Undefined});
    assert(stateBuffer);
    const auto nextState = [](rhi::ResourceState before) {
        return before == rhi::ResourceState::CopyDst ? rhi::ResourceState::CopySrc
                                                     : rhi::ResourceState::CopyDst;
    };
    graph.addPass("History").externalSideEffects().onSubmitted(history).execute(
        [&](render_graph::RenderContext& context) {
            assert(!graph.lastExecutionSubmitted());
            const auto before = stateBuffer->state();
            assert(context.commands().transition(*stateBuffer, before, nextState(before)).success);
            ++records;
        });
    graph.addPass("LaterFailure")
        .externalSideEffects()
        .execute([&](render_graph::RenderContext& context) {
            if (failRecord)
                throw std::runtime_error("test-record-failed");
            if (leavePassOpen) {
                auto pass = context.commands().beginTransferPass({"UnfinishedPass"});
                assert(pass); // Deliberately omit end: real Null finish must reject.
            }
        });
    auto compiled = graph.compile();
    const auto expectFailure = [&](const char* diagnostic, bool accepted) {
        const auto before = stateBuffer->state();
        bool failed = false;
        try {
            graph.execute(compiled);
        } catch (const std::runtime_error& error) {
            failed = std::string_view(error.what()).find(diagnostic) != std::string_view::npos;
        }
        assert(failed && graph.lastExecutionSubmitted() == accepted);
        assert(stateBuffer->state() == (accepted ? nextState(before) : before));
    };
    failRecord = true;
    expectFailure("test-record-failed", false);
    failRecord = false;
    leavePassOpen = true;
    expectFailure("command encoder finish rejected while active pass is open", false);
    leavePassOpen = false;
    device.failFenceCreation(true);
    expectFailure("RG-COMPLETION-FENCE-CREATE-FAILED", false);
    device.failFenceCreation(false);
    queue.reject = true;
    expectFailure("test-submit-rejected", false);
    assert(records == 4u && history.commits == 0u && queue.submitCount() == 0u);
    assert(!history.lastCompletion);
    queue.reject = false;
    graph.execute(compiled);
    assert(stateBuffer->state() == rhi::ResourceState::CopyDst);
    const auto firstCompletion = history.lastCompletion;
    assert(firstCompletion && firstCompletion->isSignaled());
    graph.execute(compiled);
    assert(stateBuffer->state() == rhi::ResourceState::CopySrc);
    const auto secondCompletion = history.lastCompletion;
    assert(secondCompletion && secondCompletion != firstCompletion);
    assert(history.commits == 2u && queue.submitCount() == 2u);
    queue.failAfterAcceptance = true;
    expectFailure("test-post-submit-report-failed", true);
    assert(history.commits == 3u && queue.submitCount() == 3u);
    const auto acceptedErrorCompletion = history.lastCompletion;
    assert(acceptedErrorCompletion && acceptedErrorCompletion != secondCompletion);
    // A new failed attempt clears the last-attempt status but must retain the
    // fence/lifetime of preceding accepted work.
    queue.failAfterAcceptance = false;
    queue.reject = true;
    expectFailure("test-submit-rejected", false);
    assert(history.commits == 3u);
    assert(history.lastCompletion == acceptedErrorCompletion);
    graph.reset();
    assert(!graph.lastExecutionSubmitted() && history.commits == 3u);
    assert(firstCompletion->isSignaled() && secondCompletion->isSignaled()
        && acceptedErrorCompletion->isSignaled());
    graph.setPassCullingEnabled(true);
    queue.reject = false;
    auto unused = graph.createTransientTexture({"CulledHistory",
        {4u, 4u, 1u},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::RenderAttachment,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::Undefined});
    graph.addPass("CulledHistory")
        .write(unused, rhi::ResourceState::ColorAttachment)
        .onSubmitted(history)
        .execute([](render_graph::RenderContext&) {
            assert(false);
        });
    graph.addPass("Live").externalSideEffects().execute([](render_graph::RenderContext&) {});
    graph.execute();
    assert(graph.lastExecutionSubmitted() && history.commits == 3u);
    graph.reset();
    assert(history.commits == 3u);
}

void enableTransientAliasingForCompilerContract(arp::rhi::DeviceCapabilities& capabilities)
{
    capabilities.memory.transientAliasing = true;
    capabilities.features.transientAliasing = true;
}

/// Phase 4: verify the active executor records graph passes (not only smoke).
void test_active_executor_records_graph_passes()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto tex = graph.createTransientTexture({
        "ActiveExecutorTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    graph.addPass("ClearPass")
        .write(tex, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("SamplePass")
        .read(tex, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();
    graph.execute();

    // Phase 4 acceptance: execution trace contains the graph passes.
    assert(graph.executionTrace().size() >= 2u);
    assert(graph.executionTrace()[0] == std::pmr::string("ClearPass"));
    assert(graph.executionTrace()[1] == std::pmr::string("SamplePass"));
}

/// Phase 4: verify barrier trace is non-empty after graph execution.
void test_barrier_emitted_to_encoder()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto tex = graph.createTransientTexture({
        "BarrierTex",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    graph.addPass("WriteBarrierTex")
        .write(tex, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("ReadBarrierTex")
        .read(tex, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    graph.execute();

    // Phase 4 acceptance: at least one barrier was recorded for the
    // transition between ColorAttachment (write) and ShaderRead (read).
    assert(!graph.barrierTrace().empty());
}

/// Phase 4: Null smoke path still executes independently.
void test_null_smoke_still_passes()
{
    using namespace arp::rhi;

    auto device = createNullDevice();
    assert(device);

    NullHiddenFrameSmokeRunner runner(*device);
    const auto report = runner.run({
        "Phase4Smoke",
        {320, 180},
        TextureFormat::Bgra8Unorm,
        2u,
    });

    if (!report.success) {
        std::cerr << "Null hidden-frame smoke failed: " << report.diagnostics << '\n';
    }
    assert(report.success);
    assert(report.backend == Backend::Null);
    assert(report.submittedFrameCount == 2u);
    assert(report.totalSubmittedCommandCount > 0u);
    assert(report.descriptorCompletenessValidated);
    assert(report.backendFallbacksHiddenFromUpperLayer);
    assert(!report.nativeHandleExposedToUpperLayer);
}

/// Phase 4: verify the active executor can execute through the RHI frame
/// plan path (not just the inline executor). This exercises the lowering
/// closure from Phase 4.
void test_active_executor_executes_via_rhi_plan()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();

    // Build a graph with pass barriers that must be lowered.
    FrameGraph graph(*device);

    auto colorTex = graph.createTransientTexture({
        "ColorTarget",
        {256, 256, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    auto depthTex = graph.createTransientTexture({
        "DepthTarget",
        {256, 256, 1},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    graph.addPass("DepthPrepass")
        .writeAttachment(depthTex,
            arp::rhi::ResourceState::DepthAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](RenderContext&) {});
    graph.addPass("MainPass")
        .writeAttachment(colorTex,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .read(depthTex, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});
    graph.addPass("PostProcess")
        .read(colorTex, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    // Compile and execute via the RHI plan path (closure from Phase 4).
    graph.execute();

    // Verify all passes executed.
    assert(graph.executionTrace().size() == 3u);

    // Verify barriers exist: depth transition + color transitions.
    const auto& barriers = graph.barrierTrace();
    assert(!barriers.empty());

    // The barrier trace should include depth-aspect barriers and
    // color-aspect barriers from Phase 4 lowering.
    bool hasDepthBarrier = false;
    bool hasColorBarrier = false;
    for (const auto& b : barriers) {
        if (b.resourceName == std::pmr::string("DepthTarget")) {
            hasDepthBarrier = true;
        }
        if (b.resourceName == std::pmr::string("ColorTarget")) {
            hasColorBarrier = true;
        }
    }
    assert(hasDepthBarrier);
    assert(hasColorBarrier);
}

/// Phase 4: inline executor and RHI plan path produce consistent
/// barrier traces for the same graph.
void test_inline_and_plan_path_barrier_trace_match()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();

    // Build a simple graph with a single write-read transition.
    FrameGraph graph(*device);

    auto tex = graph.createTransientTexture({
        "MatchTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    graph.addPass("WriteMatch")
        .write(tex, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("ReadMatch")
        .read(tex, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    // First execution: runs through the inline executor.
    graph.execute();
    const auto& trace1 = graph.barrierTrace();
    const size_t count1 = trace1.size();
    assert(count1 >= 1u);

    graph.reset();

    // Rebuild same graph and execute again.
    // With Phase 1, executor replays compiled plan (not recomputing from
    // live state), so both runs produce the same barrier trace.
    auto tex2 = graph.createTransientTexture({
        "MatchTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    graph.addPass("WriteMatch")
        .write(tex2, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("ReadMatch")
        .read(tex2, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    graph.execute();
    const auto& trace2 = graph.barrierTrace();
    assert(trace2.size() == count1);
}

/// Phase 4: transient textures allocated via heaps in execute() produce
/// valid textures that can be resolved after execution.
void test_executed_transient_texture_resolvable()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto tex = graph.createTransientTexture({
        "ResolvableTex",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    graph.addPass("WriteResolvable")
        .write(tex, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});

    graph.execute();

    // After execution, the texture must be resolvable (not dangling).
    auto& resolved = graph.texture(tex);
    assert(resolved.desc().label == std::pmr::string("ResolvableTex"));
}

/// Phase 5: descriptor cache reuses arenas across frames.
void test_descriptor_cache_reuses()
{
    using namespace arp::rhi;

    auto device = createNullDevice();
    auto& cache = device->descriptorCache();

    DescriptorArenaDesc arenaDesc;
    arenaDesc.label = "TestArena";
    arenaDesc.maxSets = 16;
    arenaDesc.maxDescriptors = 64;

    // First acquire: cache miss, arena created.
    auto* arena1 = cache.acquireDescriptorArena(arenaDesc);
    assert(arena1 != nullptr);
    assert(cache.arenaMissCount() == 1u);
    assert(cache.arenaHitCount() == 0u);

    // Second acquire (same config): cache hit.
    auto* arena2 = cache.acquireDescriptorArena(arenaDesc);
    assert(arena2 != nullptr);
    assert(arena2 == arena1); // Same cached arena.
    assert(cache.arenaMissCount() == 1u);
    assert(cache.arenaHitCount() == 1u);
}

/// Phase 5: fence-gated recycling — deferred-release textures are recycled
/// when their fence signals.
void test_fence_gated_recycle()
{
    using namespace arp::render_graph;
    using namespace arp::rhi;

    auto device = createNullDevice();
    FrameGraphResourcePool pool(*device);

    const TextureDesc desc{
        "FenceTex",
        {64, 64, 1},
        TextureFormat::Rgba16Float,
        TextureUsage::RenderAttachment | TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    };

    // Create a texture and release it deferred with a signaled fence.
    auto tex = pool.acquireTexture(desc);
    assert(tex != nullptr);
    assert(pool.missCount() == 1u);
    pool.configureForCompiledGraph(estimateTexturePlacementRequirements(desc).sizeBytes, 1u);

    auto fence = device->createFence();
    fence->signal(); // Fence is already signaled.
    pool.releaseDeferred(std::move(tex), std::move(fence));
    assert(pool.pendingRecycleCount() == 1u);
    assert(pool.poolSize() == 0u); // Not yet in pool.

    // Recycle completed: fence is signaled, texture moves to pool.
    pool.recycleCompleted();
    assert(pool.pendingRecycleCount() == 0u);
    assert(pool.poolSize() == 1u);

    // Acquire again: cache hit from recycled texture.
    auto tex2 = pool.acquireTexture(desc);
    assert(tex2 != nullptr);
    assert(pool.missCount() == 1u); // No new allocation.
    assert(pool.hitCount() == 1u);
}

/// Phase 5 Prototype: async compute split — verify that the frame graph
/// plan correctly records asyncComputeEligible metadata and that
/// execution with a Null backend (no compute queue) degrades gracefully.
void test_async_compute_prototype_split()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto tex = graph.createTexture({
        "AsyncTex",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Storage | arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    // Pass declared as async-compute-eligible compute pass.
    graph.addPass("AsyncComputePass")
        .write(tex, arp::rhi::ResourceState::ShaderWrite)
        .queue(QueueRole::Compute)
        .asyncComputeEligible(true)
        .execute([](RenderContext&) {});

    // Subsequent graphics pass reads the compute output.
    graph.addPass("GraphicsReadback")
        .read(tex, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();

    // Verify the compiled graph has async-compute-eligible passes.
    bool foundAsync = false;
    for (const auto& pass : compiled.passes()) {
        if (pass.name == std::pmr::string("AsyncComputePass")) {
            assert(pass.asyncComputeEligible);
            assert(pass.queue == QueueRole::Compute);
            foundAsync = true;
        }
    }
    assert(foundAsync);

    // Null backend has no compute queue → execution degrades gracefully.
    // The graph executes all passes on the main queue (degradation).
    graph.execute();
    assert(graph.executionTrace().size() >= 2u);

    // Verify barrier trace includes compute→graphics transition.
    const auto& barriers = graph.barrierTrace();
    assert(!barriers.empty());
}

/// Phase 7 [P0-A]: verify the async-compute multi-queue submit uses the
/// correct timeline semaphore ordering (compute signal -> graphics wait).
/// The previous code reversed the wait/signal args (compute waited on
/// timeline=1, graphics signalled it) and would deadlock on a native Vulkan
/// compute queue because hostWait blocks forever for a value nothing
/// signals before the compute submission. The Null backend now exposes a
/// compute queue so the multi-queue path runs; the Null timeline semaphore
/// is emulated (hostWait returns immediately) so parameter bugs do not hang,
/// but the wait/signal values are observable via the execution report.
void test_async_compute_semaphore_params_correct()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto tex = graph.createTexture({
        "AsyncTex",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Storage | arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    graph.addPass("AsyncComputePass")
        .write(tex, arp::rhi::ResourceState::ShaderWrite)
        .queue(QueueRole::Compute)
        .asyncComputeEligible(true)
        .execute([](RenderContext&) {});

    graph.addPass("GraphicsReadback")
        .read(tex, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();

    FrameGraphRhiShaderPackageCatalog resources;
    resources.resources.push_back({tex.id, &graph.texture(tex), nullptr});
    FrameGraphRhiCommandExecutor executor({2, 16, 16, 16, nullptr, &resources});
    const auto report = executor.execute(compiled, 1, *device);

    if (!report.multiQueueExecuted) {
        std::cerr << "Async compute execution failed: " << report.diagnostics << '\n';
    }
    // Null exposes a compute queue -> the multi-queue path executes.
    assert(report.multiQueueExecuted);
    assert(report.asyncComputeTimelineUsed);
    // compute submission signalled timeline=1 (did NOT wait on it).
    assert(report.asyncComputeSignalValue == 1u);
    // graphics submission waited on timeline=1 (did NOT signal it).
    assert(report.asyncComputeWaitValue == 1u);
}

/// Phase 7 [P0-A2]: verify the multi-queue submit is driven by cross-queue
/// dependency edges (segment DAG), not a fixed "all-compute-then-all-graphics"
/// split. A graphics → compute → graphics graph must produce three submit
/// segments: g(signal1) → c(wait1, signal2) → g(wait2). The compute segment
/// signals 2; the trailing graphics segment waits on 2. This is the pattern
/// the old two-stage split could not express (it forced all compute before
/// all graphics, breaking a graphics producer feeding a compute consumer).
void test_async_compute_segment_dag_supports_g_c_g()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto tex1 = graph.createTexture({
        "G2C_Tex",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Storage | arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    auto tex2 = graph.createTexture({
        "C2G_Tex",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Storage | arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    // Graphics producer.
    graph.addPass("GfxProducer")
        .write(tex1, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});
    // Compute consumer/producer (async-eligible).
    graph.addPass("ComputeMiddle")
        .read(tex1, arp::rhi::ResourceState::ShaderRead)
        .write(tex2, arp::rhi::ResourceState::ShaderWrite)
        .queue(QueueRole::Compute)
        .asyncComputeEligible(true)
        .execute([](RenderContext&) {});
    // Graphics consumer.
    graph.addPass("GfxConsumer")
        .read(tex2, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();

    FrameGraphRhiShaderPackageCatalog resources;
    resources.resources.push_back({tex1.id, &graph.texture(tex1), nullptr});
    resources.resources.push_back({tex2.id, &graph.texture(tex2), nullptr});
    FrameGraphRhiCommandExecutor executor({2, 16, 16, 16, nullptr, &resources});
    const auto report = executor.execute(compiled, 1, *device);

    assert(report.multiQueueExecuted);
    assert(report.asyncComputeTimelineUsed);
    // Three segments => two timeline handoffs: g signal1, c wait1 signal2,
    // g wait2. Compute segment (middle) signals 2.
    assert(report.asyncComputeSignalValue == 2u);
    // The graphics segment immediately after compute waits on 2.
    assert(report.asyncComputeWaitValue == 2u);
    // All three passes recorded.
    assert(report.executedPassCount == 3u);
}

/// Phase 7 [P1-B]: verify the multi-queue submit segment sequence for a
/// five-segment g→c→g→c→g graph. The report must record 5 segments with
/// queue roles "GCGCG", and the last compute segment signals timeline=4
/// while the trailing graphics segment waits on 4. This exercises the
/// segment DAG with multiple queue handoffs (recordable wait/signal
/// sequence observability).
void test_async_compute_segment_dag_five_segments()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto mkTex = [&](const char* name) {
        return graph.createTexture({
            name,
            {64, 64, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Storage | arp::rhi::TextureUsage::RenderAttachment,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
    };
    auto tex1 = mkTex("G2C1");
    auto tex2 = mkTex("C2G2");
    auto tex3 = mkTex("G2C2");
    auto tex4 = mkTex("C2G3");

    // g → c → g → c → g
    graph.addPass("G1")
        .write(tex1, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});
    graph.addPass("C1")
        .read(tex1, arp::rhi::ResourceState::ShaderRead)
        .write(tex2, arp::rhi::ResourceState::ShaderWrite)
        .queue(QueueRole::Compute)
        .asyncComputeEligible(true)
        .execute([](RenderContext&) {});
    graph.addPass("G2")
        .read(tex2, arp::rhi::ResourceState::ShaderRead)
        .write(tex3, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});
    graph.addPass("C2")
        .read(tex3, arp::rhi::ResourceState::ShaderRead)
        .write(tex4, arp::rhi::ResourceState::ShaderWrite)
        .queue(QueueRole::Compute)
        .asyncComputeEligible(true)
        .execute([](RenderContext&) {});
    graph.addPass("G3").read(tex4, arp::rhi::ResourceState::ShaderRead).execute([](RenderContext&) {
    });

    const auto compiled = graph.compile();

    FrameGraphRhiShaderPackageCatalog resources;
    resources.resources.push_back({tex1.id, &graph.texture(tex1), nullptr});
    resources.resources.push_back({tex2.id, &graph.texture(tex2), nullptr});
    resources.resources.push_back({tex3.id, &graph.texture(tex3), nullptr});
    resources.resources.push_back({tex4.id, &graph.texture(tex4), nullptr});
    FrameGraphRhiCommandExecutor executor({2, 16, 16, 16, nullptr, &resources});
    const auto report = executor.execute(compiled, 1, *device);

    assert(report.multiQueueExecuted);
    // Five contiguous same-queue segments: G, C, G, C, G.
    assert(report.submitSegmentCount == 5u);
    assert(report.submitSegmentQueueRoles == std::pmr::string("GCGCG"));
    // Timeline handoffs: G signal1, C wait1 signal2, G wait2 signal3,
    // C wait3 signal4, G wait4. Last compute (C2) signals 4; trailing
    // graphics (G3) waits on 4.
    assert(report.asyncComputeSignalValue == 4u);
    assert(report.asyncComputeWaitValue == 4u);
    assert(report.executedPassCount == 5u);
}

/// Phase 6 Prototype: null smoke — a graph with two subgraph instances
/// runs without throwing.
void test_subgraph_null_smoke()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    // Parent has a shared texture that subgraphs write to and read from.
    auto sharedTex = graph.createTransientTexture({
        "SharedTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    // Subgraph A recipe.
    SubgraphRecipe recipeA;
    recipeA.name = "SubA";
    recipeA.inputPins.push_back({"InputTex", PinUsage::SampledTexture});
    recipeA.outputPins.push_back({"OutputTex", PinUsage::ColorAttachment});

    // Subgraph B recipe.
    SubgraphRecipe recipeB;
    recipeB.name = "SubB";
    recipeB.inputPins.push_back({"InTex", PinUsage::SampledTexture});
    recipeB.outputPins.push_back({"OutTex", PinUsage::SampledTexture});

    // Parent pass produces sharedTex so the subgraph input-pin read has a
    // producer (migrate makes subgraph reads use the real parent handle, so
    // read-before-write validation now applies — the old placeholder handles
    // hid this).
    graph.addPass("ProduceShared")
        .write(sharedTex, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});

    // Create subgraph A — bind sharedTex to input pin, declare passes.
    auto& subA = graph.addSubgraph(recipeA);
    subA.bindPin("InputTex", sharedTex);
    {
        auto& local = subA.localGraph();
        auto inputTex = local.createTransientTexture({
            "InputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        subA.markPinImported(inputTex); // Data comes from parent via pin binding.
        auto outputTex = local.createTransientTexture({
            "OutputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::RenderAttachment,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        local.addPass("SubA_Process")
            .read(inputTex, arp::rhi::ResourceState::ShaderRead)
            .write(outputTex, arp::rhi::ResourceState::ColorAttachment)
            .execute([](RenderContext&) {});
    }

    // Create subgraph B — bind sharedTex to input pin, declare passes.
    auto& subB = graph.addSubgraph(recipeB);
    subB.bindPin("InTex", sharedTex);
    {
        auto& local = subB.localGraph();
        auto inputTex = local.createTransientTexture({
            "InTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        subB.markPinImported(inputTex); // Data comes from parent via pin binding.
        auto outputTex = local.createTransientTexture({
            "OutTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        local.addPass("SubB_Process")
            .read(inputTex, arp::rhi::ResourceState::ShaderRead)
            .write(outputTex, arp::rhi::ResourceState::ShaderWrite)
            .execute([](RenderContext&) {});
    }

    // Compile: parent + 2 subgraphs → should not throw.
    const auto compiled = graph.compile();
    assert(compiled.subgraphs().size() == 2u);
    assert(compiled.subgraphs()[0].name == std::pmr::string("SubA"));
    assert(compiled.subgraphs()[1].name == std::pmr::string("SubB"));
}

/// Phase 7 [P0-B]: subgraph runtime execution. migratePassesAndTextures moves
/// local pass records + transient textures into the parent with real handles,
/// so the executor indexes valid records and the alias plan includes the
/// subgraph's textures. Execute must not throw or index out of bounds.
void test_subgraph_executes_with_migrated_records()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto sharedTex = graph.createTransientTexture({
        "SharedTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    SubgraphRecipe recipe;
    recipe.name = "SubGate";
    recipe.inputPins.push_back({"InputTex", PinUsage::SampledTexture});
    recipe.outputPins.push_back({"OutputTex", PinUsage::ColorAttachment});

    auto& sub = graph.addSubgraph(recipe);
    sub.bindPin("InputTex", sharedTex);
    {
        auto& local = sub.localGraph();
        auto inputTex = local.createTransientTexture({
            "InputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        sub.markPinImported(inputTex);
        auto outputTex = local.createTransientTexture({
            "OutputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::RenderAttachment,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        local.addPass("SubGate_Process")
            .read(inputTex, arp::rhi::ResourceState::ShaderRead)
            .write(outputTex, arp::rhi::ResourceState::ColorAttachment)
            .execute([](RenderContext&) {});
    }

    // Parent producer for the input-pin texture (migrate makes subgraph reads
    // use the real parent handle, so read-before-write validation applies —
    // the old placeholder handles hid this).
    graph.addPass("ProduceInput")
        .write(sharedTex, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});

    // Compile + execute must not throw — subgraph pass records are migrated
    // into the parent with real handles, so the executor indexes valid
    // records and the alias plan includes the subgraph's transient textures.
    graph.compile();
    graph.execute();
    assert(graph.executionTrace().size() >= 2u);
}

/// Phase 7 [P0-5]: compile() must be idempotent. migratePassesAndTextures()
/// must run at most once per SubgraphBuilder — without the migrated_ guard,
/// each compile() call re-creates parent transient textures and re-appends
/// the local passes, so compile()+compile()+execute() would double the
/// executed passes and accumulate duplicate resources. This test builds the
/// same graph twice (single-compile vs double-compile) and asserts the
/// execution trace pass count is identical, proving migration does not
/// accumulate across repeated compile() calls.
static std::size_t subgraphExecTraceSize(int compileCount)
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto sharedTex = graph.createTransientTexture({
        "SharedTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    SubgraphRecipe recipe;
    recipe.name = "SubIdem";
    recipe.inputPins.push_back({"InputTex", PinUsage::SampledTexture});
    recipe.outputPins.push_back({"OutputTex", PinUsage::ColorAttachment});

    auto& sub = graph.addSubgraph(recipe);
    sub.bindPin("InputTex", sharedTex);
    {
        auto& local = sub.localGraph();
        auto inputTex = local.createTransientTexture({
            "InputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        sub.markPinImported(inputTex);
        auto outputTex = local.createTransientTexture({
            "OutputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::RenderAttachment,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        local.addPass("SubIdem_Process")
            .read(inputTex, arp::rhi::ResourceState::ShaderRead)
            .write(outputTex, arp::rhi::ResourceState::ColorAttachment)
            .execute([](RenderContext&) {});
    }

    graph.addPass("ProduceInput")
        .write(sharedTex, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});

    for (int i = 0; i < compileCount; ++i) {
        graph.compile();
    }
    graph.execute();
    return graph.executionTrace().size();
}

void test_subgraph_compile_idempotent()
{
    // Single compile is the baseline; double compile must NOT duplicate the
    // migrated passes/resources, so the execution trace must be identical.
    const auto single = subgraphExecTraceSize(1);
    const auto doubled = subgraphExecTraceSize(2);
    assert(single == doubled);
    assert(single >= 2u);
}

/// Same-state writes still require WAW visibility, emitted as a memory
/// dependency instead of an illegal no-op state transition.
void test_explicit_resource_access_scopes()
{
    using namespace arp;
    using S = rhi::PipelineStage;
    using A = rhi::ResourceAccess;
    using R = rhi::ResourceState;
    auto device = rhi::createNullDevice();
    render_graph::FrameGraph graph(*device);
    auto buffer = graph.createBuffer({"IndexedInstances",
        256,
        rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::IndirectBuffer
            | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::GpuOnly,
        R::Undefined});
    auto texture = graph.createTexture({"IndexedStorage",
        {8, 8, 1},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled,
        rhi::MemoryUsage::GpuOnly,
        R::Undefined,
        2,
        1});
    const rhi::TextureSubresourceRange mip{1, 1, 0, 1, rhi::TextureAspect::Color};
    const auto noop = [](render_graph::RenderContext&) {};
    graph.addPass("Initialize")
        .access(buffer, {R::CopyDst, S::Transfer, A::TransferWrite}, 0, 256)
        .execute(noop);
    graph.addPass("Cull")
        .kind(render_graph::PassKind::Compute)
        .access(buffer, {R::ShaderWrite, S::ComputeShader, A::ShaderRead | A::ShaderWrite}, 16, 128)
        .access(texture, {R::ShaderWrite, S::ComputeShader, A::ShaderWrite}, mip)
        .execute(noop);
    graph.addPass("Geometry")
        .access(buffer, {R::ShaderRead, S::VertexShader, A::ShaderRead}, 16, 64)
        .access(buffer, {R::ShaderRead, S::DrawIndirect, A::IndirectCommandRead}, 128, 64)
        .access(texture, {R::ShaderWrite, S::VertexShader, A::ShaderRead}, mip)
        .execute(noop);
    graph.addPass("FragmentRead")
        .access(texture, {R::ShaderWrite, S::FragmentShader, A::ShaderRead}, mip)
        .execute(noop);
    graph.addPass("Update")
        .kind(render_graph::PassKind::Compute)
        .access(buffer, {R::ShaderWrite, S::ComputeShader, A::ShaderWrite}, 0, 256)
        .access(texture, {R::ShaderWrite, S::ComputeShader, A::ShaderWrite}, mip)
        .execute(noop);
    const auto compiled = graph.compile();
    const auto& uses = compiled.passResourceUses()[2];
    assert(uses.size() == 3);
    assert(uses[1].bufferOffset == 16 && uses[1].bufferSize == 64);
    assert(uses[2].bufferOffset == 128 && uses[2].bufferSize == 64);
    assert(uses[0].textureRange == mip);
    bool geometry = false, fragment = false, war = false;
    for (const auto& barrier : compiled.barrierPlan()) {
        if (barrier.passName == "Geometry" && barrier.kind == render_graph::ResourceKind::Buffer) {
            geometry = true;
            assert(barrier.dstStage == (S::VertexShader | S::DrawIndirect));
            assert(barrier.dstAccess == (A::ShaderRead | A::IndirectCommandRead));
            assert((barrier.srcAccess & A::ShaderWrite) != A::None);
        }
        if (barrier.passName == "FragmentRead") {
            fragment = true;
            assert(barrier.memoryOnly);
            assert((barrier.srcAccess & A::ShaderWrite) != A::None);
            assert(barrier.dstStage == S::FragmentShader);
        }
        if (barrier.passName == "Update" && barrier.kind == render_graph::ResourceKind::Buffer) {
            war = true;
            assert((barrier.srcStage & (S::VertexShader | S::DrawIndirect))
                == (S::VertexShader | S::DrawIndirect));
        }
    }
    assert(geometry && fragment && war);
    graph.execute(compiled);
    graph.execute(compiled); // Replay carries final-frame scopes, including same-state barriers.
    const auto reject = [&](auto action, const char* expected) {
        bool failed = false;
        try {
            action();
        } catch (const std::runtime_error& e) {
            failed = std::string(e.what()) == expected;
        }
        assert(failed);
    };
    reject(
        [&] {
            graph.addPass("BadRange")
                .access(buffer, {R::ShaderRead, S::VertexShader, A::ShaderRead}, UINT64_MAX, 4);
        },
        "RG-ACCESS-BUFFER-RANGE-INVALID");
    reject(
        [&] {
            graph.addPass("ZeroRange")
                .access(buffer, {R::ShaderRead, S::VertexShader, A::ShaderRead}, 0, 0);
        },
        "RG-ACCESS-BUFFER-RANGE-INVALID");
    reject(
        [&] {
            graph.addPass("BadStage")
                .access(buffer, {R::ShaderRead, S::VertexShader, A::IndirectCommandRead}, 0, 4);
        },
        "RHI-ACCESS-STAGE-MISMATCH");
    reject(
        [&] {
            graph.addPass("BadState")
                .access(buffer, {R::ShaderRead, S::ComputeShader, A::ShaderWrite}, 0, 4);
        },
        "RHI-ACCESS-STATE-MISMATCH");
    reject(
        [&] {
            graph.addPass("BadMip").access(
                texture, {R::ShaderRead, S::FragmentShader, A::ShaderRead}, {2, 1, 0, 1});
        },
        "RG-ACCESS-TEXTURE-RANGE-INVALID");
    reject(
        [&] {
            graph.addPass("TextureIndirect")
                .access(texture, {R::ShaderRead, S::DrawIndirect, A::IndirectCommandRead}, mip);
        },
        "RG-ACCESS-TEXTURE-USAGE-MISMATCH");
    graph.addPass("Conflict")
        .access(buffer, {R::ShaderRead, S::VertexShader, A::ShaderRead}, 0, 4)
        .access(buffer, {R::ShaderWrite, S::ComputeShader, A::ShaderWrite}, 4, 4)
        .execute(noop);
    reject(
        [&] {
            graph.compile();
        },
        "RG-ACCESS-SAME-PASS-STATE-CONFLICT: Conflict");
}

void test_imported_geometry_read_scope()
{
    using namespace arp;
    using S = rhi::PipelineStage;
    using A = rhi::ResourceAccess;
    using U = rhi::ResourceUsage;
    using R = rhi::ResourceState;
    auto device = rhi::createNullDevice();
    for (const auto usage : {U::VertexBuffer,
             U::IndexBuffer,
             U::VertexBuffer | U::IndexBuffer | U::IndirectBuffer | U::UniformBuffer}) {
        render_graph::FrameGraph graph(*device);
        const rhi::BufferDesc desc{
            "Geometry", 64, usage, rhi::MemoryUsage::CpuToGpu, R::ShaderRead};
        auto owner = core::shareUnique(device->createBuffer(desc));
        assert(owner);
        const auto handle = graph.importBufferRef(owner);
        const auto access =
            (usage & U::VertexBuffer) != U::None ? A::VertexAttributeRead : A::IndexRead;
        graph.addPass("ReadGeometry")
            .access(handle, {R::ShaderRead, S::VertexInput, access}, 0, 64)
            .externalSideEffects()
            .execute([](render_graph::RenderContext&) {});
        const auto compiled = graph.compile();
        assert(compiled.barrierPlan().size() == 1);
        const auto& barrier = compiled.barrierPlan()[0];
        const auto conservative =
            rhi::semantics::ResourceAccessUsageValidation::conservativeBufferReadScope(desc);
        assert(barrier.srcAccess == conservative.access && barrier.srcStage == conservative.stages);
        assert(!rhi::semantics::validateResourceAccessScope(conservative));
        assert(!rhi::semantics::ResourceAccessUsageValidation::validate(desc, barrier.srcAccess));
        assert(barrier.dstAccess == access && barrier.dstStage == S::VertexInput
            && barrier.memoryOnly);
        graph.execute(compiled);
        graph.execute(compiled);
        const auto forbidden = usage == U::IndexBuffer ? A::VertexAttributeRead : A::ShaderWrite;
        bool rejected = false;
        try {
            graph.addPass("Forbidden")
                .access(handle,
                    {forbidden == A::ShaderWrite ? R::ShaderWrite : R::ShaderRead,
                        forbidden == A::ShaderWrite ? S::VertexShader : S::VertexInput,
                        forbidden},
                    0,
                    64);
        } catch (const std::runtime_error& error) {
            rejected = std::string_view(error.what()) == "RG-ACCESS-BUFFER-USAGE-MISMATCH";
        }
        assert(rejected);
    }
}

void test_imported_read_first_replay_dependency()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    render_graph::FrameGraph graph(*device);
    auto image = core::shareUnique(device->createTexture({"ReadFirst",
        {4, 4, 1},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::ShaderRead}));
    const auto handle = graph.importTextureRef(image);
    graph.addPass("ReadFirst")
        .access(handle,
            {rhi::ResourceState::ShaderRead,
                rhi::PipelineStage::VertexShader,
                rhi::ResourceAccess::ShaderRead},
            {})
        .execute([&](render_graph::RenderContext&) {
            assert(image->state() == rhi::ResourceState::ShaderRead);
        });
    graph.addPass("WriteLast")
        .kind(render_graph::PassKind::Compute)
        .access(handle,
            {rhi::ResourceState::ShaderWrite,
                rhi::PipelineStage::ComputeShader,
                rhi::ResourceAccess::ShaderWrite},
            {})
        .execute([](render_graph::RenderContext&) {});
    const auto compiled = graph.compile();
    assert(compiled.barrierPlan().front().memoryOnly);
    graph.execute(compiled);
    graph.execute(compiled);
    assert(image->state() == rhi::ResourceState::ShaderWrite);
}

void test_same_state_writes_preserve_dependency()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto tex = graph.createTransientTexture({
        "MergeTex",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Storage | arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    // pass1: Undefined -> ShaderWrite (emits one barrier).
    graph.addPass("WriteA")
        .write(tex, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});
    // pass2: ShaderWrite -> ShaderWrite (access-only dependency).
    graph.addPass("WriteB")
        .write(tex, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();

    std::uint32_t texBarriers = 0;
    for (const auto& b : compiled.barrierPlan()) {
        if (b.resourceName == std::pmr::string("MergeTex")) {
            ++texBarriers;
        }
    }
    assert(texBarriers == 2u);
    assert(compiled.barrierPlan().back().memoryOnly);
    graph.execute(compiled);
}

/// Phase 6: a DepthAttachment input pin bound to a color-format parent
/// texture is a deterministic authoring error — compile must throw with a
/// usage/format mismatch diagnostic.
void test_subgraph_pin_usage_mismatch_throws()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    // Color-format parent texture.
    auto colorTex = graph.createTransientTexture({
        "ColorParent",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    SubgraphRecipe recipe;
    recipe.name = "PinMismatch";
    // DepthAttachment pin bound to a color-format texture -> mismatch.
    recipe.inputPins.push_back({"DepthPin", PinUsage::DepthAttachment});

    auto& sub = graph.addSubgraph(recipe);
    sub.bindPin("DepthPin", colorTex);

    bool threw = false;
    try {
        graph.compile();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

/// Phase 6: subgraph whole-unit cull. When culling is enabled and every
/// output pin has no external consumer, the entire subgraph is culled as a
/// single unit (culledAsUnit=true), its pass range is fully culled, and no
/// output-pin exit barrier is emitted.
void test_subgraph_whole_unit_cull()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);
    graph.setPassCullingEnabled(true);

    auto sharedTex = graph.createTransientTexture({
        "SharedTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    // Output pin bound to a parent handle so it could have an external
    // consumer — but here it has none, so the subgraph is whole-culled.
    auto outputTex = graph.createTransientTexture({
        "OutputTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    SubgraphRecipe recipe;
    recipe.name = "SubCull";
    recipe.inputPins.push_back({"InputTex", PinUsage::SampledTexture});
    recipe.outputPins.push_back({"OutputTex", PinUsage::ColorAttachment});

    auto& sub = graph.addSubgraph(recipe);
    sub.bindPin("InputTex", sharedTex);
    sub.bindPin("OutputTex", outputTex);
    {
        auto& local = sub.localGraph();
        auto inputTex = local.createTransientTexture({
            "InputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        sub.markPinImported(inputTex);
        auto outTex = local.createTransientTexture({
            "OutputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::RenderAttachment,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        (void)outTex;
        local.addPass("SubCull_Process")
            .read(inputTex, arp::rhi::ResourceState::ShaderRead)
            .write(outTex, arp::rhi::ResourceState::ColorAttachment)
            .execute([](RenderContext&) {});
    }

    graph.addPass("ProduceInput")
        .write(sharedTex, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});

    // OutputTex has NO external consumer -> subgraph whole-culled.
    const auto compiled = graph.compile();
    assert(compiled.subgraphs().size() == 1u);
    assert(compiled.subgraphs()[0].culledAsUnit == true);
    const auto& sg = compiled.subgraphs()[0];
    for (size_t i = sg.globalPassBegin; i < sg.globalPassEnd; ++i) {
        assert(compiled.passes()[i].culled == true);
    }
}

/// Phase 6: when an output pin has an external consumer, the subgraph is
/// NOT whole-culled (culledAsUnit=false) and its passes remain executable.
void test_subgraph_whole_unit_not_culled_with_consumer()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);
    graph.setPassCullingEnabled(true);

    auto sharedTex = graph.createTransientTexture({
        "SharedTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    auto outputTex = graph.createTransientTexture({
        "OutputTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    SubgraphRecipe recipe;
    recipe.name = "SubKeep";
    recipe.inputPins.push_back({"InputTex", PinUsage::SampledTexture});
    recipe.outputPins.push_back({"OutputTex", PinUsage::ColorAttachment});

    auto& sub = graph.addSubgraph(recipe);
    sub.bindPin("InputTex", sharedTex);
    sub.bindPin("OutputTex", outputTex);
    {
        auto& local = sub.localGraph();
        auto inputTex = local.createTransientTexture({
            "InputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        sub.markPinImported(inputTex);
        auto outTex = local.createTransientTexture({
            "OutputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::RenderAttachment,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        (void)outTex;
        local.addPass("SubKeep_Process")
            .read(inputTex, arp::rhi::ResourceState::ShaderRead)
            .write(outTex, arp::rhi::ResourceState::ColorAttachment)
            .execute([](RenderContext&) {});
    }

    graph.addPass("ProduceInput")
        .write(sharedTex, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});

    // External consumer of the output pin -> subgraph must survive culling.
    graph.addPass("ConsumeOutput")
        .read(outputTex, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();
    assert(compiled.subgraphs().size() == 1u);
    assert(compiled.subgraphs()[0].culledAsUnit == false);
    const auto& sg = compiled.subgraphs()[0];
    bool anyAlive = false;
    for (size_t i = sg.globalPassBegin; i < sg.globalPassEnd; ++i) {
        if (!compiled.passes()[i].culled) {
            anyAlive = true;
        }
    }
    assert(anyAlive);
}

/// Phase 6: boundary barrier contract. A non-culled subgraph emits both an
/// input-pin entry barrier and an output-pin exit barrier in the barrier
/// plan, recording the subgraph's boundary state contract.
void test_subgraph_boundary_barriers_emitted()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto sharedTex = graph.createTransientTexture({
        "SharedTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    auto outputTex = graph.createTransientTexture({
        "OutputTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    SubgraphRecipe recipe;
    recipe.name = "SubBoundary";
    recipe.inputPins.push_back({"InputTex", PinUsage::SampledTexture});
    recipe.outputPins.push_back({"OutputTex", PinUsage::ColorAttachment});

    auto& sub = graph.addSubgraph(recipe);
    sub.bindPin("InputTex", sharedTex);
    sub.bindPin("OutputTex", outputTex);
    {
        auto& local = sub.localGraph();
        auto inputTex = local.createTransientTexture({
            "InputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        sub.markPinImported(inputTex);
        auto outTex = local.createTransientTexture({
            "OutputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::RenderAttachment,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        (void)outTex;
        local.addPass("SubBoundary_Process")
            .read(inputTex, arp::rhi::ResourceState::ShaderRead)
            .write(outTex, arp::rhi::ResourceState::ColorAttachment)
            .execute([](RenderContext&) {});
    }

    graph.addPass("ProduceInput")
        .write(sharedTex, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});
    graph.addPass("ConsumeOutput")
        .read(outputTex, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();
    // Both boundary barriers must appear in the barrier plan.
    bool hasInputEntry = false;
    bool hasOutputExit = false;
    for (const auto& b : compiled.barrierPlan()) {
        if (b.passName.find("SubBoundary_InputPin_InputTex") != std::pmr::string::npos) {
            hasInputEntry = true;
        }
        if (b.passName.find("SubBoundary_OutputPin_OutputTex") != std::pmr::string::npos) {
            hasOutputExit = true;
        }
    }
    assert(hasInputEntry);
    assert(hasOutputExit);
}

/// Phase 6: alias scope policy. An Isolated-scope subgraph's transient
/// textures get a non-zero aliasScopeId, so they only alias within their
/// scope — never with the global pool (scopeId=0), even when format/size
/// match. This test proves the scopeId enters the alias group key.
void test_subgraph_alias_scope_isolated()
{
    using namespace arp::render_graph;

    auto nullDevice = arp::rhi::createNullDevice();
    arp::tests::CapabilityOverrideDevice device(
        *nullDevice, enableTransientAliasingForCompilerContract);
    FrameGraph graph(device);

    // Global transient (scopeId=0).
    auto globalTex = graph.createTransientTexture({
        "GlobalTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    SubgraphRecipe recipe;
    recipe.name = "SubIso";
    recipe.aliasScope = AliasScopePolicy::Isolated;
    recipe.inputPins.push_back({"InputTex", PinUsage::SampledTexture});

    auto& sub = graph.addSubgraph(recipe);
    sub.aliasScopeId(7);
    sub.bindPin("InputTex", globalTex);
    {
        auto& local = sub.localGraph();
        auto inputTex = local.createTransientTexture({
            "InputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        sub.markPinImported(inputTex);
        // Internal transient: same format/extent/usage as globalTex, but
        // Isolated scope → different alias group.
        auto internalTex = local.createTransientTexture({
            "InternalTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        (void)internalTex;
        local.addPass("SubIso_Process")
            .read(inputTex, arp::rhi::ResourceState::ShaderRead)
            .write(internalTex, arp::rhi::ResourceState::ColorAttachment)
            .execute([](RenderContext&) {});
    }

    graph.addPass("ProduceGlobal")
        .write(globalTex, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();
    uint32_t globalGroup = 0;
    uint32_t internalGroup = 0;
    bool foundGlobal = false;
    bool foundInternal = false;
    for (const auto& a : compiled.aliasPlan()) {
        if (a.resourceName == "GlobalTex") {
            globalGroup = a.aliasGroup;
            foundGlobal = true;
        }
        if (a.resourceName == "InternalTex") {
            internalGroup = a.aliasGroup;
            foundInternal = true;
        }
    }
    assert(foundGlobal);
    assert(foundInternal);
    // Isolated scope → different groups despite matching format/extent/usage.
    assert(globalGroup != internalGroup);
}

/// Phase 6: AllowCrossScope leaves aliasScopeId=0, so a subgraph transient
/// with matching format/extent/usage and non-overlapping lifetime aliases
/// with a global transient (same group).
void test_subgraph_alias_scope_allow_cross_scope()
{
    using namespace arp::render_graph;

    auto nullDevice = arp::rhi::createNullDevice();
    arp::tests::CapabilityOverrideDevice device(
        *nullDevice, enableTransientAliasingForCompilerContract);
    FrameGraph graph(device);

    auto inputTex = graph.createTransientTexture({
        "InputTex",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    // Global transient used only in an early pass (non-overlapping lifetime
    // with the subgraph's internal transient).
    auto globalTex = graph.createTransientTexture({
        "GlobalTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    SubgraphRecipe recipe;
    recipe.name = "SubCross";
    recipe.aliasScope = AliasScopePolicy::AllowCrossScope;
    recipe.inputPins.push_back({"InputTex", PinUsage::SampledTexture});

    auto& sub = graph.addSubgraph(recipe);
    sub.bindPin("InputTex", inputTex);
    {
        auto& local = sub.localGraph();
        auto localInput = local.createTransientTexture({
            "InputTex",
            {64, 64, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        sub.markPinImported(localInput);
        auto internalTex = local.createTransientTexture({
            "InternalTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        (void)internalTex;
        local.addPass("SubCross_Process")
            .read(localInput, arp::rhi::ResourceState::ShaderRead)
            .write(internalTex, arp::rhi::ResourceState::ColorAttachment)
            .execute([](RenderContext&) {});
    }

    // Early pass touches globalTex; subgraph touches inputTex/internalTex.
    // globalTex lifetime does not overlap internalTex (different passes,
    // no shared dependency forcing overlap).
    graph.addPass("TouchGlobal")
        .write(globalTex, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});
    graph.addPass("ProduceInput")
        .write(inputTex, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});

    const auto compiled = graph.compile();
    uint32_t globalGroup = 0;
    uint32_t internalGroup = 0;
    bool foundGlobal = false;
    bool foundInternal = false;
    for (const auto& a : compiled.aliasPlan()) {
        if (a.resourceName == "GlobalTex") {
            globalGroup = a.aliasGroup;
            foundGlobal = true;
        }
        if (a.resourceName == "InternalTex") {
            internalGroup = a.aliasGroup;
            foundInternal = true;
        }
    }
    assert(foundGlobal);
    assert(foundInternal);
    // AllowCrossScope → scopeId=0 → enters global pool. Same format/extent/
    // usage + non-overlapping lifetime → same alias group.
    assert(globalGroup == internalGroup);
}

/// Phase 6: attachment pin loadOp conflict. An input attachment pin with
/// expectedLoadOp::Load (preserve parent content) conflicts with a subgraph
/// that clears on first attachment access -> compile must throw.
void test_subgraph_attachment_loadop_conflict_throws()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto colorTex = graph.createTransientTexture({
        "ColorParent",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    SubgraphRecipe recipe;
    recipe.name = "SubLoadConflict";
    // Input pin expects LoadOp::Load (default) — preserve parent content.
    recipe.inputPins.push_back({"InputTex", PinUsage::ColorAttachment});

    auto& sub = graph.addSubgraph(recipe);
    sub.bindPin("InputTex", colorTex);
    {
        auto& local = sub.localGraph();
        auto inputTex = local.createTransientTexture({
            "InputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::RenderAttachment,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        sub.markPinImported(inputTex);
        // Subgraph clears on first attachment access -> conflicts with the
        // pin's expectedLoadOp::Load contract.
        local.addPass("SubLoadConflict_Process")
            .writeAttachment(inputTex,
                arp::rhi::ResourceState::ColorAttachment,
                arp::rhi::LoadOp::Clear,
                arp::rhi::StoreOp::Store)
            .executeRenderPass([](RenderPassContext&) {});
    }

    graph.addPass("ProduceColor")
        .write(colorTex, arp::rhi::ResourceState::ColorAttachment)
        .execute([](RenderContext&) {});

    bool threw = false;
    try {
        graph.compile();
    } catch (const std::runtime_error& e) {
        threw = true;
        const std::string msg = e.what();
        assert(msg.find("expectedLoadOp conflicts") != std::string::npos);
    }
    assert(threw);
}

/// Phase 6: subgraph boundary markers. Executing a graph with a non-culled
/// subgraph produces balanced push/pop debug markers in markerTrace — one
/// push on entering the subgraph's pass range, one pop on leaving it.
void test_subgraph_markers_balanced()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto inputTex = graph.createTransientTexture({
        "InputTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    auto outputTex = graph.createTransientTexture({
        "OutputTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    SubgraphRecipe recipe;
    recipe.name = "SubMarker";
    recipe.inputPins.push_back({"InputTex", PinUsage::SampledTexture});
    recipe.outputPins.push_back({"OutputTex", PinUsage::ColorAttachment});

    auto& sub = graph.addSubgraph(recipe);
    sub.bindPin("InputTex", inputTex);
    sub.bindPin("OutputTex", outputTex);
    {
        auto& local = sub.localGraph();
        auto localInput = local.createTransientTexture({
            "InputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::Sampled,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        sub.markPinImported(localInput);
        auto localOutput = local.createTransientTexture({
            "OutputTex",
            {128, 128, 1},
            arp::rhi::TextureFormat::Rgba16Float,
            arp::rhi::TextureUsage::RenderAttachment,
            arp::rhi::MemoryUsage::GpuOnly,
            arp::rhi::ResourceState::Undefined,
        });
        (void)localOutput;
        local.addPass("SubMarker_Process")
            .read(localInput, arp::rhi::ResourceState::ShaderRead)
            .write(localOutput, arp::rhi::ResourceState::ColorAttachment)
            .execute([](RenderContext&) {});
    }

    graph.addPass("ProduceInput")
        .write(inputTex, arp::rhi::ResourceState::ShaderWrite)
        .execute([](RenderContext&) {});
    graph.addPass("ConsumeOutput")
        .read(outputTex, arp::rhi::ResourceState::ShaderRead)
        .execute([](RenderContext&) {});

    graph.compile();
    graph.execute();

    // markerTrace must contain a balanced push/pop pair for SubMarker.
    const auto& mt = graph.markerTrace();
    int pushCount = 0;
    int popCount = 0;
    bool hasSubMarkerPush = false;
    bool hasSubMarkerPop = false;
    for (const auto& m : mt) {
        if (m.substr(0, 5) == "push:") {
            ++pushCount;
            if (m == "push:SubMarker")
                hasSubMarkerPush = true;
        }
        if (m.substr(0, 4) == "pop:") {
            ++popCount;
            if (m == "pop:SubMarker")
                hasSubMarkerPop = true;
        }
    }
    assert(hasSubMarkerPush);
    assert(hasSubMarkerPop);
    assert(pushCount == popCount);
}

/// Subpass: verify SubpassGraph compiles and executes on the Null backend
/// (native subpass path). Two subpasses share a depth attachment via input
/// attachment read.
void test_subpass_graph_native_compile_and_execute()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto colorTex = graph.createTransientTexture({
        "SubpassColor",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    auto depthTex = graph.createTransientTexture({
        "SubpassDepth",
        {128, 128, 1},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    auto& sg = graph.addSubpassGraph("DepthPrepassAndScene");
    auto colorAtt = sg.attachment(
        colorTex, arp::rhi::ResourceState::ColorAttachment, AttachmentRole::ColorAttachment);
    auto depthAtt = sg.attachment(depthTex,
        arp::rhi::ResourceState::DepthAttachment,
        AttachmentRole::DepthAttachment,
        {.memorylessAttachment = true});

    bool subpass0Executed = false;
    bool subpass1Executed = false;

    sg.subpass("DepthPrepass")
        .writeAttachment(depthAtt, arp::rhi::LoadOp::Clear, arp::rhi::StoreOp::DontCare)
        .executeRenderPass([&](RenderPassContext& rctx) {
            subpass0Executed = true;
        });

    sg.subpass("ScenePass")
        .readInputAttachment(depthAtt, 0u, 0u)
        .writeAttachment(colorAtt, arp::rhi::LoadOp::Clear, arp::rhi::StoreOp::Store)
        .writeAttachment(depthAtt, arp::rhi::LoadOp::Load, arp::rhi::StoreOp::Store)
        .executeRenderPass([&](RenderPassContext& rctx) {
            subpass1Executed = true;
        });

    // A later ordinary pass must observe the subpass group's depth write in
    // authoring order. This catches deferred subpass lowering that appends the
    // group after later passes and falsely rejects this valid LoadOp::Load.
    bool laterPassExecuted = false;
    graph.addPass("AfterSubpassDepthLoad")
        .writeAttachment(depthTex,
            arp::rhi::ResourceState::DepthAttachment,
            arp::rhi::LoadOp::Load,
            arp::rhi::StoreOp::Store)
        .executeRenderPass([&](RenderPassContext&) {
            laterPassExecuted = true;
        });

    graph.compile();
    graph.execute();

    assert(subpass0Executed);
    assert(subpass1Executed);
    assert(laterPassExecuted);
    // The group name should appear in the execution trace.
    bool foundGroup = false;
    for (const auto& name : graph.executionTrace()) {
        if (name == std::pmr::string("DepthPrepassAndScene")) {
            foundGroup = true;
            break;
        }
    }
    assert(foundGroup);
}

/// Subpass: verify SubpassGraph degraded path (when subpassInputAttachment
/// is not supported) expands to separate passes with cross-pass sampling.
/// Uses CapabilityOverrideDevice to force subpassInputAttachment=false so the
/// FrameGraph takes the degraded else-branch (render_graph.cpp lowerSubpassGraph
/// degraded path), which remains the fallback when the native capability is
/// unavailable on a real backend.
void test_subpass_graph_degraded_expands_to_separate_passes()
{
    using namespace arp::render_graph;

    auto inner = arp::rhi::createNullDevice();
    arp::tests::CapabilityOverrideDevice device(*inner, [](arp::rhi::DeviceCapabilities& c) {
        c.features.subpassInputAttachment = false;
        c.features.subpassInputAttachmentDegraded = true;
    });

    // The degraded path takes GpuDevice&; the wrapper is the device.
    FrameGraph graph(device);

    auto colorTex = graph.createTransientTexture({
        "DegradedColor",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    auto depthTex = graph.createTransientTexture({
        "DegradedDepth",
        {64, 64, 1},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    auto& sg = graph.addSubpassGraph("DegradedGroup");
    auto colorAtt = sg.attachment(
        colorTex, arp::rhi::ResourceState::ColorAttachment, AttachmentRole::ColorAttachment);
    auto depthAtt = sg.attachment(
        depthTex, arp::rhi::ResourceState::DepthAttachment, AttachmentRole::DepthAttachment);

    bool sp0 = false, sp1 = false;
    sg.subpass("DepthOnly")
        .writeAttachment(depthAtt, arp::rhi::LoadOp::Clear, arp::rhi::StoreOp::DontCare)
        .executeRenderPass([&](RenderPassContext&) {
            sp0 = true;
        });

    sg.subpass("ColorOnly")
        .readInputAttachment(depthAtt, 0u, 0u)
        .writeAttachment(colorAtt, arp::rhi::LoadOp::Clear, arp::rhi::StoreOp::Store)
        .executeRenderPass([&](RenderPassContext&) {
            sp1 = true;
        });

    graph.compile();
    graph.execute();

    assert(sp0);
    assert(sp1);

    // Degraded path must produce TWO independent passes (named
    // "DegradedGroup::DepthOnly" and "DegradedGroup::ColorOnly"), NOT a single
    // group-named pass (which is the native-path signature). This proves the
    // else-branch was taken.
    bool foundDepthOnly = false;
    bool foundColorOnly = false;
    bool foundGroupPass = false;
    for (const auto& name : graph.executionTrace()) {
        if (name == std::pmr::string("DegradedGroup::DepthOnly")) {
            foundDepthOnly = true;
        }
        if (name == std::pmr::string("DegradedGroup::ColorOnly")) {
            foundColorOnly = true;
        }
        if (name == std::pmr::string("DegradedGroup")) {
            foundGroupPass = true;
        }
    }
    assert(foundDepthOnly);
    assert(foundColorOnly);
    assert(!foundGroupPass); // native-path artifact must NOT appear

    // The degraded topology must retain the compiler-owned logical mapping;
    // ordinary resource reads alone are insufficient to lower a shader input
    // attachment because they lose the subpass and attachment identity.
    assert(graph.subpassInputLowerings().size() == 1u);
    assert(graph.subpassInputLowerings()[0].groupName == std::pmr::string("DegradedGroup"));
    assert(graph.subpassInputLowerings()[0].subpassIndex == 1u);
    assert(graph.subpassInputLowerings()[0].logicalAttachmentIndex == depthAtt.index);
    assert(graph.subpassInputLowerings()[0].texture.id == depthTex.id);

    // DontCare->Store promotion: subpass 0 writes depth with DontCare, but
    // subpass 1 reads depth as input attachment. On the degraded path the
    // depth must be promoted to Store so it persists to main memory for the
    // cross-pass sampled read. Verify via the attachment trace.
    bool depthStorePromoted = false;
    for (const auto& att : graph.attachmentTrace()) {
        if (att.passName == std::pmr::string("DegradedGroup::DepthOnly")
            && att.state == arp::rhi::ResourceState::DepthAttachment
            && att.storeOp == arp::rhi::StoreOp::Store) {
            depthStorePromoted = true;
        }
    }
    assert(depthStorePromoted);
}

/// Subpass: verify input-attachment read-before-write is a deterministic
/// diagnostic. A subpass that reads an input attachment not written by any
/// earlier subpass must throw during compile (AGENTS.md: fail on
/// undeterminable resource semantics).
void test_subpass_input_attachment_read_before_write_throws()
{
    using namespace arp::render_graph;

    auto device = arp::rhi::createNullDevice();
    FrameGraph graph(*device);

    auto colorTex = graph.createTransientTexture({
        "IATestColor",
        {32, 32, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    auto depthTex = graph.createTransientTexture({
        "IATestDepth",
        {32, 32, 1},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });

    auto& sg = graph.addSubpassGraph("BadTimingGroup");
    auto colorAtt = sg.attachment(
        colorTex, arp::rhi::ResourceState::ColorAttachment, AttachmentRole::ColorAttachment);
    auto depthAtt = sg.attachment(
        depthTex, arp::rhi::ResourceState::DepthAttachment, AttachmentRole::DepthAttachment);

    // Subpass 0 reads depth as input attachment BEFORE any subpass writes it.
    sg.subpass("ReadsTooEarly")
        .readInputAttachment(depthAtt, 0u, 0u)
        .writeAttachment(colorAtt, arp::rhi::LoadOp::Clear, arp::rhi::StoreOp::Store)
        .executeRenderPass([&](RenderPassContext&) {});

    sg.subpass("WritesDepthLate")
        .writeAttachment(depthAtt, arp::rhi::LoadOp::Clear, arp::rhi::StoreOp::Store)
        .executeRenderPass([&](RenderPassContext&) {});

    bool threw = false;
    try {
        graph.compile();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

/// Subpass: verify the Null backend bounds-checks nextSubpass against the
/// declared subpass count. The Null backend is NATIVE for SubpassInputAttachment,
/// so it must enforce the same contract as the Vulkan backend: advancing past
/// the last subpass must produce a diagnostic failure, not silently succeed.
void test_null_backend_next_subpass_bounds_checked()
{
    using namespace arp::rhi;

    auto device = createNullDevice();
    auto encoder = device->createCommandEncoder({"SubpassBoundsTest"});

    auto colorTex = device->createTexture({
        "SubpassBoundsColor",
        {64, 64, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::ColorAttachment,
    });
    assert(colorTex);

    RenderPassDesc desc;
    desc.colorTargetName = "SubpassBoundsColor";
    RenderPassColorAttachmentDesc colorAtt;
    colorAtt.target = colorTex->id();
    colorAtt.format = TextureFormat::Bgra8Unorm;
    desc.colorAttachments.push_back(colorAtt);

    // Declare a 2-subpass layout.
    SubpassDesc sp0;
    sp0.colorAttachmentIndices.push_back(0);
    SubpassDesc sp1;
    sp1.colorAttachmentIndices.push_back(0);
    desc.subpasses.push_back(sp0);
    desc.subpasses.push_back(sp1);

    auto pass = encoder->beginRenderPass(desc);
    assert(pass);

    // Subpass 0 -> 1: valid advance.
    const auto advance1 = pass->nextSubpass();
    assert(advance1.success);

    // Subpass 1 -> 2: exceeds subpass count (2), must fail.
    const auto advance2 = pass->nextSubpass();
    assert(!advance2.success);
    assert(advance2.diagnostics.find("exceeds subpass count") != std::pmr::string::npos);

    pass->end();
    encoder->finish();
}

/// Subpass: verify the Null backend validates subpass attachment index bounds
/// in beginRenderPass. An out-of-range color/input attachment index must be
/// rejected deterministically, not silently accepted.
void test_null_backend_rejects_out_of_range_subpass_attachment()
{
    using namespace arp::rhi;

    auto device = createNullDevice();
    auto encoder = device->createCommandEncoder({"SubpassRangeTest"});

    auto colorTex = device->createTexture({
        "SubpassRangeColor",
        {64, 64, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::ColorAttachment,
    });
    assert(colorTex);

    RenderPassDesc desc;
    desc.colorTargetName = "SubpassRangeColor";
    RenderPassColorAttachmentDesc colorAtt;
    colorAtt.target = colorTex->id();
    colorAtt.format = TextureFormat::Bgra8Unorm;
    desc.colorAttachments.push_back(colorAtt);

    // Color attachment index 5 is out of range (only 1 color attachment).
    SubpassDesc sp;
    sp.colorAttachmentIndices.push_back(5);
    desc.subpasses.push_back(sp);

    auto pass = encoder->beginRenderPass(desc);
    // Must be rejected (nullptr) with a diagnostic.
    assert(!pass);
}

} // namespace

int main()
{
    {
        using namespace arp;
        using namespace arp::render_graph;
        auto device = rhi::createNullDevice();
        FrameGraph graph(*device);
        const auto compiled = graph.compile();
        FrameGraphUniformRingBuffer ring(*device, 1u, 1024u);
        const auto first = ring.allocate(0u, 16u);
        const auto submitted = device->queue().submitCount();
        FrameGraphRhiCommandExecutor executor({1u, 16u, 16u, 16u});
        const auto rejected = executor.execute(compiled, 0u, *device, ring);
        assert(!rejected.success && !rejected.recordedThroughRhiCommandEncoder);
        assert(!rejected.commandBufferCount && !rejected.executedPassCount);
        assert(
            rejected.diagnostics == "RHI-FRAME-GRAPH-UNIFORM-RING-COMPLETION-SOURCE-UNAVAILABLE");
        assert(device->queue().submitCount() == submitted);
        assert(ring.allocationCount() == 1u);
        bool refused = false;
        try {
            ring.onFrameComplete(0u);
        } catch (const std::exception& error) {
            refused = std::string_view(error.what())
                == "RHI-FRAME-GRAPH-UNIFORM-RING-COMPLETION-UNAUTHENTICATED";
        }
        assert(refused);
        const auto second = ring.allocate(0u, 16u);
        assert(first.buffer == second.buffer && second.offsetBytes > first.offsetBytes);
    }
    test_render_graph_forwards_precise_bindless_access_declarations();
    test_whole_packed_depth_stencil_graph();
    test_segment_failure_preserves_only_accepted_resource_states();
    test_submission_tokens_bind_the_original_graph();
    test_submission_observers_follow_acceptance_not_recording();
    test_active_executor_records_graph_passes();
    test_barrier_emitted_to_encoder();
    test_null_smoke_still_passes();
    test_active_executor_executes_via_rhi_plan();
    test_inline_and_plan_path_barrier_trace_match();
    test_executed_transient_texture_resolvable();
    test_descriptor_cache_reuses();
    test_fence_gated_recycle();
    test_async_compute_prototype_split();
    test_async_compute_semaphore_params_correct();
    test_async_compute_segment_dag_supports_g_c_g();
    test_async_compute_segment_dag_five_segments();
    test_subgraph_null_smoke();
    test_subgraph_executes_with_migrated_records();
    test_subgraph_compile_idempotent();
    test_imported_read_first_replay_dependency();
    test_explicit_resource_access_scopes();
    test_imported_geometry_read_scope();
    test_same_state_writes_preserve_dependency();
    test_subgraph_pin_usage_mismatch_throws();
    test_subgraph_whole_unit_cull();
    test_subgraph_whole_unit_not_culled_with_consumer();
    test_subgraph_boundary_barriers_emitted();
    test_subgraph_alias_scope_isolated();
    test_subgraph_alias_scope_allow_cross_scope();
    test_subgraph_attachment_loadop_conflict_throws();
    test_subgraph_markers_balanced();
    test_subpass_graph_native_compile_and_execute();
    test_subpass_graph_degraded_expands_to_separate_passes();
    test_subpass_input_attachment_read_before_write_throws();
    test_null_backend_next_subpass_bounds_checked();
    test_null_backend_rejects_out_of_range_subpass_attachment();
    return 0;
}
