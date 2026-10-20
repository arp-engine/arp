#pragma once
#include "rhi/backends/metal/metal_indirect_preparation.h"
#include "single_storage_contract_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
using namespace rhi;
using namespace rhi_metal;

class MetalIndirectProducerPipelineFixture final : public test::SingleStorageContractFixture {
public:
    MetalIndirectProducerPipelineFixture(
        const char* path, const std::shared_ptr<core::CpuMemoryQuota>& allocationQuota)
        : SingleStorageContractFixture(ShaderStage::Compute), quota(allocationQuota)
    {
        diagnostic.reserve(512u);
        artifact = shader::SingleEntryShaderArtifact::load(
            path, {cpuBytes, cpuBytes * 4u, cpuBytes, 16u}, quota, diagnostic);
        if (!artifact)
            std::fprintf(stderr, "Artifact: %s\n", diagnostic.c_str());
        assert(artifact);
        capability = semantics::nativeBackendCapabilityContract(Backend::Metal);
    }

    auto prepare()
    {
        return MetalRhiIndirectProducerPipeline::prepare(
            artifact, layout, capability, {1u, true, cpuBytes, 0u, 0u}, quota, diagnostic);
    }

    static constexpr std::uint64_t cpuBytes = 1024u * 1024u;
    std::shared_ptr<core::CpuMemoryQuota> quota;
    std::shared_ptr<const shader::SingleEntryShaderArtifact> artifact;
    semantics::NativeBackendCapabilityContract capability;
    core::pmr::String diagnostic;
};
} // namespace arp::test
