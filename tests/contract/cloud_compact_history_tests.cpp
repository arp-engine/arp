#include "renderer/volumetric_cloud/cloud_prepared_pipelines.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "tests/fixtures/cloud/cloud_fixture.h"

#include <cassert>
#include <cstring>

using namespace arp;
using namespace renderer::cloud;

int main(int argc, char** argv)
{
    assert(CloudSampleStorage::stride(1u) == 32u && CloudSampleStorage::stride(2u) == 24u);
    assert(!CloudSampleStorage::stride(0u) && !CloudSampleStorage::stride(3u));
    const CloudGpuSample input{{.1234567f, -0.f, 1.e-30f, .9123456f}, {65537.f, 9999.125f, 0, 0}};
    CloudGpuCompactSample packed{};
    assert(CloudSampleStorage::pack(input, packed).success);
    const auto unpacked = CloudSampleStorage::unpack(packed);
    assert(!std::memcmp(
        &input.radianceTransmittance, &unpacked.radianceTransmittance, sizeof(core::Vec4)));
    assert(!std::memcmp(&input.metadata, &unpacked.metadata, sizeof(core::Vec4)));
    auto invalid = input;
    invalid.metadata.z = 1.f;
    assert(!CloudSampleStorage::pack(invalid, packed).success);
    assert(packed.red == 0.f && packed.opaqueDistance == 0.f);
    auto config = tests::cloud::config();
    config.resolutionDivisor = 2u;
    config.viewSteps = 64u;
    config.lightSteps = 8u;
    config.maxDensityEvaluations = 3000000000ull;
    for (const auto extent :
        {rhi::Extent2D{1u, 1u}, rhi::Extent2D{3u, 2u}, rhi::Extent2D{1280u, 720u}}) {
        CloudReconstructionResources original, compact;
        assert(CloudReconstruction::describeResources(
            {1u, .025f, .025f}, config, extent, 606256u, sizeof(CloudGpuConstants), original)
                .success);
        assert(CloudReconstruction::describeResources(
            {2u, .025f, .025f}, config, extent, 606256u, sizeof(CloudGpuConstants), compact)
                .success);
        assert(original.resolvedCount == compact.resolvedCount);
        assert(compact.sampleContractVersion == 2u && original.sampleContractVersion == 1u);
        assert(compact.resolvedBufferBytes == compact.resolvedCount * 24u);
        assert(original.retainedGpuBytes - compact.retainedGpuBytes
            == compact.resolvedCount * 8u * config.framesInFlight);
        assert(original.densityEvaluationBound == compact.densityEvaluationBound);
        assert(original.raw.sampleBufferBytes == compact.raw.sampleBufferBytes);
        auto limited = config;
        limited.gpuBudgetBytes = compact.retainedGpuBytes;
        assert(CloudReconstruction::describeResources(
            {2u, .025f, .025f}, limited, extent, 606256u, sizeof(CloudGpuConstants), compact)
                .success);
        --limited.gpuBudgetBytes;
        assert(!CloudReconstruction::describeResources(
            {2u, .025f, .025f}, limited, extent, 606256u, sizeof(CloudGpuConstants), compact)
                .success);
    }
    const CloudPreparedPipelineSelection original{
        1u, CloudPreparedResolve::Reconstruction1, {1u, .025f, .025f}};
    const CloudPreparedPipelineSelection compact{
        1u, CloudPreparedResolve::Reconstruction2, {2u, .025f, .025f}};
    auto wrong = compact;
    wrong.resolve = CloudPreparedResolve::Reconstruction1;
    assert(!CloudPreparedPipelines::validateSelection(wrong).success);
    wrong = original;
    wrong.resolve = CloudPreparedResolve::Reconstruction2;
    assert(!CloudPreparedPipelines::validateSelection(wrong).success);
    constexpr std::uint64_t bytes = 1024u * 1024u;
    const auto quota = [bytes] {
        return core::makeShared<core::CpuMemoryQuota>(bytes);
    };
    core::pmr::String diagnostic;
    const core::pmr::String root = ARP_CLOUD_TEST_PUBLICATION_ROOT;
    const auto loadCompute = [&](const char* name) {
        const auto path =
            root + "/shaders/original-cloud-compute/" + name + ".computeMain.entry.yaml";
        auto artifact = shader::SingleEntryShaderArtifact::load(
            path.c_str(), {bytes, 8u * bytes, bytes, 7u}, quota(), diagnostic);
        assert(artifact && diagnostic.empty());
        return artifact;
    };
    const auto loadGraphics = [&](const char* name) {
        const auto path =
            root + "/shaders/original-cloud-graphics/" + name + ".graphics.module.yaml";
        auto artifact = shader::GraphicsShaderModuleArtifact::load(
            path.c_str(), {bytes, 8u * bytes, bytes, 4u, 4u, 256u, 2u}, quota(), diagnostic);
        assert(artifact && diagnostic.empty());
        return artifact;
    };
    const auto ray = loadCompute("cloud_raymarch");
    const auto resolve = loadCompute("cloud_spatial_resolve");
    const auto compactResolve = loadCompute("cloud_compact_resolve");
    const auto composite = loadGraphics("cloud_spatial_composite");
    const auto compactComposite = loadGraphics("cloud_compact_composite");
    assert(
        CloudPreparedPipelines::validateStorageInputs(original, ray, resolve, composite).success);
    assert(CloudPreparedPipelines::validateStorageInputs(
        compact, ray, compactResolve, compactComposite)
            .success);
    assert(!CloudPreparedPipelines::validateStorageInputs(original, ray, compactResolve, composite)
            .success);
    assert(!CloudPreparedPipelines::validateStorageInputs(compact, ray, resolve, compactComposite)
            .success);
    assert(!CloudPreparedPipelines::validateStorageInputs(compact, ray, compactResolve, composite)
            .success);
    auto device = rhi::createNullDevice();
    rhi::BufferDesc desc;
    desc.sizeBytes = 5u * CloudSampleStorage::stride(2u);
    desc.usage = rhi::ResourceUsage::StorageBuffer;
    desc.memory = rhi::MemoryUsage::CpuToGpu;
    desc.initialState = rhi::ResourceState::ShaderWrite;
    const auto before = device->memoryStats();
    auto buffer = device->createBuffer(desc);
    assert(buffer && buffer->desc().sizeBytes == 120u);
    buffer.reset();
    assert(device->memoryStats().allocatedBytes == before.allocatedBytes);
    if (argc == 2 && !std::strcmp(argv[1], "--null-hidden-frame"))
        assert(rhi::NullHiddenFrameSmokeRunner(*device).run({}).success);
}
