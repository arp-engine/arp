#pragma once

#include "rhi/backends/metal/metal_native_descriptors.h"
#include "rhi/backends/metal/metal_native_pipelines.h"
#include "rhi/frontend/bindless_shader_package.h"

// region Metal prepared binding validation
namespace arp::rhi_metal {

struct MetalRhiPreparedBindingLocation {
    bool active = false;
    bool array = false;
    std::uint32_t slot = UINT32_MAX;
    std::uint32_t argumentIndex = UINT32_MAX;
};

enum class MetalRhiPreparedMappingDiagnostic {
    Binding,
    Type,
    Array,
    Direct,
    Mapping,
    Stage,
    Range,
    Duplicate,
    Overlap,
    Missing
};

// One function's physical namespaces. No merged-stage mapping is accepted.
class MetalRhiPreparedBindingLayoutValidator final {
public:
    MetalRhiPreparedBindingLayoutValidator(
        const core::pmr::Vector<MetalRhiNativeBindingSlot>& direct,
        const core::pmr::Vector<MetalRhiNativeArgumentBufferSlot>& arrays,
        const rhi::semantics::MetalPhysicalBindingStages& physicalStages,
        rhi::ShaderStage stage,
        std::uint32_t pushSlot,
        std::uint32_t reservedBuffers = 0u)
        : direct_(direct), arrays_(arrays), physicalStages_(physicalStages), stage_(stage),
          pushSlot_(pushSlot), reservedBuffers_(reservedBuffers)
    {}

    rhi::ValidationResult resolve(std::uint32_t set,
        const MetalRhiNativeArgumentBindingPayload& binding,
        MetalRhiPreparedBindingLocation& out) const
    {
        using D = MetalRhiPreparedMappingDiagnostic;
        out = {};
        if (!binding.arrayCount || binding.arrayElement >= binding.arrayCount
            || binding.dynamicOffsetIndex != UINT32_MAX
            || binding.type == rhi::DescriptorType::InputAttachment)
            return fail(D::Binding);
        const auto expected = binding.type == rhi::DescriptorType::Sampler
            ? MetalRhiArgumentKind::Sampler
            : binding.type == rhi::DescriptorType::SampledTexture
                || binding.type == rhi::DescriptorType::StorageTexture
            ? MetalRhiArgumentKind::Texture
            : MetalRhiArgumentKind::Buffer;
        const auto resource = expected == MetalRhiArgumentKind::Buffer
            ? rhi::DescriptorResourceKind::Buffer
            : expected == MetalRhiArgumentKind::Texture ? rhi::DescriptorResourceKind::TextureView
                                                        : rhi::DescriptorResourceKind::Sampler;
        if (binding.argumentKind != expected || binding.resourceKind != resource)
            return fail(D::Type);
        unsigned matches = 0u, arrayEntries = 0u, directEntries = 0u;
        for (const auto& map : arrays_) {
            if (map.set != set || map.binding != binding.binding)
                continue;
            ++arrayEntries;
            if (!map.active)
                continue;
            ++matches;
            if (map.type != binding.type || map.capacity != binding.arrayCount)
                return fail(D::Array);
            out = {true, true, map.argumentBufferSlot, map.argumentIndex};
        }
        for (const auto& map : direct_) {
            if (map.set != set || map.binding != binding.binding)
                continue;
            ++directEntries;
            if (!map.active)
                continue;
            ++matches;
            if (map.type != binding.type || binding.arrayCount != 1u)
                return fail(D::Direct);
            out = {true, false, map.metalSlot, UINT32_MAX};
        }
        if (arrayEntries > 1u || directEntries > 1u || matches > 1u
            || (!arrayEntries && !directEntries))
            return fail(D::Mapping);
        if (!out.active)
            return {true, {}};
        if ((binding.stages & stage_) == rhi::ShaderStage::None
            || !rhi::semantics::validMetalDirectBindingSlot(
                out.array ? rhi::DescriptorType::UniformBuffer : binding.type, out.slot)
            || (physicalStages_.stages(
                    out.array ? rhi::DescriptorType::UniformBuffer : binding.type, out.slot)
                   & stage_)
                == rhi::ShaderStage::None)
            return fail(D::Stage);
        if (out.array
            && (out.argumentIndex == UINT32_MAX
                || binding.arrayCount - 1u > UINT32_MAX - out.argumentIndex))
            return fail(D::Range);
        return {true, {}};
    }

