#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "core/ownership/quota_bound_shared_identity.h"
#include "core/text/number_format.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/memory_cache_budget.h"
#include "rhi/frontend/resource_registration.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_subpass.h"
#include "rhi/semantics/shader_buffer_member_memory.h"
#include "rhi/semantics/subpass_input_lowering.h"
#include "shader/shader_compiler.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arp::rhi_webgpu {
class WebGpuOriginalArtifactComputeCreation;
class WebGpuOriginalArtifactGraphicsCreation;
class WebGpuAcceptedOriginalComputePipelineFactory;
class WebGpuAcceptedOriginalGraphicsPipelineFactory;
} // namespace arp::rhi_webgpu

namespace arp::rhi_vulkan {
class VulkanOriginalArtifactComputeCreation;
class VulkanAcceptedOriginalComputePipelineFactory;
class VulkanOriginalArtifactGraphicsCreation;
class VulkanAcceptedOriginalGraphicsPipelineFactory;
} // namespace arp::rhi_vulkan

namespace arp::rhi_metal {
class MetalOriginalArtifactGraphicsCreation;
class MetalOriginalArtifactComputeCreation;
class MetalAcceptedOriginalComputePipelineFactory;
class MetalAcceptedOriginalGraphicsPipelineFactory;
} // namespace arp::rhi_metal

namespace arp::rhi {

class BindlessAuthorization;
struct BindlessPackageModule;
class OriginalSingleEntryPipelineInput;
class OriginalGraphicsPipelineInput;
class RenderPipeline;
class ComputePipeline;

enum class ShaderSourceLanguage { Slang };

struct ShaderEntrypoint {
    core::pmr::String modulePath{&core::defaultCpuMemoryResource()};
    core::pmr::String entryPoint{&core::defaultCpuMemoryResource()};
    ShaderStage stage = ShaderStage::None;
    ShaderSourceLanguage sourceLanguage = ShaderSourceLanguage::Slang;
    // Metal dispatch lowering consumes the compiled threadgroup dimensions
    // for the selected entry point. Zero means the reflection contract did
    // not provide executable dimensions.
    std::uint32_t computeWorkgroupSizeX = 0;
    std::uint32_t computeWorkgroupSizeY = 0;
    std::uint32_t computeWorkgroupSizeZ = 0;
    /// True only when the selected fragment entry point writes the explicit
    /// layered-rendering array-index ABI. The bit is reflected from shader
    /// source and is part of pipeline identity; Metal must not infer it from
    /// a texture view's layer count.
    bool writesRenderTargetArrayIndex = false;

