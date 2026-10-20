#pragma once

#include "core/runtime/creation_receipt_lifecycle.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/semantics/original_artifact_creation_lowering.h"
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include "rhi/backends/webgpu/webgpu_original_depth_table_expansion_binding_space.h"
#include "rhi/frontend/pipeline_depth_table_lowering_validation.h"
#include "rhi/frontend/pipeline_shader_interface_validation.h"

#include <type_traits>

// region Webgpu artifact creation native methods
namespace arp::rhi_webgpu {
class WebGpuArtifactCreationNativeMethods final {
public:
    enum class Kind { Compute, Graphics };
    using Facts = rhi::semantics::OriginalArtifactCreationLowering::Facts;
    enum class Status { Prepared, Pending, Ready, Rejected, DeviceLost };

    static Status status(const core::runtime::CreationReceiptLifecycle& lifecycle) noexcept
    {
        if (webGpuNativeDeviceLost())
            return Status::DeviceLost;
        using Phase = core::runtime::CreationReceiptLifecycle::Phase;
        switch (lifecycle.phase()) {
        case Phase::Prepared:
            return Status::Prepared;
        case Phase::Pending:
            return Status::Pending;
        case Phase::Ready:
            return Status::Ready;
        case Phase::Rejected:
            return Status::Rejected;
        }
        return Status::Rejected;
    }

    static bool matchesCurrentDevice(void* device) noexcept
    {
#if defined(__EMSCRIPTEN__)
        return device && !webGpuNativeDeviceLost()
            && EM_ASM_INT(
                   {
                       // clang-format off
                       return WebGPU.getJsObject(Number($0)) === Module.preinitializedWebGPUDevice;
                       // clang-format on
                   },
                   device)
            != 0;
#else
        (void)device;
        return false;
#endif
    }

    static Facts query(Kind kind) noexcept
    {
        if (kind != Kind::Compute && kind != Kind::Graphics)
            return {};
#if defined(__EMSCRIPTEN__)
        if (webGpuNativeDeviceLost() || !webGpuNativeDeviceAvailable())
            return {};
        const auto mask = static_cast<std::uint32_t>(EM_ASM_INT(
            {
                // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                const pipeline =
                    d && typeof ($0 === 0 ? d.createComputePipeline : d.createRenderPipeline) === "function";
                return (d ? 1 : 0) | (d && typeof d.createShaderModule === "function" ? 2 : 0) |
                       (pipeline ? 4 : 0) | (d && typeof d.createBindGroupLayout === "function" ? 8 : 0) |
                       (d && typeof d.createPipelineLayout === "function" ? 16 : 0) |
                       (d && typeof d.pushErrorScope === "function" ? 32 : 0) |
                       (d && typeof d.popErrorScope === "function" ? 64 : 0);
                // clang-format on
            },
            kind == Kind::Compute ? 0u : 1u));
        return {bool(mask & 1u),
            bool(mask & 2u),
            bool(mask & 4u),
            bool(mask & 8u),
            bool(mask & 16u),
            bool(mask & 32u),
            bool(mask & 64u)};
#else
        (void)kind;
        return {};
#endif
    }
};

// endregion Webgpu artifact creation native methods

// region Webgpu original depth table expansion pipeline preflight

class WebGpuOriginalDepthTableExpansionPipelinePreflight final {
public:
    using Contract = rhi::OriginalDepthTableExpansionPipelinePreflightContract;
    using Budget = Contract::Budget;
    using Requirements = Contract::Requirements;
    using Space = WebGpuOriginalDepthTableExpansionBindingSpace;

