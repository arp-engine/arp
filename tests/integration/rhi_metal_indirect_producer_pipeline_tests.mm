#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/metal_indirect_producer_pipeline_fixture.h"
#include "../fixtures/metal_native_identity_fixture.h"
#include "rhi/backends/metal/metal_compute_workgroup_limits.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"
#include "rhi/backends/metal/metal_native_features.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/backends/metal/metal_native_runtime.h"

#import <Metal/Metal.h>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error Native producer pipeline tests require assertions
#endif
namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_metal;
static_assert(
    MetalRhiWritableUint32BufferAbiValidator::readWriteAccess == MTLBindingAccessReadWrite);
static_assert(MetalRhiWritableUint32BufferAbiValidator::uintElementType == MTLDataTypeUInt);
using Fixture = test::MetalIndirectProducerPipelineFixture;

void workgroupLimits(const MetalRhiIndirectProducerPipeline& root)
{
    const auto native = root.native();
    const auto pipeline =
        reinterpret_cast<id<MTLComputePipelineState>>(native->pipelineState.get());
    const auto device = pipeline.device;
    const auto axes = device.maxThreadsPerThreadgroup;
    const auto total = pipeline.maxTotalThreadsPerThreadgroup;
    assert(total && axes.width && axes.height && axes.depth);
    const MetalRhiComputeWorkgroupLimits limits{axes.width, axes.height, axes.depth, total};
    assert(!limits.validate(native->computeWorkgroupSizeX,
        native->computeWorkgroupSizeY,
        native->computeWorkgroupSizeZ));
    const auto probe = probeMetalRhiNativeFeatures();
    assert(probe.maxComputeWorkgroupInvocations
        == MetalRhiComputeWorkgroupLimits::deviceInvocationEnvelope(
            axes.width, axes.height, axes.depth));
    assert(probe.maxComputeWorkgroupInvocations != 0u);
    rhi::ComputePipelineDesc desc;
    desc.layout = &root.layout();
    desc.shaderPackage = &root.interface();
    desc.computeShader = root.interface().entrypoints.front();
    const auto& bytes = root.artifact()->bytes(shader::ShaderArtifactRole::Metal);
    const std::string_view source{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    rhi::PipelineValidationReport report;
    report.selectedComputeWorkgroupSizeX = native->computeWorkgroupSizeX;
    report.selectedComputeWorkgroupSizeY = native->computeWorkgroupSizeY;
    report.selectedComputeWorkgroupSizeZ = native->computeWorkgroupSizeZ;
    auto accepted = createMetalRhiNativeComputePipelinePayload(desc, report, source);
    assert(accepted && accepted->nativeRuntimeBacked && accepted->pipelineState);
    assert(accepted->computeWorkgroupSizeX == native->computeWorkgroupSizeX
        && accepted->computeWorkgroupSizeY == native->computeWorkgroupSizeY
        && accepted->computeWorkgroupSizeZ == native->computeWorkgroupSizeZ);
    for (unsigned i = 0u; i < 5u; ++i) {
        auto invalid = report;
        if (i == 0u)
            invalid.selectedComputeWorkgroupSizeX = 0u;
        if (i == 1u)
            invalid.selectedComputeWorkgroupSizeX = static_cast<std::uint32_t>(axes.width + 1u);
        if (i == 2u)
            invalid.selectedComputeWorkgroupSizeY = static_cast<std::uint32_t>(axes.height + 1u);
        if (i == 3u)
            invalid.selectedComputeWorkgroupSizeZ = static_cast<std::uint32_t>(axes.depth + 1u);
        if (i == 4u) {
            // Each axis is legal; only the independent total rejects this.
            invalid.selectedComputeWorkgroupSizeX = static_cast<std::uint32_t>(axes.width);
            invalid.selectedComputeWorkgroupSizeY = static_cast<std::uint32_t>(axes.height);
            invalid.selectedComputeWorkgroupSizeZ = static_cast<std::uint32_t>(axes.depth);
            assert(limits.validate(invalid.selectedComputeWorkgroupSizeX,
                invalid.selectedComputeWorkgroupSizeY,
                invalid.selectedComputeWorkgroupSizeZ));
        }
        auto rejected = createMetalRhiNativeComputePipelinePayload(desc, invalid, source);
        assert(rejected && !rejected->nativeRuntimeBacked && rejected->nativeRuntimeDiagnosticOnly
            && !rejected->computePipelinePayloadPresent && !rejected->pipelineState
            && rejected->diagnostics == "RHI-METAL-COMPUTE-WORKGROUP-LIMIT");
    }
    std::printf(
        "Metal compute workgroup axes%llu/%llu/%llu selectedTotal%llu envelope%u acceptedExactShape/zero/axisOver/totalOver PASS; no dispatch\n",
        static_cast<unsigned long long>(axes.width),
        static_cast<unsigned long long>(axes.height),
        static_cast<unsigned long long>(axes.depth),
        static_cast<unsigned long long>(total),
        probe.maxComputeWorkgroupInvocations);
}

void run(const char* path, const char* fragment, const std::shared_ptr<core::CpuMemoryQuota>& quota)
{
    Fixture f(path, quota);
    auto root = f.prepare();
    if (!root)
        std::fprintf(stderr, "Producer pipeline: %s\n", f.diagnostic.c_str());
    assert(root && !root->validate());
    workgroupLimits(*root);
    const auto native = root->native();
    const auto fact = root->fact();
    std::printf(
        "Compute producer ABI: slot%u access%u type%u alignment%llu scalarSpan%llu proofSpan%llu owners%zu\n",
        fact.slot,
        fact.access,
        fact.elementType,
        static_cast<unsigned long long>(fact.alignment),
        static_cast<unsigned long long>(fact.requiredBytes),
        static_cast<unsigned long long>(root->proof().byteCount),
        root->childOwnerCount());
    assert(fact.requiredBytes == 4u && root->proof().byteCount == 20u
        && root->proof().fixedWords[3] == 0xfffffffeu);
    assert(root->childOwnerCount() == 6u && native->computeFunction && native->pipelineState);
    assert(reinterpret_cast<id<MTLFunction>>(native->computeFunction.get()).functionType
        == MTLFunctionTypeKernel);
    assert(!semantics::GpuIndirectProducerLowering::current(Backend::Metal).executable);
    // Admission/pipeline creation remains cold. Existing exact/one-less helper
    // measures actual peak rather than using an empirical runtime reserve.
    test::CpuQuotaFactoryFixture factory(quota);
    const auto peak = factory.measure([&] {
        return f.prepare();
    });
    factory.verify(peak, [&] {
        return f.prepare();
    });
    assert(f.diagnostic == "RHI-METAL-INDIRECT-PRODUCER-ALLOCATION-FAILED");
    std::printf("Compute producer cold factory peak%llu exact/one-less PASS\n",
        static_cast<unsigned long long>(peak));
    auto other = f.prepare();
    assert(other);
    const auto reject = [&](const char* expected) {
        const auto* id = root->validate();
        assert(id && !std::strcmp(id, expected));
    };
    const auto nativeOnly = [&] {
        reject("RHI-METAL-INDIRECT-PRODUCER-OWNER-MISMATCH");
    };
    // The device is a singleton shared control block. Use another real owned
    // object to probe replacement/alias identity; it never reaches a native call.
    test::MetalNativeIdentityMutationProbe::verifyOwner(
        native->nativeDevice, other->native()->pipelineState, nativeOnly);
    test::MetalNativeIdentityMutationProbe::verifyOwner(
        native->shaderLibrary, other->native()->shaderLibrary, nativeOnly);
    test::MetalNativeIdentityMutationProbe::verifyOwner(
        native->computeFunction, other->native()->computeFunction, nativeOnly);
    test::MetalNativeIdentityMutationProbe::verifyOwner(
        native->pipelineState, other->native()->pipelineState, nativeOnly);
    native->computeBufferFactsVersion = 0u;
    reject("RHI-METAL-ORDINARY-OFFSET-FACTS-MISSING");
    native->computeBufferFactsVersion = 1u;
    native->computeBufferFacts.push_back(fact);
    reject("RHI-METAL-INDIRECT-PRODUCER-ABI-COUNT");
    native->computeBufferFacts.pop_back();
    auto& reflected = native->computeBufferFacts.front();
    reflected.alignment *= 2u;
    reject("RHI-METAL-INDIRECT-PRODUCER-ABI-CAPTURE-MISMATCH");
    reflected = fact;
    reflected.access = MTLBindingAccessReadOnly;
    reject("RHI-METAL-WRITABLE-UINT32-BUFFER-ABI-DOMAIN");
    reflected = fact;
    reflected.elementType = MTLDataTypeInt;
    reject("RHI-METAL-WRITABLE-UINT32-BUFFER-ABI-DOMAIN");
    reflected = fact;
    reflected.requiredBytes = 8u;
    reject("RHI-METAL-WRITABLE-UINT32-BUFFER-ABI-DOMAIN");
    reflected = fact;
    native->computeWorkgroupSizeY = 2u;
    reject("SHADER-COMPUTE-INDIRECT-PRODUCER-DISPATCH");
    native->computeWorkgroupSizeY = 1u;
    native->metalBindingSpace.front().binding++;
    reject("RHI-METAL-INDIRECT-PRODUCER-MAPPING");
    native->metalBindingSpace.front().binding--;
    native->nativeRuntimeDiagnosticOnly = true;
    reject("RHI-METAL-INDIRECT-PRODUCER-NATIVE-PAYLOAD-MISSING");
    native->nativeRuntimeDiagnosticOnly = false;
    assert(!root->validate());
    {
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i)
            assert(!root->validate());
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    f.capability.backend = Backend::Vulkan;
    assert(!f.prepare() && f.diagnostic == "RHI-METAL-INDIRECT-PRODUCER-BACKEND");
    f.capability.backend = Backend::WebGPU;
    assert(!f.prepare() && f.diagnostic == "RHI-METAL-INDIRECT-PRODUCER-BACKEND");
    f.capability.backend = Backend::Null;
    assert(!f.prepare() && f.diagnostic == "RHI-METAL-INDIRECT-PRODUCER-BACKEND");
    f.capability.backend = Backend::Metal;
    f.layout.descriptorLayouts.front().bindings.front().dynamicOffset = true;
    assert(!f.prepare() && f.diagnostic == "RHI-GPU-INDIRECT-PRODUCER-LAYOUT-DOMAIN");
    f.layout.descriptorLayouts.front().bindings.front().dynamicOffset = false;
    const auto originalArtifact = f.artifact;
    f.artifact = shader::SingleEntryShaderArtifact::load(
        fragment, {f.cpuBytes, f.cpuBytes * 4u, f.cpuBytes, 16u}, quota, f.diagnostic);
    assert(
        f.artifact && !f.prepare() && f.diagnostic == "RHI-METAL-INDIRECT-PRODUCER-ARTIFACT-STAGE");
    f.artifact = originalArtifact;
    auto wrongQuota = core::makeShared<core::CpuMemoryQuota>(f.cpuBytes);
    assert(!MetalRhiIndirectProducerPipeline::prepare(f.artifact,
               f.layout,
               f.capability,
               {1u, true, f.cpuBytes, 0u, 0u},
               wrongQuota,
               f.diagnostic)
        && f.diagnostic == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
    std::weak_ptr<void> library = native->shaderLibrary, function = native->computeFunction,
                        pipeline = native->pipelineState;
    native->shaderLibrary.reset();
    native->computeFunction.reset();
    native->pipelineState.reset();
    assert(!library.expired() && !function.expired() && !pipeline.expired());
    root.reset();
    assert(library.expired() && function.expired() && pipeline.expired());
    std::puts(
        "Compute producer selected native pipeline/queried ABI, identity mutations/quota/allocation0/original owner retirement PASS; no output/dispatch/generation authority");
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    assert(metalRhiNativeDeviceHandle());
    auto quota = core::makeShared<core::CpuMemoryQuota>(Fixture::cpuBytes);
    @autoreleasepool {
        run(argv[1], argv[2], quota);
    }
    assert(!quota->liveBytes());
}
