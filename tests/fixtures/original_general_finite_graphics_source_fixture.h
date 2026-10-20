#pragma once
#include "original_accepted_graphics_pipeline_factory_fixture.h"
#include "original_general_shader_resources_projection_fixture.h"
#include "original_target_memory_fixture.h"
#include "webgpu_ordinary_pipeline_fixture.h"

namespace arp::test {
class OriginalGeneralFiniteGraphicsSourceFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    template <class Factory>
    static void verifyActual(Factory&& create,
        bool readOnly,
        bool whole,
        bool renderProjection = false,
        bool stateStream = false,
        bool usage = false,
        bool contentSources = false,
        bool initializedStream = false,
        bool mixedRoot = false,
        bool shaderBufferContent = false,
        bool nativeCustody = false,
        bool dependencyFreeze = false,
        bool nativePayload = false,
        bool nativeCold = false,
        bool nativeCommand = false,
        bool nativeCallback = false,
        bool nativeIssuer = false,
        bool nativeRecording = false)
    {
        assert(!stateStream || renderProjection);
        assert(!usage || stateStream);
        assert(
            !contentSources || initializedStream || (!renderProjection && !stateStream && !usage));
        assert(!initializedStream || (contentSources && renderProjection && stateStream && usage));
        assert(!mixedRoot || initializedStream);
        assert(!shaderBufferContent || mixedRoot);
        assert(!nativeCustody || shaderBufferContent);
        assert(!dependencyFreeze || nativeCustody);
        assert(!nativePayload || dependencyFreeze);
        assert(!nativeCold || nativePayload);
        assert(!nativeCommand || nativeCold);
        assert(!nativeCallback || nativeCommand);
        assert(!nativeIssuer || nativeCallback);
        assert(!nativeRecording || nativeIssuer);
        constexpr std::uint64_t bytes = 65536u;
        const auto load = OriginalTargetMemoryFixture::moduleBudget();
        auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(load.maxCpuMetadataBytes);
        auto inputQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        core::pmr::String diagnostic;
        const char* publication = readOnly
            ? "/ordinary-pipeline-fixture/original_general_graphics_readonly_shader_resources/original_general_graphics_readonly_shader_resources.graphics.module.yaml"
            : "/ordinary-pipeline-fixture/original_general_graphics_shader_resources/original_general_graphics_shader_resources.graphics.module.yaml";
        auto artifact = OriginalTargetMemoryFixture::loadModuleWithTargetMemory(
            publication, load, artifactQuota, diagnostic);
        assert(artifact && artifact->vertex()->module().reflection.bindings.empty());
        auto input = rhi::OriginalGraphicsPipelineInput::prepare(
            artifact, {1u, bytes}, inputQuota, diagnostic);
        assert(input && !input->nativeExecutionProven());
        rhi::PipelineLayoutDesc layout;
        rhi::DescriptorLayoutDesc set;
        set.name = "original finite Graphics scalar resources";
        for (std::uint32_t index = 0u; index < 4u; ++index) {
            rhi::DescriptorBindingDesc binding;
            binding.binding = index;
            binding.stages = rhi::ShaderStage::Fragment;
            binding.type = index == 0u ? (readOnly ? rhi::DescriptorType::ReadOnlyStorageBuffer
                                                   : rhi::DescriptorType::UniformBuffer)
                : index == 1u          ? rhi::DescriptorType::StorageTexture
                : index == 2u          ? rhi::DescriptorType::SampledTexture
                                       : rhi::DescriptorType::Sampler;
            binding.dynamicOffset = index == 0u && !whole;
            if (index == 1u) {
                binding.storageTextureFormat = rhi::TextureFormat::Rgba16Float;
                binding.storageTextureAccess = rhi::StorageTextureAccess::WriteOnly;
            }
            if (index == 1u || index == 2u) {
                binding.textureViewDimensionConstrained = true;
                binding.textureViewDimension = rhi::TextureViewDimension::Texture2D;
            }
            set.bindings.push_back(binding);
        }
        layout.descriptorSets.push_back({0u, set.name});
        layout.descriptorLayouts.push_back(std::move(set));
        assert(
            rhi::validatePipelineLayoutAgainstShaderReflection(layout, input->package().reflection)
                .success);
        auto state = WebGpuOrdinaryPipelineFixture::graphicsState();
        state.colorFormat = rhi::TextureFormat::Rgba16Float;
        state.shaderPackage = &input->package();
        state.vertexShader = input->package().entrypoints[0];
        state.fragmentShader = input->package().entrypoints[1];
        // Source custody requires one buffer, storage and sampled view/parent
        // pairs, one sampler and one descriptor set. No render attachment or
        // draw/queue/output authority is borrowed by this source-only stage.
        const rhi::OriginalDeviceNativeLossObserverRequest observedLoss{1u, true, 3u, 4u, bytes};
        const bool outputReadback = EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-output-readback");
            // clang-format on
        });
        assert(!outputReadback || nativeRecording);
        auto context = OriginalAcceptedGraphicsPipelineFactoryFixture::prepareContext(
            std::forward<Factory>(create),
            input,
            layout,
            state,
            (contentSources ? 2u : 1u) + (outputReadback ? 1u : 0u),
            false,
            1u,
            rhi::OriginalTypedNativeLossJoinContract::registrationVersion,
            nativeCallback ? &observedLoss : nullptr,
            (renderProjection || contentSources) ? 3u : 2u,
            1u);
        context->ready();
        if (renderProjection) {
            OriginalGeneralFiniteGraphicsInputsFixture::verifyContract();
            if (stateStream)
                OriginalGeneralFiniteGraphicsStateStreamFixture::verifyContract();
            if (usage)
                OriginalGeneralFiniteGraphicsUsageFixture::verifyContract();
            OriginalGeneralShaderResourcesProjectionFixture::verifyFiniteBindings(*context,
                true,
                whole,
                false,
                true,
                stateStream,
                usage,
                contentSources,
                initializedStream,
                mixedRoot,
                shaderBufferContent,
                nativeCustody,
                dependencyFreeze,
                nativePayload,
                nativeCold,
                nativeCommand,
                nativeCallback,
                nativeIssuer,
                nativeRecording);
            if (nativeRecording && EM_ASM_INT({
                    // clang-format off
                    return new URLSearchParams(location.search).has("general-graphics-native-submission");
                    // clang-format on
                }))
                std::puts(outputReadback
                        ? "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION_PROJECTION_RETIRE_OK original-attachment graph-quotas-zero readback3"
                        : "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION_PROJECTION_RETIRE_OK original-attachment graph-quotas-zero no-readback");
            else if (nativeRecording)
                std::puts(
                    "BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING_PROJECTION_RETIRE_OK original-attachment graph-quotas-zero no-submit-or-output");
            else
                std::puts(
                    "BINDLESS_RHI_GENERAL_GRAPHICS_RENDER_PROJECTION_RETIRE_OK original-attachment graph-quotas-zero no-native-record-submit");
        } else
            OriginalGeneralShaderResourcesProjectionFixture::verifyFiniteBindings(
                *context, true, whole, false, false, false, false, contentSources);
        context->retire();
        context.reset();
        input.reset();
        artifact.reset();
        assert(!inputQuota->liveBytes() && !artifactQuota->liveBytes());
        std::puts(outputReadback
                ? "BINDLESS_RHI_GENERAL_GRAPHICS_SOURCE5_INPUT_RETIRE_OK original-module-child-custody artifact-input-quotas-zero private-output3-readback"
                : "BINDLESS_RHI_GENERAL_GRAPHICS_SOURCE5_INPUT_RETIRE_OK original-module-child-custody artifact-input-quotas-zero draw-readback-unvalidated");
    }
#endif
};
} // namespace arp::test