    rhi::ValidationResult validate(
        const MetalRhiNativeDescriptorGroupPayload& descriptors, std::uint32_t set) const
    {
        using D = MetalRhiPreparedMappingDiagnostic;
        if (stage_ != rhi::ShaderStage::Compute && stage_ != rhi::ShaderStage::Vertex
            && stage_ != rhi::ShaderStage::Fragment)
            return fail(D::Stage);
        std::array<bool, 31> buffers{};
        std::array<bool, 128> textures{};
        std::array<bool, 16> samplers{};
        for (std::size_t i = 0u; i < descriptors.bindings.size(); ++i) {
            const auto& binding = descriptors.bindings[i];
            for (std::size_t j = 0u; j < i; ++j)
                if (descriptors.bindings[j].binding == binding.binding
                    && descriptors.bindings[j].arrayElement == binding.arrayElement)
                    return fail(D::Duplicate);
            MetalRhiPreparedBindingLocation location;
            const auto valid = resolve(set, binding, location);
            if (!valid.success)
                return valid;
            if (!location.active || binding.arrayElement != 0u)
                continue;
            auto& occupied = location.array || binding.argumentKind == MetalRhiArgumentKind::Buffer
                ? buffers[location.slot]
                : binding.argumentKind == MetalRhiArgumentKind::Texture ? textures[location.slot]
                                                                        : samplers[location.slot];
            if (occupied)
                return fail(D::Overlap);
            occupied = true;
        }
        const auto complete =
            [&](std::uint32_t binding, std::uint32_t capacity, rhi::DescriptorType type) {
                std::size_t count = 0u;
                for (const auto& element : descriptors.bindings) {
                    if (element.binding != binding)
                        continue;
                    if (element.type != type || element.arrayCount != capacity
                        || element.arrayElement >= capacity)
                        return false;
                    ++count;
                }
                return count == capacity;
            };
        for (const auto& map : arrays_)
            if (map.set == set && map.active && !complete(map.binding, map.capacity, map.type))
                return fail(D::Missing);
        for (const auto& map : direct_)
            if (map.set == set && map.active && !complete(map.binding, 1u, map.type))
                return fail(D::Missing);
        if (pushSlot_ != UINT32_MAX
            && (pushSlot_ >= buffers.size() || buffers[pushSlot_]
                || (reservedBuffers_ & (1u << pushSlot_))))
            return fail(D::Overlap);
        if ((reservedBuffers_ & (1u << buffers.size())) != 0u)
            return fail(D::Overlap);
        for (std::size_t slot = 0u; slot < buffers.size(); ++slot)
            if (buffers[slot] && (reservedBuffers_ & (1u << slot)))
                return fail(D::Overlap);
        return {true, {}};
    }

private:
    rhi::ValidationResult fail(MetalRhiPreparedMappingDiagnostic reason) const
    {
        constexpr std::array compute{"RHI-METAL-PREPARED-COMPUTE-BINDING-INVALID",
            "RHI-METAL-PREPARED-COMPUTE-TYPE-MISMATCH",
            "RHI-METAL-PREPARED-COMPUTE-ARRAY-MAPPING",
            "RHI-METAL-PREPARED-COMPUTE-DIRECT-MAPPING",
            "RHI-METAL-PREPARED-COMPUTE-MAPPING-MISSING-OR-AMBIGUOUS",
            "RHI-METAL-PREPARED-COMPUTE-PHYSICAL-STAGE-MISMATCH",
            "RHI-METAL-PREPARED-COMPUTE-ARGUMENT-RANGE",
            "RHI-METAL-PREPARED-COMPUTE-DUPLICATE-ELEMENT",
            "RHI-METAL-PREPARED-COMPUTE-SLOT-OVERLAP",
            "RHI-METAL-PREPARED-COMPUTE-ELEMENT-MISSING"};
        constexpr std::array graphics{"RHI-METAL-PREPARED-GRAPHICS-BINDING-INVALID",
            "RHI-METAL-PREPARED-GRAPHICS-TYPE-MISMATCH",
            "RHI-METAL-PREPARED-GRAPHICS-ARRAY-MAPPING",
            "RHI-METAL-PREPARED-GRAPHICS-DIRECT-MAPPING",
            "RHI-METAL-PREPARED-GRAPHICS-MAPPING-MISSING-OR-AMBIGUOUS",
            "RHI-METAL-PREPARED-GRAPHICS-PHYSICAL-STAGE-MISMATCH",
            "RHI-METAL-PREPARED-GRAPHICS-ARGUMENT-RANGE",
            "RHI-METAL-PREPARED-GRAPHICS-DUPLICATE-ELEMENT",
            "RHI-METAL-PREPARED-GRAPHICS-SLOT-OVERLAP",
            "RHI-METAL-PREPARED-GRAPHICS-ELEMENT-MISSING"};
        return {false,
            (stage_ == rhi::ShaderStage::Compute ? compute
                                                 : graphics)[static_cast<std::size_t>(reason)]};
    }

