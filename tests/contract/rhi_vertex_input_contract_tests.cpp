#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/gpu_pipeline.h"
#include "shader/single_entry_shader_artifact.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error Vertex contract tests require assertions
#endif

namespace {
using namespace arp;
using namespace rhi;

auto layouts()
{
    core::pmr::Vector<VertexBufferLayoutDesc> result{&core::defaultCpuMemoryResource()};
    result.push_back({3u,
        12u,
        VertexInputRate::Vertex,
        {{0u, 0u, VertexFormat::Float32x2}, {1u, 8u, VertexFormat::Float32x1}}});
    result.push_back({8u, 32u, VertexInputRate::Instance, {{2u, 12u, VertexFormat::Float32x4}}});
    return result;
}

void expect(const char* actual, const char* expected)
{
    assert(actual && std::strcmp(actual, expected) == 0);
}

void runLayoutContract()
{
    auto source = layouts();
    auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
    std::uint64_t charge = 0;
    {
        core::CpuMemoryQuotaScope scope(quota);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        VertexInputConsumptionContract input(source);
        std::pmr::set_default_resource(previous);
        assert(!input.diagnostic() && input.matches(source));
        assert(input.findStream(3u)->fetchBytes == 12u && input.findStream(8u)->fetchBytes == 28u);
        assert(core::inspectCpuAllocationCharge(input.streams().data()).quota == quota.get());
        assert(core::inspectCpuAllocationCharge(input.attributes().data()).quota == quota.get());
        charge = quota->liveBytes();
        assert(charge);
        source[0].strideBytes++;
        assert(!input.matches(source));
        source[0].strideBytes--;
        source[0].attributes[0].format = VertexFormat::Float32x4;
        assert(!input.matches(source));
        source.clear();
        source.shrink_to_fit();
        assert(input.findAttribute(0u)->layout.format == VertexFormat::Float32x2);
        assert(!input.validateRange(3u, 116u, 8u, 5u, 4u, 3u, 2u));
        expect(input.validateRange(3u, 115u, 8u, 5u, 4u, 3u, 2u), "RHI-VERTEX-INPUT-FETCH-RANGE");
        assert(!input.validateRange(8u, 160u, 4u, 5u, 4u, 3u, 2u));
        expect(input.validateRange(8u, 159u, 4u, 5u, 4u, 3u, 2u), "RHI-VERTEX-INPUT-FETCH-RANGE");
        auto borrowed = *input.findStream(8u);
        assert(!VertexInputConsumptionContract::validateStreamRange(
            borrowed, 160u, 4u, 5u, 4u, 3u, 2u));
        expect(
            VertexInputConsumptionContract::validateStreamRange(borrowed, 159u, 4u, 5u, 4u, 3u, 2u),
            "RHI-VERTEX-INPUT-FETCH-RANGE");
        expect(VertexInputConsumptionContract::validateStreamRange(
                   borrowed, UINT64_MAX, 4u, 1u, 0u, 2u, UINT64_MAX),
            "RHI-VERTEX-INPUT-FETCH-RANGE");
        borrowed.strideBytes = 0u;
        expect(
            VertexInputConsumptionContract::validateStreamRange(borrowed, 160u, 4u, 5u, 4u, 3u, 2u),
            "RHI-VERTEX-INPUT-LAYOUT-INVALID");
        expect(
            input.validateRange(9u, 160u, 0u, 1u, 0u, 1u, 0u), "RHI-VERTEX-INPUT-SLOT-UNDECLARED");
        expect(input.validateRange(3u, 12u, 1u, 1u, 0u, 1u, 0u), "RHI-VERTEX-INPUT-BUFFER-RANGE");
        expect(input.validateRange(3u, 12u, 0u, 0u, 0u, 1u, 0u), "RHI-VERTEX-INPUT-DRAW-EMPTY");
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        const auto capacity = (maximum - 12u) / 12u + 1u;
        assert(!input.validateRange(3u, maximum, 0u, 1u, capacity - 1u, 1u, 0u));
        expect(input.validateRange(3u, maximum, 0u, 2u, capacity - 1u, 1u, 0u),
            "RHI-VERTEX-INPUT-FETCH-RANGE");
        expect(input.validateRange(3u, maximum, 0u, 2u, maximum, 1u, 0u),
            "RHI-VERTEX-INPUT-FETCH-RANGE");
        expect(input.validateRange(3u, maximum, maximum - 10u, 1u, 0u, 1u, 0u),
            "RHI-VERTEX-INPUT-BUFFER-RANGE");
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0; i < 1000u; ++i) {
            assert(!input.validateRange(3u, 116u, 8u, 5u, 4u, 3u, 2u));
            assert(input.validateRange(8u, 159u, 4u, 5u, 4u, 3u, 2u));
        }
        std::pmr::set_default_resource(previous);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    assert(!quota->liveBytes());
    source = layouts();
    auto smaller = core::makeShared<core::CpuMemoryQuota>(charge - 1u);
    bool rejected = false;
    try {
        core::CpuMemoryQuotaScope scope(smaller);
        VertexInputConsumptionContract input(source);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    assert(rejected && !smaller->liveBytes());
    const auto invalid = [&](const char* diagnostic) {
        VertexInputConsumptionContract input(source);
        expect(input.diagnostic(), diagnostic);
    };
    source[0].strideBytes = 0u;
    invalid("RHI-VERTEX-INPUT-LAYOUT-INVALID");
    source = layouts();
    source[0].attributes.clear();
    invalid("RHI-VERTEX-INPUT-LAYOUT-INVALID");
    source = layouts();
    source[1].slot = 3u;
    invalid("RHI-VERTEX-INPUT-SLOT-DUPLICATE");
    source = layouts();
    source[1].inputRate = static_cast<VertexInputRate>(255);
    invalid("RHI-VERTEX-INPUT-RATE-UNKNOWN");
    source = layouts();
    source[1].attributes[0].shaderLocation = 0u;
    invalid("RHI-VERTEX-INPUT-LOCATION-DUPLICATE");
    source = layouts();
    source[0].attributes[0].format = static_cast<VertexFormat>(255);
    invalid("RHI-VERTEX-INPUT-FORMAT-UNKNOWN");
    source = layouts();
    source[1].strideBytes = 8u;
    VertexInputConsumptionContract overlap(source);
    assert(!overlap.diagnostic() && !overlap.validateRange(8u, 36u, 0u, 1u, 0u, 2u, 0u));
    source.clear();
    VertexInputConsumptionContract empty(source);
    assert(!empty.diagnostic() && empty.matches(source));
}

void runStateCopy()
{
    RenderPipelineDesc source;
    source.label = "RetainedRenderPipelineStateWithNestedAllocatorOwners";
    source.vertexBuffers = layouts();
    source.colorTargets.resize(2u);
    source.vertexShader.modulePath = "LongSelectedVertexArtifactPathForAllocatorContract";
    source.renderPassCompatibility.colorAttachmentFormats = {TextureFormat::Rgba8Unorm};
    source.renderPassCompatibility.subpasses.resize(2u);
    for (auto& subpass : source.renderPassCompatibility.subpasses) {
        subpass.colorAttachmentIndices = {0u};
        subpass.inputAttachmentIndices = {1u};
    }
    source.renderPassCompatibility.subpassDependencies = {{0u, 1u}};
    auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
    {
        core::CpuMemoryQuotaScope scope(quota);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto copy = RenderPipelineStateCopy::copy(source);
        std::pmr::set_default_resource(previous);
        const auto charged = [&](const auto* storage) {
            assert(core::inspectCpuAllocationCharge(storage).quota == quota.get());
        };
        charged(copy.label.data());
        charged(copy.vertexShader.modulePath.data());
        charged(copy.colorTargets.data());
        charged(copy.vertexBuffers.data());
        for (const auto& stream : copy.vertexBuffers)
            charged(stream.attributes.data());
        charged(copy.renderPassCompatibility.subpasses.data());
        for (const auto& subpass : copy.renderPassCompatibility.subpasses) {
            charged(subpass.colorAttachmentIndices.data());
            charged(subpass.inputAttachmentIndices.data());
        }
        charged(copy.renderPassCompatibility.colorAttachmentFormats.data());
        charged(copy.renderPassCompatibility.subpassDependencies.data());
        source.vertexBuffers[0].attributes[0].offsetBytes = 4u;
        source.renderPassCompatibility.subpasses[0].colorAttachmentIndices[0] = 2u;
        assert(copy.vertexBuffers[0].attributes[0].offsetBytes == 0u
            && copy.renderPassCompatibility.subpasses[0].colorAttachmentIndices[0] == 0u);
    }
    assert(!quota->liveBytes());
}

void runArtifact(const char* publication)
{
    auto quota = core::makeShared<core::CpuMemoryQuota>(1024u * 1024u);
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    auto artifact = shader::SingleEntryShaderArtifact::load(
        publication, {1024u * 1024u, 4u * 1024u * 1024u, 1024u * 1024u, 16u}, quota, diagnostic);
    if (!artifact)
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
    assert(artifact);
    auto source = layouts();
    source[0].strideBytes = 8u;
    source[0].attributes.resize(1u);
    source[1].strideBytes = 24u;
    source[1].attributes = {{1u, 0u, VertexFormat::Float32x2}, {2u, 8u, VertexFormat::Float32x4}};
    VertexInputConsumptionContract exact(source);
    assert(validateSingleEntryVertexInput(exact, *artifact).success);
    source[1].attributes[1].format = VertexFormat::Float32x3;
    VertexInputConsumptionContract wrongType(source);
    assert(validateSingleEntryVertexInput(wrongType, *artifact).diagnostics
        == "RHI-VERTEX-INPUT-ARTIFACT-FORMAT-MISMATCH");
    source[1].attributes[1].shaderLocation = 7u;
    VertexInputConsumptionContract wrongLocation(source);
    assert(validateSingleEntryVertexInput(wrongLocation, *artifact).diagnostics
        == "RHI-VERTEX-INPUT-ARTIFACT-LOCATION");
    source.pop_back();
    VertexInputConsumptionContract missing(source);
    assert(validateSingleEntryVertexInput(missing, *artifact).diagnostics
        == "RHI-VERTEX-INPUT-ARTIFACT-COUNT");
}
} // namespace

int main(int argc, char** argv)
{
    runLayoutContract();
    runStateCopy();
    {
        using namespace arp::rhi;
        auto source = layouts();
        VertexInputConsumptionContract input(source);
        InstanceAttributeFetchIntent intent{1u, true};
        assert(!InstanceAttributeFetchContract::validate(intent, input));
        intent.optIn = false;
        assert(InstanceAttributeFetchContract::validate(intent, input));
        intent.optIn = true;
        intent.contractVersion = 0u;
        assert(InstanceAttributeFetchContract::validate(intent, input));
        intent.contractVersion = 1u;
        intent.stepRate = 2u;
        assert(InstanceAttributeFetchContract::validate(intent, input));
        intent.stepRate = 1u;
        intent.inputAbi = static_cast<InstanceAttributeFetchIntent::InputAbi>(255u);
        assert(InstanceAttributeFetchContract::validate(intent, input));
        for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto plan = semantics::InstanceAttributeFetchLowering::current(backend);
            assert(plan.contractVersion == 1u && !plan.executable
                && plan.validatorOnly == (backend == Backend::Null));
            assert(plan.mode
                == (plan.validatorOnly ? CapabilityMode::Equivalent
                                       : CapabilityMode::DiagnosticFailure));
        }
        const auto unknown =
            semantics::InstanceAttributeFetchLowering::current(static_cast<Backend>(255u));
        assert(!unknown.executable && !unknown.validatorOnly);
    }
    if (argc == 2)
        runArtifact(argv[1]);
    std::puts(
        "VertexInputConsumption: owned exact layout, finite Vertex/Instance fetch, quota and allocation0 PASS");
}
