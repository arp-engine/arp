#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "rhi/conformance/sampled_command_contract.h"
#include "rhi/conformance/sampled_execution.h"
#include "rhi/conformance/sampled_workload.h"
#include "rhi/frontend/command_encoder_base.h"

#include <cassert>
#include <cstring>

int main()
{
    using namespace arp;
    rhi::ConformanceSampledExecutionRequest request{
        {{1, 3, true, 49152, 4194304}, 1048576, 5000000000}, 1048576};
    assert(!rhi::ConformanceSampledExecutionContract::validate(request));
    auto invalidRequest = request;
    invalidRequest.maxNativeInputBufferBytes = 0;
    assert(rhi::ConformanceSampledExecutionContract::validate(invalidRequest));
    invalidRequest = request;
    invalidRequest.execution.observation.prototypeOptIn = false;
    assert(rhi::ConformanceSampledExecutionContract::validate(invalidRequest));
    for (const auto backend : {rhi::Backend::Null, rhi::Backend::Metal, rhi::Backend::WebGL})
        assert(rhi::ConformanceSampledExecutionContract::loweringDiagnostic(backend));
    assert(!rhi::ConformanceSampledExecutionContract::loweringDiagnostic(rhi::Backend::WebGPU));
    constexpr rhi::ConformanceWorkloadBudget budget{16384, 4096, 3};
    const auto source = core::readTextFile(
        ARP_SOURCE_DIR "/tests/fixtures/rhi_conformance_sampled.yaml", budget.maxYamlBytes);
    assert(source.success);
    const char* id = nullptr;
    auto workload = rhi::ConformanceSampledWorkload::parse(source.text, budget, id);
    assert(workload && !id && workload->frames().size() == 3);
    assert(workload->sourceSha256() == core::Sha256::hexDigest(source.text));
    assert(!workload->frames()[0].indexed && workload->frames()[1].indexed);
    assert(workload->frames()[1].uniforms[16] == .5f && workload->frames()[2].uniforms[1] == .125f);
    assert(workload->vertices()[0] == -4 && workload->indices()[2] == 2);
    {
        using Command = rhi::ConformanceSampledCommandContract;
        assert(request.artifacts.diagnostic());
        request.artifacts = {1, 65536, 65536, 65536, 65536, 65536, 65536, 512, 5000000000};
        assert(!request.artifacts.diagnostic());
        auto artifactBudget = request.artifacts;
        artifactBudget.pipelineCreationTimeoutNanoseconds = 0;
        assert(artifactBudget.diagnostic());
        artifactBudget.pipelineCreationTimeoutNanoseconds = UINT64_MAX;
        assert(artifactBudget.diagnostic());
        const auto& frame = workload->frames()[0];
        const auto& draw = frame.draws[0];
        assert(Command::viewport({0, 0, 64, 64, 0, 1}, frame.extent));
        assert(!Command::viewport({0, 0, 63, 64, 0, 1}, frame.extent));
        assert(Command::scissor({0, 0, 64, 64}, frame.extent));
        assert(!Command::scissor({1, 0, 64, 64}, frame.extent));
        assert(Command::push(
            draw, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0, 32, draw.push.data()));
        assert(!Command::push(draw, rhi::ShaderStage::Fragment, 0, 32, draw.push.data()));
        assert(Command::draw(draw, false, 3, 1, 0, 0, 0, 63));
        assert(!Command::draw(draw, false, 3, 1, 0, 0, 0, 31));
        assert(!Command::draw(draw, true, 3, 1, 0, 0, 0, 127));
        rhi::TextureSubresourceRange range;
        range.aspect = rhi::TextureAspect::Color;
        auto barrier = rhi::makeInferredResourceBarrier(rhi::allocateGpuResourceId(),
            range,
            rhi::ResourceState::ColorAttachment,
            rhi::ResourceState::ShaderRead);
        assert(Command::imageScopes(barrier));
        barrier.destinationStages = rhi::PipelineStage::ComputeShader;
        assert(!Command::imageScopes(barrier));
        barrier.destinationStages = rhi::PipelineStage::FragmentShader;
        barrier.sourceAccess = rhi::ResourceAccess::ColorAttachmentRead;
        assert(!Command::imageScopes(barrier));
    }
    for (int mode = 0; mode < 4; ++mode) {
        auto limited = budget;
        if (mode == 0)
            limited.maxYamlBytes = source.text.size() - 1;
        if (mode == 1)
            limited.maxPixelsPerFrame = 4095;
        if (mode == 2)
            limited.maxFrames = 2;
        if (mode == 3)
            limited.maxFrames = 0;
        assert(!rhi::ConformanceSampledWorkload::parse(source.text, limited, id));
        assert(std::strcmp(id, "RHI-CONFORMANCE-WORKLOAD-BUDGET") == 0);
    }
    const auto rejected = [&](core::StringView old, core::StringView replacement) {
        auto input = source.text;
        const auto offset = input.find(old);
        assert(offset != core::pmr::String::npos);
        input.replace(offset, old.size(), replacement);
        assert(!rhi::ConformanceSampledWorkload::parse(input, budget, id) && id);
    };
    rejected("contractVersion: 1", "contractVersion: 3");
    rejected("indices: [0, 1, 2]", "indices: [0, 1, 3]");
    rejected("indices: [0, 1, 2]", "indices: [0, 0, 2]");
    rejected("indices: [0, 1, 2]", "indices: [0, 1, 2.0]");
    rejected("indices: [0, 1, 2]", "indices: [0, 1]");
    rejected("indexed: false", "indexed: 0");
    rejected("width: 64", "width: 0");
    rejected("height: 64", "height: 4294967296");
    rejected("[32, 128, 192, 255]", "[32, 128, 192, 0]");
    rejected("[32, 128, 192, 255]", "[32, 128, 192, 256]");
    rejected("vertices: [-4", "vertices: [.inf");
    rejected("vertices: [-4", "vertices: [1.0e40");
    rejected("vertices: [-4", "vertices: [1.0e-300");
    rejected("vertices: [-4", "vertices: [true");
    rejected("7f32928", "7G32928");
    rejected("sourceA: [32, 128, 192, 255]", "extra: true\n    sourceA: [32, 128, 192, 255]");
    assert(!rhi::ConformanceSampledWorkload::parse(
        source.text + "\n---\nignored: true\n", budget, id));
    // Changing the oracle changes intent identity, never vertices/uniforms/push/source clears.
    auto oracle = source.text;
    oracle.replace(oracle.find("expected: [48"), std::strlen("expected: [48"), "expected: [49");
    const auto changed = rhi::ConformanceSampledWorkload::parse(oracle, budget, id);
    assert(changed && changed->sourceSha256() != workload->sourceSha256());
    assert(changed->frames()[1].uniforms == workload->frames()[1].uniforms);
    assert(changed->frames()[1].push == workload->frames()[1].push);
    assert(changed->frames()[1].sourceA == workload->frames()[1].sourceA);
    assert(changed->frames()[1].sourceB == workload->frames()[1].sourceB);
    assert(changed->vertices() == workload->vertices());
    const auto ping = core::readTextFile(
        ARP_SOURCE_DIR "/tests/fixtures/rhi_conformance_pingpong.yaml", budget.maxYamlBytes);
    assert(ping.success);
    const rhi::ConformanceSampledWorkloadBudget pingBudget{16384, 4096, 3, 4};
    const auto chain = rhi::ConformanceSampledWorkload::parse(ping.text, pingBudget, id);
    assert(chain && !id && chain->frames().size() == 3);
    using R = rhi::ConformanceSampledResource;
    for (const auto& frame : chain->frames()) {
        assert(frame.draws.size() == 4 && frame.draws[0].target == R::PingA);
        assert(frame.draws[1].sourceA == R::PingA && frame.draws[1].target == R::PingB);
        assert(frame.draws[2].sourceA == R::PingB && frame.draws[2].target == R::PingA);
        assert(frame.draws[3].sourceA == R::PingA && frame.draws[3].target == R::Output);
        assert(frame.expected == frame.draws.back().expected);
    }
    const auto pingRejected =
        [&](core::StringView old, core::StringView replacement, const char* diagnostic) {
            auto input = ping.text;
            const auto offset = input.find(old);
            assert(offset != core::pmr::String::npos);
            input.replace(offset, old.size(), replacement);
            assert(!rhi::ConformanceSampledWorkload::parse(input, pingBudget, id));
            assert(id && std::strcmp(id, diagnostic) == 0);
        };
    constexpr auto graphError = "RHI-CONFORMANCE-WORKLOAD-RESOURCE-GRAPH";
    pingRejected("sourceA: SourceA", "sourceA: PingA", graphError);
    pingRejected("sourceA: SourceA", "sourceA: PingB", graphError);
    pingRejected("sourceA: SourceA", "sourceA: Output", graphError);
    pingRejected("sourceA: SourceA", "sourceA: Unknown", graphError);
    pingRejected("sourceA: SourceA", "sourceA: SourceB", graphError);
    pingRejected("target: PingA", "target: SourceA", graphError);
    pingRejected("target: PingA", "target: Output", graphError);
    pingRejected("target: Output", "target: PingB", graphError);
    pingRejected("target: PingB", "target: PingA", graphError);
    pingRejected("target: PingA", "target: Missing", graphError);
    pingRejected("indexed: false", "indexed: 0", "RHI-CONFORMANCE-WORKLOAD-SCHEMA");
    pingRejected(
        "target: PingA", "extra: true\n    target: PingA", "RHI-CONFORMANCE-WORKLOAD-SCHEMA");
    auto limited = pingBudget;
    limited.maxDrawsPerFrame = 3;
    assert(!rhi::ConformanceSampledWorkload::parse(ping.text, limited, id));
    assert(std::strcmp(id, "RHI-CONFORMANCE-WORKLOAD-BUDGET") == 0);
    assert(!rhi::ConformanceSampledWorkload::parse(ping.text, budget, id));
    limited.maxDrawsPerFrame = 0;
    assert(!rhi::ConformanceSampledWorkload::parse(ping.text, limited, id));
    auto changedOracle = ping.text;
    const auto expectedOffset = changedOracle.find("expected: [32, 128, 192, 255]");
    assert(expectedOffset != core::pmr::String::npos);
    changedOracle.replace(expectedOffset, std::strlen("expected: [32"), "expected: [33");
    const auto independent = rhi::ConformanceSampledWorkload::parse(changedOracle, pingBudget, id);
    assert(independent && independent->sourceSha256() != chain->sourceSha256());
    for (std::size_t f = 0; f < chain->frames().size(); ++f) {
        assert(independent->frames()[f].uniforms == chain->frames()[f].uniforms);
        for (std::size_t d = 0; d < 4; ++d) {
            const auto& a = independent->frames()[f].draws[d];
            const auto& b = chain->frames()[f].draws[d];
            assert(a.sourceA == b.sourceA && a.sourceB == b.sourceB && a.target == b.target);
            assert(a.push == b.push && a.indexed == b.indexed);
        }
    }
}
