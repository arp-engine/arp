#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_compute_workgroup_limits.h"
#include "rhi/backends/metal/metal_native_features.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_original_pipeline.h"
#include "rhi/conformance/sampled_pipeline.h"
#include "rhi/conformance/storage_mrt_pipeline.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/semantics/original_synchronous_pipeline_creation_lowering.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <type_traits>

#ifdef NDEBUG
#error Contract tests require assertions
#endif

namespace {
using namespace arp;
using Factory = rhi_metal::MetalOriginalPipeline;
constexpr std::uint64_t metadata = 4u * 1024u * 1024u;
constexpr Factory::Budget budget{1u, true, 1024u * 1024u, metadata, 5000000000u};

void contracts()
{
    using Workgroup = rhi_metal::MetalRhiComputeWorkgroupLimits;
    const Workgroup limits{32u, 16u, 8u, 64u};
    assert(!limits.validate(8u, 8u, 1u));
    assert(!limits.validate(4u, 4u, 4u));
    assert(!limits.validate(32u, 2u, 1u));
    assert(!limits.validate(1u, 8u, 8u));
    for (unsigned i = 0u; i < 7u; ++i) {
        auto changed = limits;
        if (i == 0u)
            changed.x = 0u;
        if (i == 1u)
            changed.y = 0u;
        if (i == 2u)
            changed.z = 0u;
        if (i == 3u)
            changed.total = 0u;
        if (i == 4u)
            changed.x = 7u;
        if (i == 5u)
            changed.y = 7u;
        if (i == 6u)
            changed.total = 63u;
        const auto* id = changed.validate(8u, 8u, 1u);
        assert(id && !std::strcmp(id, "RHI-METAL-COMPUTE-WORKGROUP-LIMIT"));
    }
    assert(
        limits.validate(0u, 8u, 1u) && limits.validate(8u, 0u, 1u) && limits.validate(8u, 8u, 0u));
    assert(limits.validate(33u, 1u, 1u) && limits.validate(1u, 17u, 1u)
        && limits.validate(1u, 1u, 9u));
    assert(limits.validate(8u, 8u, 2u));
    const Workgroup huge{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT64_MAX};
    assert(!huge.validate(UINT32_MAX, UINT32_MAX, 1u));
    assert(huge.validate(UINT32_MAX, UINT32_MAX, UINT32_MAX));
    assert(Workgroup::deviceInvocationEnvelope(1024u, 1024u, 64u) == 1024u);
    assert(Workgroup::deviceInvocationEnvelope(32u, 64u, 128u) == 128u);
    assert(Workgroup::deviceInvocationEnvelope(128u, 32u, 64u) == 128u);
    assert(!Workgroup::deviceInvocationEnvelope(0u, 1024u, 64u));
    assert(!Workgroup::deviceInvocationEnvelope(1024u, 0u, 64u));
    assert(!Workgroup::deviceInvocationEnvelope(1024u, 64u, 0u));
    assert(!Workgroup::deviceInvocationEnvelope(UINT64_MAX, 64u, 64u));
    assert(!Workgroup::deviceInvocationEnvelope(64u, UINT64_MAX, 64u));
    assert(!Workgroup::deviceInvocationEnvelope(64u, 64u, UINT64_MAX));
    using Contract = rhi::OriginalSynchronousPipelineCreationContract;
    using Lowering = rhi::semantics::OriginalSynchronousPipelineCreationLowering;
    assert(!Contract::validateBudget(budget));
    rhi_metal::MetalRhiNativePipelinePayload dummy;
    assert(std::strcmp(Factory::diagnostic(dummy), "RHI-ORIGINAL-SYNC-PIPELINE-CUSTODY") == 0);
    for (unsigned i = 0; i < 6; ++i) {
        auto invalid = budget;
        if (i == 0)
            invalid.contractVersion = 2;
        if (i == 1)
            invalid.prototypeOptIn = false;
        if (i == 2)
            invalid.maxTargetBytes = 0;
        if (i == 3)
            invalid.maxCpuMetadataBytes = 0;
        if (i == 4)
            invalid.coldCreationTimeoutNanoseconds = 0;
        if (i == 5)
            invalid.coldCreationTimeoutNanoseconds = UINT64_MAX;
        assert(Contract::validateBudget(invalid));
    }
    for (auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255)}) {
        const auto decision = Lowering::decide(backend, {true, true, true});
        assert(!decision.executionAvailable);
        assert(decision.creationEligible == (backend == rhi::Backend::Metal));
        assert(decision.validatorOnly == (backend == rhi::Backend::Null));
        assert(bool(decision.diagnostic) == !decision.creationEligible);
    }
    for (auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        const auto caps = rhi::semantics::nativeBackendCapabilityContract(backend);
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadata);
        {
            core::CpuMemoryQuotaScope scope(quota);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto plan = rhi::semantics::buildPushConstantLoweringPlan(backend, caps);
            std::pmr::set_default_resource(previous);
            assert(
                plan.diagnosticId.get_allocator().resource() == &core::defaultCpuMemoryResource());
            assert(plan.telemetryLabel.get_allocator().resource()
                == &core::defaultCpuMemoryResource());
        }
        assert(!quota->liveBytes());
    }
    for (unsigned i = 0; i < 3; ++i) {
        Lowering::Facts facts{true, true, true};
        if (i == 0)
            facts.device = false;
        if (i == 1)
            facts.queriedCapabilities = false;
        if (i == 2)
            facts.immutableTarget = false;
        const auto decision = Lowering::decide(rhi::Backend::Metal, facts);
        assert(!decision.creationEligible && !decision.executionAvailable && decision.diagnostic);
    }
}