    const core::pmr::Vector<MetalRhiNativeBindingSlot>& direct_;
    const core::pmr::Vector<MetalRhiNativeArgumentBufferSlot>& arrays_;
    const rhi::semantics::MetalPhysicalBindingStages& physicalStages_;
    rhi::ShaderStage stage_;
    std::uint32_t pushSlot_, reservedBuffers_;
};

// endregion Metal prepared binding validation

// region Metal prepared compute validation

using MetalRhiPreparedComputeLocation = MetalRhiPreparedBindingLocation;

inline MetalRhiPreparedBindingLayoutValidator metalRhiPreparedComputeLayoutValidator(
    const MetalRhiNativePipelinePayload& pipeline)
{
    return {pipeline.metalBindingSpace,
        pipeline.metalArgumentBuffer,
        pipeline.nativeBindingStages,
        rhi::ShaderStage::Compute,
        pipeline.pushConstantMetalBufferIndex};
}

inline rhi::ValidationResult resolveMetalRhiPreparedComputeLocation(
    const MetalRhiNativePipelinePayload& pipeline,
    std::uint32_t set,
    const MetalRhiNativeArgumentBindingPayload& binding,
    MetalRhiPreparedComputeLocation& out)
{
    return metalRhiPreparedComputeLayoutValidator(pipeline).resolve(set, binding, out);
}

inline rhi::ValidationResult validateMetalRhiPreparedComputeLayout(
    const MetalRhiNativePipelinePayload& pipeline,
    const MetalRhiNativeDescriptorGroupPayload& descriptors,
    std::uint32_t set)
{
    return metalRhiPreparedComputeLayoutValidator(pipeline).validate(descriptors, set);
}

} // namespace arp::rhi_metal

// endregion Metal prepared compute validation

// region Metal prepared graphics validation

namespace arp::shader {
class SingleEntryShaderArtifact;
}

