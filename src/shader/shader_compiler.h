#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::shader {

/// Version of the serialized reflection contract consumed by every RHI
/// backend.  Bumping this value is required when an artifact field changes
/// meaning or when a backend lowering starts depending on newly generated
/// metadata.  Older artifacts must fail closed at the artifact gate.
inline constexpr const char* kShaderReflectionContractVersion = "ShaderReflection/3";

enum class ShaderStage { Vertex, Fragment, Compute, TessellationControl, TessellationEvaluation };

// Independent shader-target identity domain, not rhi::Backend ordinals.
// ShaderCompileTargetAdmission/1 rejects unknown values before source metadata.
enum class TargetBackend { SpirV, Msl, Wgsl };

enum class BindingType {
    UniformBuffer,
    StorageBuffer,
    ReadOnlyStorageBuffer,
    SampledTexture,
    StorageTexture,
    Sampler,
    /// Tile-local attachment read within a render pass (Vulkan input
    /// attachment / Metal framebuffer fetch). On backends without subpass
    /// support (WebGPU), the FrameGraph compiler auto-lowers this to a
    /// cross-pass sampled texture binding.
    InputAttachment
};

inline constexpr std::uint32_t kInvalidSubpassAttachmentIndex = UINT32_MAX;

enum class VertexAttributeFormat { Unknown, Float, Float2, Float3, Float4 };

struct ShaderBinding {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    BindingType type = BindingType::UniformBuffer;
    uint32_t set = 0;
    uint32_t binding = 0;
    /// Logical attachment index for InputAttachment bindings. This is kept
    /// separate from the descriptor binding because the latter is a physical
    /// shader interface slot, while the former identifies the FrameGraph
    /// attachment consumed by SubpassLoad().
    uint32_t subpassAttachmentIndex = kInvalidSubpassAttachmentIndex;
    /// True for a logical depth input attachment. This semantic survives the
    /// target-specific lowering so WebGPU can emit texture_depth_2d.
    bool depthTexture = false;
    /// Resource-array cardinality. Metal argument-buffer lowering and the
    /// descriptor-table contract consume this value; one is the scalar ABI.
    std::uint32_t arrayCount = 1;
    /// Descriptor visibility copied from target reflection, using the same
    /// bit positions as arp::rhi::ShaderStage. Zero means the artifact did
    /// not provide authoritative stage metadata and must not be consumed by
    /// an RHI backend.
    std::uint32_t stageMask = 0;
};

struct ShaderEntryPoint {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    ShaderStage stage = ShaderStage::Vertex;
    uint32_t computeWorkgroupSizeX = 0;
    uint32_t computeWorkgroupSizeY = 0;
    uint32_t computeWorkgroupSizeZ = 0;
    bool writesRenderTargetArrayIndex = false;
};

struct ShaderVertexAttribute {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    core::pmr::String semantic{&core::defaultCpuMemoryResource()};
    VertexAttributeFormat format = VertexAttributeFormat::Unknown;
    uint32_t location = 0;
};

// Authoritative member-level layout of a push-constant block, reflected from
// SPIR-V by spirv-reflect (offline, in scripts/shader_bindings_codegen.py) and
// serialized into the per-shader .reflection.yaml under pushConstantMembers.
// Offsets/sizes/strides are the ground truth for C++ <-> shader layout sync.
struct ShaderPushConstantMember {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    core::pmr::String scalarType{
        &core::defaultCpuMemoryResource()}; // "float" (only float scalars are produced today)
    uint32_t scalarWidth = 0;               // bits, e.g. 32
    uint32_t vecSize = 0;                   // vector component count (0 if not a vector)
    uint32_t matrixRows = 0;                // matrix row count (0 if not a matrix)
    uint32_t matrixCols = 0;                // matrix column count (0 if not a matrix)
    uint32_t matrixStride = 0;              // matrix stride in bytes
    bool rowMajor = false;                  // CPU row-memory ABI; raw SPV flags are in decorations.
    core::pmr::Vector<uint32_t> arrayDims{&core::defaultCpuMemoryResource()};
    uint32_t arrayStride = 0; // stride in bytes for the reflected array
    uint32_t offset = 0;      // byte offset within the push-constant block
    uint32_t size = 0;        // byte size of this member
    uint32_t decorations = 0; // raw spirv-reflect decoration bitfield
};

/// Physical WGSL binding emitted for one logical source binding. This stays in
/// shader reflection; the RHI mirrors it only when assembling a WebGPU shader
/// package, so other backends never consume WGSL implementation details.
struct ShaderWebGpuBindingSpaceBinding {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t group = 0;
    uint32_t webGpuBinding = 0;
    bool active = false;
    /// Physical resource type emitted in the WGSL artifact. This may differ
    /// from the logical binding type only when an explicit code-level
    /// lowering contract says so (for example InputAttachment -> texture).
    BindingType physicalType = BindingType::UniformBuffer;
    core::pmr::String loadSemantics{&core::defaultCpuMemoryResource()};
    bool requiresSampler = false;
    /// True when the physical sampled texture is a WebGPU depth texture.
    bool depthTexture = false;
    /// Binding-array element represented by this physical declaration. WebGPU
    /// binding arrays use one physical binding with an explicit capacity, so
    /// the artifact currently carries element zero as the declaration anchor.
    std::uint32_t arrayElement = 0;
    std::uint32_t capacity = 1;
};

/// Physical Metal resource slot emitted for one logical source binding.
/// Metal has independent buffer, texture, and sampler namespaces, so the
/// logical Vulkan-style binding number is not an executable Metal index.
struct ShaderMetalBindingSpaceBinding {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t metalSlot = UINT32_MAX;
    bool active = false;
    BindingType physicalType = BindingType::UniformBuffer;
};

