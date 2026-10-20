#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"
#include "rhi/frontend/pipeline_single_entry_input.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalSingleEntryPipelineInputFixture final {
    using Input = rhi::OriginalSingleEntryPipelineInput;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static auto verify(const std::shared_ptr<const shader::SingleEntryShaderArtifact>& artifact)
    {
        const Input::Budget budget{1u, 65536u};
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        diagnostic.reserve(256u);
        const auto baseline = artifact->quota()->liveBytes();
        auto factory = [&] {
            return Input::prepare(artifact, budget, quota, diagnostic);
        };
        auto expect = [&](auto original, auto authored, auto domain, const char* id) {
            const auto before = quota->liveBytes();
            auto result = Input::prepare(original, authored, domain, diagnostic);
            if (result || diagnostic != id)
                std::fprintf(stderr, "memory input expected %s, got %s\n", id, diagnostic.c_str());
            assert(!result && diagnostic == id && quota->liveBytes() == before);
        };
        auto invalid = budget;
        invalid.contractVersion = 0u;
        expect(artifact, invalid, quota, "RHI-ORIGINAL-ARTIFACT-INPUT-VERSION");
        invalid = budget;
        invalid.maxCpuMetadataBytes = 0u;
        expect(artifact, invalid, quota, "RHI-ORIGINAL-ARTIFACT-INPUT-QUOTA");
        expect(artifact,
            budget,
            std::shared_ptr<core::CpuMemoryQuota>{},
            "RHI-ORIGINAL-ARTIFACT-INPUT-QUOTA");
        invalid = budget;
        invalid.maxCpuMetadataBytes = artifact->quota()->limitBytes();
        expect(artifact, invalid, artifact->quota(), "RHI-ORIGINAL-ARTIFACT-INPUT-QUOTA-DOMAIN");
        auto forgedArtifact = std::shared_ptr<const shader::SingleEntryShaderArtifact>(
            artifact->quota(), artifact.get());
        expect(forgedArtifact, budget, quota, "SHADER-ENTRY-ORIGINAL-OWNER");
        expect(std::shared_ptr<const shader::SingleEntryShaderArtifact>{},
            budget,
            quota,
            "SHADER-ENTRY-ORIGINAL-OWNER");
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease exhausted(quota, quota->limitBytes());
            assert(exhausted.charged());
            expect(artifact, budget, quota, "RHI-ORIGINAL-ARTIFACT-INPUT-ALLOCATION");
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto input = factory();
        std::pmr::set_default_resource(previous);
        assert(input && core::SharedOwnerProjection::sameOwner(input->artifact(), artifact));
        assert(!Input::validateOriginal(input) && !input->nativeExecutionProven());
        assert(input->package().entrypoints.size() == 1u
            && input->package().entrypoints[0].entryPoint == artifact->entry());
        assert(input->package().entrypoints[0].stage
            == (artifact->stage() == shader::ShaderStage::Vertex ? rhi::ShaderStage::Vertex
                    : artifact->stage() == shader::ShaderStage::Fragment
                    ? rhi::ShaderStage::Fragment
                    : rhi::ShaderStage::Compute));
        assert(input->package().reflection.descriptorBindings.size()
            == artifact->module().reflection.bindings.size());
        const auto null = Input::selectTarget(input, rhi::Backend::Null);
        assert(null.validatorOnly && !null.bytes && !null.diagnostic);
        for (const auto pair :
            std::array{std::pair{rhi::Backend::Vulkan, shader::ShaderArtifactRole::Spirv},
                std::pair{rhi::Backend::Metal, shader::ShaderArtifactRole::Metal},
                std::pair{rhi::Backend::WebGPU, shader::ShaderArtifactRole::Wgsl}}) {
            const auto target = Input::selectTarget(input, pair.first);
            if (artifact->available(pair.second))
                assert(target.bytes == &artifact->bytes(pair.second) && !target.diagnostic
                    && !target.validatorOnly);
            else
                assert(!target.bytes && target.diagnostic
                    && std::string_view(target.diagnostic)
                        == artifact->targetDiagnostic(pair.second));
        }
        is(Input::selectTarget(input, static_cast<rhi::Backend>(255u)).diagnostic,
            "RHI-ORIGINAL-ARTIFACT-INPUT-BACKEND");
        auto forged = std::shared_ptr<const Input>(quota, input.get());
        auto nonowning = std::shared_ptr<const Input>(std::shared_ptr<const Input>{}, input.get());
        quota->setLimitBytes(0u);
        is(Input::validateOriginal(input), "RHI-ORIGINAL-ARTIFACT-INPUT-QUOTA-LIMIT");
        quota->setLimitBytes(budget.maxCpuMetadataBytes - 1u);
        is(Input::validateOriginal(input), "RHI-ORIGINAL-ARTIFACT-INPUT-QUOTA-LIMIT");
        quota->setLimitBytes(budget.maxCpuMetadataBytes);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, artifact->quota()}, [&]() noexcept {
                for (const auto& owner :
                    std::array{forged, nonowning, std::shared_ptr<const Input>{}})
                    is(Input::selectTarget(owner, rhi::Backend::WebGPU).diagnostic,
                        "RHI-ORIGINAL-ARTIFACT-INPUT-OWNER");
                return Input::validateOriginal(input);
            }));
        assert(artifact->quota()->liveBytes() == baseline);
        forged.reset();
        nonowning.reset();
        input.reset();
        assert(!quota->liveBytes());
        input = factory();
        assert(input);
        verifyMemoryLowering(input);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_ARTIFACT_MEMORY_INPUT_OK %llu stage%u exact-target-bytes independent-quota hot-allocation0 consumption-native-closed\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned>(artifact->stage()));
        return input;
    }

    static void verifyMemoryLowering(const std::shared_ptr<const Input>& input)
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto factory = [&] {
            try {
                core::CpuMemoryQuotaScope scope(quota);
                auto result = core::makeUnique<rhi_webgpu::WebGpuBindingSpaceLoweringLoadResult>(
                    rhi_webgpu::loadOriginalWebGpuBindingSpaceLowering(input));
                if (result->success)
                    return result;
            } catch (const std::bad_alloc&) {
            }
            return core::UniquePtr<rhi_webgpu::WebGpuBindingSpaceLoweringLoadResult>{};
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease exhausted(quota, quota->limitBytes());
            assert(exhausted.charged());
            assert(!factory());
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto result = factory();
        std::pmr::set_default_resource(previous);
        assert(result && result->success);
        assert(result->lowering.artifactBindings.size()
            == input->package().reflection.descriptorBindings.size());
        for (const auto& entry : result->lowering.entries)
            assert(entry.name.get_allocator().resource() == &core::defaultCpuMemoryResource());
        result.reset();
        assert(!quota->liveBytes());
        auto forged = std::shared_ptr<const Input>(input->quota(), input.get());
        auto failed = rhi_webgpu::loadOriginalWebGpuBindingSpaceLowering(forged);
        assert(!failed.success && failed.diagnostics == "RHI-ORIGINAL-ARTIFACT-INPUT-OWNER");
        std::printf(
            "BINDLESS_RHI_ORIGINAL_ARTIFACT_MEMORY_LOWERING_OK %llu shared-reflection-validator no-file-io no-yaml-parse native-closed\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyFileLowering(
        const std::shared_ptr<const Input>& input, rhi::ShaderPackageDesc package)
    {
        const auto memory = rhi_webgpu::loadOriginalWebGpuBindingSpaceLowering(input);
        const auto file = rhi_webgpu::loadWebGpuBindingSpaceLowering(package);
        assert(memory.success && file.success);
        assert(memory.lowering.contractVersion == file.lowering.contractVersion);
        assert(memory.lowering.physicalGroupCount == file.lowering.physicalGroupCount);
        assert(memory.lowering.pushConstantGroup == file.lowering.pushConstantGroup);
        assert(memory.lowering.pushConstantBinding == file.lowering.pushConstantBinding);
        assert(memory.lowering.entries.size() == file.lowering.entries.size());
        for (std::size_t i = 0u; i < memory.lowering.entries.size(); ++i) {
            const auto& a = memory.lowering.entries[i];
            const auto& b = file.lowering.entries[i];
            assert(a.name == b.name && a.set == b.set && a.binding == b.binding
                && a.group == b.group && a.webGpuBinding == b.webGpuBinding && a.active == b.active
                && a.physicalType == b.physicalType && a.loadSemantics == b.loadSemantics
                && a.hasLoadSemantics == b.hasLoadSemantics
                && a.requiresSampler == b.requiresSampler && a.depthTexture == b.depthTexture
                && a.arrayElement == b.arrayElement && a.capacity == b.capacity);
        }
        ++package.reflection.pushConstantByteCount;
        const auto failed = rhi_webgpu::loadWebGpuBindingSpaceLowering(package);
        assert(!failed.success
            && failed.diagnostics
                == "WebGPU reflection artifact disagrees with package reflection metadata");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_ARTIFACT_MEMORY_FILE_EQUIVALENCE_OK same-lowering package-disagreement-rejected");
    }
};
} // namespace arp::test