    template <class Input, class Desc>
    static const char* inspect(const std::shared_ptr<const Input>& input,
        const std::shared_ptr<const Space>& space,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const Desc& desc,
        const Budget& budget,
        Requirements& output)
    {
        output = {};
        if (budget.contractVersion != 1u)
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-OPT-IN";
        if (const auto* id = Input::validateTableExpansion(input))
            return id;
        if (const auto* id = Space::validateOriginal(space))
            return id;
        constexpr bool compute = std::is_same_v<Input, rhi::OriginalSingleEntryPipelineInput>;
        if constexpr (compute) {
            static_assert(std::is_same_v<Desc, rhi::ComputePipelineDesc>);
            if (!core::SharedOwnerProjection::sameOwner(input, space->computeInput()))
                return "RHI-ORIGINAL-TABLE-PREFLIGHT-PROJECTION";
        } else {
            static_assert(std::is_same_v<Desc, rhi::RenderPipelineDesc>);
            if (!core::SharedOwnerProjection::sameOwner(input, space->graphicsInput()))
                return "RHI-ORIGINAL-TABLE-PREFLIGHT-PROJECTION";
        }
        if (!layout || !layout->sharedOwnerMatches(layout) || desc.layout != layout.get())
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-LAYOUT";
        if (desc.shaderPackage != &input->package())
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-PACKAGE";
        using Declaration = rhi::PipelineShaderDeclarationContract;
        const auto entryAt = [&](std::size_t i) -> const rhi::ShaderEntrypoint& {
            if constexpr (compute)
                return desc.computeShader;
            else
                return i ? desc.fragmentShader : desc.vertexShader;
        };
        if (const auto* id = Declaration::validateTableExpansion({1u, true},
                input,
                layout->desc(),
                input->package(),
                input->tableExpansion()->entryCount(),
                compute ? Declaration::Kind::ComputePipeline : Declaration::Kind::GraphicsPipeline,
                entryAt))
            return id;
        Requirements r;
        r.compute = compute;
        r.groups = space->physicalGroupCount();
        r.bindings = space->entryCount();
        if constexpr (compute) {
            const auto state = rhi::validateComputePipelineState(desc);
            if (!state.success)
                return "RHI-ORIGINAL-TABLE-PREFLIGHT-STATE";
            r.workgroupX = desc.computeShader.computeWorkgroupSizeX;
            r.workgroupY = desc.computeShader.computeWorkgroupSizeY;
            r.workgroupZ = desc.computeShader.computeWorkgroupSizeZ;
        } else {
            if (const auto* id =
                    rhi::OriginalGraphicsArtifactConsumptionContract::validateProfile(desc))
                return id;
            const auto vertexVersion = input->tableExpansion()->vertexInputContractVersion();
            if (const auto* id = Contract::validateVertexInputs(desc, vertexVersion))
                return id;
            if (!desc.tessellationControlShader.entryPoint.empty()
                || !desc.tessellationEvaluationShader.entryPoint.empty()
                || desc.patchControlPointCount)
                return "RHI-ORIGINAL-TABLE-PREFLIGHT-VERTEX-PROFILE";
            r.vertexInputContractVersion = vertexVersion;
            if (vertexVersion) {
                r.vertexBuffers = 2u;
                r.vertexAttributes = 3u;
                r.vertexBufferArrayStride = 24u;
                if (r.vertexBuffers > budget.maxVertexBuffers
                    || r.vertexAttributes > budget.maxVertexAttributes
                    || r.vertexBufferArrayStride > budget.maxVertexBufferArrayStride)
                    return "RHI-ORIGINAL-TABLE-PREFLIGHT-VERTEX-BUDGET";
            }
            // Bound the shared validator's scratch before it copies formats.
            if (desc.colorTargets.size() > budget.maxColorTargets
                || rhi::effectiveColorTargetCount(desc) > budget.maxColorTargets)
                return "RHI-ORIGINAL-TABLE-PREFLIGHT-BUDGET";
            const auto state = rhi::validateRenderPipelineState(desc);
            if (!state.success)
                return "RHI-ORIGINAL-TABLE-PREFLIGHT-STATE";
            r.colorTargets = state.colorTargetCount;
        }
        for (std::uint32_t group = 0u; group < r.groups; ++group) {
            std::uint32_t count = 0u;
            for (std::uint32_t i = 0u; i < r.bindings; ++i)
                if (space->entry(i).group == group)
                    ++count;
            r.bindingsPerGroup = std::max(r.bindingsPerGroup, count);
        }
        for (const auto stage :
            {rhi::ShaderStage::Compute, rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment}) {
            std::uint32_t sampled = 0u, storage = 0u;
            for (std::uint32_t i = 0u; i < r.bindings; ++i) {
                const auto& e = space->entry(i);
                if (e.physicalCardinality != 1u || e.binding == UINT32_MAX || e.group >= r.groups)
                    return "RHI-ORIGINAL-TABLE-PREFLIGHT-MAPPING";
                if ((e.logical.stages & stage) == rhi::ShaderStage::None)
                    continue;
                if (e.logical.type == rhi::DescriptorType::SampledTexture)
                    ++sampled;
                else if (e.logical.type == rhi::DescriptorType::StorageBuffer)
                    ++storage;
                else
                    return "RHI-ORIGINAL-TABLE-PREFLIGHT-MAPPING";
            }
            r.sampledTexturesPerStage = std::max(r.sampledTexturesPerStage, sampled);
            r.storageBuffersPerStage = std::max(r.storageBuffersPerStage, storage);
        }
        const auto& artifact = *input->tableExpansion();
        r.shaderStages = r.shaderModules = static_cast<std::uint32_t>(artifact.entryCount());
        for (std::size_t i = 0u; i < artifact.entryCount(); ++i) {
            const auto bytes =
                artifact.entry(i)
                    .files.at(static_cast<std::size_t>(shader::ShaderArtifactRole::Wgsl))
                    .bytes.size();
            if (!bytes || bytes > UINT64_MAX - r.shaderSourceBytes)
                return "RHI-ORIGINAL-TABLE-PREFLIGHT-SOURCE";
            r.shaderSourceBytes += bytes;
        }
        if (const auto* id = Contract::validateBudget(budget, r))
            return id;
        output = r;
        return nullptr;
    }

    // Only actual live-device queries enter the factory-facing validation path.
    static Contract::Decision query(const Requirements& r) noexcept
    {
        const auto facts = WebGpuArtifactCreationNativeMethods::query(r.compute
                ? WebGpuArtifactCreationNativeMethods::Kind::Compute
                : WebGpuArtifactCreationNativeMethods::Kind::Graphics);
        return Contract::lowering(rhi::Backend::WebGPU, r, facts, webGpuNativeDeviceLimits());
    }
};
} // namespace arp::rhi_webgpu

// endregion Webgpu original depth table expansion pipeline preflight
