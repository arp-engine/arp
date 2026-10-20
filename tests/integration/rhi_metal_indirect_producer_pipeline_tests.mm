#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/metal_indirect_producer_pipeline_fixture.h"
#include "../fixtures/metal_native_identity_fixture.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"
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

void run(const char* path, const char* fragment, const std::shared_ptr<core::CpuMemoryQuota>& quota)
{
    Fixture f(path, quota);
    auto root = f.prepare();
    if (!root)
        std::fprintf(stderr, "Producer pipeline: %s\n", f.diagnostic.c_str());
    assert(root && !root->validate());
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