namespace arp::rhi_metal {

inline rhi::ValidationResult validateMetalRhiGraphicsStageSelection(
    const rhi::RenderPipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    rhi::ShaderStage& stages)
{
    using Stage = rhi::ShaderStage;
    stages = Stage::None;
    if (desc.vertexShader.stage != Stage::Vertex || desc.vertexShader.entryPoint.empty())
        return {false, "RHI-METAL-GRAPHICS-VERTEX-ENTRY-INVALID"};
    const bool fragmentAbsent = desc.fragmentShader.stage == Stage::None
        && desc.fragmentShader.entryPoint.empty() && desc.fragmentShader.modulePath.empty();
    if (!fragmentAbsent
        && (desc.fragmentShader.stage != Stage::Fragment || desc.fragmentShader.entryPoint.empty()))
        return {false, "RHI-METAL-GRAPHICS-FRAGMENT-ENTRY-INVALID"};
    if (report.colorTargetCount > report.colorFormats.size()
        || report.colorTargetCount != rhi::effectiveColorTargetCount(desc)
        || report.depthStencilFormat != rhi::effectiveDepthStencilFormat(desc))
        return {false, "RHI-METAL-GRAPHICS-ATTACHMENT-REPORT-MISMATCH"};
    for (std::uint32_t i = 0u; i < report.colorTargetCount; ++i)
        if (report.colorFormats[i] != rhi::effectiveColorTargetAt(desc, i).format)
            return {false, "RHI-METAL-GRAPHICS-ATTACHMENT-REPORT-MISMATCH"};
    if (fragmentAbsent
        && (report.colorTargetCount != 0u || !rhi::isDepthTextureFormat(report.depthStencilFormat)
            || !desc.depthStencil.depthTestEnabled || !desc.depthStencil.depthWriteEnabled))
        return {false, "RHI-METAL-GRAPHICS-FRAGMENT-OMISSION-REQUIRES-DEPTH-ONLY"};
    stages = fragmentAbsent ? Stage::Vertex : Stage::Vertex | Stage::Fragment;
    return {true, {}};
}

struct MetalRhiPreparedGraphicsStageLayout {
    rhi::ShaderStage stage = rhi::ShaderStage::None;
    core::pmr::String entry{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<MetalRhiNativeBindingSlot> direct{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<MetalRhiNativeArgumentBufferSlot> arrays{&core::defaultCpuMemoryResource()};
    std::uint32_t pushSlot = UINT32_MAX;

    bool matches(const MetalRhiPreparedGraphicsStageLayout& other) const noexcept
    {
        if (stage != other.stage || entry != other.entry || pushSlot != other.pushSlot
            || direct.size() != other.direct.size() || arrays.size() != other.arrays.size())
            return false;
        for (std::size_t i = 0; i < direct.size(); ++i) {
            const auto& a = direct[i];
            const auto& b = other.direct[i];
            if (a.set != b.set || a.binding != b.binding || a.metalSlot != b.metalSlot
                || a.type != b.type || a.active != b.active)
                return false;
        }
        for (std::size_t i = 0; i < arrays.size(); ++i) {
            const auto& a = arrays[i];
            const auto& b = other.arrays[i];
            if (a.set != b.set || a.binding != b.binding
                || a.argumentBufferSlot != b.argumentBufferSlot
                || a.argumentIndex != b.argumentIndex || a.capacity != b.capacity
                || a.type != b.type || a.active != b.active)
                return false;
        }
        return true;
    }

    rhi::ValidationResult validate(const MetalRhiNativeDescriptorGroupPayload& descriptors,
        std::uint32_t set,
        const rhi::semantics::MetalPhysicalBindingStages& nativeStages,
        const rhi::semantics::MetalVertexStreamMapping& streams) const
    {
        if (entry.empty()
            || (stage != rhi::ShaderStage::Vertex && stage != rhi::ShaderStage::Fragment))
            return {false, "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH"};
        std::uint32_t reserved = 0u;
        if (stage == rhi::ShaderStage::Vertex) {
            if (!streams.success)
                return {false, "RHI-METAL-PREPARED-GRAPHICS-VERTEX-STREAM-MISSING"};
            for (const auto slot : streams.slots) {
                if (slot == UINT32_MAX)
                    continue;
                if (slot >= streams.capacity || (reserved & (1u << slot)))
                    return {false, "RHI-METAL-PREPARED-GRAPHICS-VERTEX-STREAM-INVALID"};
                reserved |= 1u << slot;
            }
        }
        return MetalRhiPreparedBindingLayoutValidator(
            direct, arrays, nativeStages, stage, pushSlot, reserved)
            .validate(descriptors, set);
    }

    rhi::ValidationResult resolve(const MetalRhiNativeArgumentBindingPayload& binding,
        std::uint32_t set,
        const rhi::semantics::MetalPhysicalBindingStages& nativeStages,
        MetalRhiPreparedBindingLocation& out) const
    {
        out = {};
        if (entry.empty()
            || (stage != rhi::ShaderStage::Vertex && stage != rhi::ShaderStage::Fragment))
            return {false, "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH"};
        return MetalRhiPreparedBindingLayoutValidator(direct, arrays, nativeStages, stage, pushSlot)
            .resolve(set, binding, out);
    }
};

rhi::ValidationResult lowerMetalRhiPreparedGraphicsStage(const shader::SingleEntryShaderArtifact&,
    std::string_view entry,
    rhi::ShaderStage,
    MetalRhiPreparedGraphicsStageLayout&);

// Startup preparation from a sealed per-entry module. Does not enable execution
// or derive native physical liveness from semantic descriptor visibility.
rhi::ValidationResult lowerMetalRhiPreparedGraphicsStage(
    const std::shared_ptr<const rhi::BindlessShaderPackage>&,
    std::string_view entry,
    rhi::ShaderStage stage,
    MetalRhiPreparedGraphicsStageLayout&);
rhi::ValidationResult lowerMetalRhiPreparedGraphicsStage(const rhi::BindlessShaderPackage&,
    std::string_view entry,
    rhi::ShaderStage,
    MetalRhiPreparedGraphicsStageLayout&);

} // namespace arp::rhi_metal

// endregion Metal prepared graphics validation
