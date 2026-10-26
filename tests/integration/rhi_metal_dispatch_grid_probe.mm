#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_native_pipelines.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_original_pipeline.h"
#include "rhi/frontend/compute_dispatch_validation.h"

#import <Metal/Metal.h>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef NDEBUG
#error Dispatch probes require assertions
#endif

namespace {
using namespace arp;

struct Request {
    rhi::ComputeDispatchValidationRequest intent;
    rhi::ComputeDispatchValidationPlan plan;

    bool parse(char** values)
    {
        std::uint32_t x = 0u, y = 0u, z = 0u, byteBudget = 0u;
        std::uint32_t* fields[]{&x, &y, &z, &byteBudget};
        for (unsigned i = 0u; i < 4u; ++i) {
            const auto* end = values[i] + std::strlen(values[i]);
            const auto result = std::from_chars(values[i], end, *fields[i]);
            if (result.ec != std::errc{} || result.ptr != end || !*fields[i])
                return false;
        }
        // Authored fixture ceilings for the planned 720p acceptance workload,
        // not device limits or runtime-issued coverage.
        intent = {1u,
            true,
            {8u, 8u, 1u},
            {x, y, z},
            {byteBudget, 14400u, 52743600u, 3375590400u, 208281600u, 160u, 30000000000u}};
        return !rhi::ComputeDispatchValidationContract::inspect(intent, plan);
    }
};

void run(const char* path, const Request& request)
{
    const auto started = std::chrono::steady_clock::now();
    const auto expired = [&] {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now() - started)
                   .count()
            >= static_cast<std::int64_t>(request.intent.budget.maxColdValidationNanoseconds);
    };
    const auto& groups = request.plan.groups;
    const auto& plan = request.plan;
    constexpr std::uint64_t metadata = 4u * 1024u * 1024u;
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    const auto artifact = shader::SingleEntryShaderArtifact::load(path,
        {metadata, metadata * 8u, metadata, 16u},
        core::makeShared<core::CpuMemoryQuota>(metadata),
        diagnostic);
    if (!artifact)
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
    assert(artifact);
    const auto input = rhi::OriginalSingleEntryPipelineInput::prepare(
        artifact, {1u, metadata}, core::makeShared<core::CpuMemoryQuota>(metadata), diagnostic);
    assert(input);
    const auto& package = input->package();
    auto layout = rhi_metal::createMetalRhiPipelineLayout(
        rhi::buildPipelineLayoutDescFromShaderReflection(package.reflection));
    assert(layout);
    rhi::ComputePipelineDesc desc;
    desc.layout = layout.get();
    desc.shaderPackage = &package;
    desc.computeShader = package.entrypoints.front();
    const char* diagnosticId = nullptr;
    auto capability = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    capability.queriedFromNativeDevice = bool(rhi_metal::metalRhiNativeDeviceIdentity());
    assert(capability.queriedFromNativeDevice);
    auto pipeline = rhi_metal::MetalOriginalPipeline::create(desc,
        input,
        capability,
        {1u, true, metadata, metadata, 5000000000u},
        core::makeShared<core::CpuMemoryQuota>(metadata),
        diagnosticId);
    if (!pipeline)
        std::fprintf(stderr, "%s\n", diagnosticId ? diagnosticId : "missing diagnostic");
    assert(pipeline);
    const auto native = rhi_metal::metalRhiNativePayloadFor(*pipeline);
    assert(native && native->nativeRuntimeBacked && native->nativeDevice && native->pipelineState);
    assert(native->computeWorkgroupSizeX == 8u && native->computeWorkgroupSizeY == 8u
        && native->computeWorkgroupSizeZ == 1u);
    std::uint32_t uniformSlot = UINT32_MAX, outputSlot = UINT32_MAX;
    for (const auto& binding : native->metalBindingSpace) {
        assert(binding.set == 0u && binding.active);
        if (binding.binding == 0u)
            uniformSlot = binding.metalSlot;
        else if (binding.binding == 1u)
            outputSlot = binding.metalSlot;
        else
            assert(false);
    }
    assert(uniformSlot != UINT32_MAX && outputSlot != UINT32_MAX && uniformSlot != outputSlot);
    bool outputAlignmentVerified = false;
    assert(native->computeBufferFactsVersion == 1u);
    for (const auto& fact : native->computeBufferFacts)
        if (fact.slot == outputSlot) {
            assert(fact.alignment && plan.resultSliceBytes % fact.alignment == 0u);
            outputAlignmentVerified = true;
        }
    assert(outputAlignmentVerified);
    const auto device = reinterpret_cast<id<MTLDevice>>(native->nativeDevice.get());
    const auto state = reinterpret_cast<id<MTLComputePipelineState>>(native->pipelineState.get());
    assert(plan.nativeResultBytes <= device.maxBufferLength);
    const auto buffer = [device
        newBufferWithLength:plan.nativeResultBytes
                    options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeTracked];
    const auto queue = [device newCommandQueue];
    assert(buffer && buffer.contents && queue);
    assert(buffer.storageMode == MTLStorageModeShared
        && buffer.hazardTrackingMode == MTLHazardTrackingModeTracked);
    const std::uint32_t constants[]{groups.width, groups.height, groups.depth, 0u};
    std::uint64_t dispatches = 0u, verifiedGroups = 0u, checkedCells = 0u;
    for (std::uint32_t x = 1u; x <= groups.width; ++x) {
        assert(!expired());
        @autoreleasepool {
            std::memset(buffer.contents, 0, plan.nativeResultBytes);
            const auto commandDescriptor = [[MTLCommandBufferDescriptor alloc] init];
            commandDescriptor.retainedReferences = YES;
            const auto command = [queue commandBufferWithDescriptor:commandDescriptor];
            [commandDescriptor release];
            const auto encoder =
                [command computeCommandEncoderWithDispatchType:MTLDispatchTypeSerial];
            assert(command && encoder);
            [encoder setComputePipelineState:state];
            [encoder setBytes:constants length:sizeof(constants) atIndex:uniformSlot];
            std::uint64_t slice = 0u;
            for (std::uint32_t z = 1u; z <= groups.depth; ++z)
                for (std::uint32_t y = 1u; y <= groups.height; ++y, ++slice) {
                    [encoder setBuffer:buffer
                                offset:slice * plan.resultSliceBytes
                               atIndex:outputSlot];
                    [encoder dispatchThreadgroups:MTLSizeMake(x, y, z)
                            threadsPerThreadgroup:MTLSizeMake(8u, 8u, 1u)];
                    ++dispatches;
                }
            [encoder endEncoding];
            [command commit];
            while (command.status != MTLCommandBufferStatusCompleted
                && command.status != MTLCommandBufferStatusError && !expired())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (command.status != MTLCommandBufferStatusCompleted)
                std::fprintf(stderr,
                    "dispatch probe row%u status%lu error%s\n",
                    x,
                    static_cast<unsigned long>(command.status),
                    command.error ? command.error.description.UTF8String : "none");
            assert(command.status == MTLCommandBufferStatusCompleted && !command.error);
            slice = 0u;
            for (std::uint32_t z = 1u; z <= groups.depth; ++z)
                for (std::uint32_t y = 1u; y <= groups.height; ++y, ++slice) {
                    const auto* words = reinterpret_cast<const std::uint32_t*>(
                        static_cast<const unsigned char*>(buffer.contents)
                        + slice * plan.resultSliceBytes);
                    for (std::uint64_t index = 0u; index < plan.resultSliceBytes / 4u; ++index) {
                        const bool inside = index < plan.resultCellsPerSlice
                            && index % groups.width < x && index / groups.width % groups.height < y
                            && index / (std::uint64_t(groups.width) * groups.height) < z;
                        const auto expected =
                            inside ? static_cast<std::uint32_t>(64u + 128u * index) : 0u;
                        if (words[index] != expected)
                            std::fprintf(stderr,
                                "grid%u/%u/%u cell%llu got%u expected%u\n",
                                x,
                                y,
                                z,
                                static_cast<unsigned long long>(index),
                                words[index],
                                expected);
                        assert(words[index] == expected);
                        verifiedGroups += inside;
                        ++checkedCells;
                    }
                }
        }
        if (x == 1u || x == groups.width || x % 16u == 0u) {
            std::printf("DISPATCH_GRID_PROBE_PROGRESS x=%u/%u completed=%llu\n",
                x,
                groups.width,
                static_cast<unsigned long long>(dispatches));
            std::fflush(stdout);
        }
    }
    assert(dispatches == plan.dispatches && verifiedGroups == plan.workgroups
        && checkedCells == plan.resultChecks && !expired());
    std::printf(
        "DISPATCH_GRID_PROBE_PASS device=%s grid=%u/%u/%u local=8/8/1 tuples=%llu groups=%llu checkedCells=%llu nativeBytes=%llu capabilityPublished=false cloudFrames=0\n",
        device.name.UTF8String,
        groups.width,
        groups.height,
        groups.depth,
        static_cast<unsigned long long>(dispatches),
        static_cast<unsigned long long>(verifiedGroups),
        static_cast<unsigned long long>(checkedCells),
        static_cast<unsigned long long>(plan.nativeResultBytes));
    [queue release];
    [buffer release];
}
} // namespace

int main(int argc, char** argv)
{
    Request request;
    if (argc != 6 || !request.parse(argv + 2)) {
        std::fputs("METAL-DISPATCH-PROBE-REQUEST\n", stderr);
        return 2;
    }
    @autoreleasepool {
        run(argv[1], request);
    }
}
