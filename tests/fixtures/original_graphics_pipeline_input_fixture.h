#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_state_copy.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalGraphicsPipelineInputFixture final {
    using Input = rhi::OriginalGraphicsPipelineInput;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static auto verify(const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>& artifact)
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
        expect(artifact, invalid, quota, "RHI-ORIGINAL-GRAPHICS-INPUT-VERSION");
        invalid = budget;
        invalid.maxCpuMetadataBytes = 0u;
        expect(artifact, invalid, quota, "RHI-ORIGINAL-GRAPHICS-INPUT-QUOTA");
        expect(artifact,
            budget,
            std::shared_ptr<core::CpuMemoryQuota>{},
            "RHI-ORIGINAL-GRAPHICS-INPUT-QUOTA");
        invalid = budget;
        invalid.maxCpuMetadataBytes = artifact->quota()->limitBytes();
        expect(artifact, invalid, artifact->quota(), "RHI-ORIGINAL-GRAPHICS-INPUT-QUOTA-DOMAIN");
        auto forgedArtifact = std::shared_ptr<const shader::GraphicsShaderModuleArtifact>(
            artifact->quota(), artifact.get());
        expect(forgedArtifact, budget, quota, "SHADER-MODULE-ORIGINAL-OWNER");
        auto nonowningArtifact = std::shared_ptr<const shader::GraphicsShaderModuleArtifact>(
            std::shared_ptr<const shader::GraphicsShaderModuleArtifact>{}, artifact.get());
        expect(nonowningArtifact, budget, quota, "SHADER-MODULE-ORIGINAL-OWNER");
        expect(std::shared_ptr<const shader::GraphicsShaderModuleArtifact>{},
            budget,
            quota,
            "SHADER-MODULE-ORIGINAL-OWNER");
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease exhausted(quota, quota->limitBytes());
            assert(exhausted.charged());
            expect(artifact, budget, quota, "RHI-ORIGINAL-GRAPHICS-INPUT-ALLOCATION");
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto input = factory();
        std::pmr::set_default_resource(previous);
        assert(input && core::SharedOwnerProjection::sameOwner(input->artifact(), artifact));
        assert(!Input::validateOriginal(input) && !input->nativeExecutionProven());
        assert(input->package().entrypoints.size() == 2u);
        assert(input->package().entrypoints[0].entryPoint == artifact->vertex()->entry());
        assert(input->package().entrypoints[1].entryPoint == artifact->fragment()->entry());
        assert(input->package().entrypoints[0].stage == rhi::ShaderStage::Vertex);
        assert(input->package().entrypoints[1].stage == rhi::ShaderStage::Fragment);
        assert(input->package().reflection.descriptorBindings.size()
            == artifact->module().reflection.bindings.size());
        const auto null = Input::selectTarget(input, rhi::Backend::Null);
        assert(null.validatorOnly && !null.sourceCount && !null.diagnostic);
        const auto vulkan = Input::selectTarget(input, rhi::Backend::Vulkan);
        if (artifact->vertex()->available(shader::ShaderArtifactRole::Spirv)
            && artifact->fragment()->available(shader::ShaderArtifactRole::Spirv)) {
            assert(!vulkan.diagnostic && vulkan.sourceCount == 2u && !vulkan.combinedModule);
            assert(
                vulkan.sources[0] == &artifact->vertex()->bytes(shader::ShaderArtifactRole::Spirv));
            assert(vulkan.sources[1]
                == &artifact->fragment()->bytes(shader::ShaderArtifactRole::Spirv));
        } else
            assert(vulkan.diagnostic && !vulkan.sourceCount);
        for (const auto pair :
            std::array{std::pair{rhi::Backend::Metal, shader::ShaderArtifactRole::Metal},
                std::pair{rhi::Backend::WebGPU, shader::ShaderArtifactRole::Wgsl}}) {
            const auto target = Input::selectTarget(input, pair.first);
            if (!artifact->bytes(pair.second).empty()) {
                assert(target.sources[0] == &artifact->bytes(pair.second) && !target.diagnostic
                    && target.sourceCount == 1u && target.combinedModule && !target.validatorOnly);
                assert(target.sources[0] != &artifact->vertex()->bytes(pair.second));
                assert(target.sources[0] != &artifact->fragment()->bytes(pair.second));
            } else
                assert(!target.sourceCount && target.diagnostic);
        }
        is(Input::selectTarget(input, static_cast<rhi::Backend>(255u)).diagnostic,
            "RHI-ORIGINAL-GRAPHICS-INPUT-BACKEND");
        auto forged = std::shared_ptr<const Input>(quota, input.get());
        auto nonowning = std::shared_ptr<const Input>(std::shared_ptr<const Input>{}, input.get());
        quota->setLimitBytes(0u);
        is(Input::validateOriginal(input), "RHI-ORIGINAL-GRAPHICS-INPUT-QUOTA-LIMIT");
        quota->setLimitBytes(budget.maxCpuMetadataBytes - 1u);
        is(Input::validateOriginal(input), "RHI-ORIGINAL-GRAPHICS-INPUT-QUOTA-LIMIT");
        quota->setLimitBytes(budget.maxCpuMetadataBytes);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, artifact->quota()}, [&]() noexcept {
                for (const auto& owner :
                    std::array{forged, nonowning, std::shared_ptr<const Input>{}})
                    is(Input::selectTarget(owner, rhi::Backend::WebGPU).diagnostic,
                        "RHI-ORIGINAL-GRAPHICS-INPUT-OWNER");
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    assert(!Input::selectTarget(input, backend).diagnostic);
                return Input::validateOriginal(input);
            }));
        const auto originalCeiling = artifact->quota()->limitBytes();
        artifact->quota()->setLimitBytes(originalCeiling - 1u);
        is(Input::validateOriginal(input), "SHADER-MODULE-ORIGINAL-QUOTA-LIMIT");
        artifact->quota()->setLimitBytes(originalCeiling);
        assert(artifact->quota()->liveBytes() == baseline);
        forged.reset();
        nonowning.reset();
        input.reset();
        assert(!quota->liveBytes());
        input = factory();
        assert(input);
        verifyMemoryLowering(input);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_MEMORY_INPUT_OK %llu children2 combined1 independent-quota hot-allocation0 consumption-native-closed\n",
            static_cast<unsigned long long>(peak));
        return input;
    }

    static void verifyMemoryLowering(const std::shared_ptr<const Input>& input)
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto factory = [&] {
            try {
                core::CpuMemoryQuotaScope scope(quota);
                auto result = core::makeUnique<rhi_webgpu::WebGpuBindingSpaceLoweringLoadResult>(
                    rhi_webgpu::loadOriginalWebGpuGraphicsBindingSpaceLowering(input));
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
        auto failed = rhi_webgpu::loadOriginalWebGpuGraphicsBindingSpaceLowering(forged);
        assert(!failed.success && failed.diagnostics == "RHI-ORIGINAL-GRAPHICS-INPUT-OWNER");
        std::printf(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_MEMORY_LOWERING_OK %llu shared-reflection-validator no-file-io no-yaml-parse native-closed\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyFileLowering(
        const std::shared_ptr<const Input>& input, const rhi::ShaderPackageDesc& original)
    {
        auto package = rhi::ShaderPackageStateCopy::copy(original);
        const auto memory = rhi_webgpu::loadOriginalWebGpuGraphicsBindingSpaceLowering(input);
        const auto file = rhi_webgpu::loadWebGpuBindingSpaceLowering(package);
        if (!memory.success || !file.success)
            std::fprintf(stderr,
                "graphics memory/file lowering: %s / %s\n",
                memory.diagnostics.c_str(),
                file.diagnostics.c_str());
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
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_MEMORY_FILE_EQUIVALENCE_OK same-lowering package-disagreement-rejected");
    }
};
} // namespace arp::test
