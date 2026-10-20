#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/subpass_input_lowering.h"

#include <cstdint>

namespace arp::rhi {
class BindlessShaderPackage;
class OriginalSingleEntryPipelineInput;
class OriginalGraphicsPipelineInput;
} // namespace arp::rhi

namespace arp::rhi_webgpu {

inline constexpr std::uint32_t kWebGpuGuaranteedBindGroupCount = 4u;
inline constexpr const char* kWebGpuBindingSpaceContractVersion = "WebGpuBindingSpace/5";

/// Backend-private interpretation of WebGpuBindingSpace/5.  It is built from
/// a generated shader reflection artifact, never from descriptor-layout names
/// or an upper-layer native handle.
struct WebGpuBindingSpaceLoweringEntry {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t group = 0;
    std::uint32_t webGpuBinding = 0;
    bool active = false;
    rhi::DescriptorType physicalType = rhi::DescriptorType::UniformBuffer;
    rhi::semantics::SubpassInputLoadSemantics loadSemantics =
        rhi::semantics::SubpassInputLoadSemantics::CurrentFragment;
    bool hasLoadSemantics = false;
    bool requiresSampler = false;
    bool depthTexture = false;
    std::uint32_t arrayElement = 0;
    std::uint32_t capacity = 1;
};

struct WebGpuBindingSpaceLowering {
    core::pmr::String contractVersion{&core::defaultCpuMemoryResource()};
    /// Exact physical bind-group count emitted by the target artifact. A
    /// value greater than the WebGPU guaranteed minimum requires the queried
    /// runtime maxBindGroups limit before native pipeline creation.
    std::uint32_t physicalGroupCount = 0;
    // Physical push-constant placement is part of the generated lowering
    // artifact. Upper layers may omit these fields when constructing the
    // logical ShaderPackageDesc; WebGPU must still use the artifact values.
    std::uint32_t pushConstantGroup = UINT32_MAX;
    std::uint32_t pushConstantBinding = UINT32_MAX;
    /// Logical bindings copied from the same generated reflection artifact as
    /// the physical WGSL locations. Pipeline validation compares these with
    /// ShaderPackageDesc so an artifact/package mismatch cannot reach native
    /// bind-group creation.
    core::pmr::Vector<rhi::ShaderReflectionBinding> artifactBindings{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGpuBindingSpaceLoweringEntry> entries{&core::defaultCpuMemoryResource()};
};

struct WebGpuBindingSpaceLoweringLoadResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    WebGpuBindingSpaceLowering lowering;
};

/// Loads and validates the generated WGSL binding-space artifact associated
/// with a shader package. This is deliberately a WebGPU backend operation:
/// the generic RHI only owns logical descriptor set/binding semantics.
WebGpuBindingSpaceLoweringLoadResult loadWebGpuBindingSpaceLowering(
    const rhi::ShaderPackageDesc& package);

// Validates genuine original memory input; no file IO or YAML parsing.
WebGpuBindingSpaceLoweringLoadResult loadOriginalWebGpuBindingSpaceLowering(
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& input);
WebGpuBindingSpaceLoweringLoadResult loadOriginalWebGpuGraphicsBindingSpaceLowering(
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input);

// Bindless packages have no runtime artifact paths. This entry reads only the
// reflection and target bytes retained by the verified publication.
WebGpuBindingSpaceLoweringLoadResult loadBindlessWebGpuBindingSpaceLowering(
    const rhi::BindlessShaderPackage& package,
    std::string_view entry,
    rhi::ShaderStage stage,
    bool explicitPush = false);

struct WebGpuBindlessComputeInterface {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    rhi::PipelineLayoutDesc layout;
    rhi::ShaderPackageDesc shader;
    WebGpuBindingSpaceLowering lowering;
};

// Builds an entry-specific L2 layout from sealed reflection. Inactive bindings
// from other stages cannot leak into this compute pipeline's native layout.
WebGpuBindlessComputeInterface prepareBindlessWebGpuComputeInterface(
    const rhi::BindlessShaderPackage& package, std::string_view entry);

struct WebGpuBindlessRenderInterface {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    rhi::PipelineLayoutDesc layout;
    rhi::ShaderPackageDesc shader;
    WebGpuBindingSpaceLowering lowering;
};

// Composes the two sealed entry reflections into one explicit graphics layout.
WebGpuBindlessRenderInterface prepareBindlessWebGpuRenderInterface(
    const rhi::BindlessShaderPackage& package,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const std::shared_ptr<const rhi::BindlessShaderPackage>& pushOwner = {});

/// Checks that the logical L2 reflection and pipeline layout can be lowered
/// using the supplied physical WGSL locations without inventing a mapping.
rhi::ValidationResult validateWebGpuBindingSpaceLowering(const rhi::PipelineLayoutDesc& layout,
    const rhi::ShaderReflectionDesc& logicalReflection,
    const WebGpuBindingSpaceLowering& lowering,
    std::uint32_t pushConstantGroup);

} // namespace arp::rhi_webgpu
