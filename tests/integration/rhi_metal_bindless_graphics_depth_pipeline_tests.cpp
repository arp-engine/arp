#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/semantics/bindless_graphics_execution_lowering.h"
#include "rhi/semantics/command_lowering.h"
#include "tests/fixtures/bindless_metal_public_graphics_fixture.h"

// The genuine public authorization/pipeline path, with no command executor.
// This is a prerequisite for ordinary Graphics depth, not a comparison result.
int main(int argc, char** argv)
{
    using namespace arp;
    using namespace rhi;
    using namespace rhi_metal;
    assert(argc == 2);
    core::pmr::String diagnostic;
    const auto package = BindlessShaderPackage::load(
        argv[1], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
    assert(package && package->artifact().tables.size() == 1u);
    assert(package->artifact().additionalDomains.size() == 1u);
    const auto& ordinary = package->artifact().additionalDomains.front();
    assert(ordinary.set == 2u && ordinary.layout.bindings.size() == 5u);
    for (const bool images : {false, true}) {
        constexpr std::uint64_t cpuBytes = 65536u;
        auto lifetimeQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto registryQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto desc = test::MetalPublicGraphicsFixture::deviceDesc();
        desc.enableBindlessGraphicsImages = images;
        auto device = createMetalRhiDevice(desc);
        assert(device);
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, lifetimeQuota));
        OriginalResourceRegistrationBudget registration;
        registration.contractVersion = 3u;
        registration.prototypeOptIn = true;
        // Indexed result plus snapshot metadata/latch/readback, one arena/set.
        registration.maxBuffers = 4u;
        registration.maxDescriptorArenas = registration.maxDescriptorSets = 1u;
        registration.maxRetainedOwners = OriginalResourceRegistrationContract::fixedOwnerCount
            + OriginalResourceRegistrationContract::perResourceOwnerCount
                * (registration.maxBuffers + registration.maxDescriptorArenas)
            + OriginalResourceRegistrationContract::perDescriptorOwnerCount
                * registration.maxDescriptorSets;
        registration.maxCpuMetadataBytes = cpuBytes;
        assert(!device->prepareOriginalResourceRegistration(registration, registryQuota));
        auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        assert(extension);
        const auto request = test::BindlessPackageRequestFixture::make(
            package->artifact(), extension->capabilities().deviceIdentity);
        BindlessSnapshotPool snapshots(*device, request.budget);
        auto snapshot = core::shareUnique(snapshots.prepareOriginalDescriptors(
            request, package->artifact(), {2u, true, true}, diagnostic));
        if (!images) {
            assert(
                !snapshot && diagnostic == "RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
            continue;
        }
        if (!snapshot)
            std::fprintf(stderr, "depth declaration snapshot: %s\n", diagnostic.c_str());
        assert(snapshot);
        auto output = device->createBuffer({"DepthDeclarationResults40",
            40u * sizeof(float),
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderWrite});
        assert(output && snapshot->writeBuffer(snapshot->handle(1u, 0u), *output).success);
        assert(snapshot->seal(*device).success);
        auto authorization = extension->authorize(package, *snapshot, diagnostic);
        if (!authorization)
            std::fprintf(stderr, "depth declaration authorization: %s\n", diagnostic.c_str());
        assert(authorization);
        for (const auto fragment : {"cleanFragment", "faultFragment"}) {
            RenderPipelineDesc state;
            state.colorFormat = TextureFormat::Rgba8Unorm;
            state.dynamicViewportScissor = true;
            auto pipeline = extension->createRenderPipeline(
                authorization, state, "vertexMain", fragment, diagnostic);
            if (!pipeline)
                std::fprintf(stderr, "depth declaration pipeline: %s\n", diagnostic.c_str());
            assert(pipeline && pipeline->bindlessAuthorization() == authorization);
            const auto native = metalRhiNativePayloadFor(*pipeline);
            assert(native && native->nativeRuntimeBacked && native->pipelineState
                && native->vertexFunction && native->fragmentFunction);
            MetalRhiPreparedGraphicsStageLayout layout;
            assert(lowerMetalRhiPreparedGraphicsStage(
                *package, fragment, ShaderStage::Fragment, layout)
                    .success);
            std::size_t textures = 0u, samplers = 0u;
            for (const auto& map : layout.direct) {
                if (map.set != ordinary.set || !map.active)
                    continue;
                assert((native->nativeBindingStages.stages(map.type, map.metalSlot)
                           & ShaderStage::Fragment)
                    != ShaderStage::None);
                textures += map.type == DescriptorType::SampledTexture;
                samplers += map.type == DescriptorType::Sampler;
            }
            assert(textures == 2u && samplers == 3u);
        }
    }
    const auto closure =
        semantics::OriginalBindlessRootNativeClosureLowering::lowering(Backend::Metal, 16u);
    const auto execution =
        semantics::OriginalBindlessGraphicsExecutionLowering::lowering(Backend::Metal, 2u);
    assert(closure.nativeCorrespondenceAvailable && !closure.executionAvailable
        && execution.executionAvailable && execution.completionAvailable);
    std::puts(
        "BINDLESS_METAL_ORDINARY_DEPTH_PIPELINE_OK public-authorization clean/fault native-pipelines textures2 samplers3 image-opt-in cold-declaration-only");
}
