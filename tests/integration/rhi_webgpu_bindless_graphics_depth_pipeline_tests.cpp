#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_pipeline_ownership.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/gpu_extensions.h"
#include "tests/fixtures/bindless_package_request_fixture.h"
#include "tests/fixtures/canonical_graphics_depth_contract_fixture.h"
#include "tests/fixtures/completed_depth_write_return_contract_fixture.h"
#include "tests/fixtures/ordinary_depth_pipeline_contract_fixture.h"
#include "tests/fixtures/original_completed_depth_publication_fixture.h"
#include "tests/fixtures/webgpu_original_device_fixture.h"

#include <cassert>
#include <cstdio>
#include <emscripten/emscripten.h>

int main(int argc, char** argv)
{
    using namespace arp;
    using namespace rhi;
    using namespace rhi_webgpu;
    assert(argc == 2 || argc == 3);
    EM_ASM({
        // clang-format off
        Module.preinitializedWebGPUDevice.pushErrorScope('validation');
        // clang-format on
    });
    test::OrdinaryDepthPipelineContractFixture::verify();
    core::pmr::String diagnostic;
    const auto package = BindlessShaderPackage::load(
        argv[1], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
    assert(package && package->artifact().tables.size() == 1u
        && package->artifact().additionalDomains.size() == 1u);
    if (argc == 3) {
        if (core::StringView(argv[2]) == "--rewrite1") {
            test::CompletedDepthWriteReturnContractFixture::verify();
            for (const bool fault : {false, true})
                test::OriginalCompletedDepthPublicationFixture::verify(
                    {}, UINT32_MAX, false, package, true, 0u, fault, true);
            std::puts(
                "BINDLESS_WEBGPU_DEPTH_WRITE_RETURN1_PIPELINE_OK consumers2 numeric2 real-reclear stale-Closure20 quota0");
            return 0;
        }
        if (core::StringView(argv[2]) == "--execution14") {
            for (std::uint32_t scenario = 0u; scenario < 11u; ++scenario)
                for (const bool fault : {false, true})
                    test::OriginalCompletedDepthPublicationFixture::verify(
                        {}, UINT32_MAX, false, package, true, scenario, fault);
            std::puts(
                "BINDLESS_WEBGPU_ORDINARY_DEPTH_EXECUTION14_PIPELINE_OK scenarios22 numeric4 fault0-1 same-original completion retirement quota0");
            return 0;
        }
        assert(core::StringView(argv[2]) == "--closure19");
        test::CanonicalGraphicsDepthContractFixture::verify();
        test::OriginalCompletedDepthPublicationFixture::verify({}, UINT32_MAX, false, package);
        std::puts(
            "BINDLESS_WEBGPU_ORDINARY_DEPTH_CLOSURE19_PIPELINE_OK public-root clean/fault depth-color comparison2 stale-reclear quota0 no-execution");
        return 0;
    }
    constexpr std::uint64_t cpuBytes = 65536u;
    auto lifetimeQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    auto registryQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    {
        auto device = createWebGpuRhiDevice(test::WebGpuOriginalDeviceFixture::describe(
            "ordinary Fragment depth pipeline", 64u * 1024u * 1024u, 4096u, 4u * 1024u * 1024u));
        assert(device);
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, lifetimeQuota));
        OriginalResourceRegistrationBudget registration;
        registration.contractVersion = 3u;
        registration.prototypeOptIn = true;
        registration.maxBuffers = 4u; // Result and three snapshot-owned buffers.
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
        const auto& caps = extension->capabilities();
        assert(caps.limitsQueried
            && caps.originalOrdinaryDepthGraphicsPreparationContractVersion == 12u
            && !caps.executablePayload && caps.execution == BindlessExecution::DiagnosticFailure);
        auto request =
            test::BindlessPackageRequestFixture::make(package->artifact(), caps.deviceIdentity);
        request.originalPreparation = {12u, true};
        BindlessSnapshotPool snapshots(*device, request.budget);
        for (const auto version : {2u, 11u}) {
            auto invalid = request;
            invalid.originalPreparation.contractVersion = version;
            assert(!snapshots.prepareOriginalDescriptors(
                invalid, package->artifact(), {2u, true, true}, diagnostic));
        }
        auto snapshot = core::shareUnique(snapshots.prepareOriginalDescriptors(
            request, package->artifact(), {2u, true, true}, diagnostic));
        if (!snapshot)
            std::fprintf(stderr, "ordinary depth snapshot: %s\n", diagnostic.c_str());
        assert(snapshot && !snapshot->plan().executable);
        auto output = device->createBuffer({"DepthDeclarationResults40",
            40u * sizeof(float),
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderWrite});
        assert(output && snapshot->writeBuffer(snapshot->handle(1u, 0u), *output).success);
        assert(snapshot->seal(*device).success);
        auto authorization = extension->authorize(package, *snapshot, diagnostic);
        if (!authorization)
            std::fprintf(stderr, "ordinary depth authorization: %s\n", diagnostic.c_str());
        assert(authorization && !authorization->plan().executable);
        for (const auto fragment : {"cleanFragment", "faultFragment"}) {
            RenderPipelineDesc state;
            state.colorFormat = TextureFormat::Rgba8Unorm;
            state.dynamicViewportScissor = true;
            auto pipeline = extension->createRenderPipeline(
                authorization, state, "vertexMain", fragment, diagnostic);
            if (!pipeline)
                std::fprintf(stderr, "ordinary depth pipeline: %s\n", diagnostic.c_str());
            assert(pipeline && pipeline->bindlessAuthorization() == authorization);
            const auto native = webGpuNativePayloadFor(*pipeline);
            assert(native && native->pipeline && native->layout && native->issuedContract());
            std::size_t textures = 0u, depth = 0u, samplers = 0u, comparison = 0u;
            for (const auto& fact : native->issuedContract()->facts()) {
                if (fact.mapping.logicalSet != package->artifact().additionalDomains.front().set)
                    continue;
                assert(fact.shape.stages == ShaderStage::Fragment);
                textures += fact.shape.type == DescriptorType::SampledTexture;
                depth += fact.shape.depthTexture;
                samplers += fact.shape.type == DescriptorType::Sampler;
                comparison += fact.shape.comparisonSampler;
            }
            assert(textures == 2u && depth == 1u && samplers == 3u && comparison == 2u);
        }
    }
    assert(!lifetimeQuota->liveBytes() && !registryQuota->liveBytes());
    std::puts(
        "BINDLESS_WEBGPU_ORDINARY_DEPTH_PIPELINE_OK public-authorization clean/fault native-pipelines textures2 depth1 samplers3 comparison2 preparation12 quota0 no-execution");
}