/// Physical Metal argument-buffer member emitted for one logical descriptor
/// binding.  The direct Metal binding table above is intentionally separate:
/// an argument-buffer member is not a direct encoder slot and must never be
/// inferred from the logical Vulkan binding number.
struct ShaderMetalArgumentBufferBinding {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t argumentBufferSlot = UINT32_MAX;
    uint32_t argumentIndex = UINT32_MAX;
    uint32_t capacity = 0;
    bool active = false;
    BindingType physicalType = BindingType::UniformBuffer;
};

struct ShaderMetalFramebufferFetchBinding {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t logicalAttachmentIndex = UINT32_MAX;
    uint32_t metalColorSlot = UINT32_MAX;
    bool depth = false;
    bool active = false;
};

struct ShaderReflection {
    core::pmr::String reflectionContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String sourceSha256{&core::defaultCpuMemoryResource()};
    core::pmr::String bindlessContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String bindlessExecution{&core::defaultCpuMemoryResource()};
    core::pmr::String bindlessLoweringSha256{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderEntryPoint> entryPoints{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderBinding> bindings{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderVertexAttribute> vertexAttributes{&core::defaultCpuMemoryResource()};
    uint32_t pushConstantSizeBytes = 0;
    // Track A P5: enriched reflection metadata
    core::pmr::String pushConstantContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String pushConstantStages{&core::defaultCpuMemoryResource()};
    uint32_t pushConstantWebGpuBindingGroup = UINT32_MAX;
    uint32_t pushConstantWebGpuBindingIndex = UINT32_MAX;
    uint32_t pushConstantMetalBufferIndex = UINT32_MAX;
    uint32_t computeWorkgroupSizeX = 0;
    uint32_t computeWorkgroupSizeY = 0;
    uint32_t computeWorkgroupSizeZ = 0;
    uint32_t tessellationControlPointCount = 0;
    core::pmr::String metalGpuCountedIndirectContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String metalGpuCountedIndirectStatus{&core::defaultCpuMemoryResource()};
    uint32_t metalGpuCountedIndirectCountBufferIndex = UINT32_MAX;
    bool metalGpuCountedIndirectUsesDrawId = false;
    bool metalGpuCountedIndirectMasksOutOfRange = false;
    core::pmr::String metalTessellationContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String metalTessellationStatus{&core::defaultCpuMemoryResource()};
    core::pmr::String metalTessellationVertexEntryPoint{&core::defaultCpuMemoryResource()};
    uint32_t metalTessellationFactorBufferIndex = UINT32_MAX;
    uint32_t metalTessellationControlPointCount = 0;
    uint32_t metalTessellationPatchType = UINT32_MAX;
    uint32_t metalTessellationFactorFormat = UINT32_MAX;
    uint32_t metalTessellationFactorBytesPerPatch = 0;
    uint32_t metalTessellationFactorInstanceStrideBytes = 0;
    // Authoritative member-level layout from SPIRV-Reflect (empty when the
    // reflection file predates codegen or the shader has no push constants).
    core::pmr::Vector<ShaderPushConstantMember> pushConstantMembers{
        &core::defaultCpuMemoryResource()};
    core::pmr::String webGpuBindingSpaceContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String webGpuBindingSpaceStatus{&core::defaultCpuMemoryResource()};
    core::pmr::String webGpuBindingSpaceUnavailableReason{&core::defaultCpuMemoryResource()};
    /// Exact number of physical WGSL bind-group slots emitted by the
    /// lowering artifact. This includes the push-constant group when present
    /// and is never inferred from a logical descriptor set number.
    std::uint32_t webGpuBindingSpaceGroupCount = 0;
    core::pmr::Vector<ShaderWebGpuBindingSpaceBinding> webGpuBindingSpace{
        &core::defaultCpuMemoryResource()};
    core::pmr::String metalBindingSpaceContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String metalBindingSpaceStatus{&core::defaultCpuMemoryResource()};
    core::pmr::String metalBindingSpaceUnavailableReason{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderMetalBindingSpaceBinding> metalBindingSpace{
        &core::defaultCpuMemoryResource()};
    core::pmr::String metalArgumentBufferContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String metalArgumentBufferStatus{&core::defaultCpuMemoryResource()};
    core::pmr::String metalArgumentBufferUnavailableReason{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderMetalArgumentBufferBinding> metalArgumentBuffer{
        &core::defaultCpuMemoryResource()};
    core::pmr::String metalFramebufferFetchContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String metalFramebufferFetchStatus{&core::defaultCpuMemoryResource()};
    core::pmr::String metalFramebufferFetchUnavailableReason{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderMetalFramebufferFetchBinding> metalFramebufferFetch{
        &core::defaultCpuMemoryResource()};
};

struct ShaderCompileRequest {
    core::pmr::String sourcePath{&core::defaultCpuMemoryResource()};
    core::pmr::String sourceCode{&core::defaultCpuMemoryResource()};
    core::pmr::String moduleName{&core::defaultCpuMemoryResource()};
    TargetBackend target = TargetBackend::SpirV;
    core::pmr::Vector<ShaderEntryPoint> entryPoints{&core::defaultCpuMemoryResource()};
};

struct ShaderCompileResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    TargetBackend target = TargetBackend::SpirV;
    core::pmr::Vector<uint8_t> bytecode{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderBinding> bindings{&core::defaultCpuMemoryResource()};
    ShaderReflection reflection;
};

class ARP_API ShaderCompiler {
public:
    ShaderCompileResult compile(const ShaderCompileRequest& request) const;
};

} // namespace arp::shader