template <class Desc, class Input>
void verify(Desc state,
    const std::shared_ptr<const Input>& input,
    const rhi::semantics::NativeBackendCapabilityContract& caps,
    bool native)
{
    const auto report = Factory::validate(state, input, caps);
    if (!report.success)
        std::fprintf(stderr, "%s\n", report.diagnostics.c_str());
    assert(report.success && report.shaderPackageValidated && report.reflectionMatched);
    for (unsigned i = 0; i < 5; ++i) {
        auto changed = state;
        auto package = rhi::ShaderPackageStateCopy::copy(input->package());
        changed.shaderPackage = &package;
        if (i == 0)
            package.reflection.sourceSha256 = "forged";
        if (i == 1)
            package.artifacts.mslPath = "caller.metal";
        if (i == 2)
            package.reflection.pushConstantMetalBufferIndex = 30u;
        if (i == 3)
            package.entrypoints.front().entryPoint = "wrongEntry";
        if (i == 4)
            changed.layout = nullptr;
        const auto rejected = Factory::validate(changed, input, caps);
        assert(!rejected.success);
    }
    {
        auto changed = state;
        if constexpr (std::is_same_v<Desc, rhi::RenderPipelineDesc>)
            changed.vertexShader.entryPoint = "wrongVertex";
        else
            changed.computeShader.entryPoint = "wrongCompute";
        assert(!Factory::validate(changed, input, caps).success);
        auto differentBackend = caps;
        differentBackend.backend = rhi::Backend::Vulkan;
        assert(!Factory::validate(state, input, differentBackend).success);
        const auto inputBytes = input->quota()->limitBytes();
        input->quota()->setLimitBytes(inputBytes + 1);
        assert(!Factory::validate(state, input, caps).success);
        input->quota()->setLimitBytes(inputBytes);
        const auto artifactBytes = input->artifact()->quota()->limitBytes();
        input->artifact()->quota()->setLimitBytes(artifactBytes + 1);
        assert(!Factory::validate(state, input, caps).success);
        input->artifact()->quota()->setLimitBytes(artifactBytes);
        assert(Factory::validate(state, input, caps).success);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto hostile = Factory::validate(state, input, caps);
        std::pmr::set_default_resource(previous);
        assert(hostile.success);
    }
    auto ownerQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const char* id = nullptr;
    for (unsigned i = 0; i < 5; ++i) {
        auto invalid = budget;
        if (i == 0)
            invalid.contractVersion = 2;
        if (i == 1)
            invalid.prototypeOptIn = false;
        if (i == 2)
            invalid.maxCpuMetadataBytes = 0;
        if (i == 3)
            invalid.maxTargetBytes = 0;
        if (i == 4)
            invalid.maxTargetBytes = 1;
        auto rejected = Factory::create(state, input, caps, invalid, ownerQuota, id);
        assert(!rejected && id && ownerQuota->liveBytes() == 0);
    }
    assert(!Factory::create(state, input, caps, budget, {}, id) && id);
    assert(!Factory::create(state, std::shared_ptr<const Input>{}, caps, budget, ownerQuota, id)
        && id);
    auto invalidCaps = caps;
    invalidCaps.queriedFromNativeDevice = false;
    assert(!Factory::create(state, input, invalidCaps, budget, ownerQuota, id));
    assert(std::strcmp(id, "RHI-ORIGINAL-SYNC-PIPELINE-CAPABILITY-QUERY") == 0);
    auto aliasedBudget = budget;
    aliasedBudget.maxCpuMetadataBytes = input->quota()->limitBytes();
    assert(!Factory::create(state, input, caps, aliasedBudget, input->quota(), id));
    assert(std::strcmp(id, "RHI-ORIGINAL-SYNC-PIPELINE-QUOTA-DOMAIN") == 0);
    aliasedBudget.maxCpuMetadataBytes = input->artifact()->quota()->limitBytes();
    assert(!Factory::create(state, input, caps, aliasedBudget, input->artifact()->quota(), id));
    assert(std::strcmp(id, "RHI-ORIGINAL-SYNC-PIPELINE-QUOTA-DOMAIN") == 0);
    {
        // Every nested runtime allocation must use core even under hostile STL PMR.
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto pipeline = Factory::create(state, input, caps, budget, ownerQuota, id);
        std::pmr::set_default_resource(previous);
        if (native) {
            if (!pipeline)
                std::fprintf(stderr, "%s\n", id);
            assert(pipeline && !id);
            assert(Factory::input(*pipeline) == input);
            const auto nativePayload = rhi_metal::metalRhiNativePayloadFor(*pipeline);
            assert(nativePayload && nativePayload->nativeRuntimeBacked
                && !nativePayload->nativeRuntimeDiagnosticOnly);
            assert(pipeline->layout() != state.layout);
            assert(rhi::PipelineDefinitionIdentity::matches(
                pipeline->layout()->desc(), state.layout->desc()));
            assert(!pipeline->bindlessAuthorization());
            assert(ownerQuota->liveBytes());
            assert(!Factory::diagnostic(*nativePayload));
            const auto inputLimit = input->quota()->limitBytes();
            input->quota()->setLimitBytes(inputLimit + 1);
            assert(Factory::diagnostic(*nativePayload));
            input->quota()->setLimitBytes(inputLimit);
            ownerQuota->setLimitBytes(metadata + 1);
            assert(
                std::strcmp(Factory::diagnostic(*nativePayload), "RHI-ORIGINAL-SYNC-PIPELINE-QUOTA")
                == 0);
            ownerQuota->setLimitBytes(metadata);
            pipeline.reset();
            assert(!Factory::diagnostic(*nativePayload));
            assert(ownerQuota->liveBytes());
        } else {
            assert(!pipeline && id);
            assert(std::strcmp(id, "RHI-ORIGINAL-SYNC-PIPELINE-NATIVE-DEVICE") == 0);
        }
    }
    assert(ownerQuota->liveBytes() == 0);
    if (native) {
        core::CpuMemoryQuotaLease pressure(ownerQuota, metadata);
        assert(!Factory::create(state, input, caps, budget, ownerQuota, id));
        assert(std::strcmp(id, "RHI-ORIGINAL-SYNC-PIPELINE-CPU-ALLOCATION") == 0);
    }
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    if (argc == 2 && std::strcmp(argv[1], "--workgroup-limits") == 0) {
        contracts();
        std::puts("METAL_COMPUTE_WORKGROUP_LIMITS_CONTRACT_PASS");
        return 0;
    }
    assert(argc == 5);
    const bool deleted = std::strcmp(argv[1], "--deleted") == 0;
    const bool native = std::strcmp(argv[1], "--native") == 0;
    assert(deleted || native || std::strcmp(argv[1], "--contract") == 0);
    contracts();
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    constexpr shader::GraphicsShaderModuleLoadBudget load{
        4u * 1024u * 1024u, 32u * 1024u * 1024u, 32u * 1024u * 1024u, 32, 32, 4096, 32};
    constexpr shader::SingleEntryShaderLoadBudget computeLoad{
        4u * 1024u * 1024u, 32u * 1024u * 1024u, metadata, 32};
    std::array<std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>, 2> graphics;
    for (std::size_t i = 0; i < graphics.size(); ++i) {
        const auto artifact = shader::GraphicsShaderModuleArtifact::load(argv[2 + i],
            load,
            core::makeShared<core::CpuMemoryQuota>(load.maxCpuMetadataBytes),
            diagnostic);
        if (!artifact)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(artifact);
        graphics[i] = rhi::OriginalGraphicsPipelineInput::prepare(
            artifact, {1u, metadata}, core::makeShared<core::CpuMemoryQuota>(metadata), diagnostic);
        assert(graphics[i]);
    }
    const auto computeArtifact = shader::SingleEntryShaderArtifact::load(
        argv[4], computeLoad, core::makeShared<core::CpuMemoryQuota>(metadata), diagnostic);
    assert(computeArtifact);
    const auto compute = rhi::OriginalSingleEntryPipelineInput::prepare(computeArtifact,
        {1u, metadata},
        core::makeShared<core::CpuMemoryQuota>(metadata),
        diagnostic);
    assert(compute);
    if (deleted) {
        std::puts("METAL_ORIGINAL_INPUTS_RETAINED");
        std::fflush(stdout);
        assert(std::cin.get() == '\n');
        for (int i = 2; i < 5; ++i)
            assert(!std::filesystem::exists(argv[i]));
    }
    auto caps = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    // Host test facts validate descriptors only. Native creation additionally
    // requires the actual native identity; fabricated booleans cannot mint it.
    const bool deviceAvailable = bool(rhi_metal::metalRhiNativeDeviceIdentity());
    caps.queriedFromNativeDevice = native ? deviceAvailable : true;
    if (native)
        assert(deviceAvailable);
    for (std::size_t i = 0; i < graphics.size(); ++i) {
        const auto& package = graphics[i]->package();
        auto layout = rhi_metal::createMetalRhiPipelineLayout(i
                ? rhi::ConformanceStorageMrtPipeline::layoutDefinition(package)
                : rhi::buildPipelineLayoutDescFromShaderReflection(package.reflection));
        assert(layout);
        for (auto format : {rhi::TextureFormat::Rgba8Unorm, rhi::TextureFormat::Bgra8Unorm}) {
            auto state = i
                ? rhi::ConformanceStorageMrtPipeline::pipelineDefinition(*layout, package, format)
                : rhi::ConformanceSampledPipeline::definition(*layout, package, format);
            verify(std::move(state), graphics[i], caps, native);
        }
    }
    auto computeLayout = rhi_metal::createMetalRhiPipelineLayout(
        rhi::ConformanceStorageMrtPipeline::layoutDefinition(compute->package()));
    verify(
        rhi::ConformanceStorageMrtPipeline::computeDefinition(*computeLayout, compute->package()),
        compute,
        caps,
        native);
    std::printf(
        "METAL_ORIGINAL_PIPELINE_OK graphics=4 compute=1 deleted=%d native=%d sharedOutput=0\n",
        deleted,
        native);
}