    bool matchesDefinition(const ShaderEntrypoint& other) const noexcept
    {
        return modulePath == other.modulePath && entryPoint == other.entryPoint
            && stage == other.stage && sourceLanguage == other.sourceLanguage
            && computeWorkgroupSizeX == other.computeWorkgroupSizeX
            && computeWorkgroupSizeY == other.computeWorkgroupSizeY
            && computeWorkgroupSizeZ == other.computeWorkgroupSizeZ
            && writesRenderTargetArrayIndex == other.writesRenderTargetArrayIndex;
    }
};

struct ShaderGeneratedArtifacts {
    core::pmr::String spirvPath{&core::defaultCpuMemoryResource()};
    core::pmr::String mslPath{&core::defaultCpuMemoryResource()};
    core::pmr::String wgslPath{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectionPath{&core::defaultCpuMemoryResource()};
};

struct ShaderReflectionBinding {
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    DescriptorType type = DescriptorType::SampledTexture;
    ShaderStage stages = ShaderStage::None;
    std::uint32_t arrayCount = 1;
    /// Logical FrameGraph attachment consumed by an InputAttachment binding.
    /// UINT32_MAX means that the binding is not an input attachment.
    std::uint32_t subpassAttachmentIndex = UINT32_MAX;
    /// True for an input attachment backed by a depth texture. This is
    /// reflected separately because WebGPU requires a depth sample type.
    bool depthTexture = false;
};

/// Convert an authored descriptor layout entry into the reflection ABI used by
/// pipeline validation. Keep semantic flags such as depthTexture in this
/// conversion; aggregate initializers that copy only type/stages are lossy.
inline ShaderReflectionBinding makeShaderReflectionBinding(
    const std::uint32_t set, const DescriptorBindingDesc& descriptor);

struct ShaderPushConstantMemberDesc {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    core::pmr::String scalarType{&core::defaultCpuMemoryResource()};
    std::uint32_t scalarWidth = 0;
    std::uint32_t vecSize = 0;
    std::uint32_t matrixRows = 0;
    std::uint32_t matrixCols = 0;
    std::uint32_t matrixStride = 0;
    bool rowMajor = false;
    core::pmr::Vector<std::uint32_t> arrayDims{&core::defaultCpuMemoryResource()};
    std::uint32_t arrayStride = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t decorations = 0;
};

struct ShaderReflectionDesc {
    core::pmr::String reflectionContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String sourceSha256{&core::defaultCpuMemoryResource()};
    core::pmr::String bindlessContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String bindlessExecution{&core::defaultCpuMemoryResource()};
    core::pmr::String bindlessLoweringSha256{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderReflectionBinding> descriptorBindings{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderPushConstantMemberDesc> pushConstantMembers{
        &core::defaultCpuMemoryResource()};
    std::uint32_t pushConstantByteCount = 0;
    // Track A P5: enriched reflection metadata consumed by runtime.
    core::pmr::String pushConstantContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String pushConstantStages{&core::defaultCpuMemoryResource()};
    // UINT32_MAX means the generated WGSL artifact did not publish a physical
    // push-constant slot. WebGPU must reject that package rather than infer a
    // historical group/binding convention.
    std::uint32_t pushConstantWebGpuBindingGroup = UINT32_MAX;
    std::uint32_t pushConstantWebGpuBindingIndex = UINT32_MAX;
    // UINT32_MAX means the selected MSL artifact did not declare the
    // physical push-constant slot. Metal must reject that shader rather than
    // guessing buffer(0).
    std::uint32_t pushConstantMetalBufferIndex = UINT32_MAX;
    // Compute dispatch dimensions are part of the shader lowering contract:
    // RHI dispatch arguments are workgroup counts, while Metal additionally
    // requires the compiled threadgroup dimensions.
    std::uint32_t computeWorkgroupSizeX = 0;
    std::uint32_t computeWorkgroupSizeY = 0;
    std::uint32_t computeWorkgroupSizeZ = 0;
    std::uint32_t tessellationControlPointCount = 0;
    // Metal GPU-counted indirect lowering is an explicit shader ABI. The
    // vertex artifact must consume [[draw_id]] and make every draw whose id
    // is >= the GPU-written count produce no raster/depth side effects.
    core::pmr::String metalGpuCountedIndirectContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String metalGpuCountedIndirectStatus{&core::defaultCpuMemoryResource()};
    std::uint32_t metalGpuCountedIndirectCountBufferIndex = UINT32_MAX;
    bool metalGpuCountedIndirectUsesDrawId = false;
    bool metalGpuCountedIndirectMasksOutOfRange = false;
    // Metal tessellation uses a generated post-tessellation vertex ABI. The
    // separate L2 control/evaluation entry points are not themselves native
    // Metal function slots; the generated entry point and its factor ABI must
    // be reflected explicitly before a PatchList pipeline is executable.
    core::pmr::String metalTessellationContractVersion{&core::defaultCpuMemoryResource()};
    core::pmr::String metalTessellationStatus{&core::defaultCpuMemoryResource()};
    core::pmr::String metalTessellationVertexEntryPoint{&core::defaultCpuMemoryResource()};
    std::uint32_t metalTessellationFactorBufferIndex = UINT32_MAX;
    std::uint32_t metalTessellationControlPointCount = 0;
    /// Target ABI values required to configure the Metal tessellation
    /// pipeline. UINT32_MAX means that the generated artifact did not expose
    /// the corresponding executable fact.
    std::uint32_t metalTessellationPatchType = UINT32_MAX;
    std::uint32_t metalTessellationFactorFormat = UINT32_MAX;
    std::uint32_t metalTessellationFactorBytesPerPatch = 0;
    std::uint32_t metalTessellationFactorInstanceStrideBytes = 0;
};

inline constexpr std::string_view kMetalGpuCountedIndirectContractVersion =
    "MetalGpuCountedIndirect/1";
inline constexpr std::string_view kMetalTessellationContractVersion = "MetalTessellation/1";

inline bool hasMetalGpuCountedIndirectMaskAbi(const ShaderReflectionDesc& reflection);

inline bool hasMetalTessellationAbi(
    const ShaderReflectionDesc& reflection, std::uint32_t patchControlPointCount);

/// Validates one render-graph input-attachment route against logical shader
/// reflection.  This is intentionally backend-neutral: Vulkan consumes the
/// logical InputAttachment and WebGPU consumes a generated physical artifact,
/// but both must agree on the same set/binding and FrameGraph attachment.
inline ValidationResult validateSubpassInputBindingAgainstReflection(
    const ShaderReflectionDesc& reflection,
    const semantics::SubpassInputBindingReference& reference);

struct ShaderPackageDesc {
    core::pmr::String label{&core::defaultCpuMemoryResource()};
    core::pmr::String slangModulePath{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderEntrypoint> entrypoints{&core::defaultCpuMemoryResource()};
    ShaderGeneratedArtifacts artifacts;
    ShaderReflectionDesc reflection;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    /// True only after the package's generated reflection artifact has been
    /// loaded and its source/target identity has been validated. Runtime
    /// package consumers must not continue with hand-authored reflection when
    /// this is false.
    bool generatedReflectionHydrated = false;
};

inline bool hasShaderPathSuffix(const std::pmr::string& path, const std::pmr::string& suffix);

inline std::uint64_t hashShaderString(std::uint64_t hash, const std::pmr::string& value);

inline std::uint64_t hashShaderValue(std::uint64_t hash, std::uint64_t value);

inline std::uint64_t stableShaderPackageHash(const ShaderPackageDesc& desc);

inline std::optional<ShaderStage> parseShaderReflectionPushConstantStages(
    const std::pmr::string& encoded);

inline ValidationResult validateShaderPackageAuthorization(
    const ShaderPackageDesc& desc, bool authorizedBindless);

inline ValidationResult validateShaderPackageSourcePaths(const ShaderPackageDesc& desc);

inline ValidationResult validateShaderPackageAuxiliaryPaths(const ShaderPackageDesc& desc);

// Logical interface facts only; never target availability or native authority.
inline ValidationResult validateShaderPackageInterface(const ShaderPackageDesc& desc);

inline ValidationResult validateShaderPackageImpl(
    const ShaderPackageDesc& desc, bool authorizedBindless);

inline ValidationResult validateShaderPackage(const ShaderPackageDesc& desc);

// Used only by backend factories after consuming a device-issued ticket tied to
// an immutable, hash-verified bindless package. Ordinary pipeline paths remain
// closed to bindless reflection.
inline ValidationResult validateAuthorizedBindlessShaderPackage(const ShaderPackageDesc& desc);

inline const DescriptorLayoutDesc* findPipelineDescriptorLayout(
    const PipelineLayoutDesc& layout, const std::pmr::string& layoutName);

inline const DescriptorBindingDesc* findDescriptorBinding(
    const DescriptorLayoutDesc& layout, const ShaderReflectionBinding& binding);

inline ValidationResult validateDescriptorSetBindingAgainstReflection(
    const PipelineLayoutDesc& layout, const ShaderReflectionBinding& binding);

inline ValidationResult validateDescriptorTableBindingAgainstReflection(
    const PipelineLayoutDesc& layout, const ShaderReflectionBinding& binding);

inline std::optional<ShaderStage> parseShaderReflectionPushConstantStages(
    const std::pmr::string& encoded);

inline ValidationResult validatePipelineLayoutAgainstShaderReflection(
    const PipelineLayoutDesc& layout, const ShaderReflectionDesc& reflection);

inline PipelineLayoutDesc buildPipelineLayoutDescFromShaderReflection(
    const ShaderReflectionDesc& reflection);

class ARP_API PipelineLayout : public core::SharedObjectIdentity, public OriginalRegisteredObject {
public:
    PipelineLayout() noexcept
        : OriginalRegisteredObject(OriginalRegisteredResourceKind::PipelineLayout)
    {}

    virtual ~PipelineLayout() = default;
    virtual const PipelineLayoutDesc& desc() const = 0;
    virtual std::uint32_t descriptorSetCount() const = 0;
    virtual std::uint32_t descriptorTableCount() const = 0;
    virtual std::uint32_t pushConstantByteCount() const = 0;
    virtual bool backendFallbacksHiddenFromUpperLayer() const = 0;
    virtual bool nativeHandleExposedToUpperLayer() const = 0;
};

enum class BlendFactor { Zero, One, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha };

enum class BlendOp { Add, Subtract, ReverseSubtract, Min, Max };

enum class ColorWriteMask : std::uint32_t {
    None = 0,
    Red = 1u << 0,
    Green = 1u << 1,
    Blue = 1u << 2,
    Alpha = 1u << 3,
    All = 0x0fu
};

inline ColorWriteMask operator|(ColorWriteMask lhs, ColorWriteMask rhs)
{
    return static_cast<ColorWriteMask>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline ColorWriteMask operator&(ColorWriteMask lhs, ColorWriteMask rhs)
{
    return static_cast<ColorWriteMask>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

// Note: CompareOp is defined in gpu_common.h (included transitively via
// gpu_descriptors.h → gpu_resources.h) so that SamplerDesc can reference it
// without a circular include.

enum class StencilOp { Keep, Zero, Replace, IncrementClamp, DecrementClamp, Invert };

enum class PrimitiveTopology { TriangleList, TriangleStrip, LineList, PointList, PatchList };

enum class CullMode { None, Front, Back };

enum class FrontFace { CounterClockwise, Clockwise };

struct BlendComponent {
    BlendFactor sourceFactor = BlendFactor::One;
    BlendFactor destinationFactor = BlendFactor::Zero;
    BlendOp operation = BlendOp::Add;
};

struct ColorTargetState {
    TextureFormat format = TextureFormat::Unknown;
    bool blendEnabled = false;
    BlendComponent colorBlend;
    BlendComponent alphaBlend;
    ColorWriteMask writeMask = ColorWriteMask::All;
    bool resolveTargetRequired = false;
};

struct StencilFaceState {
    StencilOp failOp = StencilOp::Keep;
    StencilOp depthFailOp = StencilOp::Keep;
    StencilOp passOp = StencilOp::Keep;
    CompareOp compare = CompareOp::Always;
};

struct DepthStencilState {
    TextureFormat format = TextureFormat::Unknown;
    bool depthTestEnabled = false;
    bool depthWriteEnabled = false;
    CompareOp depthCompare = CompareOp::LessEqual;
    bool stencilEnabled = false;
    StencilFaceState front;
    StencilFaceState back;
    std::uint32_t stencilReadMask = 0xffu;
    std::uint32_t stencilWriteMask = 0xffu;
    bool depthBiasEnabled = false;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
};

struct MultisampleState {
    std::uint32_t sampleCount = 1;
    std::uint64_t sampleMask = 0xffffffffffffffffull;
    bool alphaToCoverageEnabled = false;
};

struct ViewportState {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct ScissorState {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

enum class VertexFormat {
    /// One 32-bit float, without padding or normalization. Null retains the
    /// scalar attribute; Vulkan lowers to R32_SFLOAT, Metal to Float, and
    /// WebGPU to Float32. Buffer stride and attribute offset remain explicit.
    Float32x1,
    Float32x2,
    Float32x3,
    Float32x4
};

/// Per-vertex vs per-instance vertex buffer advance rate. Mirrors
/// Vulkan's VkVertexInputRate, Metal's MTLVertexStepFunction, and
/// WebGPU's WGPUVertexStepMode. An Instance-rate buffer advances once
/// per instance (driven by firstInstance/instanceCount on the draw call),
/// enabling true hardware instancing without baking transforms into vertices.
enum class VertexInputRate { Vertex, Instance };

struct VertexAttributeDesc {
    std::uint32_t shaderLocation = 0;
    std::uint32_t offsetBytes = 0;
    VertexFormat format = VertexFormat::Float32x3;
};

struct VertexBufferLayoutDesc {
    // VertexStreamBinding/1: a logical vertex-stream slot is independent of
    // descriptor bindings and push constants. Backend lowering must preserve
    // all three concurrently, even where the native API shares buffer indices.
    std::uint32_t slot = 0;
    std::uint32_t strideBytes = 0;
    /// How often the buffer advances to the next element. Vertex (default)
    /// advances per vertex; Instance advances once per draw instance.
    VertexInputRate inputRate = VertexInputRate::Vertex;
    core::pmr::Vector<VertexAttributeDesc> attributes{&core::defaultCpuMemoryResource()};
};

struct RenderPipelineDesc {
    core::pmr::String label{&core::defaultCpuMemoryResource()};
    const PipelineLayout* layout = nullptr;
    const ShaderPackageDesc* shaderPackage = nullptr;
    ShaderEntrypoint vertexShader;
    ShaderEntrypoint fragmentShader;
    TextureFormat colorFormat = TextureFormat::Unknown;
    TextureFormat depthFormat = TextureFormat::Unknown;
    core::pmr::Vector<ColorTargetState> colorTargets{&core::defaultCpuMemoryResource()};
    DepthStencilState depthStencil;
    CullMode cullMode = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    MultisampleState multisample;
    bool hasViewport = false;
    ViewportState viewport;
    bool hasScissor = false;
    ScissorState scissor;
    /// Opt-in: pipeline will receive viewport/scissor via the dynamic
    /// setViewport/setScissor commands at record time instead of using the
    /// static viewport/scissor fields above. Vulkan emits
    /// VkPipelineDynamicStateCreateInfo with VK_DYNAMIC_STATE_VIEWPORT +
    /// VK_DYNAMIC_STATE_SCISSOR (pViewports/pScissors left null, counts=1).
    /// Metal/WebGPU/Null ignore this flag for pipeline creation (their native
    /// model is encoder-side dynamic state) but still honor the L2 gate
    /// ("pipeline must be bound before setViewport"). Mutually exclusive with
    /// hasViewport/hasScissor - mixing static + dynamic viewport/scissor on
    /// one pipeline is rejected by validateRenderPipelineState with [VS-E017]
    /// (AGENTS.md RHI Rule 4: no native defaults, no implicit state).
    bool dynamicViewportScissor = false;
    core::pmr::Vector<VertexBufferLayoutDesc> vertexBuffers{&core::defaultCpuMemoryResource()};
    /// Subpass index this pipeline targets within a multi-subpass render pass
    /// (0 = first/only subpass, the default). Backends that create a
    /// render-pass-compatible VkPipeline use this as VkGraphicsPipelineCreateInfo::subpass.
    /// Prototype: only subpass 0 (single-subpass) is exercised by current
    /// renderers; non-zero values are wired through for native multi-subpass
    /// pipeline creation but not yet renderer-validated.
    std::uint32_t subpassIndex = 0;
    /// Full render-pass compatibility contract for APIs whose graphics
    /// pipeline is created against a concrete render pass (Vulkan). Empty
    /// means the legacy single-subpass contract derived from this pipeline's
    /// colorTargets/depthStencil fields.
    RenderPassCompatibilityDesc renderPassCompatibility;
    /// Tessellation entry points are required by the PatchList lowering. They
    /// remain empty for all non-patch topologies. Metal consumes the explicit
    /// control-point count and the generated tessellation ABI; it must not
    /// reinterpret PatchList as triangles.
    ShaderEntrypoint tessellationControlShader;
    ShaderEntrypoint tessellationEvaluationShader;
    std::uint32_t patchControlPointCount = 0;
};

struct ComputePipelineDesc {
    core::pmr::String label{&core::defaultCpuMemoryResource()};
    const PipelineLayout* layout = nullptr;
    const ShaderPackageDesc* shaderPackage = nullptr;
    ShaderEntrypoint computeShader;
};

// L2 creation evidence only. Approved actors alone may construct this original
// owner; callers see immutable intent and live diagnostics, never GPU payloads.
class OriginalComputePipelineCreation : public core::QuotaBoundSharedObjectIdentity {
    friend class rhi_metal::MetalOriginalArtifactComputeCreation;
    friend class rhi_webgpu::WebGpuOriginalArtifactComputeCreation;
    friend class rhi_vulkan::VulkanOriginalArtifactComputeCreation;
    OriginalComputePipelineCreation() = default;

public:
    virtual ~OriginalComputePipelineCreation() = default;
    virtual const std::shared_ptr<const OriginalSingleEntryPipelineInput>&
    originalInput() const noexcept = 0;
    virtual const std::shared_ptr<PipelineLayout>& originalLayout() const noexcept = 0;
    virtual const ComputePipelineDesc& definition() const noexcept = 0;
    virtual const char* validate() const noexcept = 0;
    virtual bool usesCpuQuotaDomain(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept = 0;

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalComputePipelineCreation>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-COMPUTE-CREATION-OWNER";
        return self->validate();
    }
};

class OriginalGraphicsPipelineCreation : public core::QuotaBoundSharedObjectIdentity {
    friend class rhi_metal::MetalOriginalArtifactGraphicsCreation;
    friend class rhi_webgpu::WebGpuOriginalArtifactGraphicsCreation;
    friend class rhi_vulkan::VulkanOriginalArtifactGraphicsCreation;
    OriginalGraphicsPipelineCreation() = default;

public:
    virtual ~OriginalGraphicsPipelineCreation() = default;
    virtual const std::shared_ptr<const OriginalGraphicsPipelineInput>&
    originalInput() const noexcept = 0;
    virtual const std::shared_ptr<PipelineLayout>& originalLayout() const noexcept = 0;
    virtual const RenderPipelineDesc& definition() const noexcept = 0;
    virtual const char* validate() const noexcept = 0;
    virtual bool usesCpuQuotaDomain(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept = 0;

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalGraphicsPipelineCreation>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-GRAPHICS-CREATION-OWNER";
        return self->validate();
    }
};

struct PipelineValidationReport {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    Backend backend = Backend::Null;
    std::uint32_t entrypointCount = 0;
    std::uint32_t descriptorBindingCount = 0;
    std::uint32_t descriptorLayoutCount = 0;
    std::uint32_t descriptorSetCount = 0;
    std::uint32_t descriptorTableCount = 0;
    std::uint32_t pushConstantBytes = 0;
    bool shaderPackageValidated = false;
    bool reflectionMatched = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t colorTargetCount = 0;
    std::pmr::vector<TextureFormat> colorFormats{&core::defaultCpuMemoryResource()};
    TextureFormat depthStencilFormat = TextureFormat::Unknown;
    std::uint32_t sampleCount = 1;
    bool blendStateValidated = false;
    bool colorWriteMaskValidated = false;
    bool stencilStateValidated = false;
    bool depthBiasStateValidated = false;
    bool rasterStateValidated = false;
    bool topologyValidated = false;
    bool msaaStateValidated = false;
    bool resolveStateValidated = false;
    bool viewportScissorStateValidated = false;
    bool depthOnlyStateValidated = false;
    bool unsupportedStateDiagnosed = false;
    bool pipelineCacheKeyStable = false;
    std::uint64_t pipelineCacheKey = 0;
    /// Backend lowering may source selected-entrypoint metadata from the
    /// generated reflection artifact rather than a hand-built package view.
    /// These fields are transport-only and remain backend-neutral.
    bool selectedFragmentWritesRenderTargetArrayIndex = false;
    std::uint32_t selectedComputeWorkgroupSizeX = 0;
    std::uint32_t selectedComputeWorkgroupSizeY = 0;
    std::uint32_t selectedComputeWorkgroupSizeZ = 0;
};

// Private factory custody of exact original layout and copied package metadata.
// Paths/hydration are metadata only, never immutable native artifact evidence.
class OriginalRegisteredPipeline : public OriginalRegisteredObject {
    friend class rhi_metal::MetalAcceptedOriginalComputePipelineFactory;
    friend class rhi_metal::MetalAcceptedOriginalGraphicsPipelineFactory;
    friend class GpuDevice;
    friend class rhi_webgpu::WebGpuAcceptedOriginalComputePipelineFactory;
    friend class rhi_webgpu::WebGpuAcceptedOriginalGraphicsPipelineFactory;
    friend class rhi_vulkan::VulkanAcceptedOriginalGraphicsPipelineFactory;
    friend class rhi_vulkan::VulkanAcceptedOriginalComputePipelineFactory;

public:
    // The accepted factory records allocation provenance. Shared publication
    // uses that same quota; generic shareUnique cannot claim this receipt.
    template <class T>
    static std::shared_ptr<T> shareAcceptedOriginal(
        core::UniquePtr<T> object, const char*& diagnostic)
    {
        static_assert(std::is_same_v<T, RenderPipeline> || std::is_same_v<T, ComputePipeline>);
        diagnostic = nullptr;
        if (!object || !object->originalWrapperQuota_ || !object->originalWrapperCpuBytes_) {
            diagnostic = "RHI-ORIGINAL-PIPELINE-SHARED-QUOTA-RECEIPT";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(object->originalWrapperQuota_);
            auto shared = core::shareUnique(std::move(object));
            shared->originalAcceptedShared_ = true;
            return shared;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-PIPELINE-SHARED-ALLOCATION";
            return {};
        }
    }

    bool originalAcceptedSharedQuotaBound() const noexcept
    {
        return originalAcceptedShared_ && originalWrapperQuota_
            && originalWrapperQuota_->limitBytes() == originalWrapperCpuBytes_;
    }

    const auto& originalLayoutOwner() const noexcept { return originalLayoutOwner_; }

    const auto& originalPackageOwner() const noexcept { return originalPackageOwner_; }

    const auto& originalRenderDefinitionOwner() const noexcept { return originalRenderDefinition_; }

    const auto& originalComputeDefinitionOwner() const noexcept
    {
        return originalComputeDefinition_;
    }

    const char* independentAcceptedCpuQuotaDiagnostic(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (const auto* id = originalResourceRegistration().independentCpuQuotaDiagnostic(quota))
            return id;
        if (!originalWrapperQuota_
            || originalWrapperQuota_->limitBytes() != originalWrapperCpuBytes_)
            return "RHI-ORIGINAL-PIPELINE-WRAPPER-QUOTA";
        if (core::SharedOwnerProjection::overlapsOwner(quota, originalWrapperQuota_)
            || (originalComputeCreation_ && originalComputeCreation_->usesCpuQuotaDomain(quota))
            || (originalGraphicsCreation_ && originalGraphicsCreation_->usesCpuQuotaDomain(quota)))
            return "RHI-ORIGINAL-PIPELINE-QUOTA-DOMAIN";
        return nullptr;
    }

    const ShaderPackageDesc* originalShaderPackageMetadata() const noexcept
    {
        return originalPackageOwner_.get();
    }

    const RenderPipelineDesc* originalRenderPipelineDefinition() const noexcept
    {
        return originalRenderDefinition_.get();
    }

    const ComputePipelineDesc* originalComputePipelineDefinition() const noexcept
    {
        return originalComputeDefinition_.get();
    }

    const auto& originalComputeCreation() const noexcept { return originalComputeCreation_; }

    const char* originalArtifactComputeDiagnostic() const noexcept
    {
        if (const auto* id = originalFullDefinitionDiagnostic())
            return id;
        if (const auto* id =
                OriginalComputePipelineCreation::validateOriginal(originalComputeCreation_))
            return id;
        if (const auto* id = acceptedWrapperSealDiagnostic(originalComputeCreation_))
            return id;
        if (originalComputeCreation_->originalLayout() != originalLayoutOwner_)
            return "RHI-ORIGINAL-COMPUTE-CREATION-LAYOUT";
        return nullptr;
    }

    const auto& originalGraphicsCreation() const noexcept { return originalGraphicsCreation_; }

    const char* originalArtifactGraphicsDiagnostic() const noexcept
    {
        if (const auto* id = originalFullDefinitionDiagnostic())
            return id;
        if (const auto* id =
                OriginalGraphicsPipelineCreation::validateOriginal(originalGraphicsCreation_))
            return id;
        if (const auto* id = acceptedWrapperSealDiagnostic(originalGraphicsCreation_))
            return id;
        if (originalGraphicsCreation_->originalLayout() != originalLayoutOwner_)
            return "RHI-ORIGINAL-GRAPHICS-CREATION-LAYOUT";
        return nullptr;
    }

    const char* originalDescriptorBufferBindingDiagnostic(
        const OriginalDescriptorBufferBindingContract::Request& request,
        DescriptorType type,
        std::uint64_t bufferBytes,
        std::uint32_t dynamicOffset) const noexcept
    {
        if (const auto* id = OriginalDescriptorBufferBindingContract::validateRequest(request))
            return id;
        const auto kind = originalRegisteredKind();
        if (kind == OriginalRegisteredResourceKind::ComputePipeline) {
            if (const auto* id = originalArtifactComputeDiagnostic())
                return id;
        } else if (kind == OriginalRegisteredResourceKind::GraphicsPipeline) {
            if (const auto* id = originalArtifactGraphicsDiagnostic())
                return id;
        } else
            return "RHI-ORIGINAL-DESCRIPTOR-BUFFER-PIPELINE-KIND";
        return OriginalDescriptorBufferBindingContract::validateBinding(
            type, bufferBytes, dynamicOffset, originalDescriptorBufferLimits_);
    }

    const char* originalFullDefinitionDiagnostic() const noexcept
    {
        if (const auto* id = originalResourceRegistration().diagnostic())
            return id;
        if (!originalLayoutOwner_
            || !originalLayoutOwner_->sharedOwnerMatches(originalLayoutOwner_))
            return "RHI-ORIGINAL-PIPELINE-LAYOUT-OWNER";
        if (!originalResourceRegistration().matchesParent(
                originalLayoutOwner_->originalResourceRegistration()))
            return "RHI-ORIGINAL-PIPELINE-DEFINITION-PARENT";
        const auto kind = originalRegisteredKind();
        const auto* render = originalRenderPipelineDefinition();
        const auto* compute = originalComputePipelineDefinition();
        if ((kind == OriginalRegisteredResourceKind::GraphicsPipeline && (!render || compute))
            || (kind == OriginalRegisteredResourceKind::ComputePipeline && (!compute || render)))
            return "RHI-ORIGINAL-PIPELINE-FULL-DEFINITION-MISSING";
        if (!originalPackageOwner_
            || (render
                && (render->layout != originalLayoutOwner_.get()
                    || render->shaderPackage != originalPackageOwner_.get()))
            || (compute
                && (compute->layout != originalLayoutOwner_.get()
                    || compute->shaderPackage != originalPackageOwner_.get())))
            return "RHI-ORIGINAL-PIPELINE-DEFINITION-RELATIONSHIP";
        return nullptr;
    }

protected:
    explicit OriginalRegisteredPipeline(OriginalRegisteredResourceKind kind) noexcept
        : OriginalRegisteredObject(kind)
    {}

    ~OriginalRegisteredPipeline() = default;

private:
    const char* acceptedWrapperSealDiagnostic(
        const std::shared_ptr<const OriginalComputePipelineCreation>& creation) const noexcept
    {
        return creation
                && core::SharedOwnerProjection::sameOwner(
                    acceptedComputeWrapperCreation_.lock(), creation)
            ? nullptr
            : "RHI-ACCEPTED-COMPUTE-FACTORY-WRAPPER-SEAL";
    }

    const char* acceptedWrapperSealDiagnostic(
        const std::shared_ptr<const OriginalGraphicsPipelineCreation>& creation) const noexcept
    {
        return creation
                && core::SharedOwnerProjection::sameOwner(
                    acceptedGraphicsWrapperCreation_.lock(), creation)
            ? nullptr
            : "RHI-ACCEPTED-GRAPHICS-FACTORY-WRAPPER-SEAL";
    }

    OriginalDescriptorBufferBindingContract::Limits originalDescriptorBufferLimits_;
    std::weak_ptr<const OriginalComputePipelineCreation> acceptedComputeWrapperCreation_;
    std::weak_ptr<const OriginalGraphicsPipelineCreation> acceptedGraphicsWrapperCreation_;
    std::shared_ptr<core::CpuMemoryQuota> originalWrapperQuota_;
    std::uint64_t originalWrapperCpuBytes_ = 0u;
    bool originalAcceptedShared_ = false;
    std::shared_ptr<PipelineLayout> originalLayoutOwner_;
    std::shared_ptr<const ShaderPackageDesc> originalPackageOwner_;
    std::shared_ptr<const RenderPipelineDesc> originalRenderDefinition_;
    std::shared_ptr<const ComputePipelineDesc> originalComputeDefinition_;
    std::shared_ptr<const OriginalComputePipelineCreation> originalComputeCreation_;
    std::shared_ptr<const OriginalGraphicsPipelineCreation> originalGraphicsCreation_;
};

// Range requirements only, never a proof of member interpretation, shader
// arithmetic, resource content or execution. Zero alignment is explicitly
// unknown here; descriptor/device alignment validation remains independent.
struct ShaderBufferMemoryRequirement {
    enum class Evidence { None, BinaryDeclaration, ValidatedTargetDeclaration, DriverQuery };
    std::uint64_t sourceOccupiedBytes = 0u, minimumBytes = 0u, alignmentBytes = 0u;
    // /2 admits one complete runtime element and derives capacity from the
    // original bound range. Zero target stride means target layout is unknown;
    // DriverQuery alone never proves member/array-layout equivalence.
    std::uint64_t sourceMinimumBytes = 0u;
    bool hasRuntimeArray = false;
    std::uint64_t sourceRuntimeArrayOffsetBytes = 0u, sourceRuntimeArrayStrideBytes = 0u;
    std::uint64_t targetRuntimeArrayOffsetBytes = 0u, targetRuntimeArrayStrideBytes = 0u;
    std::uint64_t sourceElementCapacity = 0u;
    Evidence evidence = Evidence::None;
};

class ARP_API RenderPipeline : public core::SharedObjectIdentity,
                               public OriginalRegisteredPipeline {
public:
    RenderPipeline() noexcept
        : OriginalRegisteredPipeline(OriginalRegisteredResourceKind::GraphicsPipeline)
    {}

    virtual ~RenderPipeline() = default;
    virtual const PipelineLayout* layout() const = 0;
    virtual const PipelineValidationReport& validationReport() const = 0;
    virtual const ShaderReflectionDesc& shaderReflection() const = 0;
    virtual ShaderSourceLanguage shaderSourceLanguage() const = 0;
    virtual bool backendFallbacksHiddenFromUpperLayer() const = 0;
    virtual bool nativeHandleExposedToUpperLayer() const = 0;

    /// Opaque issuer ticket for explicitly authorized BindlessCross pipelines.
    /// Ordinary pipeline implementations return an empty pointer.
    virtual std::shared_ptr<const BindlessAuthorization> bindlessAuthorization() const noexcept
    {
        return {};
    }

    // PreparedGraphicsBinding/1 uses selected retained entries, never inferred
    // descriptor visibility. None keeps unimplemented preparation closed.
    virtual ShaderStage bindlessShaderStages() const noexcept { return ShaderStage::None; }

    // SelectedGraphicsEntry/1 borrows the exact module retained by this
    // authorized factory-created pipeline. Equal reflection is not identity.
    // The authorization keeps the package alive; ordinary/unknown profiles
    // return null. This metadata grants no recording or completion authority.
    virtual const BindlessPackageModule* bindlessShaderModule(
        ShaderStage, std::uint32_t) const noexcept
    {
        return nullptr;
    }

    // OrdinaryGraphicsBufferRange/1 queries factory-frozen declaration or real
    // native reflection facts. Default/unknown profiles fail with empty output.
    virtual const char* bindlessOrdinaryBufferRequirement(ShaderStage,
        std::uint32_t,
        std::uint32_t,
        ShaderBufferMemoryRequirement& output,
        std::uint32_t) const noexcept
    {
        output = {};
        return "RHI-BINDLESS-ORDINARY-RANGE-UNAVAILABLE";
    }

    // OriginalGraphicsMemberMemory/1: factory-frozen static member proof only.
    // Query2 adds runtime-tail member facts, never a bound element capacity.
    // Range-only requirements, caller metadata and ordinary pipelines refuse.
    virtual const char* bindlessOrdinaryBufferMemberMemory(ShaderStage,
        std::uint32_t,
        std::uint32_t,
        ShaderBufferMemberMemory& output,
        std::uint32_t) const noexcept
    {
        output = {};
        return "RHI-BINDLESS-MEMBER-MEMORY-UNAVAILABLE";
    }

    // BindlessGeometryDefinition/1: borrowed immutable, factory-frozen L2
    // layout. It grants no initialized fetch or recording/execution authority.
    // An unsupported backend/version fails closed; callers cannot supply it.
    virtual const core::pmr::Vector<VertexBufferLayoutDesc>* bindlessVertexBuffers(
        std::uint32_t) const noexcept
    {
        return nullptr;
    }

    /// Native render-bundle encoders must validate that the recording
    /// topology matches the consuming pipeline. The default keeps existing
    /// pipeline implementations source-compatible; concrete backends should
    /// return the descriptor's topology when they expose executable bundles.
    virtual PrimitiveTopology primitiveTopology() const { return PrimitiveTopology::TriangleList; }

    /// Pipeline fact consumed by the shared tessellation-factor-buffer
    /// lifecycle. The default keeps existing non-tessellation backends
    /// source-compatible; PatchList backends override it explicitly.
    virtual bool usesPatchListTessellation() const { return false; }
};

class ARP_API ComputePipeline : public core::SharedObjectIdentity,
                                public OriginalRegisteredPipeline {
public:
    ComputePipeline() noexcept
        : OriginalRegisteredPipeline(OriginalRegisteredResourceKind::ComputePipeline)
    {}

    virtual ~ComputePipeline() = default;
    virtual const PipelineLayout* layout() const = 0;
    virtual const PipelineValidationReport& validationReport() const = 0;
    virtual const ShaderReflectionDesc& shaderReflection() const = 0;
    virtual ShaderSourceLanguage shaderSourceLanguage() const = 0;
    virtual bool backendFallbacksHiddenFromUpperLayer() const = 0;
    virtual bool nativeHandleExposedToUpperLayer() const = 0;

    /// Opaque issuer ticket for explicitly authorized BindlessCross pipelines.
    /// Ordinary pipeline implementations return an empty pointer.
    virtual std::shared_ptr<const BindlessAuthorization> bindlessAuthorization() const noexcept
    {
        return {};
    }
};

template <class T> struct OriginalRegisteredPipelineTraits {
    using Base = std::conditional_t<std::is_base_of_v<PipelineLayout, T>,
        PipelineLayout,
        std::conditional_t<std::is_base_of_v<RenderPipeline, T>, RenderPipeline, ComputePipeline>>;
    static_assert(std::is_base_of_v<Base, T>);
};

struct PipelineStateValidationResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    bool unsupportedStateDiagnosed = false;
    std::uint32_t colorTargetCount = 0;
    std::pmr::vector<TextureFormat> colorFormats{&core::defaultCpuMemoryResource()};
    TextureFormat depthStencilFormat = TextureFormat::Unknown;
    std::uint32_t sampleCount = 1;
    bool blendStateValidated = false;
    bool colorWriteMaskValidated = false;
    bool stencilStateValidated = false;
    bool depthBiasStateValidated = false;
    bool rasterStateValidated = false;
    bool topologyValidated = false;
    bool msaaStateValidated = false;
    bool resolveStateValidated = false;
    bool viewportScissorStateValidated = false;
    bool depthOnlyStateValidated = false;
    bool vertexInputStateValidated = false;
    std::uint64_t pipelineCacheKey = 0;
};

inline bool hasColorWriteMask(ColorWriteMask mask);

inline bool isColorRenderTargetFormat(TextureFormat format);

inline bool isDepthStencilFormat(TextureFormat format);

inline bool hasStencilAspect(TextureFormat format);

inline bool isSupportedMultisampleCount(std::uint32_t sampleCount);

inline bool isSupportedPrimitiveTopology(PrimitiveTopology topology);

inline bool isSupportedCullMode(CullMode cullMode);

inline bool isSupportedFrontFace(FrontFace frontFace);

inline std::uint32_t pipelineFloatBits(float value);

inline ColorTargetState fallbackColorTargetState(TextureFormat format);

inline std::uint32_t effectiveColorTargetCount(const RenderPipelineDesc& desc);

inline ColorTargetState effectiveColorTargetAt(const RenderPipelineDesc& desc, std::uint32_t index);

inline TextureFormat effectiveDepthStencilFormat(const RenderPipelineDesc& desc);

inline bool renderPipelineHasDepthStencilState(const RenderPipelineDesc& desc);

inline bool isDepthOnlyRenderPipeline(const RenderPipelineDesc& desc);

// Forward declaration: stablePipelineLayoutKey is defined below (after the
// render/compute state-key functions) but is used by those keys to fold the
// FULL pipeline layout (every descriptor set, binding, table and push-constant
// range) into the cache key. See the definition for the hashed fields.
inline std::uint64_t stablePipelineLayoutKey(const PipelineLayoutDesc& desc);

inline std::uint64_t stableRenderPipelineStateKey(const RenderPipelineDesc& desc);

inline bool renderPipelineRequiresResolve(const RenderPipelineDesc& desc);

inline PipelineStateValidationResult validateRenderPipelineState(const RenderPipelineDesc& desc);

/// Stable hash for a compute pipeline state. Combines the shader package
/// hash with the compute entrypoint path/name and a layout-derived key.
/// Used as the cache key for PipelineCache compute pipeline lookup.
inline std::uint64_t stableComputePipelineStateKey(const ComputePipelineDesc& desc);

/// L2 compute pipeline state validation result. Mirrors the shape of
/// `PipelineStateValidationResult` (render) but only carries the compute-
/// relevant validation flags. Like the render variant, this is the shared L2
/// state validator (no device-level reflection checks); device-level
/// `validateComputePipeline` still owns the full reflection/layout match.
struct ComputePipelineStateValidationResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    bool unsupportedStateDiagnosed = false;
    bool shaderStateValidated = false;
    bool layoutStateValidated = false;
    bool pushConstantStateValidated = false;
    bool descriptorBindingStateValidated = false;
    std::uint64_t pipelineCacheKey = 0;
    bool pipelineCacheKeyStable = false;
};

/// Shared L2 compute pipeline state validator. Runs the compute-specific
/// state checks (layout present, shader package present, compute entrypoint
/// is a Slang compute-stage shader) that every backend must satisfy before
/// native pipeline creation. Device-level reflection/layout match checks
/// remain in each backend's `validateComputePipeline` (mirroring the render
/// split between `validateRenderPipelineState` and `validateRenderPipeline`).
/// `lowerComputePipelineState` runs this first so a descriptor that fails
/// L2 state validation produces a deterministic diagnostic failure rather
/// than reaching the native compute pipeline builder.
inline ComputePipelineStateValidationResult validateComputePipelineState(
    const ComputePipelineDesc& desc);

/// Stable hash for a pipeline layout descriptor. Used as the cache key for
/// PipelineCache layout lookup.
inline std::uint64_t stablePipelineLayoutKey(const PipelineLayoutDesc& desc);

class GpuDevice;

/// Backend-owned pipeline cache. Stores RenderPipeline, ComputePipeline, and
/// PipelineLayout objects keyed by their stable state hash. On a cache hit,
/// the existing object is returned without creating a new GPU resource; on a
/// miss, the pipeline/layout is created via the device and stored for future
/// reuse.
///
/// The cache is allocator-compliant (core::pmr containers) and does not
/// expose native handles to the upper layer. Lifetime is backend-owned:
/// callers receive non-owning pointers and must not destroy them.
///
/// Fence-gated eviction: entries not hit for maxUnusedFrames frames are
/// evicted when evictStale is called with the current frame serial.
class ARP_API PipelineCache {
public:
    /// Construct with an explicit CPU memory resource. When omitted, the
    /// process-wide default resource is used. Threaded through to all pmr
    /// members so a backend can bind a per-instance intent/budget (e.g. a
    /// device-scoped arena) instead of silently falling back to the global
    /// default.
    explicit PipelineCache(GpuDevice& device,
        std::pmr::memory_resource* memoryResource = &core::defaultCpuMemoryResource());
    ~PipelineCache();

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    /// Acquire a render pipeline. Returns a cached pipeline on hit, or
    /// creates and caches a new one on miss. Returns nullptr on creation
    /// failure (the device's createRenderPipeline returned null).
    RenderPipeline* acquireRenderPipeline(const RenderPipelineDesc& desc);

    /// Acquire a compute pipeline. Same semantics as acquireRenderPipeline.
    ComputePipeline* acquireComputePipeline(const ComputePipelineDesc& desc);

    /// Acquire a pipeline layout. Same semantics.
    PipelineLayout* acquirePipelineLayout(const PipelineLayoutDesc& desc);

    /// Mark the current frame as complete. All entries hit during this frame
    /// have their lastUsedFrameSerial updated.
    void onFrameComplete();

    /// Evict entries that have not been hit for at least maxUnusedFrames
    /// frames. Called after onFrameComplete with the current frame serial.
    void evictStale(std::uint64_t currentFrameSerial, std::uint32_t maxUnusedFrames);
    /// Release CPU cache ownership at an explicit teardown boundary. This
    /// invalidates borrowed cache pointers; it does not prove GPU completion.
    void clearForShutdown();

    std::uint32_t renderPipelineHitCount() const noexcept { return renderHitCount_; }

    std::uint32_t renderPipelineMissCount() const noexcept { return renderMissCount_; }

    std::uint32_t computePipelineHitCount() const noexcept { return computeHitCount_; }

    std::uint32_t computePipelineMissCount() const noexcept { return computeMissCount_; }

    std::uint32_t layoutHitCount() const noexcept { return layoutHitCount_; }

    std::uint32_t layoutMissCount() const noexcept { return layoutMissCount_; }

    std::uint32_t entryCount() const noexcept;

    /// Must be called before the first cache entry is created. An invalid or
    /// late policy is rejected without changing cache state.
    bool configureBudget(const semantics::CacheBudget& budget) noexcept;
    semantics::CacheBudgetStats budgetStats() const noexcept;

    const char* lastBudgetDiagnostic() const noexcept { return lastBudgetDiagnostic_; }

    /// Reset all telemetry counters (for testing).
    void resetTelemetry();

private:
    struct RenderEntry {
        arp::core::UniquePtr<RenderPipeline> pipeline;
        std::uint64_t lastUsedFrameSerial = 0;
    };

    struct ComputeEntry {
        arp::core::UniquePtr<ComputePipeline> pipeline;
        std::uint64_t lastUsedFrameSerial = 0;
    };

    struct LayoutEntry {
        arp::core::UniquePtr<PipelineLayout> layout;
        std::uint64_t lastUsedFrameSerial = 0;
    };

    GpuDevice& device_;
    std::pmr::memory_resource* memoryResource_;
    core::pmr::UnorderedMap<std::uint64_t, RenderEntry> renderCache_;
    core::pmr::UnorderedMap<std::uint64_t, ComputeEntry> computeCache_;
    core::pmr::UnorderedMap<std::uint64_t, LayoutEntry> layoutCache_;
    std::uint64_t currentFrameSerial_ = 0;
    std::uint32_t renderHitCount_ = 0;
    std::uint32_t renderMissCount_ = 0;
    std::uint32_t computeHitCount_ = 0;
    std::uint32_t computeMissCount_ = 0;
    std::uint32_t layoutHitCount_ = 0;
    std::uint32_t layoutMissCount_ = 0;
    semantics::CacheBudget budget_;
    std::uint64_t peakEntryCount_ = 0u;
    std::uint64_t budgetRejectionCount_ = 0u;
    const char* lastBudgetDiagnostic_ = nullptr;
};

} // namespace arp::rhi

namespace arp::shader {
class SingleEntryShaderArtifact;
class GraphicsShaderModuleArtifact;
} // namespace arp::shader

namespace arp::rhi {

class VertexInputConsumptionContract;
ARP_API ValidationResult validateReflectedVertexInput(
    const VertexInputConsumptionContract&, const shader::ShaderReflection&);
ARP_API ValidationResult validateSingleEntryVertexInput(
    const VertexInputConsumptionContract&, const shader::SingleEntryShaderArtifact&);

ARP_API std::optional<DescriptorType> descriptorTypeFromShaderBinding(shader::BindingType) noexcept;

inline constexpr std::optional<ShaderStage> shaderStageFromCompilerStage(
    shader::ShaderStage stage) noexcept
{
    switch (stage) {
    case shader::ShaderStage::Vertex:
        return ShaderStage::Vertex;
    case shader::ShaderStage::Fragment:
        return ShaderStage::Fragment;
    case shader::ShaderStage::Compute:
        return ShaderStage::Compute;
    case shader::ShaderStage::TessellationControl:
        return ShaderStage::TessellationControl;
    case shader::ShaderStage::TessellationEvaluation:
        return ShaderStage::TessellationEvaluation;
    }
    return std::nullopt;
}

// Sealed startup facts only; the caller supplies quota scope and owns the result.
ARP_API ValidationResult populateSingleEntryShaderInterface(
    const shader::SingleEntryShaderArtifact&, ShaderPackageDesc&);

// Versioned deterministic metadata merge. It grants no artifact/native proof.
class ARP_API GraphicsShaderInterfaceMerger final {
public:
    static ValidationResult merge(std::uint32_t contractVersion,
        const ShaderPackageDesc& vertex,
        const ShaderPackageDesc& fragment,
        ShaderPackageDesc& output);
};

// Vertex-only is an explicit selected profile used by depth-only providers.
ARP_API ValidationResult populateSelectedGraphicsShaderInterface(
    const shader::SingleEntryShaderArtifact& vertex,
    const shader::SingleEntryShaderArtifact* fragment,
    ShaderPackageDesc&);
ARP_API ValidationResult populateGraphicsModuleShaderInterface(
    const shader::GraphicsShaderModuleArtifact&, ShaderPackageDesc&);

// OriginalAvailableTargetRuntimeLengthInput/1 cold memory conversion only.
// Generic package validation still rejects the resulting missing-MSL package.
ARP_API ValidationResult populateRuntimeLengthShaderInterface(
    const std::shared_ptr<const shader::SingleEntryShaderArtifact>&, ShaderPackageDesc&);
ARP_API ValidationResult populateRuntimeLengthShaderInterface(
    const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>&, ShaderPackageDesc&);

// Copy artifact-owned ABI facts from validated reflection. Logical descriptor
// bindings are authored/merged separately and remain untouched.
ARP_API void copyArtifactOwnedShaderReflection(
    ShaderReflectionDesc&, const shader::ShaderReflection&);

} // namespace arp::rhi

// Inline definitions follow the complete public declarations.
#include "rhi/frontend/pipeline_interface_validation.inl"
