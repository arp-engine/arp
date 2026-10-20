#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_completed_depth_publication_fixture.h"
#include "original_depth_table_expansion_creation_actor_fixture.h"
#include "original_depth_table_expansion_creation_source_fixture.h"
#include "original_depth_table_expansion_native_emission_fixture.h"
#include "original_depth_table_expansion_pipeline_definition_fixture.h"
#include "original_depth_table_registration_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"

#include <cstdio>
#include <type_traits>

namespace arp::test {
class OriginalDepthTableExpansionPipelineFixture final {
    using Preflight = rhi_webgpu::WebGpuOriginalDepthTableExpansionPipelinePreflight;
    using Contract = Preflight::Contract;
    using Space = Preflight::Space;

    static void is(const char* actual, core::StringView expected)
    {
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static inline bool nativeQueryRequested = false;
    static inline bool nativeEmissionRequested = false;
    static inline bool nativeCreationRequested = false;
    static inline bool nativeRegistrationRequested = false;
    static inline bool nativePublicationRequested = false;
    static inline bool nativeExecutionRequested = false;

    template <class Input>
    static void verify(const std::shared_ptr<const Input>& input,
        const std::shared_ptr<const Space>& space,
        const rhi::PipelineLayoutDesc& authoredLayout)
    {
        constexpr bool compute = std::is_same_v<Input, rhi::OriginalSingleEntryPipelineInput>;
        using Desc = std::conditional_t<compute, rhi::ComputePipelineDesc, rhi::RenderPipelineDesc>;
        using Factory = std::conditional_t<compute,
            rhi_webgpu::WebGpuOriginalArtifactComputeFactory,
            rhi_webgpu::WebGpuOriginalArtifactGraphicsFactory>;
        auto layout = core::shareUnique(rhi_webgpu::createWebGpuRhiPipelineLayout(authoredLayout));
        assert(layout);
        Desc desc;
        desc.layout = layout.get();
        desc.shaderPackage = &input->package();
        if constexpr (compute)
            desc.computeShader = input->package().entrypoints[0];
        else {
            desc.vertexShader = input->package().entrypoints[0];
            desc.fragmentShader = input->package().entrypoints[1];
            desc.colorTargets.push_back({rhi::TextureFormat::Rgba16Float});
            if (input->tableExpansion()->vertexInputContractVersion())
                desc.vertexBuffers = {{0u,
                                          8u,
                                          rhi::VertexInputRate::Vertex,
                                          {{0u, 0u, rhi::VertexFormat::Float32x2}}},
                    {1u,
                        24u,
                        rhi::VertexInputRate::Instance,
                        {{1u, 0u, rhi::VertexFormat::Float32x2},
                            {2u, 8u, rhi::VertexFormat::Float32x4}}}};
        }
        std::uint64_t bytes = 0u;
        for (std::size_t i = 0u; i < input->tableExpansion()->entryCount(); ++i)
            bytes += input->tableExpansion()
                         ->entry(i)
                         .files[static_cast<std::size_t>(shader::ShaderArtifactRole::Wgsl)]
                         .bytes.size();
        Contract::Budget budget{1u,
            true,
            compute ? 2u : 1u,
            compute ? 4u : 3u,
            3u,
            3u,
            compute ? 1u : 0u,
            compute ? 1u : 2u,
            compute ? 1u : 2u,
            compute ? 0u : 1u,
            bytes};
        const bool geometry = input->tableExpansion()->vertexInputContractVersion() == 1u;
        if (geometry) {
            budget.maxVertexBuffers = 2u;
            budget.maxVertexAttributes = 3u;
            budget.maxVertexBufferArrayStride = 24u;
        }
        Contract::Requirements required;
        auto inspect = [&](const auto& candidate, const auto& authored) {
            return Preflight::inspect(input, space, layout, candidate, authored, required);
        };
        assert(!inspect(desc, budget));
        assert(required.groups == budget.maxGroups && required.bindings == budget.maxBindings
            && required.bindingsPerGroup == 3u && required.sampledTexturesPerStage == 3u
            && required.storageBuffersPerStage == budget.maxStorageBuffersPerStage
            && required.shaderModules == budget.maxShaderModules
            && required.shaderSourceBytes == bytes);
        const auto genuine = required;
        assert(required.vertexInputContractVersion == (geometry ? 1u : 0u)
            && required.vertexBuffers == (geometry ? 2u : 0u)
            && required.vertexAttributes == (geometry ? 3u : 0u)
            && required.vertexBufferArrayStride == (geometry ? 24u : 0u));
        auto failed = [&](const auto& candidate, const auto& authored) {
            required = genuine;
            assert(inspect(candidate, authored));
            assert(!required.groups && !required.bindings && !required.shaderSourceBytes);
        };
        for (std::uint32_t mutate = 0u; mutate < 11u; ++mutate) {
            auto b = budget;
            if (mutate == 0u)
                b.contractVersion = 0u;
            if (mutate == 1u)
                b.prototypeOptIn = false;
            if (mutate == 2u)
                --b.maxGroups;
            if (mutate == 3u)
                --b.maxBindings;
            if (mutate == 4u)
                --b.maxBindingsPerGroup;
            if (mutate == 5u)
                --b.maxSampledTexturesPerStage;
            if (mutate == 6u) {
                if constexpr (compute)
                    --b.maxStorageBuffersPerStage;
                else
                    continue;
            }
            if (mutate == 7u)
                --b.maxShaderStages;
            if (mutate == 8u)
                --b.maxShaderModules;
            if (mutate == 9u) {
                if constexpr (!compute)
                    --b.maxColorTargets;
                else
                    continue;
            }
            if (mutate == 10u)
                --b.maxShaderSourceBytes;
            failed(desc, b);
        }
        for (std::uint32_t mutate = 0u; mutate < 3u; ++mutate) {
            Desc changed;
            changed = desc;
            if (mutate == 0u)
                changed.layout = nullptr;
            if (mutate == 1u)
                changed.shaderPackage = nullptr;
            if (mutate == 2u) {
                if constexpr (compute)
                    changed.computeShader.entryPoint = "wrong";
                else
                    changed.fragmentShader.entryPoint = "wrong";
            }
            failed(changed, budget);
        }
        if constexpr (compute) {
            Desc changed;
            changed = desc;
            ++changed.computeShader.computeWorkgroupSizeZ;
            failed(changed, budget);
        } else
            for (std::uint32_t mutate = 0u; mutate < 7u; ++mutate) {
                Desc changed;
                changed = desc;
                if (mutate == 0u)
                    changed.topology = rhi::PrimitiveTopology::LineList;
                if (mutate == 1u)
                    changed.vertexBuffers.resize(1u);
                if (mutate == 2u)
                    changed.multisample.sampleCount = 2u;
                if (mutate == 3u)
                    changed.tessellationControlShader.entryPoint = "extra";
                if (mutate == 4u)
                    changed.patchControlPointCount = 1u;
                if (mutate == 5u)
                    changed.colorTargets[0].writeMask = rhi::ColorWriteMask::None;
                if (mutate == 6u)
                    changed.depthStencil.depthBiasEnabled = true;
                failed(changed, budget);
            }
        if constexpr (!compute) {
            for (const auto version : {0u, 1u, 2u}) {
                const auto* id = Contract::validateVertexInputs(desc, version);
                assert((id == nullptr) == (version == (geometry ? 1u : 0u)));
            }
            if (geometry) {
                for (const auto field : {&Contract::Budget::maxVertexBuffers,
                         &Contract::Budget::maxVertexAttributes,
                         &Contract::Budget::maxVertexBufferArrayStride}) {
                    auto b = budget;
                    --(b.*field);
                    failed(desc, b);
                }
                for (std::uint32_t mutate = 0u; mutate < 9u; ++mutate) {
                    auto changed = rhi::RenderPipelineStateCopy::copy(desc);
                    if (mutate == 0u)
                        changed.vertexBuffers.clear();
                    if (mutate == 1u)
                        ++changed.vertexBuffers[0].strideBytes;
                    if (mutate == 2u)
                        ++changed.vertexBuffers[1].slot;
                    if (mutate == 3u)
                        changed.vertexBuffers[1].inputRate = rhi::VertexInputRate::Vertex;
                    if (mutate == 4u)
                        ++changed.vertexBuffers[1].attributes[0].shaderLocation;
                    if (mutate == 5u)
                        --changed.vertexBuffers[1].attributes[1].offsetBytes;
                    if (mutate == 6u)
                        changed.vertexBuffers[1].attributes[1].format =
                            rhi::VertexFormat::Float32x3;
                    if (mutate == 7u)
                        changed.vertexBuffers[1].attributes.pop_back();
                    if (mutate == 8u)
                        changed.vertexBuffers[0].attributes.push_back(
                            changed.vertexBuffers[0].attributes.front());
                    failed(changed, budget);
                }
            }
        }
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        typename Input::ExpansionBudget otherBudget{3u, 65536u, 1u, true};
        auto otherQuota = core::makeShared<core::CpuMemoryQuota>(otherBudget.maxCpuMetadataBytes);
        auto other = Input::prepareTableExpansion(
            input->tableExpansion(), otherBudget, otherQuota, diagnostic);
        assert(other);
        is(Preflight::inspect(other, space, layout, desc, budget, required),
            "RHI-ORIGINAL-TABLE-PREFLIGHT-PROJECTION");
        auto forgedSpace = std::shared_ptr<const Space>(space->quota(), space.get());
        is(Preflight::inspect(input, forgedSpace, layout, desc, budget, required),
            "RHI-WEBGPU-TABLE-SPACE-OWNER");
        auto forgedLayout = std::shared_ptr<rhi::PipelineLayout>(space->quota(), layout.get());
        is(Preflight::inspect(input, space, forgedLayout, desc, budget, required),
            "RHI-ORIGINAL-TABLE-PREFLIGHT-LAYOUT");
        assert(!inspect(desc, budget));
        assert(!Contract::validateRequirements(required));
        rhi_webgpu::WebGpuNativeDeviceLimits limits;
        limits.maxBindGroups = required.groups;
        limits.maxDescriptorBindings = required.bindingsPerGroup;
        limits.maxSampledTexturesPerShaderStage = required.sampledTexturesPerStage;
        limits.maxStorageBuffersPerShaderStage = required.storageBuffersPerStage;
        limits.maxColorAttachments = required.colorTargets;
        limits.maxComputeWorkgroupSizeX = required.workgroupX;
        limits.maxComputeWorkgroupSizeY = required.workgroupY;
        limits.maxComputeWorkgroupSizeZ = required.workgroupZ;
        limits.maxComputeInvocationsPerWorkgroup =
            required.workgroupX * required.workgroupY * required.workgroupZ;
        limits.maxVertexBuffers = required.vertexBuffers;
        limits.maxVertexAttributes = required.vertexAttributes;
        limits.maxVertexBufferArrayStride = required.vertexBufferArrayStride;
        assert(!Contract::validateLimits(required, limits));
        const std::
            array<std::pair<std::uint32_t rhi_webgpu::WebGpuNativeDeviceLimits::*, const char*>, 9>
                usedLimits{{{&rhi_webgpu::WebGpuNativeDeviceLimits::maxBindGroups,
                                "RHI-ORIGINAL-TABLE-PREFLIGHT-GROUP-LIMIT"},
                    {&rhi_webgpu::WebGpuNativeDeviceLimits::maxDescriptorBindings,
                        "RHI-ORIGINAL-TABLE-PREFLIGHT-BINDING-LIMIT"},
                    {&rhi_webgpu::WebGpuNativeDeviceLimits::maxSampledTexturesPerShaderStage,
                        "RHI-ORIGINAL-TABLE-PREFLIGHT-SAMPLED-LIMIT"},
                    {&rhi_webgpu::WebGpuNativeDeviceLimits::maxStorageBuffersPerShaderStage,
                        "RHI-ORIGINAL-TABLE-PREFLIGHT-STORAGE-LIMIT"},
                    {&rhi_webgpu::WebGpuNativeDeviceLimits::maxColorAttachments,
                        "RHI-ORIGINAL-TABLE-PREFLIGHT-COLOR-LIMIT"},
                    {&rhi_webgpu::WebGpuNativeDeviceLimits::maxComputeWorkgroupSizeX,
                        "RHI-ORIGINAL-TABLE-PREFLIGHT-WORKGROUP-LIMIT"},
                    {&rhi_webgpu::WebGpuNativeDeviceLimits::maxComputeWorkgroupSizeY,
                        "RHI-ORIGINAL-TABLE-PREFLIGHT-WORKGROUP-LIMIT"},
                    {&rhi_webgpu::WebGpuNativeDeviceLimits::maxComputeWorkgroupSizeZ,
                        "RHI-ORIGINAL-TABLE-PREFLIGHT-WORKGROUP-LIMIT"},
                    {&rhi_webgpu::WebGpuNativeDeviceLimits::maxComputeInvocationsPerWorkgroup,
                        "RHI-ORIGINAL-TABLE-PREFLIGHT-INVOCATION-LIMIT"}}};
        for (const auto& [field, id] : usedLimits)
            if (limits.*field)
                for (const auto value : {0u, limits.*field - 1u}) {
                    auto changed = limits;
                    changed.*field = value;
                    is(Contract::validateLimits(required, changed), id);
                }
        if (geometry)
            for (const auto field : {&rhi_webgpu::WebGpuNativeDeviceLimits::maxVertexBuffers,
                     &rhi_webgpu::WebGpuNativeDeviceLimits::maxVertexAttributes,
                     &rhi_webgpu::WebGpuNativeDeviceLimits::maxVertexBufferArrayStride})
                for (const auto value : {0u, limits.*field - 1u}) {
                    auto changed = limits;
                    changed.*field = value;
                    is(Contract::validateLimits(required, changed),
                        "RHI-ORIGINAL-TABLE-PREFLIGHT-VERTEX-LIMIT");
                }
        auto overflow = required;
        if constexpr (compute) {
            overflow.workgroupX = overflow.workgroupY = overflow.workgroupZ = UINT32_MAX;
            auto large = limits;
            large.maxComputeWorkgroupSizeX = large.maxComputeWorkgroupSizeY =
                large.maxComputeWorkgroupSizeZ = UINT32_MAX;
            large.maxComputeInvocationsPerWorkgroup = UINT32_MAX;
            is(Contract::validateLimits(overflow, large),
                "RHI-ORIGINAL-TABLE-PREFLIGHT-INVOCATION-LIMIT");
        }
        using Facts = rhi::semantics::OriginalArtifactCreationLowering::Facts;
        const Facts methods{true, true, true, true, true, true, true};
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Contract::lowering(backend, required, methods, limits);
            assert(!decision.creationAvailable && !decision.executionAvailable);
            assert(decision.preflightEligible == (backend == rhi::Backend::WebGPU));
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
        }
        for (const auto field : {&Facts::device,
                 &Facts::shader,
                 &Facts::pipeline,
                 &Facts::bindGroupLayout,
                 &Facts::pipelineLayout,
                 &Facts::pushScope,
                 &Facts::popScope}) {
            auto missing = methods;
            missing.*field = false;
            const auto decision =
                Contract::lowering(rhi::Backend::WebGPU, required, missing, limits);
            assert(decision.diagnostic && !decision.preflightEligible && !decision.creationAvailable
                && !decision.executionAvailable);
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!inspect(desc, budget));
        std::pmr::set_default_resource(previous);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{space->quota(), input->quota(), input->tableExpansion()->quota()},
            [&]() noexcept {
                return Contract::validateLimits(required, limits);
            }));
        const auto* native =
            Factory::validateTableExpansionDefinition(input, space, layout, desc, budget);
        if (nativeQueryRequested) {
            if (native)
                std::fprintf(stderr, "actual native preflight %s\n", native);
            assert(!native);
            const auto decision = Preflight::query(required);
            assert(decision.preflightEligible && !decision.creationAvailable
                && !decision.executionAvailable);
#if defined(__EMSCRIPTEN__)
            const auto actual = rhi_webgpu::webGpuNativeDeviceLimits();
            assert(actual.maxComputeWorkgroupSizeZ == static_cast<std::uint32_t>(EM_ASM_INT({
                // clang-format off
                    return Module.preinitializedWebGPUDevice.limits.maxComputeWorkgroupSizeZ;
                // clang-format on
            })));
#endif
        } else
            is(native, "RHI-ORIGINAL-TABLE-PREFLIGHT-DEVICE");
        OriginalDepthTableRegistrationFixture::verifyWriteIntent(authoredLayout);
        OriginalDepthTableExpansionPipelineDefinitionFixture::verify<Factory>(
            space, layout, desc, budget, nativeQueryRequested);
        OriginalDepthTableExpansionCreationSourceFixture::verify(space, layout, desc, budget);
        if (!nativeCreationRequested)
            OriginalDepthTableExpansionCreationActorFixture::verify(
                space, layout, desc, budget, false);
        OriginalDepthTableExpansionNativeEmissionFixture::verify<Factory>(
            space, layout, desc, budget, nativeEmissionRequested);
        if constexpr (!compute) {
            if (nativeExecutionRequested
                && !OriginalDepthTableContentPlanFixture::mixedIssuersRequested)
                OriginalCompletedDepthPublicationFixture::verify();
        }
        if (OriginalDepthTableContentPlanFixture::geometryCompositionRequested) {
            assert(!compute && nativeExecutionRequested);
            OriginalDepthTableRegistrationFixture::verifyNative(
                space, authoredLayout, desc, budget, false, true, true, false, true);
        } else {
            if (nativeRegistrationRequested)
                OriginalDepthTableRegistrationFixture::verifyNative(
                    space, authoredLayout, desc, budget, false, nativePublicationRequested);
            if (nativeRegistrationRequested)
                OriginalDepthTableRegistrationFixture::verifyNative(
                    space, authoredLayout, desc, budget, true, nativePublicationRequested);
            if constexpr (!compute) {
                if (nativeExecutionRequested
                    && !OriginalDepthTableContentPlanFixture::mixedIssuersRequested)
                    OriginalDepthTableRegistrationFixture::verifyNative(
                        space, authoredLayout, desc, budget, false, true, true, true);
                if (nativeExecutionRequested)
                    OriginalDepthTableRegistrationFixture::verifyNative(space,
                        authoredLayout,
                        desc,
                        budget,
                        OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested,
                        true,
                        true,
                        false,
                        true);
                // The publication smoke owns the complete legacy cold/loss case.
                // Execution smoke retains real Execution2 and mixed readback custody
                // instead of rerunning that separate case inside its fixed deadline.
                if (nativePublicationRequested && !nativeExecutionRequested) {
                    OriginalDepthTableRegistrationFixture::verifyNative(
                        space, authoredLayout, desc, budget, true, true, true);
                }
            }
        }
        if (nativeCreationRequested
            && !(OriginalDepthTableContentPlanFixture::geometryCompositionRequested
                && OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested))
            OriginalDepthTableExpansionCreationActorFixture::verify(
                space, layout, desc, budget, true);
        if (nativeRegistrationRequested
            && !OriginalDepthTableContentPlanFixture::geometryCompositionRequested)
            OriginalDepthTableRegistrationFixture::verifyActualLoss<Desc>();
        if constexpr (!compute) {
            if (nativePublicationRequested && !nativeExecutionRequested)
                OriginalDepthTableRegistrationFixture::verifyActualLoss<Desc>(true);
            if (OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested
                && !OriginalDepthTableContentPlanFixture::geometryCompositionRequested)
                OriginalDepthTableRegistrationFixture::verifyActualLoss<Desc>(true);
        }
        std::printf(
            "ORIGINAL_TABLE_EXPANSION_PREFLIGHT_OK %s groups%u bindings%u per-group%u sampled-stage%u storage-stage%u modules%u source%llu exact-less zero-limits distinct-owner full-state atomic-failure native-query%u creation-closed\n",
            compute ? "Compute" : "Graphics",
            required.groups,
            required.bindings,
            required.bindingsPerGroup,
            required.sampledTexturesPerStage,
            required.storageBuffersPerStage,
            required.shaderModules,
            static_cast<unsigned long long>(required.shaderSourceBytes),
            nativeQueryRequested ? 1u : 0u);
    }
};
} // namespace arp::test
