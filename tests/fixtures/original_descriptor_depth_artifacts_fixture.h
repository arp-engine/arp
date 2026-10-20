#pragma once

#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_single_entry_input.h"
#include "shader/graphics_shader_module_artifact.h"
#include "webgpu_descriptor_array_abi_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalDescriptorDepthArtifactsFixture final {
    static void depth(const shader::ShaderBinding& binding, std::uint32_t stageMask)
    {
        assert(binding.name == "Inputs" && binding.type == shader::BindingType::SampledTexture);
        assert(binding.set == 0u && binding.binding == 0u && binding.arrayCount == 3u);
        assert(binding.depthTexture && binding.stageMask == stageMask);
    }

    template <class Artifact> static void bytes(const Artifact& artifact)
    {
        for (const auto role : {shader::ShaderArtifactRole::Source,
                 shader::ShaderArtifactRole::Spirv,
                 shader::ShaderArtifactRole::Metal,
                 shader::ShaderArtifactRole::Wgsl,
                 shader::ShaderArtifactRole::Reflection})
            assert(!artifact.bytes(role).empty());
        assert(!artifact.nativeExecutionProven());
    }

public:
    static void verify(const std::filesystem::path& computePublication,
        const std::filesystem::path& graphicsPublication)
    {
        WebGpuDescriptorArrayAbiFixture::verifyContract();
        core::pmr::String diagnostic;
        const shader::SingleEntryShaderLoadBudget computeBudget{
            1024u * 1024u, 4u * 1024u * 1024u, 1024u * 1024u, 4u};
        const shader::GraphicsShaderModuleLoadBudget graphicsBudget{
            1024u * 1024u, 16u * 1024u * 1024u, 4u * 1024u * 1024u, 4u, 4u, 256u, 2u};
        auto computeQuota =
            core::makeShared<core::CpuMemoryQuota>(computeBudget.maxCpuMetadataBytes);
        auto graphicsQuota =
            core::makeShared<core::CpuMemoryQuota>(graphicsBudget.maxCpuMetadataBytes);
        {
            auto compute = shader::SingleEntryShaderArtifact::load(
                computePublication, computeBudget, computeQuota, diagnostic);
            if (!compute)
                std::fprintf(stderr, "depth compute publication: %s\n", diagnostic.c_str());
            assert(compute);
            assert(!shader::SingleEntryShaderArtifact::validateOriginal(compute));
            bytes(*compute);
            assert(compute->entry() == "computeMain"
                && compute->stage() == shader::ShaderStage::Compute);
            const auto& reflection = compute->module().reflection;
            assert(reflection.bindings.size() == 2u && reflection.entryPoints.size() == 1u);
            depth(reflection.bindings[0], static_cast<std::uint32_t>(rhi::ShaderStage::Compute));
            const auto& output = reflection.bindings[1];
            assert(output.name == "Output" && output.type == shader::BindingType::StorageBuffer
                && !output.depthTexture);
            assert(output.set == 1u && output.binding == 0u && output.arrayCount == 1u);
            assert(output.stageMask == static_cast<std::uint32_t>(rhi::ShaderStage::Compute));
            const auto& entry = reflection.entryPoints.front();
            assert(entry.computeWorkgroupSizeX == 5u && entry.computeWorkgroupSizeY == 1u
                && entry.computeWorkgroupSizeZ == 1u);
            auto graphics = shader::GraphicsShaderModuleArtifact::load(
                graphicsPublication, graphicsBudget, graphicsQuota, diagnostic);
            if (!graphics)
                std::fprintf(stderr, "depth graphics publication: %s\n", diagnostic.c_str());
            assert(graphics);
            assert(!shader::GraphicsShaderModuleArtifact::validateOriginal(graphics));
            bytes(*graphics);
            bytes(*graphics->vertex());
            bytes(*graphics->fragment());
            assert(graphics->vertex()->entry() == "vertexMain"
                && graphics->fragment()->entry() == "fragmentMain");
            assert(graphics->vertex()->stage() == shader::ShaderStage::Vertex
                && graphics->fragment()->stage() == shader::ShaderStage::Fragment);
            assert(graphics->vertex()->module().reflection.bindings.empty());
            assert(graphics->fragment()->module().reflection.bindings.size() == 1u);
            assert(graphics->module().reflection.bindings.size() == 1u);
            depth(graphics->fragment()->module().reflection.bindings.front(),
                static_cast<std::uint32_t>(rhi::ShaderStage::Fragment));
            depth(graphics->module().reflection.bindings.front(),
                static_cast<std::uint32_t>(rhi::ShaderStage::Fragment));
            auto inputQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
            auto input = rhi::OriginalSingleEntryPipelineInput::prepare(
                compute, {1u, 65536u}, inputQuota, diagnostic);
            assert(input && !rhi::OriginalSingleEntryPipelineInput::validateOriginal(input));
            auto graphicsInputQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
            auto graphicsInput = rhi::OriginalGraphicsPipelineInput::prepare(
                graphics, {1u, 65536u}, graphicsInputQuota, diagnostic);
            assert(graphicsInput
                && !rhi::OriginalGraphicsPipelineInput::validateOriginal(graphicsInput));
            // Memory input construction consumes the complete immutable shader
            // facts, but cannot promote backend array execution capabilities.
            assert(input->package().reflection.descriptorBindings.size() == 2u);
            assert(graphicsInput->package().reflection.descriptorBindings.size() == 1u);
            assert(!input->nativeExecutionProven() && !graphicsInput->nativeExecutionProven());
#if defined(__EMSCRIPTEN__)
            WebGpuDescriptorArrayAbiFixture::verifyActual(input, graphicsInput);
#endif
            input.reset();
            graphicsInput.reset();
            assert(!inputQuota->liveBytes() && !graphicsInputQuota->liveBytes());
        }
        assert(!computeQuota->liveBytes() && !graphicsQuota->liveBytes());
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_DEPTH_ARTIFACTS_OK genuine-publications array3 binding0 depth fragment-or-compute workgroup5x1x1 original-memory-inputs quotas4-zero native-closed");
    }
};
} // namespace arp::test
