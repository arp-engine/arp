#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/gpu_pipeline.h"
#include "shader/single_entry_shader_artifact.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Contract tests require assertions
#endif

int main(int argc, char** argv)
{
    using namespace arp;
    using namespace rhi_metal;
    assert(argc == 3);
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    diagnostic.reserve(256u);
    const shader::SingleEntryShaderLoadBudget loadBudget{
        1024u * 1024u, 4u * 1024u * 1024u, 1024u * 1024u, 16u};
    auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(loadBudget.maxCpuMetadataBytes);
    const auto artifact =
        shader::SingleEntryShaderArtifact::load(argv[1], loadBudget, sourceQuota, diagnostic);
    if (!artifact) {
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(artifact);
    }
    const auto stage = artifact->stage() == shader::ShaderStage::Vertex ? rhi::ShaderStage::Vertex
        : artifact->stage() == shader::ShaderStage::Fragment            ? rhi::ShaderStage::Fragment
                                                                        : rhi::ShaderStage::Compute;
    auto quota = core::makeShared<core::CpuMemoryQuota>(1024u * 1024u);
    const core::StringView expected(argv[2]);
    {
        core::CpuMemoryQuotaScope scope(quota);
        auto* errorPrevious = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        MetalRhiPreparedGraphicsStageLayout layout;
        const auto result =
            lowerMetalRhiPreparedGraphicsStage(*artifact, artifact->entry(), stage, layout);
        std::pmr::set_default_resource(errorPrevious);
        if (expected != "valid") {
            if (result.success || result.diagnostics.find(expected) == result.diagnostics.npos)
                std::fprintf(stderr, "Expected %s; got %s\n", argv[2], result.diagnostics.c_str());
            assert(!result.success && result.diagnostics.find(expected) != result.diagnostics.npos);
            assert(layout.entry.empty() && layout.direct.empty() && layout.arrays.empty()
                && layout.stage == rhi::ShaderStage::None);
        } else {
            if (!result.success)
                std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
            assert(result.success);
            assert(layout.entry == artifact->entry() && layout.stage == stage);
            assert(layout.direct.get_allocator().resource() == &core::defaultCpuMemoryResource());
            assert(layout.arrays.get_allocator().resource() == &core::defaultCpuMemoryResource());
            rhi::ShaderPackageDesc interface;
            assert(rhi::populateSingleEntryShaderInterface(*artifact, interface).success);
            assert(interface.reflection.sourceSha256 == artifact->module().reflection.sourceSha256);
            assert(interface.reflection.descriptorBindings.size()
                == artifact->module().reflection.bindings.size());
            assert(interface.entrypoints.size() == 1u && interface.entrypoints[0].stage == stage);
            if (interface.reflection.descriptorBindings.empty()) {
                for (const auto& map : layout.direct)
                    assert(!map.active);
                assert(layout.arrays.empty());
            } else {
                for (const auto& binding : interface.reflection.descriptorBindings) {
                    unsigned count = 0u;
                    for (const auto& map : layout.direct)
                        if (map.active && map.set == binding.set && map.binding == binding.binding)
                            ++count;
                    for (const auto& map : layout.arrays)
                        if (map.active && map.set == binding.set
                            && map.binding == binding.binding) {
                            ++count;
                            assert(map.capacity == binding.arrayCount);
                        }
                    assert(count == 1u);
                }
            }
            // A hostile PMR default must not redirect any runtime allocation.
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            MetalRhiPreparedGraphicsStageLayout hostile;
            const auto hostileResult =
                lowerMetalRhiPreparedGraphicsStage(*artifact, artifact->entry(), stage, hostile);
            std::pmr::set_default_resource(previous);
            if (!hostileResult.success)
                std::fprintf(stderr, "Hostile PMR: %s\n", hostileResult.diagnostics.c_str());
            assert(hostileResult.success);
            assert(hostile.entry == layout.entry && hostile.direct.size() == layout.direct.size()
                && hostile.arrays.size() == layout.arrays.size());
            assert(!lowerMetalRhiPreparedGraphicsStage(*artifact, "wrongEntry", stage, hostile)
                    .success);
            assert(hostile.entry.empty() && hostile.direct.empty() && hostile.arrays.empty());
            assert(!lowerMetalRhiPreparedGraphicsStage(
                *artifact, artifact->entry(), rhi::ShaderStage::Compute, hostile)
                    .success);
            assert(hostile.entry.empty());
            if (!layout.direct.empty()) {
                const auto charge = core::inspectCpuAllocationCharge(layout.direct.data());
                assert(charge.found && charge.quota == quota.get());
            }
        }
    }
    assert(quota->liveBytes() == 0u);
    if (expected == "valid") {
        auto tiny = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(tiny);
            MetalRhiPreparedGraphicsStageLayout rejected;
            const auto result =
                lowerMetalRhiPreparedGraphicsStage(*artifact, artifact->entry(), stage, rejected);
            assert(!result.success
                && result.diagnostics.find("LOWERING-ALLOCATION-FAILED")
                    != result.diagnostics.npos);
            assert(rejected.entry.empty() && rejected.direct.empty() && rejected.arrays.empty());
        }
        assert(!tiny->liveBytes());
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
        {
            core::CpuMemoryQuotaScope scope(quota);
            MetalRhiPreparedGraphicsStageLayout rejected;
            const auto result =
                lowerMetalRhiPreparedGraphicsStage(*artifact, artifact->entry(), stage, rejected);
            assert(!result.success
                && result.diagnostics.find("LOWERING-ALLOCATION-FAILED")
                    != result.diagnostics.npos);
            assert(rejected.entry.empty() && rejected.direct.empty() && rejected.arrays.empty());
        }
        pressure.release();
        assert(!quota->liveBytes());
    }
    std::puts(
        "sealed ordinary per-entry interface/mapping: exact identity, explicit active/inactive, atomic failure and core quota pass");
}
