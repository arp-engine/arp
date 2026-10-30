#include "core/memory/cpu_allocator.h"
#include "rhi/backends/webgpu/native_validation.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"
#include "rhi/backends/webgpu/webgpu_completed_texture_publication.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_creation.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"
#include "rhi/backends/webgpu/webgpu_ordinary_resource_retention.h"
#include "rhi/backends/webgpu/webgpu_original_general_binding_lowering.h"
#include "rhi/backends/webgpu/webgpu_original_runtime_length_interval_lowering.h"
#include "rhi/backends/webgpu/webgpu_original_whole_buffer_binding_lowering.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/frontend/bindless_initialized_graphics.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/buffer_placement.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/descriptor_general_write_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/descriptor_table_publication.h"
#include "rhi/frontend/pipeline_shader_interface_validation.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/frontend/pipeline_vertex_slots.h"
#include "rhi/gpu_bindless.h"
#include "rhi/semantics/bindless_ordinary_depth_pipeline.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_scene_raster.h"
#include "rhi/semantics/command_lowering.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <variant>

#if defined(__EMSCRIPTEN__) && defined(__has_include)
#if __has_include(<webgpu/webgpu.h>)
#define ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE 1
#include <emscripten/emscripten.h>
#include <webgpu/webgpu.h>
#endif
#endif

namespace arp::rhi_webgpu {

#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
WGPUTextureFormat nativeTextureFormat(rhi::TextureFormat format) noexcept
{
    switch (format) {
    case rhi::TextureFormat::Bgra8Unorm:
        return WGPUTextureFormat_BGRA8Unorm;
    case rhi::TextureFormat::Rgba8Unorm:
        return WGPUTextureFormat_RGBA8Unorm;
    case rhi::TextureFormat::Rgba8UnormSrgb:
        return WGPUTextureFormat_RGBA8UnormSrgb;
    case rhi::TextureFormat::R8Unorm:
        return WGPUTextureFormat_R8Unorm;
    case rhi::TextureFormat::Rgba16Float:
        return WGPUTextureFormat_RGBA16Float;
    case rhi::TextureFormat::Rg16Float:
        return WGPUTextureFormat_RG16Float;
    case rhi::TextureFormat::R32Float:
        return WGPUTextureFormat_R32Float;
    case rhi::TextureFormat::D32Float:
        return WGPUTextureFormat_Depth32Float;
    case rhi::TextureFormat::Bc1Unorm:
        return WGPUTextureFormat_BC1RGBAUnorm;
    case rhi::TextureFormat::Bc3Unorm:
        return WGPUTextureFormat_BC3RGBAUnorm;
    case rhi::TextureFormat::Bc7Unorm:
        return WGPUTextureFormat_BC7RGBAUnorm;
    case rhi::TextureFormat::Bc7UnormSrgb:
        return WGPUTextureFormat_BC7RGBAUnormSrgb;
    case rhi::TextureFormat::Depth24Stencil8:
    case rhi::TextureFormat::Unknown:
        break;
    }
    return WGPUTextureFormat_Undefined;
}
#endif

namespace {

// Shared cold query for finite, whole and selected table group preparation.
// Actual device/pipeline methods are facts, independently of execution grants.
rhi::OriginalFiniteDescriptorWriteContract::Facts originalDescriptorGroupMethods() noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    const auto mask = static_cast<std::uint32_t>(EM_ASM_INT({
        // clang-format off
        const d = Module.preinitializedWebGPUDevice;
        return (d ? 1 : 0) | (d && typeof d.createBindGroup === 'function' ? 2 : 0) |
            (typeof GPUComputePipeline !== 'undefined' &&
                    typeof GPUComputePipeline.prototype.getBindGroupLayout === 'function' &&
                    typeof GPURenderPipeline !== 'undefined' &&
                    typeof GPURenderPipeline.prototype.getBindGroupLayout === 'function' ? 4 : 0) |
            (d && typeof d.pushErrorScope === 'function' ? 8 : 0) |
            (d && typeof d.popErrorScope === 'function' ? 16 : 0);
        // clang-format on
    }));
    return {bool(mask & 1u), bool(mask & 2u), bool(mask & 4u), bool(mask & 8u), bool(mask & 16u)};
#else
    return {};
#endif
}

#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)

bool browserWebGpuEnvironmentAvailable()
{
    return emscripten_run_script_int("typeof navigator !== 'undefined' && !!navigator.gpu"
                                     "&& typeof Module !== 'undefined'"
                                     "&& !!Module.preinitializedWebGPUDevice"
                                     "&& !Module.webGpuDeviceLostInfo")
        != 0;
}

struct OwnedDeviceRef {
    WGPUDevice value = nullptr;

    explicit OwnedDeviceRef(WGPUDevice device) : value(device) {}

    ~OwnedDeviceRef()
    {
        if (value && !webGpuNativeDeviceLost()) {
            wgpuDeviceRelease(value);
        }
    }

    OwnedDeviceRef(const OwnedDeviceRef&) = delete;
    OwnedDeviceRef& operator=(const OwnedDeviceRef&) = delete;
};

void appendNativeShaderStageFact(WebGpuNativePipelineOwnership& pipeline,
    const WebGpuNativeShaderModulePayload& module,
    rhi::ShaderStage stage,
    core::StringView entry)
{
    WebGpuNativePipelineStageFact fact;
    fact.stage = stage;
    fact.entry.assign(entry.data(), entry.size());
    fact.sourceSha256 = module.sourceSha256();
    fact.sourceBytes = module.sourceBytes();
    fact.moduleIdentity = module.module;
    pipeline.pendingShaderFacts.push_back(std::move(fact));
}

WGPUVertexFormat nativeVertexFormat(rhi::VertexFormat format)
{
    switch (format) {
    case rhi::VertexFormat::Float32x1:
        return WGPUVertexFormat_Float32;
    case rhi::VertexFormat::Float32x2:
        return WGPUVertexFormat_Float32x2;
    case rhi::VertexFormat::Float32x3:
        return WGPUVertexFormat_Float32x3;
    case rhi::VertexFormat::Float32x4:
        return WGPUVertexFormat_Float32x4;
    }
    return static_cast<WGPUVertexFormat>(0);
}

// Cold backend-owned storage shared by ordinary and bindless creation. Native
// descriptors borrow this storage only for the synchronous creation call.
class WebGpuNativeVertexLayouts final {
public:
    enum class SlotMapping { PhysicalOrdinals, LogicalSlots };

    explicit WebGpuNativeVertexLayouts(const core::pmr::Vector<rhi::VertexBufferLayoutDesc>& source,
        SlotMapping mapping = SlotMapping::PhysicalOrdinals)
    {
        std::uint32_t positionalSlots = 0u;
        if (mapping == SlotMapping::LogicalSlots) {
            diagnostic_ = rhi::VertexStreamSlotLayout::count(
                source, webGpuNativeDeviceLimits().maxVertexBuffers, positionalSlots);
            if (diagnostic_)
                return;
        }
        const auto count = mapping == SlotMapping::LogicalSlots ? positionalSlots : source.size();
        // Undefined stepMode explicitly represents an unbound positional gap.
        layouts_.resize(count, WGPU_VERTEX_BUFFER_LAYOUT_INIT);
        attributes_.resize(source.size());
        for (std::size_t i = 0u; i < source.size(); ++i) {
            const auto& input = source[i];
            auto& attributes = attributes_[i];
            attributes.reserve(input.attributes.size());
            for (const auto& attribute : input.attributes) {
                WGPUVertexAttribute native{};
                native.format = nativeVertexFormat(attribute.format);
                native.offset = attribute.offsetBytes;
                native.shaderLocation = attribute.shaderLocation;
                attributes.push_back(native);
            }
            auto& layout = layouts_[mapping == SlotMapping::LogicalSlots ? input.slot : i];
            layout.arrayStride = input.strideBytes;
            layout.stepMode = input.inputRate == rhi::VertexInputRate::Instance
                ? WGPUVertexStepMode_Instance
                : WGPUVertexStepMode_Vertex;
            layout.attributeCount = attributes.size();
            layout.attributes = attributes.empty() ? nullptr : attributes.data();
        }
    }

    bool apply(WGPUVertexState& state) const noexcept
    {
        if (diagnostic_) {
            std::printf("[arp-webgpu] vertex layout rejected: %s\n", diagnostic_);
            return false;
        }
        state.bufferCount = layouts_.size();
        state.buffers = layouts_.empty() ? nullptr : layouts_.data();
        return true;
    }

private:
    const char* diagnostic_ = nullptr;
    core::pmr::Vector<WGPUVertexBufferLayout> layouts_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::Vector<WGPUVertexAttribute>> attributes_{
        &core::defaultCpuMemoryResource()};
};
#endif

bool hasResourceUsage(rhi::ResourceUsage usage, rhi::ResourceUsage flag)
{
    return static_cast<std::uint32_t>(usage & flag) != 0u;
}

bool hasTextureUsage(rhi::TextureUsage usage, rhi::TextureUsage flag)
{
    return static_cast<std::uint32_t>(usage & flag) != 0u;
}

#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
WGPUBufferUsage nativeBufferUsage(rhi::ResourceUsage usage)
{
    WGPUBufferUsage nativeUsage = static_cast<WGPUBufferUsage>(0);
    if (hasResourceUsage(usage, rhi::ResourceUsage::VertexBuffer)) {
        nativeUsage = static_cast<WGPUBufferUsage>(nativeUsage | WGPUBufferUsage_Vertex);
    }
    if (hasResourceUsage(usage, rhi::ResourceUsage::IndexBuffer)) {
        nativeUsage = static_cast<WGPUBufferUsage>(nativeUsage | WGPUBufferUsage_Index);
    }
    if (hasResourceUsage(usage, rhi::ResourceUsage::UniformBuffer)) {
        nativeUsage = static_cast<WGPUBufferUsage>(nativeUsage | WGPUBufferUsage_Uniform);
    }
    if (hasResourceUsage(usage, rhi::ResourceUsage::StorageBuffer)) {
        nativeUsage = static_cast<WGPUBufferUsage>(nativeUsage | WGPUBufferUsage_Storage);
    }
    if (hasResourceUsage(usage, rhi::ResourceUsage::CopySrc)) {
        nativeUsage = static_cast<WGPUBufferUsage>(nativeUsage | WGPUBufferUsage_CopySrc);
    }
    if (hasResourceUsage(usage, rhi::ResourceUsage::CopyDst)) {
        nativeUsage = static_cast<WGPUBufferUsage>(nativeUsage | WGPUBufferUsage_CopyDst);
    }
    if (hasResourceUsage(usage, rhi::ResourceUsage::IndirectBuffer)) {
        nativeUsage = static_cast<WGPUBufferUsage>(nativeUsage | WGPUBufferUsage_Indirect);
    }
    return nativeUsage;
}

// Shared native descriptor lowering for ordinary cold creation and the
// preallocated original readback envelope. No allocation or native call.
bool lowerNativeBufferCreation(
    const rhi::BufferDesc& desc, WGPUBufferDescriptor& bufferDesc) noexcept
{
    const auto usage = nativeBufferUsage(desc.usage);
    if (!desc.sizeBytes || !usage)
        return false;
    bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.label = webGpuNativeStringView(desc.label);
    bufferDesc.size = desc.sizeBytes;
    bufferDesc.mappedAtCreation = false;
    bufferDesc.usage = usage;
    if (desc.memory == rhi::MemoryUsage::GpuToCpu)
        bufferDesc.usage = static_cast<WGPUBufferUsage>(bufferDesc.usage | WGPUBufferUsage_MapRead);
    return true;
}

WGPUTextureUsage nativeTextureUsage(rhi::TextureUsage usage)
{
    WGPUTextureUsage nativeUsage = static_cast<WGPUTextureUsage>(0);
    if (hasTextureUsage(usage, rhi::TextureUsage::Sampled)) {
        nativeUsage = static_cast<WGPUTextureUsage>(nativeUsage | WGPUTextureUsage_TextureBinding);
    }
    if (hasTextureUsage(usage, rhi::TextureUsage::Storage)) {
        nativeUsage = static_cast<WGPUTextureUsage>(nativeUsage | WGPUTextureUsage_StorageBinding);
    }
    if (hasTextureUsage(usage, rhi::TextureUsage::RenderAttachment)) {
        nativeUsage =
            static_cast<WGPUTextureUsage>(nativeUsage | WGPUTextureUsage_RenderAttachment);
    }
    if (hasTextureUsage(usage, rhi::TextureUsage::CopySrc)) {
        nativeUsage = static_cast<WGPUTextureUsage>(nativeUsage | WGPUTextureUsage_CopySrc);
    }
    if (hasTextureUsage(usage, rhi::TextureUsage::CopyDst)) {
        nativeUsage = static_cast<WGPUTextureUsage>(nativeUsage | WGPUTextureUsage_CopyDst);
    }
    return nativeUsage;
}

WGPUTextureAspect nativeTextureAspect(rhi::TextureAspect aspect)
{
    switch (aspect) {
    case rhi::TextureAspect::Color:
        return WGPUTextureAspect_All;
    case rhi::TextureAspect::Depth:
        return WGPUTextureAspect_DepthOnly;
    case rhi::TextureAspect::Stencil:
        return WGPUTextureAspect_StencilOnly;
    }
    return WGPUTextureAspect_All;
}

WebGpuNativeTextureViewCacheKey textureViewCreationKey(
    const WebGpuNativeTexturePayload& texture, const rhi::TextureViewDesc& desc) noexcept
{
    return {
        static_cast<std::uint32_t>(desc.format == rhi::TextureFormat::Unknown
                ? texture.format
                : nativeTextureFormat(desc.format)),
        desc.usage,
        static_cast<std::uint32_t>(desc.dimension == rhi::TextureViewDimension::Texture3D
                ? WGPUTextureViewDimension_3D
                : desc.dimension == rhi::TextureViewDimension::Texture2DArray
                ? WGPUTextureViewDimension_2DArray
                : desc.dimension == rhi::TextureViewDimension::TextureCube
                ? WGPUTextureViewDimension_Cube
                : WGPUTextureViewDimension_2D),
        desc.baseMipLevel,
        desc.mipLevelCount,
        desc.baseArrayLayer,
        desc.arrayLayerCount,
        static_cast<std::uint32_t>(nativeTextureAspect(desc.aspect)),
    };
}

WGPUFilterMode nativeFilter(rhi::SamplerFilter filter)
{
    return filter == rhi::SamplerFilter::Nearest ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
}

WGPUAddressMode nativeAddressMode(rhi::SamplerAddressMode mode)
{
    return mode == rhi::SamplerAddressMode::Repeat ? WGPUAddressMode_Repeat
                                                   : WGPUAddressMode_ClampToEdge;
}

WGPUPrimitiveTopology nativePrimitiveTopology(rhi::PrimitiveTopology topology)
{
    switch (topology) {
    case rhi::PrimitiveTopology::TriangleList:
        return WGPUPrimitiveTopology_TriangleList;
    case rhi::PrimitiveTopology::TriangleStrip:
        return WGPUPrimitiveTopology_TriangleStrip;
    case rhi::PrimitiveTopology::LineList:
        return WGPUPrimitiveTopology_LineList;
    case rhi::PrimitiveTopology::PointList:
        return WGPUPrimitiveTopology_PointList;
    case rhi::PrimitiveTopology::PatchList:
        break;
    }
    return WGPUPrimitiveTopology_TriangleList;
}

WGPUCullMode nativeCullMode(rhi::CullMode cullMode)
{
    switch (cullMode) {
    case rhi::CullMode::None:
        return WGPUCullMode_None;
    case rhi::CullMode::Front:
        return WGPUCullMode_Front;
    case rhi::CullMode::Back:
        return WGPUCullMode_Back;
    }
    return WGPUCullMode_None;
}

WGPUFrontFace nativeFrontFace(rhi::FrontFace frontFace)
{
    return frontFace == rhi::FrontFace::Clockwise ? WGPUFrontFace_CW : WGPUFrontFace_CCW;
}

WGPUCompareFunction nativeCompareFunction(rhi::CompareOp compare)
{
    switch (compare) {
    case rhi::CompareOp::Never:
        return WGPUCompareFunction_Never;
    case rhi::CompareOp::Less:
        return WGPUCompareFunction_Less;
    case rhi::CompareOp::LessEqual:
        return WGPUCompareFunction_LessEqual;
    case rhi::CompareOp::Equal:
        return WGPUCompareFunction_Equal;
    case rhi::CompareOp::Greater:
        return WGPUCompareFunction_Greater;
    case rhi::CompareOp::GreaterEqual:
        return WGPUCompareFunction_GreaterEqual;
    case rhi::CompareOp::NotEqual:
        return WGPUCompareFunction_NotEqual;
    case rhi::CompareOp::Always:
        return WGPUCompareFunction_Always;
    }
    // Defensive default for an out-of-range enum value. Unified across backends
    // to Never (most conservative: reject all comparisons) to avoid divergent
    // fallback semantics - previously Vulkan/WebGPU returned Always here while
    // Metal returned Never, an inconsistent silent-fallback that violates the
    // "no guesswork fallback" constraint.
    return WGPUCompareFunction_Never;
}

WGPUStencilOperation nativeStencilOperation(rhi::StencilOp operation)
{
    switch (operation) {
    case rhi::StencilOp::Keep:
        return WGPUStencilOperation_Keep;
    case rhi::StencilOp::Zero:
        return WGPUStencilOperation_Zero;
    case rhi::StencilOp::Replace:
        return WGPUStencilOperation_Replace;
    case rhi::StencilOp::IncrementClamp:
        return WGPUStencilOperation_IncrementClamp;
    case rhi::StencilOp::DecrementClamp:
        return WGPUStencilOperation_DecrementClamp;
    case rhi::StencilOp::Invert:
        return WGPUStencilOperation_Invert;
    }
    // L2 StencilOp has no Wrap variants; the mapping is 1:1. Defensive default
    // to Keep (preserve stencil buffer) matches the Vulkan/Metal lowering and
    // avoids a silent semantics divergence on an out-of-range value.
    return WGPUStencilOperation_Keep;
}

WGPUStencilFaceState nativeStencilFaceState(const rhi::StencilFaceState& face)
{
    WGPUStencilFaceState state = WGPU_STENCIL_FACE_STATE_INIT;
    state.failOp = nativeStencilOperation(face.failOp);
    state.depthFailOp = nativeStencilOperation(face.depthFailOp);
    state.passOp = nativeStencilOperation(face.passOp);
    state.compare = nativeCompareFunction(face.compare);
    return state;
}

WGPUBlendFactor nativeBlendFactor(rhi::BlendFactor factor)
{
    switch (factor) {
    case rhi::BlendFactor::Zero:
        return WGPUBlendFactor_Zero;
    case rhi::BlendFactor::One:
        return WGPUBlendFactor_One;
    case rhi::BlendFactor::SrcAlpha:
        return WGPUBlendFactor_SrcAlpha;
    case rhi::BlendFactor::OneMinusSrcAlpha:
        return WGPUBlendFactor_OneMinusSrcAlpha;
    case rhi::BlendFactor::DstAlpha:
        return WGPUBlendFactor_DstAlpha;
    case rhi::BlendFactor::OneMinusDstAlpha:
        return WGPUBlendFactor_OneMinusDstAlpha;
    }
    return WGPUBlendFactor_One;
}

WGPUBlendOperation nativeBlendOperation(rhi::BlendOp operation)
{
    switch (operation) {
    case rhi::BlendOp::Add:
        return WGPUBlendOperation_Add;
    case rhi::BlendOp::Subtract:
        return WGPUBlendOperation_Subtract;
    case rhi::BlendOp::ReverseSubtract:
        return WGPUBlendOperation_ReverseSubtract;
    case rhi::BlendOp::Min:
        return WGPUBlendOperation_Min;
    case rhi::BlendOp::Max:
        return WGPUBlendOperation_Max;
    }
    return WGPUBlendOperation_Add;
}

WGPUColorWriteMask nativeColorWriteMask(rhi::ColorWriteMask mask)
{
    WGPUColorWriteMask flags = WGPUColorWriteMask_None;
    if ((mask & rhi::ColorWriteMask::Red) != rhi::ColorWriteMask::None) {
        flags |= WGPUColorWriteMask_Red;
    }
    if ((mask & rhi::ColorWriteMask::Green) != rhi::ColorWriteMask::None) {
        flags |= WGPUColorWriteMask_Green;
    }
    if ((mask & rhi::ColorWriteMask::Blue) != rhi::ColorWriteMask::None) {
        flags |= WGPUColorWriteMask_Blue;
    }
    if ((mask & rhi::ColorWriteMask::Alpha) != rhi::ColorWriteMask::None) {
        flags |= WGPUColorWriteMask_Alpha;
    }
    return flags;
}

WGPUShaderStage nativeShaderStages(rhi::ShaderStage stages)
{
    WGPUShaderStage nativeStages = static_cast<WGPUShaderStage>(0);
    if (static_cast<std::uint32_t>(stages & rhi::ShaderStage::Vertex) != 0u) {
        nativeStages = static_cast<WGPUShaderStage>(nativeStages | WGPUShaderStage_Vertex);
    }
    if (static_cast<std::uint32_t>(stages & rhi::ShaderStage::Fragment) != 0u) {
        nativeStages = static_cast<WGPUShaderStage>(nativeStages | WGPUShaderStage_Fragment);
    }
    if (static_cast<std::uint32_t>(stages & rhi::ShaderStage::Compute) != 0u) {
        nativeStages = static_cast<WGPUShaderStage>(nativeStages | WGPUShaderStage_Compute);
    }
    return nativeStages;
}

WGPUTextureViewDimension nativeTextureViewDimension(rhi::TextureViewDimension dimension)
{
    switch (dimension) {
    case rhi::TextureViewDimension::Texture2D:
        return WGPUTextureViewDimension_2D;
    case rhi::TextureViewDimension::Texture2DArray:
        return WGPUTextureViewDimension_2DArray;
    case rhi::TextureViewDimension::Texture3D:
        return WGPUTextureViewDimension_3D;
    case rhi::TextureViewDimension::TextureCube:
        return WGPUTextureViewDimension_Cube;
    }
    return WGPUTextureViewDimension_2D;
}

std::optional<WGPUBindGroupLayoutEntry> nativeLayoutEntry(
    const rhi::DescriptorBindingDesc& binding, rhi::DescriptorType physicalType)
{
    if (WebGpuDescriptorArrayAbiContract::validateCount(binding.arrayCount))
        return std::nullopt;
    auto entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = binding.binding;
    entry.bindingArraySize = 0u;
    entry.visibility = nativeShaderStages(binding.stages);
    switch (physicalType) {
    case rhi::DescriptorType::UniformBuffer:
        entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.hasDynamicOffset = binding.dynamicOffset ? WGPU_TRUE : WGPU_FALSE;
        break;
    case rhi::DescriptorType::StorageBuffer:
        entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entry.buffer.type = WGPUBufferBindingType_Storage;
        entry.buffer.hasDynamicOffset = binding.dynamicOffset ? WGPU_TRUE : WGPU_FALSE;
        break;
    case rhi::DescriptorType::ReadOnlyStorageBuffer:
        entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        entry.buffer.hasDynamicOffset = binding.dynamicOffset ? WGPU_TRUE : WGPU_FALSE;
        break;
    case rhi::DescriptorType::SampledTexture:
        entry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        entry.texture.sampleType = binding.depthTexture
            ? WGPUTextureSampleType_Depth
            : (binding.sampledTextureFilterable ? WGPUTextureSampleType_Float
                                                : WGPUTextureSampleType_UnfilterableFloat);
        entry.texture.viewDimension = nativeTextureViewDimension(binding.textureViewDimension);
        break;
    case rhi::DescriptorType::StorageTexture:
        entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        switch (binding.storageTextureAccess) {
        case rhi::StorageTextureAccess::WriteOnly:
            entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
            break;
        case rhi::StorageTextureAccess::ReadOnly:
            entry.storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
            break;
        case rhi::StorageTextureAccess::ReadWrite:
            entry.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
            break;
        default:
            return std::nullopt;
        }
        // WebGPU requires the bind group layout to declare a concrete
        // WGPUTextureFormat for storage textures - it cannot be inferred from
        // the bound view at bind-group creation time. The L2 contract source
        // is DescriptorBindingDesc.storageTextureFormat (default Unknown).
        // If the caller did not set it, this is a contract violation: fall
        // through to WGPUTextureFormat_Undefined, which causes
        // wgpuDeviceCreateBindGroupLayout to fail closed (caught by the
        // caller, which returns nullptr).
        entry.storageTexture.format = nativeTextureFormat(binding.storageTextureFormat);
        entry.storageTexture.viewDimension =
            nativeTextureViewDimension(binding.textureViewDimension);
        break;
    case rhi::DescriptorType::Sampler:
        entry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        entry.sampler.type = binding.comparisonSampler
            ? WGPUSamplerBindingType_Comparison
            : (binding.sampledTextureFilterable ? WGPUSamplerBindingType_Filtering
                                                : WGPUSamplerBindingType_NonFiltering);
        break;
    case rhi::DescriptorType::InputAttachment:
        // InputAttachment is a logical type only. An executable WebGPU path
        // must pass the explicit code-level lowering as SampledTexture.
        return std::nullopt;
    }
    return entry;
}

struct NativeDescriptorBinding {
    std::uint32_t logicalSet = 0;
    std::uint32_t logicalBinding = 0;
    std::uint32_t group = 0;
    std::uint32_t binding = 0;
    const rhi::DescriptorBindingDesc* desc = nullptr;
    rhi::DescriptorType physicalType = rhi::DescriptorType::UniformBuffer;
};

WGPUPipelineLayout emitNativePipelineLayout(WGPUDevice device,
    const rhi::PipelineLayoutDesc& desc,
    std::uint32_t requiredGroupCount,
    std::uint32_t pushGroup,
    std::uint32_t pushBinding,
    const core::pmr::Vector<NativeDescriptorBinding>& nativeBindings,
    core::pmr::Vector<WebGpuNativePipelineBindingFact>& bindingFacts)
{
    if (!webGpuNativeBindGroupCountSupported(requiredGroupCount)) {
        return nullptr;
    }
    bindingFacts.reserve(nativeBindings.size());
    for (const auto& binding : nativeBindings) {
        const auto entry = nativeLayoutEntry(*binding.desc, binding.physicalType);
        if (!entry)
            return nullptr;
        bindingFacts.push_back(
            {{binding.logicalSet, binding.logicalBinding, binding.group, binding.binding},
                *binding.desc,
                binding.physicalType,
                entry->buffer.minBindingSize});
    }
    core::pmr::Vector<WGPUBindGroupLayout> groups(&core::defaultCpuMemoryResource());
    groups.resize(requiredGroupCount, nullptr);
    // Finish all quota-backed scratch before creating native references. A
    // later group must not strand an earlier native layout on bad_alloc.
    core::pmr::Vector<core::pmr::Vector<WGPUBindGroupLayoutEntry>> groupEntries{
        &core::defaultCpuMemoryResource()};
    groupEntries.resize(requiredGroupCount);
    // Pipeline layouts are indexed densely from group zero.  Create every
    // artifact-declared group, including empty groups between or after the
    // groups used by this selected logical layout.
    for (std::uint32_t groupIndex = 0u; groupIndex < requiredGroupCount; ++groupIndex) {
        auto& entries = groupEntries[groupIndex];
        for (const auto& binding : nativeBindings) {
            if (binding.group != groupIndex) {
                continue;
            }
            const auto nativeEntry = nativeLayoutEntry(*binding.desc, binding.physicalType);
            if (!nativeEntry) {
                return nullptr;
            }
            auto physicalEntry = *nativeEntry;
            physicalEntry.binding = binding.binding;
            entries.push_back(physicalEntry);
        }
        if (groupIndex == pushGroup && !desc.pushConstantRanges.empty()) {
            auto entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entry.binding = pushBinding;
            entry.visibility = nativeShaderStages(desc.pushConstantRanges.front().stages);
            entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            entry.buffer.type = WGPUBufferBindingType_Uniform;
            // B2 keeps one backend-owned bind group for the pipeline. Each
            // atomic update selects its ring slice through WebGPU's native
            // dynamic-offset argument; no per-push bind-group creation is
            // allowed on this path.
            entry.buffer.hasDynamicOffset = WGPU_TRUE;
            entries.push_back(entry);
        }
    }
    for (std::uint32_t groupIndex = 0u; groupIndex < requiredGroupCount; ++groupIndex) {
        const auto& entries = groupEntries[groupIndex];
        auto layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        char layoutLabel[96]{};
        std::snprintf(layoutLabel,
            sizeof(layoutLabel),
            "RHI WebGPU explicit bind-group layout group %u",
            groupIndex);
        layoutDesc.label = webGpuNativeStringView(layoutLabel);
        layoutDesc.entryCount = static_cast<std::uint32_t>(entries.size());
        layoutDesc.entries = entries.empty() ? nullptr : entries.data();
        std::printf("[wgpu-layout] group=%u entries=%zu label='%s'\n",
            groupIndex,
            layoutDesc.entryCount,
            layoutLabel);
        groups[groupIndex] = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
        if (!groups[groupIndex]) {
            for (auto& group : groups) {
                if (group)
                    wgpuBindGroupLayoutRelease(group);
            }
            return nullptr;
        }
    }
    auto pipelineDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineDesc.label = webGpuNativeStringView("RHI WebGPU explicit pipeline layout");
    pipelineDesc.bindGroupLayoutCount = static_cast<std::uint32_t>(groups.size());
    pipelineDesc.bindGroupLayouts = groups.data();
    auto layout = wgpuDeviceCreatePipelineLayout(device, &pipelineDesc);
    for (auto& group : groups) {
        wgpuBindGroupLayoutRelease(group);
    }
    return layout;
}

WGPUPipelineLayout createNativePipelineLayout(WGPUDevice device,
    const rhi::PipelineLayoutDesc& desc,
    const WebGpuBindingSpaceLowering& lowering,
    const rhi::ShaderReflectionDesc& reflection,
    core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering>& descriptorSetGroups,
    core::pmr::Vector<WebGpuNativeDescriptorBindingLowering>& descriptorBindings,
    core::pmr::Vector<WebGpuNativeDescriptorTableLowering>& descriptorTables,
    core::pmr::Vector<WebGpuNativePipelineBindingFact>& bindingFacts)
{
    core::pmr::Vector<NativeDescriptorBinding> nativeBindings{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<rhi::DescriptorBindingDesc> tableBindingDescs{
        &core::defaultCpuMemoryResource()};
    nativeBindings.reserve(lowering.entries.size());
    tableBindingDescs.reserve(desc.descriptorTables.size());
    descriptorSetGroups.clear();
    descriptorBindings.clear();
    descriptorTables.clear();
    bindingFacts.clear();
    for (const auto& slot : desc.descriptorSets) {
        const auto* sourceLayout = rhi::findPipelineDescriptorLayout(desc, slot.layoutName);
        if (!sourceLayout) {
            return nullptr;
        }
        bool groupKnown = false;
        std::uint32_t physicalGroup = 0u;
        for (const auto& binding : sourceLayout->bindings) {
            const WebGpuBindingSpaceLoweringEntry* mapping = nullptr;
            for (const auto& candidate : lowering.entries) {
                if (candidate.active && candidate.set == slot.set
                    && candidate.binding == binding.binding) {
                    mapping = &candidate;
                    break;
                }
            }
            if (!mapping) {
                return nullptr;
            }
            if (mapping->group >= lowering.physicalGroupCount
                || mapping->webGpuBinding == UINT32_MAX || mapping->arrayElement != 0u
                || mapping->capacity != binding.arrayCount || mapping->capacity == 0u) {
                return nullptr;
            }
            if (binding.type == rhi::DescriptorType::InputAttachment
                && (mapping->physicalType != rhi::DescriptorType::SampledTexture
                    || !mapping->hasLoadSemantics
                    || mapping->loadSemantics
                        != rhi::semantics::SubpassInputLoadSemantics::CurrentFragment
                    || mapping->requiresSampler || mapping->depthTexture != binding.depthTexture)) {
                return nullptr;
            }
            if (!groupKnown) {
                physicalGroup = mapping->group;
                groupKnown = true;
            } else if (physicalGroup != mapping->group) {
                return nullptr;
            }
            for (const auto& existing : nativeBindings) {
                if (existing.group == mapping->group
                    && existing.binding == mapping->webGpuBinding) {
                    return nullptr;
                }
            }
            const auto physicalType = binding.type == rhi::DescriptorType::InputAttachment
                ? rhi::DescriptorType::SampledTexture
                : binding.type;
            nativeBindings.push_back({
                slot.set,
                binding.binding,
                mapping->group,
                mapping->webGpuBinding,
                &binding,
                physicalType,
            });
            descriptorBindings.push_back({
                slot.set,
                binding.binding,
                mapping->group,
                mapping->webGpuBinding,
            });
        }
        if (!groupKnown) {
            return nullptr;
        }
        descriptorSetGroups.push_back({slot.set, physicalGroup});
    }
    for (const auto& table : desc.descriptorTables) {
        const rhi::ShaderReflectionBinding* reflected = nullptr;
        for (const auto& candidate : reflection.descriptorBindings) {
            if (candidate.set == table.set && candidate.type == table.type
                && candidate.arrayCount == table.capacity
                && candidate.depthTexture == table.depthTexture
                && (table.stages & candidate.stages) == candidate.stages
                && candidate.arrayCount != 0u) {
                if (reflected != nullptr) {
                    return nullptr;
                }
                reflected = &candidate;
            }
        }
        if (!reflected) {
            return nullptr;
        }
        const WebGpuBindingSpaceLoweringEntry* mapping = nullptr;
        for (const auto& candidate : lowering.entries) {
            if (candidate.active && candidate.set == table.set
                && candidate.binding == reflected->binding) {
                mapping = &candidate;
                break;
            }
        }
        if (!mapping || mapping->group >= lowering.physicalGroupCount
            || mapping->webGpuBinding == UINT32_MAX || mapping->arrayElement != 0u
            || mapping->capacity != table.capacity || mapping->depthTexture != table.depthTexture) {
            return nullptr;
        }
        rhi::DescriptorBindingDesc tableBinding;
        tableBinding.binding = reflected->binding;
        tableBinding.type = table.type;
        tableBinding.stages = table.stages;
        tableBinding.arrayCount = table.capacity;
        tableBinding.tableBacked = true;
        tableBinding.textureViewDimensionConstrained = table.textureViewDimensionConstrained;
        tableBinding.textureViewDimension = table.textureViewDimension;
        tableBinding.depthTexture = table.depthTexture;
        tableBindingDescs.push_back(tableBinding);
        const auto* tableDesc = &tableBindingDescs.back();
        for (const auto& existing : nativeBindings) {
            if (existing.group == mapping->group && existing.binding == mapping->webGpuBinding) {
                return nullptr;
            }
        }
        nativeBindings.push_back({
            table.set,
            reflected->binding,
            mapping->group,
            mapping->webGpuBinding,
            tableDesc,
            table.type,
        });
        descriptorBindings.push_back({
            table.set,
            reflected->binding,
            mapping->group,
            mapping->webGpuBinding,
        });
        descriptorSetGroups.push_back({table.set, mapping->group});
        descriptorTables.push_back({
            table.set,
            reflected->binding,
            mapping->group,
            mapping->webGpuBinding,
            table.capacity,
            table.type,
        });
    }
    if (!desc.pushConstantRanges.empty()) {
        if (reflection.pushConstantContractVersion != "AtomicFullBlockPush/1"
            || desc.pushConstantRanges.size() != 1u
            || desc.pushConstantRanges.front().offsetBytes != 0u
            || lowering.pushConstantGroup == UINT32_MAX
            || lowering.pushConstantGroup >= lowering.physicalGroupCount
            || lowering.pushConstantBinding == UINT32_MAX) {
            return nullptr;
        }
        for (const auto& binding : nativeBindings) {
            if (binding.group == lowering.pushConstantGroup
                && binding.binding == lowering.pushConstantBinding) {
                return nullptr;
            }
        }
    }

    return emitNativePipelineLayout(device,
        desc,
        lowering.physicalGroupCount,
        lowering.pushConstantGroup,
        lowering.pushConstantBinding,
        nativeBindings,
        bindingFacts);
}

WGPUPipelineLayout createExpandedNativePipelineLayout(WGPUDevice device,
    const WebGpuOriginalDepthTableExpansionPipelineDefinition& definition,
    core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering>& sets,
    core::pmr::Vector<WebGpuNativeDescriptorBindingLowering>& mappings,
    core::pmr::Vector<WebGpuNativeDescriptorTableLowering>& tables,
    core::pmr::Vector<WebGpuNativePipelineBindingFact>& facts)
{
    const auto& space = *definition.space();
    const auto& layout = definition.layoutDefinition();
    core::pmr::Vector<NativeDescriptorBinding> bindings{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<rhi::DescriptorBindingDesc> shapes{&core::defaultCpuMemoryResource()};
    bindings.reserve(space.entryCount());
    shapes.reserve(space.entryCount());
    sets.clear();
    mappings.clear();
    tables.clear();
    facts.clear();
    for (std::uint32_t i = 0u; i < space.entryCount(); ++i) {
        const auto& e = space.entry(i);
        const auto shape =
            WebGpuOriginalDepthTableExpansionNativeContract::physicalShape(definition, i);
        shapes.push_back(shape);
        bindings.push_back(
            {e.logical.set, e.logical.binding, e.group, e.binding, &shapes.back(), e.logical.type});
        mappings.push_back({e.logical.set, e.logical.binding, e.group, e.binding});
        bool known = false;
        for (const auto& set : sets)
            if (set.logicalSet == e.logical.set) {
                if (set.webGpuGroup != e.group)
                    return nullptr;
                known = true;
            }
        if (!known)
            sets.push_back({e.logical.set, e.group});
        if (e.logical.type == rhi::DescriptorType::SampledTexture && e.arrayElement == 0u)
            tables.push_back({e.logical.set,
                e.logical.binding,
                e.group,
                e.binding,
                e.logical.arrayCount,
                e.logical.type});
    }
    return emitNativePipelineLayout(
        device, layout, space.physicalGroupCount(), UINT32_MAX, UINT32_MAX, bindings, facts);
}

const WebGpuNativeDescriptorBindingPayload* findNativeBinding(
    const core::pmr::Vector<WebGpuNativeDescriptorBindingPayload>& bindings, std::uint32_t binding)
{
    for (const auto& candidate : bindings) {
        if (candidate.binding.binding == binding) {
            return &candidate;
        }
    }
    return nullptr;
}
#endif

} // namespace

WebGpuNativeIssuedPipelineContract::WebGpuNativeIssuedPipelineContract(ConstructionKey,
    void* device,
    void* object,
    void* layout,
    WebGpuNativePipelineBindingSemantics semantics,
    const core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering>& sets,
    const core::pmr::Vector<WebGpuNativeDescriptorBindingLowering>& bindings,
    const core::pmr::Vector<WebGpuNativeDescriptorTableLowering>& tables,
    core::pmr::Vector<WebGpuNativePipelineBindingFact> facts,
    core::pmr::Vector<WebGpuNativePipelineStageFact> stages,
    WebGpuNativePipelineGraphicsCompatibility graphics,
    std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> artifactInput,
    std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> graphicsInput)
    : device_(device), object_(object), layout_(layout),
      sources_(semantics, OrdinaryInputs{std::move(artifactInput), std::move(graphicsInput)}),
      sets_(sets, &core::defaultCpuMemoryResource()),
      bindings_(bindings, &core::defaultCpuMemoryResource()),
      tables_(tables, &core::defaultCpuMemoryResource()), facts_(std::move(facts)),
      stages_(std::move(stages)), graphics_(std::move(graphics)),
      quota_(core::currentCpuMemoryQuota()),
      issuedCpuQuotaLimit_(quota_ ? quota_->limitBytes() : 0u)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    wgpuDeviceAddRef(static_cast<WGPUDevice>(device_));
#endif
}

WebGpuNativeIssuedPipelineContract::WebGpuNativeIssuedPipelineContract(ConstructionKey,
    void* device,
    void* object,
    void* layout,
    WebGpuNativePipelineBindingSemantics semantics,
    const core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering>& sets,
    const core::pmr::Vector<WebGpuNativeDescriptorBindingLowering>& bindings,
    const core::pmr::Vector<WebGpuNativeDescriptorTableLowering>& tables,
    core::pmr::Vector<WebGpuNativePipelineBindingFact> facts,
    core::pmr::Vector<WebGpuNativePipelineStageFact> stages,
    WebGpuNativePipelineGraphicsCompatibility graphics,
    std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition> expansion)
    : device_(device), object_(object), layout_(layout), sources_(semantics, std::move(expansion)),
      sets_(sets, &core::defaultCpuMemoryResource()),
      bindings_(bindings, &core::defaultCpuMemoryResource()),
      tables_(tables, &core::defaultCpuMemoryResource()), facts_(std::move(facts)),
      stages_(std::move(stages)), graphics_(std::move(graphics)),
      quota_(core::currentCpuMemoryQuota()),
      issuedCpuQuotaLimit_(quota_ ? quota_->limitBytes() : 0u)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    wgpuDeviceAddRef(static_cast<WGPUDevice>(device_));
#endif
}

WebGpuNativeIssuedPipelineContract::~WebGpuNativeIssuedPipelineContract()
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (device_)
        wgpuDeviceRelease(static_cast<WGPUDevice>(device_));
#endif
}

namespace {
template <class Pipeline>
const char* validateIssuedPipeline(const std::shared_ptr<Pipeline>& pipeline,
    const WebGpuNativeIssuedPipelineContract* original) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (webGpuNativeDeviceLost())
        return "RHI-WEBGPU-ORDINARY-PIPELINE-DEVICE-LOST";
    if (!pipeline || !pipeline->sharedOwnerMatches(pipeline))
        return "RHI-WEBGPU-ORDINARY-PIPELINE-ORIGINAL-OWNER";
    const auto& issued = pipeline->issuedContract();
    if (!issued || issued.get() != original || !pipeline->pendingBindingFacts.empty()
        || !pipeline->pendingShaderFacts.empty())
        return "RHI-WEBGPU-ORDINARY-PIPELINE-UNISSUED";
    if (!issued->matches(pipeline->pipeline,
            pipeline->layout,
            webGpuPipelineBindingSemantics(*pipeline),
            pipeline->descriptorSetGroups,
            pipeline->descriptorBindings,
            pipeline->descriptorTables)
        || !issued->graphics().matches(*pipeline))
        return "RHI-WEBGPU-ORDINARY-PIPELINE-IDENTITY-CHANGED";
    return nullptr;
#else
    (void)pipeline;
    (void)original;
    return "RHI-WEBGPU-ORDINARY-PIPELINE-NATIVE-UNAVAILABLE";
#endif
}
} // namespace

template <class Pipeline>
core::UniquePtr<WebGpuOrdinaryPipelineObservation> WebGpuOrdinaryPipelineObservation::prepareImpl(
    const std::shared_ptr<Pipeline>& pipeline,
    const WebGpuOrdinaryPipelineBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-PIPELINE-QUOTA";
        return {};
    }
    diagnostic =
        validateIssuedPipeline(pipeline, pipeline ? pipeline->issuedContract().get() : nullptr);
    if (diagnostic)
        return {};
    const auto& issued = *pipeline->issuedContract();
    if (issued.artifactInput() || issued.graphicsInput()) {
        diagnostic = "RHI-WEBGPU-ORIGINAL-ARTIFACT-PIPELINE-ORDINARY-BRIDGE";
        return {};
    }
    if (!issued.quota() || issued.quota()->limitBytes() != budget.maxOriginalMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-PIPELINE-ORIGINAL-QUOTA";
        return {};
    }
    if (issued.semantics().groupCount > budget.maxGroups || issued.sets().size() > budget.maxSets
        || issued.bindings().size() > budget.maxBindings
        || issued.facts().size() > budget.maxBindings
        || issued.tables().size() > budget.maxTables) {
        diagnostic = "RHI-WEBGPU-ORDINARY-PIPELINE-CAPACITY";
        return {};
    }
    if (issued.stages().size() > budget.maxShaderStages
        || issued.graphics().colorFormats.size() > budget.maxColorTargets) {
        diagnostic = "RHI-WEBGPU-ORDINARY-PIPELINE-SOURCE-CAPACITY";
        return {};
    }
    WebGpuNativePipelineShaderSourceContract::Inventory source;
    diagnostic = WebGpuNativePipelineShaderSourceContract::inspect(
        issued.stages(), issued.graphics().graphics, source);
    if (diagnostic)
        return {};
    if (source.modules > budget.maxShaderModules || source.bytes > budget.maxShaderSourceBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-PIPELINE-SOURCE-CAPACITY";
        return {};
    }
    if (!issued.semantics().shaderPackageHash
        || issued.facts().size() != issued.bindings().size()) {
        diagnostic = "RHI-WEBGPU-ORDINARY-PIPELINE-FACTS";
        return {};
    }
    for (std::size_t i = 0u; i < issued.facts().size(); ++i) {
        const auto& fact = issued.facts()[i];
        if (!(fact.mapping == issued.bindings()[i])
            || fact.mapping.webGpuGroup >= issued.semantics().groupCount
            || fact.mapping.webGpuBinding == UINT32_MAX
            || fact.shape.binding != fact.mapping.logicalBinding || !fact.shape.arrayCount) {
            diagnostic = "RHI-WEBGPU-ORDINARY-PIPELINE-FACTS";
            return {};
        }
        for (std::size_t j = 0u; j < i; ++j)
            if ((fact.mapping.webGpuGroup == issued.facts()[j].mapping.webGpuGroup
                    && fact.mapping.webGpuBinding == issued.facts()[j].mapping.webGpuBinding)
                || (fact.mapping.logicalSet == issued.facts()[j].mapping.logicalSet
                    && fact.mapping.logicalBinding == issued.facts()[j].mapping.logicalBinding)) {
                diagnostic = "RHI-WEBGPU-ORDINARY-PIPELINE-FACTS";
                return {};
            }
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        core::UniquePtr<WebGpuOrdinaryPipelineObservation> result;
        if constexpr (std::is_same_v<Pipeline, WebGpuNativeComputePipelinePayload>)
            result = core::makeUnique<WebGpuOrdinaryPipelineObservation>(
                ConstructionKey{}, pipeline, nullptr);
        else
            result = core::makeUnique<WebGpuOrdinaryPipelineObservation>(
                ConstructionKey{}, nullptr, pipeline);
        result->original_ = &issued;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-PIPELINE-PREPARATION-ALLOCATION";
        return {};
    }
}

core::UniquePtr<WebGpuOrdinaryPipelineObservation> WebGpuOrdinaryPipelineObservation::prepare(
    const std::shared_ptr<WebGpuNativeComputePipelinePayload>& pipeline,
    const WebGpuOrdinaryPipelineBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    return prepareImpl(pipeline, budget, std::move(quota), diagnostic);
}

core::UniquePtr<WebGpuOrdinaryPipelineObservation> WebGpuOrdinaryPipelineObservation::prepare(
    const std::shared_ptr<WebGpuNativeRenderPipelinePayload>& pipeline,
    const WebGpuOrdinaryPipelineBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    return prepareImpl(pipeline, budget, std::move(quota), diagnostic);
}

const char* WebGpuOrdinaryPipelineObservation::validate() const noexcept
{
    return compute_ ? validateIssuedPipeline(compute_, original_)
                    : validateIssuedPipeline(graphics_, original_);
}

const WebGpuNativeIssuedPipelineContract&
WebGpuOrdinaryPipelineObservation::contract() const noexcept
{
    return *original_;
}

std::shared_ptr<WebGpuNativeBufferPayload> createWebGpuNativeBufferPayload(
    const rhi::BufferDesc& desc, const WebGpuNativeBufferInitializer& initializer)
{
    if (const auto* id = rhi::HostVisibleDeviceLocalBufferContract::validate(
            desc, rhi::Backend::WebGPU, false)) {
        rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, id);
        return {};
    }
    if (bool(initializer.context) != bool(initializer.initialize)
        || (initializer.initialize
            && (!desc.sizeBytes || desc.sizeBytes > SIZE_MAX || desc.sizeBytes % 4u)))
        return nullptr;
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        return nullptr;
    }
    OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
    auto* device = deviceRef.value;
    auto bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    if (!device || !lowerNativeBufferCreation(desc, bufferDesc)) {
        return nullptr;
    }
    bufferDesc.mappedAtCreation = bool(initializer.initialize);
    auto payload = arp::core::makeShared<WebGpuNativeBufferPayload>(desc.initialState);
    payload->sizeBytes = desc.sizeBytes;
    payload->buffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
    payload->ownerDevice = device;
    if (payload->buffer && initializer.initialize) {
        auto* mapped = wgpuBufferGetMappedRange(payload->buffer, 0u, desc.sizeBytes);
        const bool initialized =
            mapped && initializer.initialize(initializer.context, mapped, desc.sizeBytes);
        wgpuBufferUnmap(payload->buffer);
        if (!initialized)
            return nullptr;
    }
    return payload->buffer ? payload : nullptr;
#else
    (void)desc;
    (void)initializer;
    return nullptr;
#endif
}

std::shared_ptr<WebGpuNativeBufferCreation> WebGpuNativeBufferCreation::prepareReadbackOriginal(
    const rhi::BufferDesc& desc,
    void* exactDevice,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const char*& diagnostic)
{
    if ((diagnostic = rhi::HostVisibleDeviceLocalBufferContract::validate(
             desc, rhi::Backend::WebGPU, false)))
        return {};
    diagnostic = nullptr;
    if (!quota || !quota->limitBytes()) {
        diagnostic = "RHI-READBACK-BUFFER-CREATION-QUOTA";
        return {};
    }
    if (!exactDevice || !desc.sizeBytes || desc.usage != rhi::ResourceUsage::CopyDst
        || desc.memory != rhi::MemoryUsage::GpuToCpu
        || desc.initialState != rhi::ResourceState::CopyDst) {
        diagnostic = "RHI-READBACK-BUFFER-CREATION-INTENT";
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable() || webGpuNativeDeviceLost()) {
        diagnostic = "RHI-READBACK-BUFFER-CREATION-NATIVE-UNAVAILABLE";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        return shareOriginal(
            core::makeUnique<WebGpuNativeBufferCreation>(ConstructionKey{}, desc, exactDevice));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-READBACK-BUFFER-CREATION-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-READBACK-BUFFER-CREATION-ALLOCATION";
        return {};
    }
#else
    diagnostic = "RHI-READBACK-BUFFER-CREATION-NATIVE-UNAVAILABLE";
    return {};
#endif
}

const char* WebGpuNativeBufferCreation::initialize(
    const std::shared_ptr<WebGpuNativeBufferCreation>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-READBACK-BUFFER-CREATION-OWNER";
    if (self->attempted_)
        return "RHI-READBACK-BUFFER-CREATION-STATE";
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable() || webGpuNativeDeviceLost())
        return "RHI-READBACK-BUFFER-CREATION-NATIVE-UNAVAILABLE";
    const auto& payload = self->candidate_;
    if (!payload || !payload->sharedOwnerMatches(payload) || payload->buffer || payload->ownerDevice
        || payload->sizeBytes != self->desc_.sizeBytes
        || payload->creationState() != self->desc_.initialState
        || payload->recordedState().pending()
        || payload->recordedState().state() != self->desc_.initialState)
        return "RHI-READBACK-BUFFER-CREATION-PAYLOAD";
    auto bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    if (!lowerNativeBufferCreation(self->desc_, bufferDesc))
        return "RHI-READBACK-BUFFER-CREATION-INTENT";
    self->attempted_ = true;
    auto buffer = wgpuDeviceCreateBuffer(static_cast<WGPUDevice>(self->device_), &bufferDesc);
    if (!buffer)
        return "RHI-READBACK-DESTINATION-NATIVE-CREATE";
    payload->buffer = buffer;
    payload->ownerDevice = static_cast<WGPUDevice>(self->device_);
    self->ready_ = true;
    return nullptr;
#else
    return "RHI-READBACK-BUFFER-CREATION-NATIVE-UNAVAILABLE";
#endif
}

bool writeWebGpuNativeBufferPayload(const WebGpuNativeBufferPayload& payload,
    const void* data,
    const std::uint64_t sizeBytes,
    const std::uint64_t offsetBytes,
    std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!payload.buffer) {
        diagnostics = "WebGPU RHI buffer write requires a native buffer payload";
        return false;
    }
    if (!data || sizeBytes == 0u
        || sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<size_t>::max())) {
        diagnostics = "WebGPU RHI buffer write payload is invalid";
        return false;
    }
    if (!browserWebGpuEnvironmentAvailable()) {
        diagnostics = "WebGPU RHI buffer write requires a browser WebGPU environment";
        return false;
    }
    OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
    auto* device = deviceRef.value;
    if (!device) {
        diagnostics = "WebGPU RHI buffer write requires a native device";
        return false;
    }
    auto queue = wgpuDeviceGetQueue(device);
    if (!queue) {
        diagnostics = "WebGPU RHI buffer write requires a native queue";
        return false;
    }
    wgpuQueueWriteBuffer(queue, payload.buffer, offsetBytes, data, static_cast<size_t>(sizeBytes));
    if (!webGpuNativeDeviceLost())
        wgpuQueueRelease(queue);
    return true;
#else
    (void)payload;
    (void)data;
    (void)sizeBytes;
    (void)offsetBytes;
    diagnostics = "WebGPU RHI buffer write requires Emscripten native WebGPU support";
    return false;
#endif
}

std::shared_ptr<WebGpuNativeTexturePayload> createWebGpuNativeTexturePayload(
    const rhi::TextureDesc& desc)
{
    return WebGpuNativeTextureFactory::create(desc);
}

std::shared_ptr<WebGpuNativeTexturePayload> WebGpuNativeTextureFactory::create(
    const rhi::TextureDesc& desc)
{
    return create(desc, false);
}

const char* WebGpuNativeTextureFactory::validateOriginalUpload(
    const WebGpuNativeTexturePayload& payload, const rhi::TextureDesc& desc) noexcept
{
    return validateOriginalContent(payload, desc, false);
}

const char* WebGpuNativeTextureFactory::validateOriginalAttachment(
    const WebGpuNativeTexturePayload& payload, const rhi::TextureDesc& desc) noexcept
{
    return validateOriginalContent(payload, desc, true);
}

const char* WebGpuNativeTextureFactory::validateOriginalContent(
    const WebGpuNativeTexturePayload& payload,
    const rhi::TextureDesc& desc,
    bool attachment) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (webGpuNativeDeviceLost())
        return "RHI-TEXTURE-DESTINATION-DEVICE-LOST";
    if (!payload.texture || !payload.ownerDevice
        || (attachment ? !payload.contentAccessPolicy().originalAttachmentExclusive()
                       : !payload.contentAccessPolicy().originalUploadExclusive()))
        return "RHI-TEXTURE-DESTINATION-NATIVE-FACTS";
    const auto depth = desc.extent.depth > 1u ? desc.extent.depth : desc.arrayLayerCount;
    const auto dimension =
        desc.extent.depth > 1u ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
    if (wgpuTextureGetWidth(payload.texture) != desc.extent.width
        || wgpuTextureGetHeight(payload.texture) != desc.extent.height
        || wgpuTextureGetDepthOrArrayLayers(payload.texture) != depth
        || wgpuTextureGetDimension(payload.texture) != dimension
        || wgpuTextureGetFormat(payload.texture) != nativeTextureFormat(desc.format)
        || wgpuTextureGetUsage(payload.texture) != nativeTextureUsage(desc.usage)
        || wgpuTextureGetMipLevelCount(payload.texture) != desc.mipLevelCount
        || wgpuTextureGetSampleCount(payload.texture) != desc.sampleCount)
        return "RHI-TEXTURE-DESTINATION-NATIVE-LAYOUT";
    if (rhi::isBlockCompressed(desc.format)
        && !wgpuDeviceHasFeature(payload.ownerDevice, WGPUFeatureName_TextureCompressionBC))
        return "RHI-TEXTURE-DESTINATION-BC-FEATURE";
    return nullptr;
#else
    (void)payload;
    (void)desc;
    (void)attachment;
    return "RHI-TEXTURE-DESTINATION-NATIVE-UNAVAILABLE";
#endif
}

std::shared_ptr<WebGpuNativeTexturePayload> WebGpuNativeTextureFactory::createOriginalUpload(
    const rhi::TextureDesc& desc)
{
    return create(desc, true);
}

std::shared_ptr<WebGpuNativeTexturePayload> WebGpuNativeTextureFactory::createCanonicalContent(
    const rhi::TextureDesc& desc,
    const rhi::OriginalTextureContentCreationContract::Decision& decision)
{
    const auto approved = rhi::OriginalTextureContentCreationContract::lowering(
        rhi::Backend::WebGPU, decision.contractVersion);
    if (decision.contractVersion != approved.contractVersion
        || decision.disposition != approved.disposition || !decision.exclusiveCreationAvailable
        || decision.nativeContentExecutionAvailable)
        return {};
    return rhi::OriginalTextureContentCreationContract::isCellCreation(decision.contractVersion)
        ? create(desc, false, true)
        : create(desc, true);
}

std::shared_ptr<WebGpuNativeTexturePayload> WebGpuNativeTextureFactory::create(
    const rhi::TextureDesc& desc, bool originalUpload, bool originalAttachment)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    const auto reportFailure = [&](const char* reason) {
        std::fprintf(stderr,
            "[arp-webgpu] native texture create failed label=%s reason=%s\n",
            desc.label.c_str(),
            reason);
#if defined(__EMSCRIPTEN__)
        EM_ASM(
            {
                // clang-format off
                document.body.dataset.woccNativeTextureCreateDiagnostic = UTF8ToString($0);
                // clang-format on
            },
            reason);
#endif
    };
    if (!browserWebGpuEnvironmentAvailable()) {
        reportFailure("browser WebGPU environment unavailable");
        return nullptr;
    }
    OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
    auto* device = deviceRef.value;
    const auto format = nativeTextureFormat(desc.format);
    const auto usage = nativeTextureUsage(desc.usage);
    if (!device || desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0
        || desc.mipLevelCount == 0 || desc.arrayLayerCount == 0 || desc.sampleCount == 0
        || format == WGPUTextureFormat_Undefined || usage == 0) {
        reportFailure("native device or texture descriptor is invalid");
        return nullptr;
    }
    if (originalUpload && rhi::isBlockCompressed(desc.format)
        && !wgpuDeviceHasFeature(device, WGPUFeatureName_TextureCompressionBC)) {
        reportFailure("RHI-TEXTURE-DESTINATION-BC-FEATURE");
        return nullptr;
    }
    const char* stateDiagnostic = nullptr;
    if (!rhi::NormalizedTextureStateCreationContract::inspect(desc,
            core::pmr::Vector<rhi::ResourceState>{&core::defaultCpuMemoryResource()}.max_size(),
            stateDiagnostic)) {
        reportFailure(stateDiagnostic);
        return nullptr;
    }
    // Allocation failure follows the existing exception boundary: original
    // upload/create owners publish their stable RESOURCE-ALLOCATION diagnostic.
    // Both arrays finish before any native texture exists or can be published.
    auto payload = originalAttachment
        ? arp::core::makeShared<WebGpuNativeTexturePayload>(
              WebGpuNativeTexturePayload::OriginalAttachmentKey{}, desc)
        : originalUpload ? arp::core::makeShared<WebGpuNativeTexturePayload>(
                               WebGpuNativeTexturePayload::OriginalUploadKey{}, desc)
                         : arp::core::makeShared<WebGpuNativeTexturePayload>(desc);
    auto textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.label = webGpuNativeStringView(desc.label);
    textureDesc.usage = usage;
    textureDesc.dimension =
        desc.extent.depth > 1 ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
    textureDesc.size = WGPUExtent3D{
        desc.extent.width,
        desc.extent.height,
        desc.extent.depth > 1u || desc.arrayLayerCount > 1u
            ? (desc.extent.depth > 1u ? desc.extent.depth : desc.arrayLayerCount)
            : 1u,
    };
    textureDesc.format = format;
    textureDesc.mipLevelCount = desc.mipLevelCount;
    textureDesc.sampleCount = desc.sampleCount;
    payload->texture = wgpuDeviceCreateTexture(device, &textureDesc);
    payload->ownerDevice = device;
    payload->format = format;
    payload->dimension = textureDesc.dimension;
    payload->rhiFormat = desc.format;
    payload->extent = desc.extent;
    payload->mipLevelCount = desc.mipLevelCount;
    payload->arrayLayerCount = desc.arrayLayerCount;
    payload->sampleCount = desc.sampleCount;
    if (!payload->texture) {
        reportFailure("wgpuDeviceCreateTexture returned null");
        return nullptr;
    }
    return payload;
#else
    (void)desc;
    (void)originalUpload;
    (void)originalAttachment;
    return nullptr;
#endif
}

std::shared_ptr<WebGpuNativeTextureViewPayload> createWebGpuNativeTextureViewPayload(
    const WebGpuNativeTexturePayload& texture, const rhi::TextureViewDesc& desc)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!texture.texture)
        return nullptr;
    if (desc.usage == rhi::TextureUsage::None) {
        rhi::RhiErrorHandler::get().report(
            rhi::RhiSeverity::Error, "RHI-WEBGPU-TEXTURE-VIEW-USAGE-UNRESOLVED");
        return nullptr;
    }
    auto payload = arp::core::makeShared<WebGpuNativeTextureViewPayload>();
    const auto key = textureViewCreationKey(texture, desc);
    auto viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.label = webGpuNativeStringView(desc.label);
    viewDesc.format = static_cast<WGPUTextureFormat>(key.nativeFormat);
    viewDesc.dimension = static_cast<WGPUTextureViewDimension>(key.nativeDimension);
    viewDesc.usage = nativeTextureUsage(key.usage);
    viewDesc.baseMipLevel = desc.baseMipLevel;
    viewDesc.mipLevelCount = desc.mipLevelCount;
    viewDesc.baseArrayLayer = desc.baseArrayLayer;
    viewDesc.arrayLayerCount = desc.arrayLayerCount;
    viewDesc.aspect = nativeTextureAspect(desc.aspect);
    payload->view = viewDesc.format == WGPUTextureFormat_Undefined
        ? nullptr
        : wgpuTextureCreateView(texture.texture, &viewDesc);
    payload->format = viewDesc.format;
    payload->creationKey = key;
    return payload->view ? payload : nullptr;
#else
    (void)texture;
    (void)desc;
    return nullptr;
#endif
}

std::shared_ptr<WebGpuNativeTextureViewPayload> getOrCreateWebGpuNativeTextureViewPayload(
    const WebGpuNativeTexturePayload& texture, const rhi::TextureViewDesc& desc)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (desc.usage == rhi::TextureUsage::None)
        return createWebGpuNativeTextureViewPayload(texture, desc);
    const auto key = textureViewCreationKey(texture, desc);
    const auto entry = std::find_if(
        texture.viewCache.begin(), texture.viewCache.end(), [&](const auto& candidate) {
            return candidate.key == key;
        });
    if (entry != texture.viewCache.end()) {
        if (auto cached = entry->payload.lock())
            return cached;
    }
    auto payload = createWebGpuNativeTextureViewPayload(texture, desc);
    if (payload) {
        if (entry != texture.viewCache.end())
            entry->payload = payload;
        else
            texture.viewCache.push_back({key, payload});
    }
    return payload;
#else
    (void)texture;
    (void)desc;
    return nullptr;
#endif
}

bool WebGpuNativeTextureViewPayload::matchesDescription(
    const rhi::TextureViewDesc& desc) const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    return parentTexture && creationKey == textureViewCreationKey(*parentTexture, desc)
        && static_cast<std::uint32_t>(format) == creationKey.nativeFormat;
#else
    (void)desc;
    return false;
#endif
}

std::shared_ptr<WebGpuNativeSamplerPayload> createWebGpuNativeSamplerPayload(
    const rhi::SamplerDesc& desc)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        return nullptr;
    }
    OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
    auto* device = deviceRef.value;
    if (!device) {
        return nullptr;
    }
    auto payload = arp::core::makeShared<WebGpuNativeSamplerPayload>();
    auto samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    samplerDesc.label = webGpuNativeStringView(desc.label);
    samplerDesc.addressModeU = nativeAddressMode(desc.addressU);
    samplerDesc.addressModeV = nativeAddressMode(desc.addressV);
    samplerDesc.addressModeW = nativeAddressMode(desc.addressW);
    samplerDesc.minFilter = nativeFilter(desc.minFilter);
    samplerDesc.magFilter = nativeFilter(desc.magFilter);
    // Mipmap filter mode. WebGPU exposes WGPUMipmapFilterMode; None has no
    // direct native equivalent and is lowered explicitly to Nearest.
    const bool anisotropic = desc.maxAnisotropy > 1.0f;
    switch (desc.mipFilter) {
    case rhi::SamplerMipFilter::Nearest:
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        break;
    case rhi::SamplerMipFilter::None:
        // WebGPU has no `None` mip filter. Lock both LOD clamps to zero
        // so this remains a level-0-only sampler instead of silently
        // selecting another mip with nearest filtering.
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        if (!anisotropic) {
            samplerDesc.lodMinClamp = 0.0f;
            samplerDesc.lodMaxClamp = 0.0f;
        }
        break;
    default:
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        break;
    }
    // Anisotropic filtering. WebGPU's sampler descriptor carries the field
    // directly; only meaningful when greater than 1.
    if (anisotropic) {
        samplerDesc.maxAnisotropy = static_cast<std::uint16_t>(desc.maxAnisotropy);
        // The RHI contract makes anisotropy imply linear mip filtering, so
        // do not leave a `None` request's LOD clamp active in this case.
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    }
    // Hardware depth comparison for shadow map sampling (shadow PCF).
    // When compareOp is not Never, WebGPU performs the compare function
    // between the sampled depth and the shader reference value.
    if (desc.compareOp != rhi::CompareOp::Never) {
        samplerDesc.compare = nativeCompareFunction(desc.compareOp);
    }
    payload->sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    payload->ownerDevice = device;
    return payload->sampler ? payload : nullptr;
#else
    (void)desc;
    return nullptr;
#endif
}

namespace {
std::shared_ptr<WebGpuNativeDescriptorGroupPayload> createDescriptorMetadata(
    const rhi::DescriptorLayoutDesc& layout,
    const core::pmr::Vector<WebGpuNativeDescriptorBindingPayload>& bindings,
    const core::pmr::Vector<WebGpuOriginalBufferRangePublication>* publications,
    const core::pmr::Vector<WebGpuCompletedTexturePublication>* completed = nullptr,
    bool allowCells = false)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        return nullptr;
    }
    if (layout.bindings.empty()) {
        return nullptr;
    }
    if (completed)
        for (const auto& publication : *completed) {
            const auto found =
                std::find_if(bindings.begin(), bindings.end(), [&](const auto& binding) {
                    return rhi::descriptorBindingWriteMatches(binding.write, publication.write);
                });
            if (found == bindings.end() || (!allowCells && publication.cellIdentity)
                || validateWebGpuOriginalTexturePublication(publication, *found))
                return nullptr;
            const auto* authored = rhi::findDescriptorSetBinding(layout, found->write.binding);
            if (!authored || !rhi::descriptorBindingLayoutMatches(*authored, found->binding)
                || !rhi::descriptorBindingLayoutMatches(found->binding, *authored))
                return nullptr;
            if (std::count_if(completed->begin(),
                    completed->end(),
                    [&](const auto& other) {
                        return other.write.binding == publication.write.binding
                            && other.write.arrayElement == publication.write.arrayElement;
                    })
                != 1)
                return nullptr;
        }
    // The generic factory stays whole-only. Typed finite publication must
    // authenticate every slice before retaining any descriptor metadata.
    for (const auto& payload : bindings) {
        if (!publications) {
            if (rhi::DescriptorBufferBindingRangeContract::writeDiagnostic(
                    payload.write.bufferBindingRange, true))
                return nullptr;
        } else {
            if (rhi::DescriptorBufferBindingRangeContract::validateSchema(
                    payload.write.bufferBindingRange, true))
                return nullptr;
            if (payload.write.bufferBindingRange.contractVersion != 2u)
                continue;
            const auto publication =
                std::find_if(publications->begin(), publications->end(), [&](const auto& original) {
                    return rhi::descriptorBindingWriteMatches(original.write, payload.write);
                });
            if (publication == publications->end() || !publication->source
                || rhi::OriginalCommandBufferLease::validateOriginal(publication->source)
                || publication->source->resource() != payload.write.resource || !payload.buffer
                || !core::SharedOwnerProjection::sameOwner(
                    payload.buffer, webGpuNativePayloadFor(*publication->source->originalObject())))
                return nullptr;
        }
    }
    for (const auto& binding : layout.bindings) {
        if (binding.arrayCount > 1u) {
            // The current emdawnwebgpu C header has no binding-array entry
            // payload fields. Keep the L2 contract explicit and diagnostic
            // until the browser runtime promotes that extension.
            return nullptr;
        }
    }

    core::pmr::Vector<WGPUBindGroupLayoutEntry> layoutEntries;
    core::pmr::Vector<WGPUBindGroupEntry> groupEntries;

    struct NativeResourceArray {
        core::pmr::Vector<WGPUBuffer> buffers{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<WGPUTextureView> textureViews{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<WGPUSampler> samplers{&core::defaultCpuMemoryResource()};
    };

    core::pmr::Vector<NativeResourceArray> resourceArrays;
    layoutEntries.reserve(layout.bindings.size());
    groupEntries.reserve(layout.bindings.size());
    resourceArrays.reserve(layout.bindings.size());

    for (const auto& binding : layout.bindings) {
        core::pmr::Vector<const WebGpuNativeDescriptorBindingPayload*> elements;
        for (const auto& candidate : bindings) {
            if (candidate.binding.binding == binding.binding) {
                elements.push_back(&candidate);
            }
        }
        if (elements.size() != binding.arrayCount) {
            return nullptr;
        }
        std::sort(elements.begin(), elements.end(), [](const auto* lhs, const auto* rhs) {
            return lhs->arrayElement < rhs->arrayElement;
        });
        for (std::uint32_t element = 0u; element < binding.arrayCount; ++element) {
            if (elements[element]->arrayElement != element) {
                return nullptr;
            }
        }
        const auto* payload = elements.front();

        auto groupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        groupEntry.binding = binding.binding;
        const auto physicalType = binding.type == rhi::DescriptorType::InputAttachment
            ? payload->write.physicalLowering.enabled ? payload->write.physicalLowering.physicalType
                                                      : rhi::DescriptorType::InputAttachment
            : binding.type;
        if (binding.type == rhi::DescriptorType::InputAttachment
            && (!payload->write.physicalLowering.enabled
                || physicalType != rhi::DescriptorType::SampledTexture
                || payload->write.physicalLowering.shaderPackageHash == 0)) {
            // The descriptor-set write is the only legal boundary at which a
            // logical WebGPU InputAttachment may become a sampled texture.
            // A missing ticket must never be repaired here by inference.
            return nullptr;
        }
        if (physicalType == rhi::DescriptorType::UniformBuffer
            || physicalType == rhi::DescriptorType::StorageBuffer
            || physicalType == rhi::DescriptorType::ReadOnlyStorageBuffer) {
            const auto limits = webGpuNativeDeviceLimits();
            const auto maxBindingSize = physicalType == rhi::DescriptorType::UniformBuffer
                ? limits.maxUniformBufferBindingSize
                : limits.maxStorageBufferBindingSize;
            for (const auto* element : elements) {
                rhi::DescriptorBufferBindingRangeContract::Resolved range;
                if (!element->buffer || maxBindingSize == 0u
                    || rhi::DescriptorBufferBindingRangeContract::resolve(
                        element->write.bufferBindingRange, element->buffer->sizeBytes, 0u, range)
                    || range.sizeBytes > maxBindingSize) {
                    return nullptr;
                }
                const auto alignment = physicalType == rhi::DescriptorType::UniformBuffer
                    ? limits.uniformBufferOffsetAlignment
                    : limits.storageBufferOffsetAlignment;
                if (!alignment || range.offsetBytes % alignment || (range.sizeBytes % 4u))
                    return nullptr;
            }
        }
        resourceArrays.emplace_back();
        auto& resourceArray = resourceArrays.back();
        switch (physicalType) {
        case rhi::DescriptorType::UniformBuffer:
        case rhi::DescriptorType::StorageBuffer:
        case rhi::DescriptorType::ReadOnlyStorageBuffer:
            resourceArray.buffers.reserve(elements.size());
            for (const auto* element : elements) {
                if (!element->buffer || !element->buffer->buffer) {
                    return nullptr;
                }
                resourceArray.buffers.push_back(element->buffer->buffer);
            }
            groupEntry.buffer = resourceArray.buffers.front();
            {
                rhi::DescriptorBufferBindingRangeContract::Resolved range;
                if (rhi::DescriptorBufferBindingRangeContract::resolve(
                        payload->write.bufferBindingRange, payload->buffer->sizeBytes, 0u, range))
                    return nullptr;
                groupEntry.offset = range.offsetBytes;
                groupEntry.size = range.sizeBytes;
            }
            break;
        case rhi::DescriptorType::SampledTexture:
        case rhi::DescriptorType::StorageTexture:
            resourceArray.textureViews.reserve(elements.size());
            for (const auto* element : elements) {
                if (!element->textureView || !element->textureView->view) {
                    return nullptr;
                }
                if (!element->textureView->parentTexture)
                    return nullptr;
                if (element->textureView->parentTexture->contentAccessPolicy()
                        .ordinaryAccessDiagnostic()) {
                    if (!completed)
                        return nullptr;
                    const auto found = std::find_if(
                        completed->begin(), completed->end(), [&](const auto& publication) {
                            return rhi::descriptorBindingWriteMatches(
                                publication.write, element->write);
                        });
                    if (found == completed->end()
                        || validateWebGpuOriginalTexturePublication(*found, *element))
                        return nullptr;
                }
                resourceArray.textureViews.push_back(element->textureView->view);
            }
            groupEntry.textureView = resourceArray.textureViews.front();
            break;
        case rhi::DescriptorType::Sampler:
            resourceArray.samplers.reserve(elements.size());
            for (const auto* element : elements) {
                if (!element->sampler || !element->sampler->sampler) {
                    return nullptr;
                }
                resourceArray.samplers.push_back(element->sampler->sampler);
            }
            groupEntry.sampler = resourceArray.samplers.front();
            break;
        case rhi::DescriptorType::InputAttachment:
            // The logical type is lowered above. Reaching this case means
            // the physical descriptor lowering contract was bypassed.
            return nullptr;
        }
        const auto nativeEntry = nativeLayoutEntry(binding, physicalType);
        if (!nativeEntry) {
            return nullptr;
        }
        layoutEntries.push_back(*nativeEntry);
        groupEntries.push_back(groupEntry);
    }

    auto payload = arp::core::makeShared<WebGpuNativeDescriptorGroupPayload>();
    payload->bindings = bindings;
    if (completed)
        payload->completedTexturePublications = *completed;
    for (const auto& binding : layout.bindings) {
        if (binding.dynamicOffset) {
            ++payload->dynamicOffsetCount;
            payload->dynamicOffsetBindings.push_back(binding.binding);
        }
    }
    std::sort(payload->dynamicOffsetBindings.begin(), payload->dynamicOffsetBindings.end());
    for (const auto& binding : layout.bindings) {
        if (binding.type == rhi::DescriptorType::InputAttachment) {
            payload->containsLoweredInputAttachment = true;
            break;
        }
    }
    if (payload->containsLoweredInputAttachment) {
        for (const auto& binding : bindings) {
            if (binding.binding.type != rhi::DescriptorType::InputAttachment) {
                continue;
            }
            const auto hash = binding.write.physicalLowering.shaderPackageHash;
            if (hash == 0) {
                return nullptr;
            }
            if (payload->shaderPackageHash == 0) {
                payload->shaderPackageHash = hash;
            } else if (payload->shaderPackageHash != hash) {
                return nullptr;
            }
        }
    }
    // Do not smuggle a layout-name-specific group remap through a descriptor
    // payload.  The command receives an L2 set index; a future versioned
    // WebGPU binding-space lowering artifact must map that index consistently
    // for pipeline-layout creation and set-bind recording.  Until then the
    // UINT32_MAX sentinel makes the command use the L2 set and lets native
    // validation diagnose any WGSL group mismatch instead of binding data to
    // an unrelated group.
    // The logical descriptor-set layout is not executable on WebGPU after
    // binding-space lowering. Keep only the backend-owned resources here; the
    // command path assembles a physical bind group against the active
    // pipeline layout in buildCompactDescriptorGroup(). Creating a second
    // logical bind group would produce an invalid object for lowered layouts
    // and then flood the browser console on every frame.
    return payload;
#else
    (void)layout;
    (void)bindings;
    (void)publications;
    (void)completed;
    (void)allowCells;
    return nullptr;
#endif
}
} // namespace

std::shared_ptr<WebGpuNativeDescriptorGroupPayload> createWebGpuNativeDescriptorGroupPayload(
    const rhi::DescriptorLayoutDesc& layout,
    const core::pmr::Vector<WebGpuNativeDescriptorBindingPayload>& bindings)
{
    return createDescriptorMetadata(layout, bindings, nullptr);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
createWebGpuNativeOriginalRangeDescriptorPayload(const rhi::DescriptorLayoutDesc& layout,
    const core::pmr::Vector<WebGpuNativeDescriptorBindingPayload>& bindings,
    const core::pmr::Vector<WebGpuOriginalBufferRangePublication>& publications)
{
    return publications.empty() ? nullptr
                                : createDescriptorMetadata(layout, bindings, &publications);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
createWebGpuNativeOriginalCompletedTextureDescriptorPayload(const rhi::DescriptorLayoutDesc& layout,
    const core::pmr::Vector<WebGpuNativeDescriptorBindingPayload>& bindings,
    const core::pmr::Vector<WebGpuOriginalBufferRangePublication>& ranges,
    const core::pmr::Vector<WebGpuCompletedTexturePublication>& completed)
{
    return completed.empty() ? nullptr
                             : createDescriptorMetadata(layout, bindings, &ranges, &completed);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
createWebGpuNativeOriginalTextureCellDescriptorPayload(const rhi::DescriptorLayoutDesc& layout,
    const core::pmr::Vector<WebGpuNativeDescriptorBindingPayload>& bindings,
    const core::pmr::Vector<WebGpuOriginalBufferRangePublication>& ranges,
    const core::pmr::Vector<WebGpuCompletedTexturePublication>& publications)
{
    if (std::none_of(publications.begin(), publications.end(), [](const auto& value) {
            return bool(value.cellIdentity);
        }))
        return nullptr;
    return createDescriptorMetadata(layout, bindings, &ranges, &publications, true);
}

namespace {
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
std::shared_ptr<WebGpuNativeShaderModulePayload> emitWebGpuNativeShaderModule(
    core::StringView label,
    core::StringView source,
    WebGpuNativeShaderModulePayload::ConstructionKey key)
{
    if (!browserWebGpuEnvironmentAvailable() || source.empty())
        return {};
    OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
    if (!deviceRef.value)
        return {};
    auto payload = core::makeShared<WebGpuNativeShaderModulePayload>(key, source);
    auto wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = {source.data(), source.size()};
    auto desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.label = {label.data(), label.size()};
    desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
    payload->module = wgpuDeviceCreateShaderModule(deviceRef.value, &desc);
    return payload->module ? payload : nullptr;
}
#endif
} // namespace

std::shared_ptr<WebGpuNativeShaderModulePayload> createWebGpuNativeShaderModulePayload(
    const rhi::ShaderPackageDesc& package)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable() || package.artifacts.wgslPath.empty())
        return {};
    rhi::GeneratedShaderTargetInput input;
    if (!rhi::selectGeneratedShaderPackageTarget(package, shader::TargetBackend::Wgsl, input)
            .success)
        return {};
    return emitWebGpuNativeShaderModule(
        package.label, input.text, WebGpuNativeShaderModulePayload::ConstructionKey{});
#else
    (void)package;
    return {};
#endif
}

std::shared_ptr<WebGpuNativeShaderModulePayload> createWebGpuNativeBindlessShaderModulePayload(
    const rhi::BindlessShaderPackage& package,
    std::string_view entry,
    rhi::ShaderStage stage,
    core::pmr::String& diagnostic,
    bool explicitPush)
{
    core::StringView wgslBytes;
    if (!rhi::selectBindlessEmulatedWgsl(package, entry, stage, wgslBytes, diagnostic))
        return nullptr;
    const auto lowering =
        loadBindlessWebGpuBindingSpaceLowering(package, entry, stage, explicitPush);
    if (!lowering.success) {
        diagnostic = lowering.diagnostics;
        return nullptr;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        diagnostic = "RHI-BINDLESS-WEBGPU-NATIVE-DEVICE-UNAVAILABLE";
        return nullptr;
    }
    OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
    if (!deviceRef.value) {
        diagnostic = "RHI-BINDLESS-WEBGPU-NATIVE-DEVICE-UNAVAILABLE";
        return nullptr;
    }
    auto payload = core::makeShared<WebGpuNativeShaderModulePayload>(
        WebGpuNativeShaderModulePayload::ConstructionKey{}, wgslBytes);
    auto wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = {wgslBytes.data(), wgslBytes.size()};
    auto desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.label = webGpuNativeStringView(package.artifact().physicalLayout.name);
    desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
    payload->module = wgpuDeviceCreateShaderModule(deviceRef.value, &desc);
    if (!payload->module) {
        diagnostic = "RHI-BINDLESS-WEBGPU-SHADER-MODULE-FAILED";
        return nullptr;
    }
    diagnostic.clear();
    return payload;
#else
    diagnostic = "RHI-BINDLESS-WEBGPU-NATIVE-DEVICE-UNAVAILABLE";
    return nullptr;
#endif
}

std::shared_ptr<WebGpuNativeComputePipelinePayload>
createWebGpuNativeBindlessComputePipelinePayload(const rhi::BindlessShaderPackage& package,
    std::string_view entry,
    core::pmr::String& diagnostic)
{
    auto prepared = prepareBindlessWebGpuComputeInterface(package, entry);
    if (!prepared.success) {
        diagnostic = prepared.diagnostics;
        return nullptr;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        diagnostic = "RHI-BINDLESS-WEBGPU-NATIVE-DEVICE-UNAVAILABLE";
        return nullptr;
    }
    OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
    if (!deviceRef.value) {
        diagnostic = "RHI-BINDLESS-WEBGPU-NATIVE-DEVICE-UNAVAILABLE";
        return nullptr;
    }
    auto shader = createWebGpuNativeBindlessShaderModulePayload(
        package, entry, rhi::ShaderStage::Compute, diagnostic);
    if (!shader || !shader->module)
        return nullptr;

    auto payload = core::makeShared<WebGpuNativeComputePipelinePayload>();
    appendNativeShaderStageFact(*payload,
        *shader,
        rhi::ShaderStage::Compute,
        prepared.shader.entrypoints.front().entryPoint);

    payload->shaderPackageHash = package.artifact().shaderPackageIdentity;
    payload->bindGroupCount = prepared.lowering.physicalGroupCount;
    payload->layout = createNativePipelineLayout(deviceRef.value,
        prepared.layout,
        prepared.lowering,
        prepared.shader.reflection,
        payload->descriptorSetGroups,
        payload->descriptorBindings,
        payload->descriptorTables,
        payload->pendingBindingFacts);
    if (!payload->layout) {
        diagnostic = "RHI-BINDLESS-WEBGPU-PIPELINE-LAYOUT-FAILED";
        return nullptr;
    }
    auto desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.label = webGpuNativeStringView(package.artifact().physicalLayout.name);
    desc.layout = payload->layout;
    desc.compute.module = shader->module;
    desc.compute.entryPoint =
        webGpuNativeStringView(prepared.shader.entrypoints.front().entryPoint);
    payload->pipeline = wgpuDeviceCreateComputePipeline(deviceRef.value, &desc);
    if (!payload->pipeline) {
        diagnostic = "RHI-BINDLESS-WEBGPU-PIPELINE-CREATION-FAILED";
        return nullptr;
    }
    if (!payload->issueOriginal(WebGpuNativeIssuedPipelineContract::ConstructionKey{},
            deviceRef.value,
            payload->pipeline,
            payload->layout,
            webGpuPipelineBindingSemantics(*payload),
            payload->descriptorSetGroups,
            payload->descriptorBindings,
            payload->descriptorTables,
            webGpuPipelineGraphicsCompatibility(*payload))) {
        diagnostic = "RHI-WEBGPU-PIPELINE-ISSUANCE-INVALID";
        return nullptr;
    }
    diagnostic.clear();
    return payload;
#else
    diagnostic = "RHI-BINDLESS-WEBGPU-NATIVE-DEVICE-UNAVAILABLE";
    return nullptr;
#endif
}

namespace {
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
std::shared_ptr<WebGpuNativeRenderPipelinePayload> emitWebGpuNativeRenderPipeline(
    const rhi::RenderPipelineDesc&,
    const WebGpuBindingSpaceLowering&,
    const std::shared_ptr<WebGpuNativeShaderModulePayload>&,
    WGPUDevice,
    WebGpuNativeIssuedPipelineContract::ConstructionKey,
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&,
    const std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition>&,
    const std::shared_ptr<WebGpuNativeShaderModulePayload>&,
    const WebGpuBindlessRenderInterface*,
    std::uint64_t);
#endif
} // namespace

std::shared_ptr<WebGpuNativeRenderPipelinePayload> createWebGpuNativeBindlessRenderPipelinePayload(
    const rhi::BindlessShaderPackage& package,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const rhi::RenderPipelineDesc& state,
    core::pmr::String& diagnostic,
    const rhi::BindlessAuthorization* authorization,
    const rhi::BindlessGraphicsPushRequest* push)
{
    if (push && (!authorization || push->diagnostic())) {
        diagnostic = "RHI-WEBGPU-BINDLESS-GRAPHICS-PUSH-INTENT";
        return nullptr;
    }
    if (!authorization) {
        const auto authored = rhi::validateBindlessAuthoredRenderState(state, true);
        if (!authored.success) {
            diagnostic = authored.diagnostics;
            return nullptr;
        }
        // Borrowed backend projection; the public factory admits its canonical
        // shared authorization before reaching this helper.
    } else if (!authorization->current() || &authorization->package() != &package
        || (!authorization->plan().executable
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 2u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 3u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 5u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 11u)
            && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                authorization->plan(), 12u))
        || authorization->plan().execution != rhi::BindlessExecution::Emulated
        || authorization->plan().shaderPackageIdentity
            != package.artifact().shaderPackageIdentity) {
        diagnostic = "RHI-WEBGPU-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
        return nullptr;
    }
    const auto validatedState = rhi::validateRenderPipelineState(state);
    if (!validatedState.success) {
        diagnostic = validatedState.diagnostics;
        return nullptr;
    }
    for (const auto& domain : package.artifact().additionalDomains)
        for (const auto& binding : domain.layout.bindings)
            if (binding.depthTexture || binding.comparisonSampler)
                if (const auto* id =
                        rhi::semantics::OriginalBindlessOrdinaryDepthPipelineDeclaration::inspect(
                            rhi::Backend::WebGPU, binding)) {
                    diagnostic = id;
                    return nullptr;
                }
    const auto prepared = prepareBindlessWebGpuRenderInterface(package,
        vertexEntry,
        fragmentEntry,
        push ? authorization->originalPackageOwner()
             : std::shared_ptr<const rhi::BindlessShaderPackage>{});
    if (!prepared.success) {
        diagnostic = prepared.diagnostics;
        return nullptr;
    }
    const auto scene = rhi::selectBindlessInitializedGraphics(package, vertexEntry, fragmentEntry);
    if (scene.diagnostic) {
        diagnostic = scene.diagnostic;
        return nullptr;
    }
    if (scene.recognized) {
        if (const auto* id =
                rhi::semantics::BindlessSceneRasterContract::inspect(rhi::Backend::WebGPU, state)) {
            diagnostic = id;
            return nullptr;
        }
    } else {
        // Preserve the existing non-scene scope. SceneRaster/1 adds preparation
        // only; no existing execution gate is expanded by this state adapter.
        const auto target = rhi::effectiveColorTargetAt(state, 0u);
        if (validatedState.colorTargetCount != 1u || target.format != rhi::TextureFormat::Rgba8Unorm
            || target.blendEnabled || target.writeMask != rhi::ColorWriteMask::All
            || rhi::effectiveDepthStencilFormat(state) != rhi::TextureFormat::Unknown
            || state.topology != rhi::PrimitiveTopology::TriangleList
            || state.multisample.sampleCount != 1u || state.multisample.alphaToCoverageEnabled) {
            diagnostic = "RHI-BINDLESS-WEBGPU-RENDER-STATE-UNVALIDATED";
            return nullptr;
        }
    }
    // Consume the selected authenticated entry, not merged stage metadata or
    // untrusted authored declarations. Layout construction stays cold.
    const auto module =
        std::find_if(package.modules().begin(), package.modules().end(), [&](const auto& value) {
            return value.entry == vertexEntry;
        });
    if (module == package.modules().end()) {
        diagnostic = "RHI-BINDLESS-WEBGPU-VERTEX-ENTRY";
        return nullptr;
    }
    const rhi::VertexInputConsumptionContract vertexInput(state.vertexBuffers);
    const auto vertexValid =
        rhi::validateReflectedVertexInput(vertexInput, module->reflection.reflection);
    if (!vertexValid.success) {
        diagnostic = vertexValid.diagnostics;
        return nullptr;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable()) {
        diagnostic = "RHI-BINDLESS-WEBGPU-NATIVE-DEVICE-UNAVAILABLE";
        return nullptr;
    }
    OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
    if (!deviceRef.value) {
        diagnostic = "RHI-BINDLESS-WEBGPU-NATIVE-DEVICE-UNAVAILABLE";
        return nullptr;
    }
    if (!state.vertexBuffers.empty()) {
        auto limits = WGPU_LIMITS_INIT;
        if (wgpuDeviceGetLimits(deviceRef.value, &limits) != WGPUStatus_Success
            || !limits.maxVertexBuffers || !limits.maxVertexAttributes
            || !limits.maxVertexBufferArrayStride
            || vertexInput.streams().size() > limits.maxVertexBuffers
            || vertexInput.attributes().size() > limits.maxVertexAttributes) {
            diagnostic = "RHI-BINDLESS-WEBGPU-VERTEX-DEVICE-LIMIT";
            return nullptr;
        }
        for (const auto& stream : vertexInput.streams()) {
            if (stream.strideBytes > limits.maxVertexBufferArrayStride
                || stream.strideBytes % sizeof(float)) {
                diagnostic = "RHI-BINDLESS-WEBGPU-VERTEX-DEVICE-STRIDE";
                return nullptr;
            }
        }
        for (const auto& attribute : vertexInput.attributes()) {
            if (attribute.layout.shaderLocation >= limits.maxVertexAttributes
                || attribute.layout.offsetBytes % sizeof(float)) {
                diagnostic = "RHI-BINDLESS-WEBGPU-VERTEX-DEVICE-ATTRIBUTE";
                return nullptr;
            }
        }
    }
    auto vertex = createWebGpuNativeBindlessShaderModulePayload(
        package, vertexEntry, rhi::ShaderStage::Vertex, diagnostic, bool(push));
    if (!vertex || !vertex->module)
        return nullptr;
    auto fragment = createWebGpuNativeBindlessShaderModulePayload(
        package, fragmentEntry, rhi::ShaderStage::Fragment, diagnostic, bool(push));
    if (!fragment || !fragment->module)
        return nullptr;

    auto projected = state;
    projected.vertexShader = prepared.shader.entrypoints[0u];
    projected.fragmentShader = prepared.shader.entrypoints[1u];
    projected.shaderPackage = &prepared.shader;
    auto payload = emitWebGpuNativeRenderPipeline(projected,
        prepared.lowering,
        vertex,
        deviceRef.value,
        WebGpuNativeIssuedPipelineContract::ConstructionKey{},
        {},
        {},
        fragment,
        &prepared,
        package.artifact().shaderPackageIdentity);
    if (!payload) {
        diagnostic = "RHI-BINDLESS-WEBGPU-RENDER-PIPELINE-FAILED";
        return nullptr;
    }
    diagnostic.clear();
    return payload;
#else
    diagnostic = "RHI-BINDLESS-WEBGPU-NATIVE-DEVICE-UNAVAILABLE";
    return nullptr;
#endif
}

namespace {
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
std::shared_ptr<WebGpuNativeRenderPipelinePayload> emitWebGpuNativeRenderPipeline(
    const rhi::RenderPipelineDesc& desc,
    const WebGpuBindingSpaceLowering& lowering,
    const std::shared_ptr<WebGpuNativeShaderModulePayload>& shader,
    WGPUDevice device,
    WebGpuNativeIssuedPipelineContract::ConstructionKey key,
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& graphicsInput = {},
    const std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition>& expansion =
        {},
    const std::shared_ptr<WebGpuNativeShaderModulePayload>& fragmentShader = {},
    const WebGpuBindlessRenderInterface* bindless = nullptr,
    std::uint64_t bindlessPackageHash = 0u)
{
    const auto colorTargetCount = rhi::effectiveColorTargetCount(desc);
    const bool depthOnlyRenderPipeline = colorTargetCount == 0u;
    core::pmr::Vector<WGPUBlendState> blendStates(&core::defaultCpuMemoryResource());
    blendStates.resize(colorTargetCount, WGPU_BLEND_STATE_INIT);
    core::pmr::Vector<WGPUColorTargetState> colorTargets(&core::defaultCpuMemoryResource());
    colorTargets.resize(colorTargetCount, WGPU_COLOR_TARGET_STATE_INIT);
    for (std::uint32_t index = 0; index < colorTargetCount; ++index) {
        const auto target = rhi::effectiveColorTargetAt(desc, index);
        auto& blendState = blendStates[index];
        blendState.color.srcFactor = nativeBlendFactor(target.colorBlend.sourceFactor);
        blendState.color.dstFactor = nativeBlendFactor(target.colorBlend.destinationFactor);
        blendState.color.operation = nativeBlendOperation(target.colorBlend.operation);
        blendState.alpha.srcFactor = nativeBlendFactor(target.alphaBlend.sourceFactor);
        blendState.alpha.dstFactor = nativeBlendFactor(target.alphaBlend.destinationFactor);
        blendState.alpha.operation = nativeBlendOperation(target.alphaBlend.operation);
        auto& colorTarget = colorTargets[index];
        colorTarget.format = nativeTextureFormat(target.format);
        colorTarget.blend = target.blendEnabled ? &blendState : nullptr;
        colorTarget.writeMask = nativeColorWriteMask(target.writeMask);
    }
    auto fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentShader ? fragmentShader->module : shader->module;
    fragment.entryPoint = webGpuNativeStringView(desc.fragmentShader.entryPoint);
    fragment.targetCount = colorTargetCount;
    fragment.targets = depthOnlyRenderPipeline ? nullptr : colorTargets.data();

    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    const auto depthFormat = rhi::effectiveDepthStencilFormat(desc);
    const bool hasDepthStencilState = rhi::isDepthStencilFormat(depthFormat);
    if (hasDepthStencilState) {
        depthStencil.format = nativeTextureFormat(depthFormat);
        depthStencil.depthWriteEnabled =
            desc.depthStencil.depthWriteEnabled ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = nativeCompareFunction(!desc.depthStencil.depthTestEnabled
                ? rhi::CompareOp::Always
                : desc.depthStencil.depthCompare);
        // depthBias: WebGPU spec guarantees WGPUDepthStencilState.depthBias
        // (int32), depthBiasSlopeScale and depthBiasClamp on every conformant
        // implementation; the capability contract reports
        // depthBiasClampSupported=true for WebGPU. Previously these three
        // fields were silently dropped, so a pipeline that enabled depthBias
        // rendered without any bias - a silent native semantics drop the RHI
        // contract forbids. The L2 depthBiasConstantFactor is a float; WebGPU
        // takes int32, so round here (matches Vulkan's int32 interpretation
        // for the constant-factor component).
        depthStencil.depthBias = desc.depthStencil.depthBiasEnabled
            ? static_cast<std::int32_t>(desc.depthStencil.depthBiasConstantFactor)
            : 0;
        depthStencil.depthBiasSlopeScale =
            desc.depthStencil.depthBiasEnabled ? desc.depthStencil.depthBiasSlopeFactor : 0.0f;
        depthStencil.depthBiasClamp =
            desc.depthStencil.depthBiasEnabled ? desc.depthStencil.depthBiasClamp : 0.0f;
        // stencil front/back: previously the WGPUDepthStencilState was built
        // without stencilFront/stencilBack even when stencilEnabled was true,
        // so stencil state was silently lost. Lower both faces explicitly;
        // stencilReadMask/stencilWriteMask are shared between faces in WebGPU.
        depthStencil.stencilFront = nativeStencilFaceState(desc.depthStencil.front);
        depthStencil.stencilBack = nativeStencilFaceState(desc.depthStencil.back);
        depthStencil.stencilReadMask =
            static_cast<std::uint32_t>(desc.depthStencil.stencilReadMask);
        depthStencil.stencilWriteMask =
            static_cast<std::uint32_t>(desc.depthStencil.stencilWriteMask);
    }

    auto pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.label = webGpuNativeStringView(desc.label);
    pipelineDesc.vertex.module = shader->module;
    pipelineDesc.vertex.entryPoint = webGpuNativeStringView(desc.vertexShader.entryPoint);
    pipelineDesc.primitive.topology = nativePrimitiveTopology(desc.topology);
    pipelineDesc.primitive.cullMode = nativeCullMode(desc.cullMode);
    pipelineDesc.primitive.frontFace = nativeFrontFace(desc.frontFace);
    pipelineDesc.fragment = &fragment;
    if (hasDepthStencilState) {
        pipelineDesc.depthStencil = &depthStencil;
    }
    pipelineDesc.multisample.count = desc.multisample.sampleCount;
    pipelineDesc.multisample.mask = static_cast<std::uint32_t>(desc.multisample.sampleMask);
    pipelineDesc.multisample.alphaToCoverageEnabled = desc.multisample.alphaToCoverageEnabled;

    auto payload = arp::core::makeShared<WebGpuNativeRenderPipelinePayload>();
    appendNativeShaderStageFact(
        *payload, *shader, rhi::ShaderStage::Vertex, desc.vertexShader.entryPoint);
    appendNativeShaderStageFact(*payload,
        fragmentShader ? *fragmentShader : *shader,
        rhi::ShaderStage::Fragment,
        desc.fragmentShader.entryPoint);

    payload->hasViewport = desc.hasViewport;
    payload->viewport = desc.viewport;
    payload->hasScissor = desc.hasScissor;
    payload->scissor = desc.scissor;
    payload->sourceContractVersion = expansion ? 2u : 1u;
    payload->shaderPackageHash =
        bindless ? bindlessPackageHash : rhi::stableShaderPackageHash(*desc.shaderPackage);
    payload->primitiveTopology = desc.topology;
    payload->vertexBufferCount = static_cast<std::uint32_t>(desc.vertexBuffers.size());
    payload->depthStencilFormat = rhi::effectiveDepthStencilFormat(desc);
    payload->sampleCount = desc.multisample.sampleCount;
    payload->colorFormats.reserve(colorTargetCount);
    for (std::uint32_t index = 0; index < colorTargetCount; ++index) {
        payload->colorFormats.push_back(rhi::effectiveColorTargetAt(desc, index).format);
    }
    payload->usesPushConstantGroup = bindless
        ? !bindless->layout.pushConstantRanges.empty()
        : desc.layout && !desc.layout->desc().pushConstantRanges.empty();
    if (desc.layout) {
        for (const auto& descriptorLayout : desc.layout->desc().descriptorLayouts) {
            for (const auto& binding : descriptorLayout.bindings) {
                if (binding.type == rhi::DescriptorType::InputAttachment) {
                    payload->hasValidatedInputAttachmentLowering = true;
                }
            }
        }
    }
    payload->pushConstantGroup = expansion ? UINT32_MAX : lowering.pushConstantGroup;
    payload->pushConstantBinding = expansion ? UINT32_MAX : lowering.pushConstantBinding;
    payload->bindGroupCount =
        expansion ? expansion->space()->physicalGroupCount() : lowering.physicalGroupCount;
    payload->layout = expansion     ? createExpandedNativePipelineLayout(device,
                                      *expansion,
                                      payload->descriptorSetGroups,
                                      payload->descriptorBindings,
                                      payload->descriptorTables,
                                      payload->pendingBindingFacts)
        : (bindless || desc.layout) ? createNativePipelineLayout(device,
                                          bindless ? bindless->layout : desc.layout->desc(),
                                          lowering,
                                          desc.shaderPackage->reflection,
                                          payload->descriptorSetGroups,
                                          payload->descriptorBindings,
                                          payload->descriptorTables,
                                          payload->pendingBindingFacts)
                                    : nullptr;
    if (!payload->layout) {
        std::printf("[wgpu-pipe] native layout creation failed label='%s'\n", desc.label.c_str());
        return nullptr;
    }
    pipelineDesc.layout = payload->layout;

    const WebGpuNativeVertexLayouts vertexLayouts(desc.vertexBuffers,
        bindless ? WebGpuNativeVertexLayouts::SlotMapping::PhysicalOrdinals
                 : WebGpuNativeVertexLayouts::SlotMapping::LogicalSlots);
    if (!vertexLayouts.apply(pipelineDesc.vertex))
        return {};
    payload->vertexBufferCount = pipelineDesc.vertex.bufferCount;

    std::printf("[wgpu-pipe] create-render-pipeline\n");
    payload->pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    std::printf("[wgpu-pipe] render-pipeline=%p\n", (void*)payload->pipeline);
    if (payload->pipeline && expansion) {
        if (!payload->issueExpansion(key,
                device,
                payload->pipeline,
                payload->layout,
                webGpuPipelineBindingSemantics(*payload),
                payload->descriptorSetGroups,
                payload->descriptorBindings,
                payload->descriptorTables,
                webGpuPipelineGraphicsCompatibility(*payload),
                expansion))
            return nullptr;
    } else if (payload->pipeline
        && !payload->issueOriginal(key,
            device,
            payload->pipeline,
            payload->layout,
            webGpuPipelineBindingSemantics(*payload),
            payload->descriptorSetGroups,
            payload->descriptorBindings,
            payload->descriptorTables,
            webGpuPipelineGraphicsCompatibility(*payload),
            {},
            graphicsInput))
        return nullptr;
    return payload->pipeline ? payload : nullptr;
}
#endif
} // namespace

std::shared_ptr<WebGpuNativeRenderPipelinePayload> createWebGpuNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc& desc)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    const bool pipeEnvOk = browserWebGpuEnvironmentAvailable();
    std::printf("[wgpu-pipe] enter envOk=%d hasShaderPkg=%d\n",
        pipeEnvOk ? 1 : 0,
        desc.shaderPackage != nullptr ? 1 : 0);
    if (!pipeEnvOk || !desc.shaderPackage) {
        return nullptr;
    }

    // The reflection artifact is the executable WebGPU lowering contract.
    // Validate it before touching any browser-native shader object so an
    // unavailable target (for example the atomic cull shaders) fails at the
    // lowering boundary rather than first attempting to load a missing WGSL
    // file and only then discovering the capability diagnostic.
    const auto lowering = loadWebGpuBindingSpaceLowering(*desc.shaderPackage);
    if (!lowering.success) {
        std::printf("[wgpu-pipe] lowering failed label='%s' diag='%s'\n",
            desc.label.c_str(),
            lowering.diagnostics.c_str());
        return nullptr;
    }
    const auto loweringValidation = validateWebGpuBindingSpaceLowering(
        desc.layout ? desc.layout->desc() : rhi::PipelineLayoutDesc{},
        desc.shaderPackage->reflection,
        lowering.lowering,
        lowering.lowering.pushConstantGroup);
    if (!loweringValidation.success) {
        std::printf("[wgpu-pipe] lowering validation failed label='%s' diag='%s'\n",
            desc.label.c_str(),
            loweringValidation.diagnostics.c_str());
        return nullptr;
    }

    OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
    auto* device = deviceRef.value;
    auto shader = createWebGpuNativeShaderModulePayload(*desc.shaderPackage);
    const auto colorTargetCount = rhi::effectiveColorTargetCount(desc);
    const bool depthOnlyRenderPipeline = colorTargetCount == 0u;
    if (!device || !shader || !shader->module
        || (!depthOnlyRenderPipeline
            && nativeTextureFormat(rhi::effectiveColorTargetAt(desc, 0).format)
                == WGPUTextureFormat_Undefined)) {
        std::printf("[wgpu-pipe] native prerequisites failed label='%s' device=%p shader=%p "
                    "module=%p depthOnly=%d\n",
            desc.label.c_str(),
            (void*)device,
            (void*)shader.get(),
            shader ? (void*)shader->module : nullptr,
            depthOnlyRenderPipeline ? 1 : 0);
        return nullptr;
    }

    return emitWebGpuNativeRenderPipeline(desc,
        lowering.lowering,
        shader,
        device,
        WebGpuNativeIssuedPipelineContract::ConstructionKey{});
#else
    (void)desc;
    return nullptr;
#endif
}

namespace {
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
std::shared_ptr<WebGpuNativeComputePipelinePayload> emitWebGpuNativeComputePipeline(
    const rhi::ComputePipelineDesc& desc,
    const WebGpuBindingSpaceLowering& lowering,
    const std::shared_ptr<WebGpuNativeShaderModulePayload>& shader,
    WGPUDevice device,
    WebGpuNativeIssuedPipelineContract::ConstructionKey key,
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& artifactInput = {},
    const std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition>& expansion =
        {})
{
    auto pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.label = webGpuNativeStringView(desc.label);
    pipelineDesc.compute.module = shader->module;
    pipelineDesc.compute.entryPoint = webGpuNativeStringView(desc.computeShader.entryPoint);

    auto payload = arp::core::makeShared<WebGpuNativeComputePipelinePayload>();
    appendNativeShaderStageFact(
        *payload, *shader, rhi::ShaderStage::Compute, desc.computeShader.entryPoint);

    payload->sourceContractVersion = expansion ? 2u : 1u;
    payload->shaderPackageHash = rhi::stableShaderPackageHash(*desc.shaderPackage);
    payload->usesPushConstantGroup = desc.layout && !desc.layout->desc().pushConstantRanges.empty();
    if (desc.layout) {
        for (const auto& descriptorLayout : desc.layout->desc().descriptorLayouts) {
            for (const auto& binding : descriptorLayout.bindings) {
                if (binding.type == rhi::DescriptorType::InputAttachment) {
                    payload->hasValidatedInputAttachmentLowering = true;
                }
            }
        }
    }
    payload->pushConstantGroup = expansion ? UINT32_MAX : lowering.pushConstantGroup;
    payload->pushConstantBinding = expansion ? UINT32_MAX : lowering.pushConstantBinding;
    payload->bindGroupCount =
        expansion ? expansion->space()->physicalGroupCount() : lowering.physicalGroupCount;
    payload->layout = expansion ? createExpandedNativePipelineLayout(device,
                                      *expansion,
                                      payload->descriptorSetGroups,
                                      payload->descriptorBindings,
                                      payload->descriptorTables,
                                      payload->pendingBindingFacts)
        : desc.layout           ? createNativePipelineLayout(device,
                            desc.layout->desc(),
                            lowering,
                            desc.shaderPackage->reflection,
                            payload->descriptorSetGroups,
                            payload->descriptorBindings,
                            payload->descriptorTables,
                            payload->pendingBindingFacts)
                                : nullptr;
    if (!payload->layout) {
        return nullptr;
    }
    pipelineDesc.layout = payload->layout;
    std::printf("[wgpu-pipe] create-compute-pipeline\n");
    payload->pipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    std::printf("[wgpu-pipe] compute-pipeline=%p\n", (void*)payload->pipeline);
    if (payload->pipeline && expansion) {
        if (!payload->issueExpansion(key,
                device,
                payload->pipeline,
                payload->layout,
                webGpuPipelineBindingSemantics(*payload),
                payload->descriptorSetGroups,
                payload->descriptorBindings,
                payload->descriptorTables,
                webGpuPipelineGraphicsCompatibility(*payload),
                expansion))
            return nullptr;
    } else if (payload->pipeline
        && !payload->issueOriginal(key,
            device,
            payload->pipeline,
            payload->layout,
            webGpuPipelineBindingSemantics(*payload),
            payload->descriptorSetGroups,
            payload->descriptorBindings,
            payload->descriptorTables,
            webGpuPipelineGraphicsCompatibility(*payload),
            artifactInput))
        return nullptr;
    return payload->pipeline ? payload : nullptr;
}
#endif
} // namespace

std::shared_ptr<WebGpuNativeComputePipelinePayload> createWebGpuNativeComputePipelinePayload(
    const rhi::ComputePipelineDesc& desc)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable() || !desc.shaderPackage) {
        return nullptr;
    }

    // Keep lowering and artifact validation ahead of native module creation.
    // An unavailable WGSL target is a semantic capability failure, not a
    // native shader-file lookup failure.
    const auto lowering = loadWebGpuBindingSpaceLowering(*desc.shaderPackage);
    if (!lowering.success) {
        return nullptr;
    }
    const auto loweringValidation = validateWebGpuBindingSpaceLowering(
        desc.layout ? desc.layout->desc() : rhi::PipelineLayoutDesc{},
        desc.shaderPackage->reflection,
        lowering.lowering,
        lowering.lowering.pushConstantGroup);
    if (!loweringValidation.success) {
        return nullptr;
    }

    OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
    auto* device = deviceRef.value;
    auto shader = createWebGpuNativeShaderModulePayload(*desc.shaderPackage);
    if (!device || !shader || !shader->module) {
        return nullptr;
    }

    return emitWebGpuNativeComputePipeline(desc,
        lowering.lowering,
        shader,
        device,
        WebGpuNativeIssuedPipelineContract::ConstructionKey{});
#else
    (void)desc;
    return nullptr;
#endif
}

std::shared_ptr<WebGpuNativeComputePipelinePayload> WebGpuOriginalArtifactComputeFactory::create(
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& input,
    const rhi::PipelineLayout& layout,
    core::StringView label,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    using Contract = rhi::OriginalPipelineArtifactConsumptionContract;
    if (const auto* id = Contract::validateBudget(budget)) {
        diagnostic = id;
        return {};
    }
    const auto target =
        rhi::OriginalSingleEntryPipelineInput::selectTarget(input, rhi::Backend::WebGPU);
    if (target.diagnostic) {
        diagnostic = target.diagnostic;
        return {};
    }
    if (input->artifact()->stage() != shader::ShaderStage::Compute) {
        diagnostic = "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-STAGE";
        return {};
    }
    if (target.bytes->size() > budget.maxTargetBytes) {
        diagnostic = "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-TARGET-BUDGET";
        return {};
    }
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-QUOTA";
        return {};
    }
    for (const auto& domain : std::array{input->quota(), input->artifact()->quota()}) {
        if (quota.get() == domain.get()
            || core::SharedOwnerProjection::sameControlBlock(quota, domain)) {
            diagnostic = "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-QUOTA-DOMAIN";
            return {};
        }
    }
    const auto decision = Contract::lowering(
        rhi::Backend::WebGPU, webGpuNativeDeviceAvailable() && !webGpuNativeDeviceLost());
    if (decision.diagnostic) {
        diagnostic = decision.diagnostic;
        return {};
    }
    if (const auto* id = WebGpuDescriptorArrayAbiContract::validateBindings(
            input->package().reflection.descriptorBindings)) {
        diagnostic = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    try {
        core::CpuMemoryQuotaScope scope(quota);
        rhi::ComputePipelineDesc desc;
        desc.label = label;
        desc.layout = &layout;
        desc.shaderPackage = &input->package();
        desc.computeShader = input->package().entrypoints.front();
        const auto state = rhi::validateComputePipelineState(desc);
        if (!state.success) {
            diagnostic = "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-COMPUTE-STATE";
            return {};
        }
        using Declarations = rhi::PipelineShaderDeclarationContract;
        if (const auto* id = Declarations::validateOriginal(input,
                layout.desc(),
                input->package(),
                1u,
                Declarations::Kind::ComputePipeline,
                [&](std::size_t) -> const rhi::ShaderEntrypoint& {
                    return desc.computeShader;
                })) {
            diagnostic = id;
            return {};
        }
        const auto lowering = loadOriginalWebGpuBindingSpaceLowering(input);
        if (!lowering.success) {
            diagnostic = lowering.diagnostics;
            return {};
        }
        const auto valid = validateWebGpuBindingSpaceLowering(layout.desc(),
            input->package().reflection,
            lowering.lowering,
            lowering.lowering.pushConstantGroup);
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return {};
        }
        OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
        auto shader = emitWebGpuNativeShaderModule(label,
            shader::shaderArtifactText(*target.bytes),
            WebGpuNativeShaderModulePayload::ConstructionKey{});
        if (!deviceRef.value || !shader || !shader->module) {
            diagnostic = "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-SHADER-FAILED";
            return {};
        }
        auto pipeline = emitWebGpuNativeComputePipeline(desc,
            lowering.lowering,
            shader,
            deviceRef.value,
            WebGpuNativeIssuedPipelineContract::ConstructionKey{},
            input);
        if (!pipeline) {
            diagnostic = "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-PIPELINE-FAILED";
            return {};
        }
        const auto& stages = pipeline->issuedContract()->stages();
        const auto digest = core::Sha256::digest(shader::shaderArtifactText(*target.bytes));
        if (stages.size() != 1u || stages[0].sourceBytes != target.bytes->size()
            || !std::equal(digest.begin(), digest.end(), stages[0].sourceSha256.begin())) {
            diagnostic = "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-SOURCE-MISMATCH";
            return {};
        }
        if (const auto* id = validateConsumedOriginal(pipeline)) {
            diagnostic = id;
            return {};
        }
        return pipeline;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-ALLOCATION";
        return {};
    }
#else
    (void)layout;
    (void)label;
    diagnostic = "RHI-ORIGINAL-ARTIFACT-WEBGPU-DEVICE-UNAVAILABLE";
    return {};
#endif
}

const char* WebGpuOriginalArtifactComputeFactory::validateConsumedOriginal(
    const std::shared_ptr<WebGpuNativeComputePipelinePayload>& pipeline) noexcept
{
    if (!pipeline || !pipeline->sharedOwnerMatches(pipeline))
        return "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-PIPELINE-OWNER";
    const auto* issued = pipeline->issuedContract().get();
    if (!issued || !issued->artifactInput())
        return "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-RECEIPT-MISSING";
    if (const auto* id = validateIssuedPipeline(pipeline, issued))
        return id;
    if (const auto* id =
            rhi::OriginalSingleEntryPipelineInput::validateOriginal(issued->artifactInput()))
        return id;
    if (!issued->quota() || issued->quota()->limitBytes() != issued->issuedCpuQuotaLimit())
        return "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-QUOTA-LIMIT";
    const auto& input = *issued->artifactInput();
    const auto& stages = issued->stages();
    if (stages.size() != 1u || stages[0].stage != rhi::ShaderStage::Compute
        || stages[0].entry != input.artifact()->entry()
        || stages[0].sourceBytes
            != input.artifact()->bytes(shader::ShaderArtifactRole::Wgsl).size())
        return "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-SOURCE-MISMATCH";
    return nullptr;
}

bool WebGpuOriginalArtifactGraphicsFactory::validateDefinition(
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
    const rhi::RenderPipelineDesc& desc,
    core::pmr::String& diagnostic,
    std::uint32_t profileVersion)
{
    diagnostic.clear();
    if (const auto* id = rhi::OriginalGraphicsPipelineInput::validateOriginal(input)) {
        diagnostic = id;
        return false;
    }
    if (desc.shaderPackage != &input->package()) {
        diagnostic = "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-PACKAGE";
        return false;
    }
    if (!desc.layout) {
        diagnostic = "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-LAYOUT";
        return false;
    }
    if (const auto* id = rhi::OriginalGraphicsArtifactConsumptionContract::validateProfile(
            desc, profileVersion)) {
        diagnostic = id;
        return false;
    }
    if (!desc.vertexShader.matchesDefinition(input->package().entrypoints[0])
        || !desc.fragmentShader.matchesDefinition(input->package().entrypoints[1])) {
        diagnostic = "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-SELECTED-ENTRIES";
        return false;
    }
    using Declarations = rhi::PipelineShaderDeclarationContract;
    if (const auto* id = Declarations::validateOriginal(input,
            desc.layout->desc(),
            input->package(),
            2u,
            Declarations::Kind::GraphicsPipeline,
            [&](std::size_t i) -> const auto& {
                return i ? desc.fragmentShader : desc.vertexShader;
            })) {
        diagnostic = id;
        return false;
    }
    rhi::VertexInputConsumptionContract vertex(desc.vertexBuffers);
    const auto vertexValid =
        rhi::validateSingleEntryVertexInput(vertex, *input->artifact()->vertex());
    if (!vertexValid.success) {
        diagnostic = vertexValid.diagnostics;
        return false;
    }
    const auto state = validateOriginalWebGpuRhiRenderPipeline(desc, input);
    if (!state.success) {
        diagnostic = state.diagnostics;
        return false;
    }
    return true;
}

std::shared_ptr<WebGpuNativeRenderPipelinePayload> WebGpuOriginalArtifactGraphicsFactory::create(
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
    const rhi::RenderPipelineDesc& authored,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    using Contract = rhi::OriginalGraphicsArtifactConsumptionContract;
    const auto fail = [&](const char* id) -> std::shared_ptr<WebGpuNativeRenderPipelinePayload> {
        diagnostic = id;
        return {};
    };
    if (const auto* id = Contract::validateBudget(budget))
        return fail(id);
    const auto target =
        rhi::OriginalGraphicsPipelineInput::selectTarget(input, rhi::Backend::WebGPU);
    if (target.diagnostic)
        return fail(target.diagnostic);
    if (target.sourceCount != 1u || !target.combinedModule)
        return fail("RHI-ORIGINAL-GRAPHICS-CONSUMPTION-TARGET");
    if (target.sources[0]->size() > budget.maxTargetBytes)
        return fail("RHI-ORIGINAL-GRAPHICS-CONSUMPTION-TARGET-BUDGET");
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
        return fail("RHI-ORIGINAL-GRAPHICS-CONSUMPTION-QUOTA");
    for (const auto& domain : std::array{input->quota(), input->artifact()->quota()})
        if (quota.get() == domain.get()
            || core::SharedOwnerProjection::sameControlBlock(quota, domain))
            return fail("RHI-ORIGINAL-GRAPHICS-CONSUMPTION-QUOTA-DOMAIN");
    if (authored.shaderPackage != &input->package())
        return fail("RHI-ORIGINAL-GRAPHICS-CONSUMPTION-PACKAGE");
    if (!authored.layout)
        return fail("RHI-ORIGINAL-GRAPHICS-CONSUMPTION-LAYOUT");
    const auto decision = Contract::lowering(
        rhi::Backend::WebGPU, webGpuNativeDeviceAvailable() && !webGpuNativeDeviceLost());
    if (decision.diagnostic)
        return fail(decision.diagnostic);
    if (const auto* id = WebGpuDescriptorArrayAbiContract::validateBindings(
            input->package().reflection.descriptorBindings))
        return fail(id);
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    try {
        core::CpuMemoryQuotaScope scope(quota);
        const auto desc = rhi::RenderPipelineStateCopy::copy(authored);
        if (!validateDefinition(input, desc, diagnostic, budget.contractVersion))
            return {};
        const auto lowering = loadOriginalWebGpuGraphicsBindingSpaceLowering(input);
        if (!lowering.success) {
            diagnostic = lowering.diagnostics;
            return {};
        }
        OwnedDeviceRef deviceRef{emscripten_webgpu_get_device()};
        auto shader = emitWebGpuNativeShaderModule(desc.label,
            shader::shaderArtifactText(*target.sources[0]),
            WebGpuNativeShaderModulePayload::ConstructionKey{});
        if (!deviceRef.value || !shader || !shader->module)
            return fail("RHI-ORIGINAL-GRAPHICS-CONSUMPTION-SHADER-FAILED");
        auto pipeline = emitWebGpuNativeRenderPipeline(desc,
            lowering.lowering,
            shader,
            deviceRef.value,
            WebGpuNativeIssuedPipelineContract::ConstructionKey{},
            input);
        if (!pipeline)
            return fail("RHI-ORIGINAL-GRAPHICS-CONSUMPTION-PIPELINE-FAILED");
        const auto digest = core::Sha256::digest(shader::shaderArtifactText(*target.sources[0]));
        for (const auto& stage : pipeline->issuedContract()->stages())
            if (!std::equal(digest.begin(), digest.end(), stage.sourceSha256.begin()))
                return fail("RHI-ORIGINAL-GRAPHICS-CONSUMPTION-SOURCE-MISMATCH");
        if (const auto* id = validateConsumedOriginal(pipeline))
            return fail(id);
        return pipeline;
    } catch (const std::bad_alloc&) {
        return fail("RHI-ORIGINAL-GRAPHICS-CONSUMPTION-ALLOCATION");
    }
#else
    return fail("RHI-ORIGINAL-GRAPHICS-WEBGPU-DEVICE-UNAVAILABLE");
#endif
}

const char* WebGpuOriginalArtifactGraphicsFactory::validateConsumedOriginal(
    const std::shared_ptr<WebGpuNativeRenderPipelinePayload>& pipeline) noexcept
{
    if (!pipeline || !pipeline->sharedOwnerMatches(pipeline))
        return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-PIPELINE-OWNER";
    const auto* issued = pipeline->issuedContract().get();
    if (!issued || !issued->graphicsInput() || issued->artifactInput())
        return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-RECEIPT-MISSING";
    if (const auto* id = validateIssuedPipeline(pipeline, issued))
        return id;
    if (const auto* id =
            rhi::OriginalGraphicsPipelineInput::validateOriginal(issued->graphicsInput()))
        return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!EM_ASM_INT(
            {
                // clang-format off
                return Module.preinitializedWebGPUDevice === WebGPU.getJsObject(Number($0));
                // clang-format on
            },
            issued->nativeDevice()))
        return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-DEVICE-CHANGED";
#endif
    if (!issued->quota() || issued->quota()->limitBytes() != issued->issuedCpuQuotaLimit())
        return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-QUOTA-LIMIT";
    const auto& input = *issued->graphicsInput();
    const auto& stages = issued->stages();
    if (stages.size() != 2u || stages[0].stage != rhi::ShaderStage::Vertex
        || stages[1].stage != rhi::ShaderStage::Fragment
        || stages[0].entry != input.artifact()->vertex()->entry()
        || stages[1].entry != input.artifact()->fragment()->entry() || !stages[0].moduleIdentity
        || stages[0].moduleIdentity != stages[1].moduleIdentity
        || stages[0].sourceSha256 != stages[1].sourceSha256
        || stages[0].sourceBytes != input.artifact()->bytes(shader::ShaderArtifactRole::Wgsl).size()
        || stages[0].sourceBytes != stages[1].sourceBytes)
        return "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-SOURCE-MISMATCH";
    return nullptr;
}

namespace {
using ExpansionDefinition = WebGpuOriginalDepthTableExpansionPipelineDefinition;
using ExpansionEmission = rhi::OriginalDepthTableExpansionNativeEmissionContract;

const char* validateExpansionEmission(const std::shared_ptr<const ExpansionDefinition>& definition,
    ExpansionEmission::Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    bool compute)
{
    if (const auto* id = ExpansionEmission::validateBudget(budget))
        return id;
    if (const auto* id = ExpansionDefinition::validateOriginal(definition))
        return id;
    if (definition->backend() != rhi::Backend::WebGPU)
        return "RHI-ORIGINAL-TABLE-EMISSION-DEFINITION-BACKEND";
    if (definition->requirements().compute != compute)
        return "RHI-ORIGINAL-TABLE-EMISSION-PROFILE";
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
        return "RHI-ORIGINAL-TABLE-EMISSION-QUOTA";
    const auto& space = *definition->space();
    const auto& inputQuota =
        compute ? space.computeInput()->quota() : space.graphicsInput()->quota();
    const auto& artifact =
        compute ? space.computeInput()->tableExpansion() : space.graphicsInput()->tableExpansion();
    const std::array domains{
        quota, definition->quota(), space.quota(), inputQuota, artifact->quota()};
    if (!core::SharedOwnerProjection::hasIndependentOwners(
            domains.size(), [&](std::size_t i) -> const auto& {
                return domains[i];
            }))
        return "RHI-ORIGINAL-TABLE-EMISSION-QUOTA-DOMAIN";
    if (definition->requirements().shaderSourceBytes > budget.maxTargetBytes)
        return "RHI-ORIGINAL-TABLE-EMISSION-TARGET-BUDGET";
    if (!compute)
        if (const auto* id = ExpansionEmission::validateState(definition->graphicsDefinition()))
            return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!compute) {
        const auto& desc = definition->graphicsDefinition();
        for (std::uint32_t i = 0u; i < rhi::effectiveColorTargetCount(desc); ++i)
            if (nativeTextureFormat(rhi::effectiveColorTargetAt(desc, i).format)
                == WGPUTextureFormat_Undefined)
                return "RHI-ORIGINAL-TABLE-EMISSION-FORMAT";
        const auto depth = rhi::effectiveDepthStencilFormat(desc);
        if (depth != rhi::TextureFormat::Unknown
            && nativeTextureFormat(depth) == WGPUTextureFormat_Undefined)
            return "RHI-ORIGINAL-TABLE-EMISSION-FORMAT";
    }
#endif
    if (webGpuNativeDeviceLost())
        return "RHI-ORIGINAL-TABLE-EMISSION-DEVICE-LOST";
    const auto decision = ExpansionEmission::lowering(
        rhi::Backend::WebGPU, ExpansionDefinition::Preflight::query(definition->requirements()));
    return decision.emissionAvailable ? nullptr : decision.diagnostic;
}

template <class Pipeline>
const char* validateExpandedPipeline(
    const std::shared_ptr<Pipeline>& pipeline, bool compute) noexcept
{
    if (!pipeline || !pipeline->sharedOwnerMatches(pipeline))
        return "RHI-ORIGINAL-TABLE-EMISSION-PIPELINE-OWNER";
    const auto* issued = pipeline->issuedContract().get();
    if (!issued)
        return "RHI-ORIGINAL-TABLE-EMISSION-ISSUED-MISSING";
    if (const auto* id = validateIssuedPipeline(pipeline, issued))
        return id;
    if (issued->semantics().contractVersion != 2u)
        return "RHI-ORIGINAL-TABLE-EMISSION-SOURCE-VERSION";
    if (const auto* id = WebGpuOriginalDepthTableExpansionNativeContract::validateJoin(*issued))
        return id;
    if (issued->expansionDefinition()->requirements().compute != compute)
        return "RHI-ORIGINAL-TABLE-EMISSION-PROFILE";
    if (!issued->quota() || issued->quota()->limitBytes() != issued->issuedCpuQuotaLimit())
        return "RHI-ORIGINAL-TABLE-EMISSION-QUOTA-LIMIT";
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    if (!EM_ASM_INT(
            {
                // clang-format off
                return Module.preinitializedWebGPUDevice === WebGPU.getJsObject(Number($0));
                // clang-format on
            },
            issued->nativeDevice()))
        return "RHI-ORIGINAL-TABLE-EMISSION-DEVICE-CHANGED";
#endif
    return nullptr;
}
} // namespace

std::shared_ptr<WebGpuNativeComputePipelinePayload>
WebGpuOriginalArtifactComputeFactory::createTableExpansion(
    const std::shared_ptr<const ExpansionDefinition>& definition,
    ExpansionBudget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (const auto* id = validateExpansionEmission(definition, budget, quota, true)) {
        diagnostic = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    try {
        core::CpuMemoryQuotaScope scope(quota);
        const auto& desc = definition->computeDefinition();
        const auto& artifact = *definition->space()->computeInput()->tableExpansion();
        const auto& bytes = artifact.entry(0u)
                                .files[static_cast<std::size_t>(shader::ShaderArtifactRole::Wgsl)]
                                .bytes;
        OwnedDeviceRef device{emscripten_webgpu_get_device()};
        if (!device.value) {
            diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-DEVICE";
            return {};
        }
        auto module = emitWebGpuNativeShaderModule(desc.label,
            shader::shaderArtifactText(bytes),
            WebGpuNativeShaderModulePayload::ConstructionKey{});
        if (!module || !module->module) {
            diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-SHADER";
            return {};
        }
        auto result = emitWebGpuNativeComputePipeline(desc,
            {},
            module,
            device.value,
            WebGpuNativeIssuedPipelineContract::ConstructionKey{},
            {},
            definition);
        if (!result) {
            diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-PIPELINE";
            return {};
        }
        if (const auto* id = validateConsumedTableExpansion(result)) {
            diagnostic = id;
            return {};
        }
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-ALLOCATION";
        return {};
    }
#else
    diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-DEVICE";
    return {};
#endif
}

const char* WebGpuOriginalArtifactComputeFactory::validateConsumedTableExpansion(
    const std::shared_ptr<WebGpuNativeComputePipelinePayload>& pipeline) noexcept
{
    return validateExpandedPipeline(pipeline, true);
}

std::shared_ptr<WebGpuNativeRenderPipelinePayload>
WebGpuOriginalArtifactGraphicsFactory::createTableExpansion(
    const std::shared_ptr<const ExpansionDefinition>& definition,
    ExpansionBudget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (const auto* id = validateExpansionEmission(definition, budget, quota, false)) {
        diagnostic = id;
        return {};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_RESOURCES_HAS_NATIVE)
    try {
        core::CpuMemoryQuotaScope scope(quota);
        const auto& desc = definition->graphicsDefinition();
        const auto& artifact = *definition->space()->graphicsInput()->tableExpansion();
        OwnedDeviceRef device{emscripten_webgpu_get_device()};
        if (!device.value) {
            diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-DEVICE";
            return {};
        }
        auto vertex = emitWebGpuNativeShaderModule(desc.label,
            shader::shaderArtifactText(artifact.entry(0u)
                    .files[static_cast<std::size_t>(shader::ShaderArtifactRole::Wgsl)]
                    .bytes),
            WebGpuNativeShaderModulePayload::ConstructionKey{});
        if (!vertex || !vertex->module) {
            diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-SHADER";
            return {};
        }
        auto fragment = emitWebGpuNativeShaderModule(desc.label,
            shader::shaderArtifactText(artifact.entry(1u)
                    .files[static_cast<std::size_t>(shader::ShaderArtifactRole::Wgsl)]
                    .bytes),
            WebGpuNativeShaderModulePayload::ConstructionKey{});
        if (!fragment || !fragment->module || vertex->module == fragment->module) {
            diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-SHADER";
            return {};
        }
        auto result = emitWebGpuNativeRenderPipeline(desc,
            {},
            vertex,
            device.value,
            WebGpuNativeIssuedPipelineContract::ConstructionKey{},
            {},
            definition,
            fragment);
        if (!result) {
            diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-PIPELINE";
            return {};
        }
        if (const auto* id = validateConsumedTableExpansion(result)) {
            diagnostic = id;
            return {};
        }
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-ALLOCATION";
        return {};
    }
#else
    diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-DEVICE";
    return {};
#endif
}

const char* WebGpuOriginalArtifactGraphicsFactory::validateConsumedTableExpansion(
    const std::shared_ptr<WebGpuNativeRenderPipelinePayload>& pipeline) noexcept
{
    return validateExpandedPipeline(pipeline, false);
}

// Backend-private candidate custody inside the existing descriptor payload.
// This state never issues a canonical command/native completion credential.
struct WebGpuNativeDescriptorGroupPayload::OriginalDepthWriteState final {
    rhi::OriginalDepthTablePublicationRequest request;
    core::LifetimeObserver<const rhi::DescriptorTable> table;
    core::pmr::Vector<rhi::OriginalDepthTablePublicationWrite> writes{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGpuOrdinaryResourceObservation> resources{
        &core::defaultCpuMemoryResource()};
    std::uint64_t allocationBytes = 0u;
};

struct WebGpuNativeDescriptorGroupPayload::OriginalGeneralWriteState final {
    rhi::OriginalGeneralDescriptorWriteRequest request;

    struct Binding {
        rhi::OriginalCommandDescriptorSetElement element;
        rhi::OriginalCommandDescriptorDependencies::Owner owner;
        std::variant<WebGpuTransferResourceObservation, WebGpuOrdinaryResourceObservation>
            observation;
    };

    core::pmr::Vector<Binding> bindings{&core::defaultCpuMemoryResource()};
};

struct WebGpuNativeDescriptorGroupPayload::OriginalFiniteWriteState final {
    using Contract = rhi::OriginalFiniteDescriptorWriteContract;
    using Lifecycle = core::runtime::CreationReceiptLifecycle;
    const rhi::GpuDevice* device = nullptr;
    rhi::OriginalDeviceLifetime::Ticket deviceLifetime;
    core::LifetimeObserver<const rhi::DescriptorSet> descriptor;
    std::shared_ptr<const rhi::DescriptorArena> arena;
    std::shared_ptr<rhi::ComputePipeline> compute;
    std::shared_ptr<rhi::RenderPipeline> graphics;
    std::array<std::shared_ptr<core::CpuMemoryQuota>, Contract::quotaCount> quotas;
    using LegacyRequest = std::variant<rhi::OriginalFiniteDescriptorWriteRequest,
        rhi::OriginalWholeDescriptorWriteRequest>;
    using Request = std::variant<rhi::OriginalFiniteDescriptorWriteRequest,
        rhi::OriginalWholeDescriptorWriteRequest,
        rhi::OriginalFiniteTargetMemoryWriteRequest,
        core::UniquePtr<OriginalDepthWriteState>,
        core::UniquePtr<OriginalGeneralWriteState>>;
    static_assert(sizeof(Request) == sizeof(LegacyRequest));
    static_assert(alignof(Request) == alignof(LegacyRequest));
    Request request;

    const OriginalGeneralWriteState* general() const noexcept
    {
        const auto* value = std::get_if<core::UniquePtr<OriginalGeneralWriteState>>(&request);
        return value ? value->get() : nullptr;
    }

    const OriginalDepthWriteState* depth() const noexcept
    {
        const auto* value = std::get_if<core::UniquePtr<OriginalDepthWriteState>>(&request);
        return value ? value->get() : nullptr;
    }

    bool targetMemory() const noexcept
    {
        return std::holds_alternative<rhi::OriginalFiniteTargetMemoryWriteRequest>(request);
    }

    bool whole() const noexcept
    {
        return std::holds_alternative<rhi::OriginalWholeDescriptorWriteRequest>(request);
    }

    std::uint32_t maxBindings() const noexcept
    {
        return std::visit(
            [](const auto& r) {
                if constexpr (std::is_same_v<std::decay_t<decltype(r)>,
                                  core::UniquePtr<OriginalDepthWriteState>>
                    || std::is_same_v<std::decay_t<decltype(r)>,
                        core::UniquePtr<OriginalGeneralWriteState>>)
                    return r->request.maxBindings;
                else
                    return r.maxBindings;
            },
            request);
    }

    const auto& maxCpuBytes() const noexcept
    {
        return std::visit(
            [](const auto& r) -> const auto& {
                if constexpr (std::is_same_v<std::decay_t<decltype(r)>,
                                  core::UniquePtr<OriginalDepthWriteState>>
                    || std::is_same_v<std::decay_t<decltype(r)>,
                        core::UniquePtr<OriginalGeneralWriteState>>)
                    return r->request.maxCpuBytes;
                else
                    return r.maxCpuBytes;
            },
            request);
    }

    const char* requestDiagnostic() const noexcept
    {
        return std::visit(
            [](const auto& r) {
                using R = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<R, core::UniquePtr<OriginalDepthWriteState>>)
                    return rhi::OriginalDepthTablePublicationContract::validate(r->request);
                else if constexpr (std::is_same_v<R, core::UniquePtr<OriginalGeneralWriteState>>)
                    return rhi::OriginalGeneralDescriptorWriteContract::validate(r->request);
                else {
                    using C = std::conditional_t<
                        std::is_same_v<R, rhi::OriginalWholeDescriptorWriteRequest>,
                        rhi::OriginalWholeDescriptorWriteContract,
                        std::conditional_t<
                            std::is_same_v<R, rhi::OriginalFiniteTargetMemoryWriteRequest>,
                            rhi::OriginalFiniteTargetMemoryWriteContract,
                            Contract>>;
                    return C::validate(r);
                }
            },
            request);
    }

    std::uint32_t logicalSet = 0u, physicalGroup = 0u;
    std::uint64_t revision = 0u;
    bool committed = false;
    Lifecycle lifecycle{Contract::callbackCount};
    WebGpuArtifactCreationScopeCallbacks<WebGpuNativeDescriptorGroupPayload> callbacks;
    // Reuse the existing declaration slots as structural binding storage; the
    // request variant alone selects the independent finite/whole semantics.
    core::pmr::Vector<rhi::OriginalFiniteDescriptorBufferWrite> declarations{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<rhi::DescriptorBindingWrite> candidateWrites{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> dynamicOffsets{&core::defaultCpuMemoryResource()};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    core::pmr::Vector<WGPUBindGroupEntry> entries{&core::defaultCpuMemoryResource()};
    WGPUDevice nativeDevice = nullptr; // retained by the genuine accepted pipeline
    WGPUBindGroup originalGroup = nullptr;
    WGPUBindGroupLayout originalLayout = nullptr;
#endif
    template <class Pipeline>
    const char* validateGeneral(const std::shared_ptr<Pipeline>& pipeline) const noexcept
    {
        const auto* selected = general();
        const auto* object = descriptor.get();
        if (!selected || !object || !pipeline || !pipeline->originalLayoutOwner())
            return "RHI-ORIGINAL-GENERAL-WRITE-OWNERS";
        if (!selected->bindings.size() || selected->bindings.size() > maxBindings())
            return "RHI-ORIGINAL-GENERAL-WRITE-BINDING-COUNT";
        const auto& definition = pipeline->originalLayoutOwner()->desc();
        const auto* slot = rhi::findDescriptorSetSlot(definition, logicalSet);
        const auto* layout =
            slot ? rhi::findDescriptorSetLayout(definition, slot->layoutName) : nullptr;
        if (!layout || !rhi::descriptorLayoutDefinitionMatches(*layout, object->layout())
            || layout->bindings.size() != selected->bindings.size())
            return "RHI-ORIGINAL-GENERAL-WRITE-LAYOUT";
        for (const auto& quota : quotas)
            if (const auto* id = pipeline->independentAcceptedCpuQuotaDiagnostic(quota))
                return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (entries.size() != selected->bindings.size())
            return "RHI-ORIGINAL-GENERAL-WRITE-BINDING-COUNT";
        for (std::size_t i = 0u; i < selected->bindings.size(); ++i) {
            const auto& item = selected->bindings[i];
            if (i && item.element.write.binding <= selected->bindings[i - 1u].element.write.binding)
                return "RHI-ORIGINAL-GENERAL-WRITE-BINDING-ORDER";
            const auto* ownerId = std::visit(
                [&](const auto& source) -> const char* {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(source)>::element_type>;
                    if (const auto* id = T::validateOriginal(source))
                        return id;
                    const auto check = [&](const auto& lease) -> const char* {
                        for (const auto& quota : quotas) {
                            if (core::SharedOwnerProjection::overlapsOwner(
                                    quota, lease->originalMetadataQuota()))
                                return "RHI-ORIGINAL-GENERAL-WRITE-QUOTA-DOMAIN";
                            if (const auto* id = lease->originalObject()
                                    ->originalResourceRegistration()
                                    .independentCpuQuotaDiagnostic(quota))
                                return id;
                        }
                        return nullptr;
                    };
                    if (const auto* id = check(source))
                        return id;
                    if constexpr (std::is_same_v<T, rhi::OriginalCommandTextureViewSource>) {
                        if (const auto* id = check(source->originalViewLease()))
                            return id;
                        if (const auto* id = check(source->originalParentLease()))
                            return id;
                    }
                    if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>) {
                        if (item.element.write.type == rhi::DescriptorType::StorageBuffer
                            && source->originalObject()->gpuWritesSealed())
                            return "RHI-BUFFER-GPU-WRITES-SEALED";
                        const auto* observation =
                            std::get_if<WebGpuTransferResourceObservation>(&item.observation);
                        return observation ? validateOriginalWebGpuResource(
                                                 *source->originalObject(), *observation)
                                           : "RHI-ORIGINAL-GENERAL-WRITE-OBSERVATION";
                    } else {
                        const auto* observation =
                            std::get_if<WebGpuOrdinaryResourceObservation>(&item.observation);
                        return observation ? validateOriginalWebGpuResource(
                                                 *source->originalObject(), *observation)
                                           : "RHI-ORIGINAL-GENERAL-WRITE-OBSERVATION";
                    }
                },
                item.owner);
            if (ownerId)
                return ownerId;
            WGPUBindGroupEntry entry{};
            std::uint32_t group = 0u;
            if (const auto* id = WebGpuOriginalGeneralBindingLowering::lowerFrozen(
                    {selected->request.contractVersion, true},
                    *device,
                    pipeline,
                    item.owner,
                    logicalSet,
                    item.element,
                    entry,
                    group))
                return id;
            const auto& frozen = entries[i];
            if (group != physicalGroup || entry.binding != frozen.binding
                || entry.buffer != frozen.buffer || entry.textureView != frozen.textureView
                || entry.sampler != frozen.sampler || entry.offset != frozen.offset
                || entry.size != frozen.size)
                return "RHI-ORIGINAL-GENERAL-WRITE-NATIVE-CHANGED";
            for (std::size_t j = 0u; j < i; ++j)
                if (entries[j].binding == entry.binding)
                    return "RHI-ORIGINAL-GENERAL-WRITE-PHYSICAL-BINDING";
        }
        const auto native = webGpuNativePayloadFor(*pipeline);
        std::size_t physicalCount = 0u;
        for (const auto& mapping : native->descriptorBindings) {
            if (mapping.webGpuGroup != physicalGroup)
                continue;
            if (mapping.logicalSet != logicalSet)
                return "RHI-ORIGINAL-GENERAL-WRITE-MIXED-GROUP";
            bool found = false;
            for (std::size_t i = 0u; i < selected->bindings.size(); ++i)
                if (selected->bindings[i].element.write.binding == mapping.logicalBinding
                    && entries[i].binding == mapping.webGpuBinding)
                    found = true;
            if (!found)
                return "RHI-ORIGINAL-GENERAL-WRITE-PHYSICAL-BINDING";
            ++physicalCount;
        }
        if (physicalCount != selected->bindings.size())
            return "RHI-ORIGINAL-GENERAL-WRITE-PHYSICAL-BINDING";
#endif
        using Allocation = core::SharedAllocationProjection;
        const auto allocation = Allocation::inspect(selected->bindings.size(),
            maxBindings(),
            selected->request.maxRetainedAllocationBytes,
            [&](std::size_t i) noexcept {
                return std::visit(
                    [](const auto& observation) -> Allocation::Record {
                        using O = std::decay_t<decltype(observation)>;
                        if constexpr (std::is_same_v<O, WebGpuTransferResourceObservation>)
                            return observation.allocationRecord();
                        else
                            return observation.sampler() ? Allocation::Record{}
                                                         : observation.backing().allocationRecord();
                    },
                    selected->bindings[i].observation);
            });
        return allocation.status == Allocation::Status::Complete
            ? nullptr
            : "RHI-ORIGINAL-GENERAL-WRITE-ALLOCATION-BUDGET";
    }

    template <class Pipeline>
    const char* validateDepth(const std::shared_ptr<Pipeline>& pipeline) const noexcept
    {
        const auto* selected = depth();
        const auto* object = selected ? selected->table.get() : nullptr;
        if (!object)
            return "RHI-ORIGINAL-TABLE-PUBLICATION-TABLE-EXPIRED";
        if (!object->isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (object->contentRevision() != revision + (committed ? 1u : 0u))
            return "RHI-ORIGINAL-TABLE-PUBLICATION-REVISION";
        if (const auto* id = device->validateOriginalResourceRegistration(*object))
            return id;
        if (!arena || !pipeline || !pipeline->sharedOwnerMatches(pipeline))
            return "RHI-ORIGINAL-TABLE-PUBLICATION-OWNERS";
        if (const auto* id = device->validateOriginalResourceRegistration(*arena))
            return id;
        if (const auto* id = device->validateOriginalResourceRegistration(*pipeline))
            return id;
        if (!object->originalResourceRegistration().matchesParent(
                arena->originalResourceRegistration())
            || !object->allocationEpochTicket().sameCurrentEpoch(arena->allocationEpochTicket()))
            return "RHI-ORIGINAL-TABLE-PUBLICATION-ARENA-PARENT";
        if (const auto* id = requestDiagnostic())
            return id;
        const auto& originalLayout = pipeline->originalLayoutOwner();
        if (!originalLayout)
            return "RHI-ORIGINAL-TABLE-PUBLICATION-LAYOUT";
        if (const auto* id =
                rhi::OriginalDepthTablePublicationContract::validateWrites(originalLayout->desc(),
                    object->desc(),
                    selected->writes.data(),
                    selected->writes.size()))
            return id;
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            if (!quotas[i] || quotas[i]->limitBytes() != selected->request.maxCpuBytes[i])
                return "RHI-ORIGINAL-TABLE-PUBLICATION-QUOTA";
            for (std::size_t j = 0u; j < i; ++j)
                if (core::SharedOwnerProjection::overlapsOwner(quotas[i], quotas[j]))
                    return "RHI-ORIGINAL-TABLE-PUBLICATION-QUOTA-DOMAIN";
            if (const auto* id =
                    object->originalResourceRegistration().independentCpuQuotaDiagnostic(quotas[i]))
                return id;
            if (const auto* id =
                    arena->originalResourceRegistration().independentCpuQuotaDiagnostic(quotas[i]))
                return id;
            if (const auto* id =
                    pipeline->originalResourceRegistration().independentCpuQuotaDiagnostic(
                        quotas[i]))
                return id;
        }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto native = webGpuNativePayloadFor(*pipeline);
        const char* nativeId = nullptr;
        if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
            nativeId = WebGpuOriginalArtifactComputeFactory::validateConsumedTableExpansion(native);
        else
            nativeId =
                WebGpuOriginalArtifactGraphicsFactory::validateConsumedTableExpansion(native);
        if (nativeId)
            return nativeId;
        const auto& issued = native->issuedContract();
        if (!issued || issued->semantics().contractVersion != 2u
            || issued->nativeDevice() != nativeDevice || !issued->expansionDefinition()
            || !core::SharedOwnerProjection::sameOwner(
                issued->expansionDefinition()->layout(), originalLayout))
            return "RHI-ORIGINAL-TABLE-PUBLICATION-PIPELINE-IDENTITY";
        if (selected->resources.size() != selected->writes.size()
            || entries.size() != selected->writes.size())
            return "RHI-ORIGINAL-TABLE-PUBLICATION-PHYSICAL-COVERAGE";
        std::uint64_t retained = 0u;
        for (std::size_t i = 0u; i < selected->writes.size(); ++i) {
            const auto& item = selected->writes[i];
            const auto* view = item.view->originalObject();
            const auto* parent = item.view->originalParentLease()->originalObject();
            if (const auto* id = device->validateOriginalResourceRegistration(*view))
                return id;
            if (const auto* id = device->validateOriginalResourceRegistration(*parent))
                return id;
            for (const auto& quota : quotas) {
                if (core::SharedOwnerProjection::overlapsOwner(
                        quota, item.view->originalMetadataQuota()))
                    return "RHI-ORIGINAL-TABLE-PUBLICATION-QUOTA-DOMAIN";
                if (const auto* id =
                        view->originalResourceRegistration().independentCpuQuotaDiagnostic(quota))
                    return id;
                if (const auto* id =
                        parent->originalResourceRegistration().independentCpuQuotaDiagnostic(quota))
                    return id;
            }
            if (const auto* id = validateOriginalWebGpuResource(*view, selected->resources[i]))
                return id;
            const auto& resource = selected->resources[i];
            if (resource.nativeDevice() != nativeDevice || !resource.view()
                || !resource.view()->view)
                return "RHI-ORIGINAL-TABLE-PUBLICATION-NATIVE-DEVICE";
            bool seen = false;
            for (std::size_t j = 0u; j < i; ++j)
                seen |= resource.sharesAllocationWith(selected->resources[j]);
            const auto bytes = resource.backing().allocationBytes;
            if (!bytes
                || (!seen
                    && (bytes > selected->request.maxRetainedAllocationBytes
                        || retained > selected->request.maxRetainedAllocationBytes - bytes)))
                return "RHI-ORIGINAL-TABLE-PUBLICATION-ALLOCATION-BUDGET";
            if (!seen)
                retained += bytes;
            bool found = false;
            for (std::size_t f = 0u; f < issued->facts().size(); ++f) {
                const auto fact = issued->expandedBindingFact(f);
                if (fact.original.logical.set != 0u || fact.original.logical.binding != 0u
                    || fact.original.arrayElement != i)
                    continue;
                if (found || fact.original.physicalCardinality != 1u
                    || fact.original.group != physicalGroup
                    || fact.original.binding != entries[i].binding
                    || entries[i].textureView != resource.view()->view || entries[i].buffer
                    || entries[i].sampler
                    || fact.native.physicalType != rhi::DescriptorType::SampledTexture)
                    return "RHI-ORIGINAL-TABLE-PUBLICATION-PHYSICAL-MAPPING";
                found = true;
            }
            if (!found)
                return "RHI-ORIGINAL-TABLE-PUBLICATION-PHYSICAL-MAPPING";
        }
        return retained == selected->allocationBytes
            ? nullptr
            : "RHI-ORIGINAL-TABLE-PUBLICATION-ALLOCATION-CHANGED";
#else
        return "RHI-ORIGINAL-TABLE-PUBLICATION-NATIVE-METHODS";
#endif
    }

    template <class Pipeline>
    const char* validateBindings(const std::shared_ptr<Pipeline>& pipeline) const noexcept
    {
        using Lowering = WebGpuOriginalRuntimeLengthIntervalLowering;
        if (!pipeline)
            return "RHI-ORIGINAL-FINITE-WRITE-PIPELINE";
        const auto* object = descriptor.get();
        if (!object)
            return "RHI-ORIGINAL-FINITE-WRITE-DESCRIPTOR-EXPIRED";
        // Validate the accepted owner before dereferencing its original layout.
        for (const auto& item : declarations) {
            if (whole()) {
                if (const auto* id = WebGpuOriginalWholeBufferBindingLowering::validate(
                        *device, pipeline, item.buffer, logicalSet, item.write, item.dynamicOffset))
                    return id;
            } else if (targetMemory()) {
                rhi::OriginalTargetMemoryIntervalContract::Observation facts;
                if (const auto* id = device->diagnoseOriginalTargetMemoryBufferInterval({1u, true},
                        pipeline,
                        item.buffer,
                        logicalSet,
                        item.write.binding,
                        item.write.bufferBindingRange,
                        item.dynamicOffset,
                        facts))
                    return id;
            } else {
                rhi::OriginalDescriptorBufferBindingContract::IntervalDiagnosticObservation facts;
                if (const auto* id =
                        device->diagnoseOriginalRuntimeProfileBufferInterval({1u, true},
                            pipeline,
                            item.buffer,
                            logicalSet,
                            item.write.binding,
                            item.write.bufferBindingRange,
                            item.dynamicOffset,
                            facts))
                    return id;
            }
        }
        const auto* slot =
            rhi::findDescriptorSetSlot(pipeline->originalLayoutOwner()->desc(), logicalSet);
        const auto* layout = slot ? rhi::findDescriptorSetLayout(
                                        pipeline->originalLayoutOwner()->desc(), slot->layoutName)
                                  : nullptr;
        if (!layout || !rhi::descriptorLayoutDefinitionMatches(*layout, object->layout())
            || layout->bindings.size() != declarations.size())
            return "RHI-ORIGINAL-FINITE-WRITE-LAYOUT";
        if (!pipeline->originalLayoutOwner()->desc().descriptorTables.empty())
            return "RHI-ORIGINAL-FINITE-WRITE-TABLE-PROFILE";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto native = webGpuNativePayloadFor(*pipeline);
        if (!native || (native->usesPushConstantGroup && native->pushConstantGroup == physicalGroup)
            || !native->descriptorTables.empty())
            return "RHI-ORIGINAL-FINITE-WRITE-PHYSICAL-PROFILE";
        std::size_t physicalCount = 0u;
        for (const auto& mapping : native->descriptorBindings) {
            if (mapping.webGpuGroup != physicalGroup)
                continue;
            if (mapping.logicalSet != logicalSet)
                return "RHI-ORIGINAL-FINITE-WRITE-MIXED-GROUP";
            bool found = false;
            for (std::size_t i = 0u; i < declarations.size(); ++i)
                if (declarations[i].write.binding == mapping.logicalBinding
                    && entries[i].binding == mapping.webGpuBinding)
                    found = true;
            if (!found)
                return "RHI-ORIGINAL-FINITE-WRITE-PHYSICAL-BINDING";
            ++physicalCount;
        }
        if (physicalCount != declarations.size())
            return "RHI-ORIGINAL-FINITE-WRITE-PHYSICAL-BINDING";
#endif
        for (const auto& quota : quotas)
            if (const auto* id = pipeline->independentAcceptedCpuQuotaDiagnostic(quota))
                return id;
        std::uint32_t previous = 0u;
        for (std::size_t i = 0u; i < declarations.size(); ++i) {
            const auto& item = declarations[i];
            const auto& write = item.write;
            const auto* binding = rhi::findDescriptorSetBinding(*layout, write.binding);
            if (!binding || binding->arrayCount != 1u || binding->tableBacked || write.arrayElement
                || write.physicalLowering.enabled
                || write.resourceKind != rhi::DescriptorResourceKind::Buffer
                || (write.type != rhi::DescriptorType::StorageBuffer
                    && write.type != rhi::DescriptorType::ReadOnlyStorageBuffer)
                || write.type != binding->type
                || write.resource != item.buffer->originalObject()->id()
                || (i && write.binding <= previous))
                return "RHI-ORIGINAL-FINITE-WRITE-BINDING";
            previous = write.binding;
            if (write.type == rhi::DescriptorType::StorageBuffer
                && item.buffer->originalObject()->gpuWritesSealed())
                return "RHI-BUFFER-GPU-WRITES-SEALED";
            for (const auto& quota : quotas) {
                if (core::SharedOwnerProjection::overlapsOwner(
                        quota, item.buffer->originalMetadataQuota()))
                    return "RHI-ORIGINAL-FINITE-WRITE-QUOTA-DOMAIN";
                if (const auto* id = item.buffer->originalObject()
                        ->originalResourceRegistration()
                        .independentCpuQuotaDiagnostic(quota))
                    return id;
            }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
            std::uint32_t group = 0u, displacement = 0u;
            const auto* id = whole() ? WebGpuOriginalWholeBufferBindingLowering::lower(*device,
                                           pipeline,
                                           item.buffer,
                                           logicalSet,
                                           write,
                                           item.dynamicOffset,
                                           entry,
                                           group,
                                           displacement)
                : targetMemory()     ? Lowering::lowerTargetMemoryBindingDiagnostic({1u, true},
                                       *device,
                                       pipeline,
                                       item.buffer,
                                       logicalSet,
                                       write.binding,
                                       write.bufferBindingRange,
                                       item.dynamicOffset,
                                       entry,
                                       group,
                                       displacement)
                                     : Lowering::lowerBindingDiagnostic({1u, true},
                                       *device,
                                       pipeline,
                                       item.buffer,
                                       logicalSet,
                                       write.binding,
                                       write.bufferBindingRange,
                                       item.dynamicOffset,
                                       entry,
                                       group,
                                       displacement);
            if (id)
                return id;
            if (i && group != physicalGroup)
                return "RHI-ORIGINAL-FINITE-WRITE-MIXED-GROUP";
            if (!entries.empty()
                && (entry.binding != entries[i].binding || entry.buffer != entries[i].buffer
                    || entry.offset != entries[i].offset || entry.size != entries[i].size
                    || group != physicalGroup))
                return "RHI-ORIGINAL-FINITE-WRITE-NATIVE-CHANGED";
            for (std::size_t j = 0u; j < i; ++j)
                if (entries.size() > j && entries[j].binding == entry.binding)
                    return "RHI-ORIGINAL-FINITE-WRITE-PHYSICAL-BINDING";
#endif
        }
        return nullptr;
    }

    const char* validate() const noexcept
    {
        // Destruction/loss is externally serialized. Never dereference the
        // borrowed logical device after its own weak lifetime ticket fails.
        if (const auto* id = deviceLifetime.diagnostic())
            return id;
        if (!device)
            return "RHI-ORIGINAL-FINITE-WRITE-DEVICE";
        if (const auto* id = device->validateOriginalDeviceLifetime(deviceLifetime))
            return id;
        if (depth())
            return compute ? validateDepth(compute) : validateDepth(graphics);
        const auto* object = descriptor.get();
        if (!object)
            return "RHI-ORIGINAL-FINITE-WRITE-DESCRIPTOR-EXPIRED";
        if (!object->isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (object->contentRevision() != revision + (committed ? 1u : 0u))
            return "RHI-ORIGINAL-FINITE-WRITE-REVISION";
        if (const auto* id = device->validateOriginalResourceRegistration(*object))
            return id;
        if (!arena)
            return "RHI-ORIGINAL-FINITE-WRITE-ARENA";
        if (const auto* id = device->validateOriginalResourceRegistration(*arena))
            return id;
        if (!object->originalResourceRegistration().matchesParent(
                arena->originalResourceRegistration()))
            return "RHI-ORIGINAL-FINITE-WRITE-ARENA-PARENT";
        if (!object->allocationEpochTicket().sameCurrentEpoch(arena->allocationEpochTicket()))
            return "RHI-ORIGINAL-FINITE-WRITE-ARENA-EPOCH";
        if (const auto* id = requestDiagnostic())
            return id;
        if (!general() && (declarations.empty() || declarations.size() > maxBindings()))
            return "RHI-ORIGINAL-FINITE-WRITE-BINDING-COUNT";
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            if (!quotas[i] || quotas[i]->limitBytes() != maxCpuBytes()[i])
                return "RHI-ORIGINAL-FINITE-WRITE-QUOTA";
            if (const auto* id =
                    object->originalResourceRegistration().independentCpuQuotaDiagnostic(quotas[i]))
                return id;
            for (std::size_t j = 0u; j < i; ++j)
                if (core::SharedOwnerProjection::overlapsOwner(quotas[i], quotas[j]))
                    return "RHI-ORIGINAL-FINITE-WRITE-QUOTA-DOMAIN";
        }
        if (general())
            return compute ? validateGeneral(compute) : validateGeneral(graphics);
        return compute ? validateBindings(compute) : validateBindings(graphics);
    }
};

bool WebGpuNativeDescriptorGroupPayload::containsOriginalFiniteWrite() const noexcept
{
    return originalFiniteWrite_ && !originalFiniteWrite_->whole() && !originalFiniteWrite_->depth()
        && !originalFiniteWrite_->general();
}

bool WebGpuNativeDescriptorGroupPayload::containsOriginalGeneralWrite() const noexcept
{
    return originalFiniteWrite_ && originalFiniteWrite_->general();
}

bool WebGpuNativeDescriptorGroupPayload::containsOriginalDepthTableWrite() const noexcept
{
    return originalFiniteWrite_ && originalFiniteWrite_->depth();
}

bool WebGpuNativeDescriptorGroupPayload::containsOriginalFiniteTargetMemoryWrite() const noexcept
{
    return originalFiniteWrite_ && originalFiniteWrite_->targetMemory();
}

bool WebGpuNativeDescriptorGroupPayload::containsOriginalWholeWrite() const noexcept
{
    return originalFiniteWrite_ && originalFiniteWrite_->whole();
}

void WebGpuNativeDescriptorGroupPayload::observeCreationScope(
    std::uint32_t participant, bool successful) noexcept
{
    if (!originalFiniteWrite_)
        return;
    auto& state = *originalFiniteWrite_;
    // Callback only records actual scope evidence. Finish performs all joins.
    state.lifecycle.observe(participant,
        successful,
        state.depth() ? "RHI-ORIGINAL-TABLE-PUBLICATION-NATIVE-SCOPE"
                      : "RHI-ORIGINAL-FINITE-WRITE-NATIVE-SCOPE",
        []() noexcept -> const char* {
            return nullptr;
        });
}

const char* WebGpuNativeDescriptorGroupPayload::finiteNativeIdentityDiagnostic() const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!originalFiniteWrite_)
        return "RHI-ORIGINAL-FINITE-WRITE-UNPREPARED";
    const auto& state = *originalFiniteWrite_;
    if (const auto* selected = state.depth()) {
        if (!state.nativeDevice || !state.originalGroup || !state.originalLayout
            || group != state.originalGroup || layout != state.originalLayout || !tableBacked
            || containsLoweredInputAttachment || shaderPackageHash || dynamicOffsetCount
            || !dynamicOffsetBindings.empty() || bindings.size() != selected->writes.size())
            return "RHI-ORIGINAL-TABLE-PUBLICATION-NATIVE-IDENTITY";
        for (std::size_t i = 0u; i < bindings.size(); ++i) {
            const auto& binding = bindings[i];
            const auto& write = selected->writes[i].write;
            const auto& issued = state.compute
                ? webGpuNativePayloadFor(*state.compute)->issuedContract()
                : webGpuNativePayloadFor(*state.graphics)->issuedContract();
            bool shapeMatches = false;
            for (std::size_t f = 0u; f < issued->facts().size(); ++f) {
                const auto fact = issued->expandedBindingFact(f);
                if (fact.original.logical.set == 0u && fact.original.logical.binding == 0u
                    && fact.original.arrayElement == i)
                    shapeMatches = binding.binding.stages == fact.native.shape.stages
                        && rhi::descriptorBindingLayoutMatches(binding.binding, fact.native.shape);
            }
            if (binding.buffer || binding.sampler || !binding.textureView
                || !core::SharedOwnerProjection::sameOwner(
                    binding.textureView, selected->resources[i].view())
                || binding.textureView->view != state.entries[i].textureView || binding.arrayElement
                || !shapeMatches || binding.binding.arrayCount != 1u
                || !binding.binding.depthTexture
                || binding.binding.type != rhi::DescriptorType::SampledTexture
                || binding.binding.stages != selected->table.get()->desc().stages
                || binding.write.binding != 0u || binding.write.arrayElement != i
                || binding.write.type != write.type || binding.write.resource != write.resource
                || binding.write.resourceKind != write.resourceKind)
                return "RHI-ORIGINAL-TABLE-PUBLICATION-NATIVE-IDENTITY";
        }
        return nullptr;
    }
    if (const auto* selected = state.general()) {
        if (!state.nativeDevice || !state.originalGroup || !state.originalLayout
            || group != state.originalGroup || layout != state.originalLayout || tableBacked
            || containsLoweredInputAttachment || shaderPackageHash
            || dynamicOffsetCount != state.dynamicOffsets.size()
            || dynamicOffsetBindings.size() != state.dynamicOffsets.size()
            || bindings.size() != selected->bindings.size()
            || state.entries.size() != bindings.size())
            return "RHI-ORIGINAL-GENERAL-WRITE-NATIVE-IDENTITY";
        const auto* descriptor = state.descriptor.get();
        if (!descriptor)
            return "RHI-ORIGINAL-GENERAL-WRITE-DESCRIPTOR-EXPIRED";
        for (std::size_t i = 0u; i < bindings.size(); ++i) {
            const auto& current = bindings[i];
            const auto& original = selected->bindings[i];
            const auto* declared =
                rhi::findDescriptorSetBinding(descriptor->layout(), original.element.write.binding);
            if (!declared || current.binding.stages != declared->stages
                || !rhi::descriptorBindingLayoutMatches(current.binding, *declared)
                || !rhi::descriptorBindingWriteMatches(current.write, original.element.write)
                || current.arrayElement)
                return "RHI-ORIGINAL-GENERAL-WRITE-NATIVE-IDENTITY";
            const bool matches = std::visit(
                [&](const auto& source) {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(source)>::element_type>;
                    const auto payload = webGpuNativePayloadFor(*source->originalObject());
                    if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>)
                        return !current.textureView && !current.sampler && current.buffer
                            && core::SharedOwnerProjection::sameOwner(current.buffer, payload)
                            && current.buffer->buffer == state.entries[i].buffer;
                    else if constexpr (std::is_same_v<T, rhi::OriginalCommandTextureViewSource>)
                        return !current.buffer && !current.sampler && current.textureView
                            && core::SharedOwnerProjection::sameOwner(current.textureView, payload)
                            && current.textureView->view == state.entries[i].textureView;
                    else
                        return !current.buffer && !current.textureView && current.sampler
                            && core::SharedOwnerProjection::sameOwner(current.sampler, payload)
                            && current.sampler->sampler == state.entries[i].sampler;
                },
                original.owner);
            if (!matches)
                return "RHI-ORIGINAL-GENERAL-WRITE-NATIVE-IDENTITY";
        }
        std::size_t dynamic = 0u;
        for (const auto& binding : bindings)
            dynamic += binding.binding.dynamicOffset ? 1u : 0u;
        if (dynamic != state.dynamicOffsets.size())
            return "RHI-ORIGINAL-GENERAL-WRITE-NATIVE-IDENTITY";
        for (std::size_t d = 0u; d < dynamic; ++d) {
            if (d && dynamicOffsetBindings[d] <= dynamicOffsetBindings[d - 1u])
                return "RHI-ORIGINAL-GENERAL-WRITE-NATIVE-IDENTITY";
            bool found = false;
            for (std::size_t i = 0u; i < bindings.size(); ++i)
                if (bindings[i].binding.dynamicOffset
                    && dynamicOffsetBindings[d] == state.entries[i].binding) {
                    if (found
                        || state.dynamicOffsets[d] != selected->bindings[i].element.dynamicOffset)
                        return "RHI-ORIGINAL-GENERAL-WRITE-NATIVE-IDENTITY";
                    found = true;
                }
            if (!found)
                return "RHI-ORIGINAL-GENERAL-WRITE-NATIVE-IDENTITY";
        }
        return nullptr;
    }
    // Existing frozen declarations/entries are the sole binding facts. Never
    // reconstruct a second inventory or accept a mutable payload as provenance.
    if (!state.nativeDevice || !state.originalGroup || !state.originalLayout
        || group != state.originalGroup || layout != state.originalLayout || tableBacked
        || containsLoweredInputAttachment || shaderPackageHash
        || bindings.size() != state.declarations.size() || state.entries.size() != bindings.size()
        || dynamicOffsetCount != state.dynamicOffsets.size()
        || dynamicOffsetBindings.size() != state.dynamicOffsets.size())
        return "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY";
    const auto* descriptor = state.descriptor.get();
    if (!descriptor)
        return "RHI-ORIGINAL-FINITE-WRITE-DESCRIPTOR-EXPIRED";
    std::size_t dynamic = 0u;
    for (std::size_t i = 0u; i < bindings.size(); ++i) {
        const auto& current = bindings[i];
        const auto& original = state.declarations[i];
        const auto* declared =
            rhi::findDescriptorSetBinding(descriptor->layout(), original.write.binding);
        if (!declared || current.binding.stages != declared->stages
            || !rhi::descriptorBindingLayoutMatches(current.binding, *declared)
            || !rhi::descriptorBindingWriteMatches(current.write, original.write)
            || current.arrayElement || current.textureView || current.sampler
            || !core::SharedOwnerProjection::sameOwner(
                current.buffer, webGpuNativePayloadFor(*original.buffer->originalObject()))
            || !current.buffer || current.buffer->ownerDevice != state.nativeDevice
            || current.buffer->buffer != state.entries[i].buffer)
            return "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY";
        if (declared->dynamicOffset)
            ++dynamic;
    }
    if (dynamic != dynamicOffsetBindings.size())
        return "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY";
    // WebGPU dynamic offsets are ordered by physical binding number, which
    // need not equal the logical declaration order. Reuse the frozen entries.
    for (std::size_t d = 0u; d < dynamicOffsetBindings.size(); ++d) {
        const auto physical = dynamicOffsetBindings[d];
        if (d && physical <= dynamicOffsetBindings[d - 1u])
            return "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY";
        bool found = false;
        for (std::size_t i = 0u; i < state.entries.size(); ++i)
            if (state.entries[i].binding == physical) {
                const auto& original = state.declarations[i];
                const auto* declared =
                    rhi::findDescriptorSetBinding(descriptor->layout(), original.write.binding);
                if (!declared || !declared->dynamicOffset
                    || state.dynamicOffsets[d] != original.dynamicOffset)
                    return "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY";
                found = true;
            }
        if (!found)
            return "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY";
    }
    return nullptr;
#else
    return "RHI-ORIGINAL-FINITE-WRITE-NATIVE-METHODS";
#endif
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalNativeIdentity(
    const rhi::OriginalDescriptorNativeIdentityRequest& request) const noexcept
{
    using Contract = rhi::OriginalDescriptorNativeIdentityContract;
    if (const auto* id = Contract::validateIntent(request))
        return id;
    if (const auto* id = Contract::lowering(rhi::Backend::WebGPU, request.profile).diagnostic)
        return id;
    if (!originalFiniteWrite_)
        return "RHI-ORIGINAL-FINITE-WRITE-UNPREPARED";
    const auto& state = *originalFiniteWrite_;
    if (state.depth())
        return "RHI-ORIGINAL-TABLE-PUBLICATION-ORDINARY-CLOSED";
    if (state.whole() != (request.profile == Contract::Request::Profile::WholeWrite1)
        || bool(state.general())
            != (request.profile == Contract::Request::Profile::GeneralWrite1
                || request.profile == Contract::Request::Profile::GeneralWrite2)
        || state.targetMemory()
            != (request.profile == Contract::Request::Profile::FiniteTargetMemory1))
        return "RHI-ORIGINAL-SCOPED-WRITE-PROFILE";
    if (const auto* selected = state.general())
        if ((selected->request.contractVersion == 2u)
            != (request.profile == Contract::Request::Profile::GeneralWrite2))
            return "RHI-ORIGINAL-SCOPED-WRITE-PROFILE";
    if ((state.whole() || state.general()) && !state.committed)
        return "RHI-ORIGINAL-WHOLE-WRITE-UNCOMMITTED";
    if (const auto* id = state.validate())
        return id;
    // validate() proves the weak descriptor and device tickets before any
    // dereference. Its existing content gate also rejects pending replacements;
    // the private whole checker never calls this public identity observer.
    if (state.whole() || state.general())
        if (const auto* id = state.descriptor.get()->originalScopedContentDiagnostic())
            return id;
    if (state.lifecycle.phase() != decltype(state.lifecycle)::Phase::Ready
        || !state.lifecycle.evidence().proven())
        return "RHI-ORIGINAL-FINITE-WRITE-SCOPE-EVIDENCE";
    return finiteNativeIdentityDiagnostic();
}

// NativeClosure2 creation metadata projection. Only the existing closure may
// inspect these owners; no recording/submission proof is issued here.
void* WebGpuNativeDescriptorGroupPayload::originalScopedNativeDevice() const noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    return originalFiniteWrite_ ? originalFiniteWrite_->nativeDevice : nullptr;
#else
    return nullptr;
#endif
}

std::shared_ptr<const void> WebGpuNativeDescriptorGroupPayload::originalScopedQuotaOwner(
    std::size_t i) const noexcept
{
    return originalFiniteWrite_ && i < originalFiniteWrite_->quotas.size()
        ? originalFiniteWrite_->quotas[i]
        : nullptr;
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalClosurePipeline(
    const std::shared_ptr<const rhi::ComputePipeline>& pipeline, std::uint32_t set) const noexcept
{
    if (!originalFiniteWrite_
        || !core::SharedOwnerProjection::sameOwner(pipeline, originalFiniteWrite_->compute)
        || set != originalFiniteWrite_->logicalSet)
        return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-PIPELINE";
    return nullptr;
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalClosurePipeline(
    const std::shared_ptr<const rhi::RenderPipeline>& pipeline, std::uint32_t set) const noexcept
{
    if (!originalFiniteWrite_
        || !core::SharedOwnerProjection::sameOwner(pipeline, originalFiniteWrite_->graphics)
        || set != originalFiniteWrite_->logicalSet)
        return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-PIPELINE";
    return nullptr;
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalClosureBinding(std::size_t index,
    std::size_t count,
    const rhi::DescriptorBindingWrite& write,
    const std::shared_ptr<const rhi::OriginalCommandBufferLease>& buffer,
    std::uint32_t dynamicOffset) const noexcept
{
    if (!originalFiniteWrite_ || count != originalFiniteWrite_->declarations.size()
        || index >= count)
        return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-BINDING";
    const auto& original = originalFiniteWrite_->declarations[index];
    if (!core::SharedOwnerProjection::sameOwner(buffer, original.buffer)
        || dynamicOffset != original.dynamicOffset
        || !rhi::descriptorBindingWriteMatches(write, original.write))
        return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-BINDING";
    return nullptr;
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalGeneralClosureBinding(
    std::size_t index,
    std::size_t count,
    const rhi::OriginalCommandDescriptorSetElement& element,
    const rhi::OriginalCommandResourceSources& sources) const noexcept
{
    if (const auto* id = generalContentDiagnostic())
        return id;
    const auto& bindings = originalFiniteWrite_->general()->bindings;
    if (count != bindings.size() || index >= count)
        return "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-BINDING";
    const auto& original = bindings[index];
    const auto& e = original.element;
    if (!rhi::descriptorBindingWriteMatches(element.write, e.write)
        || element.sourceIndex != e.sourceIndex
        || element.registrationGeneration != e.registrationGeneration
        || element.bufferAccessOffset != e.bufferAccessOffset
        || element.bufferAccessBytes != e.bufferAccessBytes || element.stages != e.stages
        || element.access != e.access || element.dynamicOffset != e.dynamicOffset)
        return "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-BINDING";
    const auto owner =
        rhi::OriginalCommandDescriptorDependencies::capture(sources, element.sourceIndex);
    if (!owner)
        return "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-OWNER";
    return std::visit(
        [&](const auto& canonical) -> const char* {
            const auto* retained = std::get_if<std::decay_t<decltype(canonical)>>(&original.owner);
            return retained && core::SharedOwnerProjection::sameOwner(canonical, *retained)
                ? nullptr
                : "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-OWNER";
        },
        *owner);
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalFiniteGroup(
    std::uint32_t& group, const std::uint32_t*& offsets, std::uint32_t& count) const noexcept
{
    group = 0u;
    offsets = nullptr;
    count = 0u;
    if (!containsOriginalFiniteWrite() || !originalFiniteWrite_->committed)
        return "RHI-ORIGINAL-FINITE-WRITE-UNCOMMITTED";
    const auto& state = *originalFiniteWrite_;
    if (const auto* id = state.validate())
        return id;
    if (!state.lifecycle.evidence().proven())
        return "RHI-ORIGINAL-FINITE-WRITE-SCOPE-EVIDENCE";
    if (const auto* id = finiteNativeIdentityDiagnostic())
        return id;
    group = state.physicalGroup;
    offsets = state.dynamicOffsets.data();
    count = static_cast<std::uint32_t>(state.dynamicOffsets.size());
    return nullptr;
}

const char* WebGpuNativeDescriptorGroupPayload::wholeContentDiagnostic() const noexcept
{
    if (!containsOriginalWholeWrite() || !originalFiniteWrite_->committed)
        return "RHI-ORIGINAL-WHOLE-WRITE-UNCOMMITTED";
    const auto& state = *originalFiniteWrite_;
    if (const auto* id = state.validate())
        return id;
    if (state.lifecycle.phase() != decltype(state.lifecycle)::Phase::Ready
        || !state.lifecycle.evidence().proven())
        return "RHI-ORIGINAL-WHOLE-WRITE-SCOPE-EVIDENCE";
    return finiteNativeIdentityDiagnostic();
}

const char* WebGpuNativeDescriptorGroupPayload::generalContentDiagnostic() const noexcept
{
    if (!containsOriginalGeneralWrite() || !originalFiniteWrite_->committed)
        return "RHI-ORIGINAL-GENERAL-WRITE-UNCOMMITTED";
    const auto& state = *originalFiniteWrite_;
    if (const auto* id = state.validate())
        return id;
    if (state.lifecycle.phase() != decltype(state.lifecycle)::Phase::Ready
        || !state.lifecycle.evidence().proven())
        return "RHI-ORIGINAL-GENERAL-WRITE-SCOPE-EVIDENCE";
    return finiteNativeIdentityDiagnostic();
}

std::uint32_t WebGpuNativeDescriptorGroupPayload::generalContentContractVersion() const noexcept
{
    const auto* selected = originalFiniteWrite_ ? originalFiniteWrite_->general() : nullptr;
    return selected ? selected->request.contractVersion : 0u;
}

template <class Pipeline>
const char* WebGpuNativeDescriptorGroupPayload::generalSourceDiagnostic(
    const std::shared_ptr<const Pipeline>& pipeline,
    const std::shared_ptr<const rhi::OriginalCommandResourceSources>& resources,
    std::uint32_t set,
    const rhi::OriginalCommandDescriptorSetFrozenElement* elements,
    std::size_t count,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    if (generalContentContractVersion() != 2u)
        return "RHI-ORIGINAL-GENERAL-SOURCE-WRITE-PROFILE";
    if (!pipeline)
        return "RHI-ORIGINAL-GENERAL-SOURCE-PIPELINE";
    using Profile = rhi::OriginalDescriptorNativeIdentityRequest::Profile;
    if (const auto* id = diagnoseOriginalNativeIdentity({4u, true, Profile::GeneralWrite2}))
        return id;
    if (const auto* id = diagnoseOriginalClosurePipeline(pipeline, set))
        return id;
    if (const auto* id =
            rhi::OriginalCommandDescriptorDependencies::validateInventoryOwner(resources, quota))
        return id;
    const auto& state = *originalFiniteWrite_;
    const auto* object = state.descriptor.get(); // NativeIdentity proved this ticket.
    if (!elements || count != state.general()->bindings.size())
        return "RHI-ORIGINAL-GENERAL-SOURCE-BINDING-COUNT";
    if (const auto* id = pipeline->independentAcceptedCpuQuotaDiagnostic(quota))
        return id;
    if (const auto* id =
            object->originalResourceRegistration().independentCpuQuotaDiagnostic(quota))
        return id;
    if (const auto* id =
            state.arena->originalResourceRegistration().independentCpuQuotaDiagnostic(quota))
        return id;
    for (const auto& owner : state.quotas)
        if (core::SharedOwnerProjection::overlapsOwner(quota, owner))
            return "RHI-ORIGINAL-GENERAL-SOURCE-QUOTA-DOMAIN";
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& frozen = elements[i];
        const auto& e = frozen.declaration;
        if (!frozen.observation.matchesObject(*object)
            || !rhi::descriptorBindingWriteMatches(e.write, frozen.observation.write()))
            return "RHI-ORIGINAL-GENERAL-SOURCE-RECEIPT";
        if (!rhi::OriginalCommandDescriptorDependencies::matches(
                *resources, e.sourceIndex, frozen.resourceOwner))
            return "RHI-ORIGINAL-GENERAL-SOURCE-RESOURCE-POSITION";
        if (const auto* id = rhi::OriginalCommandDescriptorDependencies::validateOwner(
                frozen.resourceOwner, e.registrationGeneration, quota))
            return id;
        // Reuse the complete original declaration/control-block association.
        if (const auto* id = diagnoseOriginalGeneralClosureBinding(i, count, e, *resources))
            return id;
    }
    return nullptr;
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalGeneralSource(
    const std::shared_ptr<const rhi::ComputePipeline>& pipeline,
    const std::shared_ptr<const rhi::OriginalCommandResourceSources>& resources,
    std::uint32_t set,
    const rhi::OriginalCommandDescriptorSetFrozenElement* elements,
    std::size_t count,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    return generalSourceDiagnostic(pipeline, resources, set, elements, count, quota);
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalGeneralSource(
    const std::shared_ptr<const rhi::RenderPipeline>& pipeline,
    const std::shared_ptr<const rhi::OriginalCommandResourceSources>& resources,
    std::uint32_t set,
    const rhi::OriginalCommandDescriptorSetFrozenElement* elements,
    std::size_t count,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    return generalSourceDiagnostic(pipeline, resources, set, elements, count, quota);
}

template <class Pipeline>
const char* WebGpuNativeDescriptorGroupPayload::finiteSourceDiagnostic(
    const std::shared_ptr<const Pipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    bool targetMemory) const noexcept
{
    if (!containsOriginalFiniteWrite() || !originalFiniteWrite_->committed)
        return "RHI-ORIGINAL-FINITE-WRITE-UNCOMMITTED";
    const auto& state = *originalFiniteWrite_;
    if (state.targetMemory() != targetMemory)
        return "RHI-ORIGINAL-SCOPED-WRITE-PROFILE";
    if (const auto* id = state.validate())
        return id;
    if (!state.lifecycle.evidence().proven())
        return "RHI-ORIGINAL-FINITE-WRITE-SCOPE-EVIDENCE";
    if (const auto* id = finiteNativeIdentityDiagnostic())
        return id;
    const auto& original = [&]() -> const auto& {
        if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
            return state.compute;
        else
            return state.graphics;
    }();
    if (!pipeline || !core::SharedOwnerProjection::sameOwner(pipeline, original))
        return "RHI-ORIGINAL-FINITE-SOURCE-PIPELINE";
    if (set != state.logicalSet)
        return "RHI-ORIGINAL-FINITE-SOURCE-SET";
    if (!writes || count != state.declarations.size())
        return "RHI-ORIGINAL-FINITE-SOURCE-BINDING-COUNT";
    if (!quota)
        return "RHI-ORIGINAL-FINITE-SOURCE-QUOTA";
    if (const auto* id = pipeline->independentAcceptedCpuQuotaDiagnostic(quota))
        return id;
    const auto* object = state.descriptor.get(); // validate() proved its ticket
    if (const auto* id =
            object->originalResourceRegistration().independentCpuQuotaDiagnostic(quota))
        return id;
    for (const auto& owner : state.quotas)
        if (core::SharedOwnerProjection::overlapsOwner(quota, owner))
            return "RHI-ORIGINAL-FINITE-SOURCE-QUOTA-DOMAIN";
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& declaration = state.declarations[i];
        const auto& input = writes[i];
        if (!core::SharedOwnerProjection::sameOwner(input.buffer, declaration.buffer))
            return "RHI-ORIGINAL-FINITE-SOURCE-BUFFER";
        if (!core::SharedOwnerProjection::sameOwner(
                quota, declaration.buffer->originalMetadataQuota()))
            return "RHI-ORIGINAL-DESCRIPTOR-SOURCE-RESOURCE-QUOTA";
        if (!rhi::descriptorBindingWriteMatches(input.write, declaration.write))
            return "RHI-ORIGINAL-FINITE-SOURCE-WRITE";
        if (input.dynamicOffset != declaration.dynamicOffset)
            return "RHI-ORIGINAL-FINITE-SOURCE-DYNAMIC";
        const char* diagnostic = nullptr;
        const auto receipt = object->observeSuccessfulBindingWrite(
            input.write.binding, input.write.arrayElement, diagnostic);
        if (diagnostic)
            return diagnostic;
        if (!receipt || !rhi::descriptorBindingWriteMatches(input.write, receipt->write()))
            return "RHI-ORIGINAL-FINITE-SOURCE-RECEIPT";
    }
    return nullptr;
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalFiniteSource(
    const std::shared_ptr<const rhi::ComputePipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    return finiteSourceDiagnostic(pipeline, set, writes, count, quota);
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalFiniteSource(
    const std::shared_ptr<const rhi::RenderPipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    return finiteSourceDiagnostic(pipeline, set, writes, count, quota);
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalFiniteTargetMemorySource(
    const std::shared_ptr<const rhi::ComputePipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    return finiteSourceDiagnostic(pipeline, set, writes, count, quota, true);
}

const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalFiniteTargetMemorySource(
    const std::shared_ptr<const rhi::RenderPipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
{
    return finiteSourceDiagnostic(pipeline, set, writes, count, quota, true);
}

template <class Pipeline>
std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalDepthTable(const rhi::GpuDevice& device,
    const rhi::DescriptorTable& object,
    const std::shared_ptr<const rhi::DescriptorArena>& arena,
    const std::shared_ptr<Pipeline>& pipeline,
    const rhi::OriginalDepthTablePublicationWrite* writes,
    std::size_t count,
    const rhi::OriginalDepthTablePublicationRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const char*& diagnostic)
{
    using Contract = rhi::OriginalDepthTablePublicationContract;
    const auto fail = [&](const char* id) -> std::shared_ptr<WebGpuNativeDescriptorGroupPayload> {
        diagnostic = id;
        return {};
    };
    diagnostic = nullptr;
    if (const auto* id = Contract::validate(request))
        return fail(id);
    if (!pipeline || !pipeline->originalLayoutOwner())
        return fail("RHI-ORIGINAL-TABLE-PUBLICATION-LAYOUT");
    if (const auto* id = Contract::validateWrites(
            pipeline->originalLayoutOwner()->desc(), object.desc(), writes, count))
        return fail(id);
    for (std::size_t i = 0u; i < quotas.size(); ++i) {
        if (!quotas[i] || quotas[i]->limitBytes() != request.maxCpuBytes[i])
            return fail("RHI-ORIGINAL-TABLE-PUBLICATION-QUOTA");
        for (std::size_t j = 0u; j < i; ++j)
            if (core::SharedOwnerProjection::overlapsOwner(quotas[i], quotas[j]))
                return fail("RHI-ORIGINAL-TABLE-PUBLICATION-QUOTA-DOMAIN");
    }
    const auto decision = Contract::decide(device.backend(), originalDescriptorGroupMethods());
    if (!decision.preparationAvailable)
        return fail(decision.diagnostic);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (const auto* id = device.validateOriginalResourceRegistration(*pipeline))
        return fail(id);
    const auto native = webGpuNativePayloadFor(*pipeline);
    if (!native || !native->issuedContract()
        || native->issuedContract()->semantics().contractVersion != 2u)
        return fail("RHI-ORIGINAL-TABLE-PUBLICATION-PIPELINE-IDENTITY");
    try {
        core::UniquePtr<OriginalFiniteWriteState> state;
        {
            core::CpuMemoryQuotaScope scope(quotas[0]);
            state = core::makeUnique<OriginalFiniteWriteState>();
            auto selected = core::makeUnique<OriginalDepthWriteState>();
            selected->request = request;
            selected->table = {&object, object.lifetimeTicket()};
            selected->writes.reserve(count);
            selected->resources.reserve(count);
            state->device = &device;
            state->deviceLifetime = device.originalDeviceLifetimeTicket();
            state->arena = arena;
            if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
                state->compute = pipeline;
            else
                state->graphics = pipeline;
            state->quotas = quotas;
            state->revision = object.contentRevision();
            state->nativeDevice = static_cast<WGPUDevice>(native->issuedContract()->nativeDevice());
            state->entries.reserve(count);
            state->dynamicOffsets.reserve(count); // complete ordered logical indices
            for (std::size_t i = 0u; i < count; ++i) {
                selected->writes.push_back(writes[i]);
                WebGpuOrdinaryResourceObservation resource;
                if (const auto* id =
                        observeOriginalWebGpuResource(*writes[i].view->originalObject(), resource))
                    return fail(id);
                bool seen = false;
                for (const auto& previous : selected->resources)
                    seen |= resource.sharesAllocationWith(previous);
                const auto bytes = resource.backing().allocationBytes;
                if (!bytes
                    || (!seen
                        && (bytes > request.maxRetainedAllocationBytes
                            || selected->allocationBytes
                                > request.maxRetainedAllocationBytes - bytes)))
                    return fail("RHI-ORIGINAL-TABLE-PUBLICATION-ALLOCATION-BUDGET");
                if (!seen)
                    selected->allocationBytes += bytes;
                selected->resources.push_back(std::move(resource));
                auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
                bool found = false;
                const auto& issued = native->issuedContract();
                for (std::size_t f = 0u; f < issued->facts().size(); ++f) {
                    const auto fact = issued->expandedBindingFact(f);
                    if (fact.original.logical.set != 0u || fact.original.logical.binding != 0u
                        || fact.original.arrayElement != i)
                        continue;
                    if (found || (i && state->physicalGroup != fact.original.group))
                        return fail("RHI-ORIGINAL-TABLE-PUBLICATION-PHYSICAL-MAPPING");
                    state->physicalGroup = fact.original.group;
                    entry.binding = fact.original.binding;
                    entry.textureView = selected->resources.back().view()->view;
                    found = true;
                }
                if (!found)
                    return fail("RHI-ORIGINAL-TABLE-PUBLICATION-PHYSICAL-MAPPING");
                state->entries.push_back(entry);
                state->dynamicOffsets.push_back(static_cast<std::uint32_t>(i));
            }
            state->request = std::move(selected);
        }
        if (const auto* id = state->validate())
            return fail(id);
        std::shared_ptr<WebGpuNativeDescriptorGroupPayload> payload;
        {
            core::CpuMemoryQuotaScope scope(quotas[1]);
            payload = core::makeShared<WebGpuNativeDescriptorGroupPayload>();
            payload->tableBacked = true;
            payload->bindings.reserve(count);
            const auto& issued = native->issuedContract();
            for (std::size_t i = 0u; i < count; ++i) {
                for (std::size_t f = 0u; f < issued->facts().size(); ++f) {
                    const auto fact = issued->expandedBindingFact(f);
                    if (fact.original.logical.set != 0u || fact.original.logical.binding != 0u
                        || fact.original.arrayElement != i)
                        continue;
                    rhi::DescriptorBindingWrite write;
                    write.binding = 0u;
                    write.arrayElement = static_cast<std::uint32_t>(i);
                    write.type = writes[i].write.type;
                    write.resource = writes[i].write.resource;
                    write.resourceKind = writes[i].write.resourceKind;
                    payload->bindings.push_back({fact.native.shape,
                        write,
                        0u,
                        nullptr,
                        state->depth()->resources[i].view(),
                        nullptr});
                }
            }
        }
        state->callbacks.prepare(quotas[2]);
        payload->originalFiniteWrite_ = std::move(state);
        auto& prepared = *payload->originalFiniteWrite_;
        (void)prepared.lifecycle.begin();
        WebGpuNativeErrorScopeBatch::push(prepared.nativeDevice);
        if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
            payload->layout =
                wgpuComputePipelineGetBindGroupLayout(native->pipeline, prepared.physicalGroup);
        else
            payload->layout =
                wgpuRenderPipelineGetBindGroupLayout(native->pipeline, prepared.physicalGroup);
        if (payload->layout) {
            auto desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            desc.layout = payload->layout;
            desc.entryCount = prepared.entries.size();
            desc.entries = prepared.entries.data();
            payload->group = wgpuDeviceCreateBindGroup(prepared.nativeDevice, &desc);
        }
        prepared.originalLayout = payload->layout;
        prepared.originalGroup = payload->group;
        if (!payload->layout || !payload->group)
            prepared.lifecycle.reject("RHI-ORIGINAL-TABLE-PUBLICATION-NATIVE-PAYLOAD");
        prepared.callbacks.pop(prepared.nativeDevice, payload);
        diagnostic = prepared.lifecycle.failure();
        return payload; // Uncertain objects remain retained through actual callback drain.
    } catch (const std::bad_alloc&) {
        return fail("RHI-ORIGINAL-TABLE-PUBLICATION-ALLOCATION");
    } catch (const std::length_error&) {
        return fail("RHI-ORIGINAL-TABLE-PUBLICATION-ALLOCATION");
    }
#else
    return fail("RHI-ORIGINAL-TABLE-PUBLICATION-NATIVE-METHODS");
#endif
}

template std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalDepthTable(const rhi::GpuDevice&,
    const rhi::DescriptorTable&,
    const std::shared_ptr<const rhi::DescriptorArena>&,
    const std::shared_ptr<rhi::ComputePipeline>&,
    const rhi::OriginalDepthTablePublicationWrite*,
    std::size_t,
    const rhi::OriginalDepthTablePublicationRequest&,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
    const char*&);
template std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalDepthTable(const rhi::GpuDevice&,
    const rhi::DescriptorTable&,
    const std::shared_ptr<const rhi::DescriptorArena>&,
    const std::shared_ptr<rhi::RenderPipeline>&,
    const rhi::OriginalDepthTablePublicationWrite*,
    std::size_t,
    const rhi::OriginalDepthTablePublicationRequest&,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&,
    const char*&);

const core::pmr::Vector<rhi::OriginalDepthTablePublicationWrite>&
WebGpuNativeDescriptorGroupPayload::depthCandidateWrites() const noexcept
{
    return originalFiniteWrite_->depth()->writes;
}

core::pmr::Vector<std::uint32_t>&
WebGpuNativeDescriptorGroupPayload::depthCandidateIndices() noexcept
{
    return originalFiniteWrite_->dynamicOffsets;
}

const char* WebGpuNativeDescriptorGroupPayload::depthContentDiagnostic() const noexcept
{
    if (!containsOriginalDepthTableWrite() || !originalFiniteWrite_->committed)
        return "RHI-ORIGINAL-TABLE-PUBLICATION-UNCOMMITTED";
    if (const auto* id = originalFiniteWrite_->validate())
        return id;
    if (!originalFiniteWrite_->lifecycle.evidence().proven())
        return "RHI-ORIGINAL-TABLE-PUBLICATION-SCOPE-EVIDENCE";
    return finiteNativeIdentityDiagnostic();
}

const char* WebGpuNativeDescriptorGroupPayload::depthPipelineDiagnostic(
    const rhi::ComputePipeline& pipeline) const noexcept
{
    const auto decision =
        rhi::semantics::OriginalCommandDescriptorBindingLowering::depthTableLowering(
            rhi::Backend::WebGPU, containsOriginalDepthTableWrite());
    if (!decision.positionalAvailable)
        return decision.diagnostic;
    if (const auto* id = depthContentDiagnostic())
        return id;
    return originalFiniteWrite_->compute.get() == &pipeline ? nullptr
                                                            : "RHI-ORIGINAL-TABLE-BINDING-PIPELINE";
}

const char* WebGpuNativeDescriptorGroupPayload::depthPipelineDiagnostic(
    const rhi::RenderPipeline& pipeline) const noexcept
{
    const auto decision =
        rhi::semantics::OriginalCommandDescriptorBindingLowering::depthTableLowering(
            rhi::Backend::WebGPU, containsOriginalDepthTableWrite());
    if (!decision.positionalAvailable)
        return decision.diagnostic;
    if (const auto* id = depthContentDiagnostic())
        return id;
    return originalFiniteWrite_->graphics.get() == &pipeline
        ? nullptr
        : "RHI-ORIGINAL-TABLE-BINDING-PIPELINE";
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalGeneral(const rhi::GpuDevice& device,
    const rhi::DescriptorSet& object,
    const std::shared_ptr<const rhi::DescriptorArena>& arena,
    const std::shared_ptr<rhi::ComputePipeline>& pipeline,
    const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
    std::uint32_t set,
    const rhi::OriginalCommandDescriptorSetElement* elements,
    std::size_t count,
    const rhi::OriginalGeneralDescriptorWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const char*& diagnostic)
{
    return prepareOriginalScopedImpl(device,
        object,
        arena,
        pipeline,
        set,
        elements,
        count,
        request,
        quotas,
        diagnostic,
        sources);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalGeneral(const rhi::GpuDevice& device,
    const rhi::DescriptorSet& object,
    const std::shared_ptr<const rhi::DescriptorArena>& arena,
    const std::shared_ptr<rhi::RenderPipeline>& pipeline,
    const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
    std::uint32_t set,
    const rhi::OriginalCommandDescriptorSetElement* elements,
    std::size_t count,
    const rhi::OriginalGeneralDescriptorWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const char*& diagnostic)
{
    return prepareOriginalScopedImpl(device,
        object,
        arena,
        pipeline,
        set,
        elements,
        count,
        request,
        quotas,
        diagnostic,
        sources);
}

template <class Pipeline, class Request, class Write>
std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalScopedImpl(const rhi::GpuDevice& device,
    const rhi::DescriptorSet& object,
    const std::shared_ptr<const rhi::DescriptorArena>& arena,
    const std::shared_ptr<Pipeline>& pipeline,
    std::uint32_t set,
    const Write* writes,
    std::size_t count,
    const Request& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const char*& diagnostic,
    const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources)
{
    constexpr bool whole = std::is_same_v<Request, rhi::OriginalWholeDescriptorWriteRequest>;
    constexpr bool general = std::is_same_v<Request, rhi::OriginalGeneralDescriptorWriteRequest>;
    constexpr bool targetMemory =
        std::is_same_v<Request, rhi::OriginalFiniteTargetMemoryWriteRequest>;
    using Contract = std::conditional_t<general,
        rhi::OriginalGeneralDescriptorWriteContract,
        std::conditional_t<whole,
            rhi::OriginalWholeDescriptorWriteContract,
            std::conditional_t<targetMemory,
                rhi::OriginalFiniteTargetMemoryWriteContract,
                rhi::OriginalFiniteDescriptorWriteContract>>>;
    const auto fail = [&](const char* id) -> std::shared_ptr<WebGpuNativeDescriptorGroupPayload> {
        diagnostic = id;
        return {};
    };
    diagnostic = nullptr;
    if (const auto* id = Contract::validate(request))
        return fail(id);
    if (!writes || !count || count > request.maxBindings)
        return fail("RHI-ORIGINAL-FINITE-WRITE-BINDING-COUNT");
    for (std::size_t i = 0u; i < quotas.size(); ++i) {
        if (!quotas[i] || quotas[i]->limitBytes() != request.maxCpuBytes[i])
            return fail("RHI-ORIGINAL-FINITE-WRITE-QUOTA");
        for (std::size_t j = 0u; j < i; ++j)
            if (core::SharedOwnerProjection::overlapsOwner(quotas[i], quotas[j]))
                return fail("RHI-ORIGINAL-FINITE-WRITE-QUOTA-DOMAIN");
    }
    const auto decision = Contract::decide(device.backend(), originalDescriptorGroupMethods());
    if (!decision.preparationAvailable)
        return fail(decision.diagnostic);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    try {
        core::UniquePtr<WebGpuNativeDescriptorGroupPayload::OriginalFiniteWriteState> state;
        {
            core::CpuMemoryQuotaScope scope(quotas[0]);
            state =
                core::makeUnique<WebGpuNativeDescriptorGroupPayload::OriginalFiniteWriteState>();
            state->device = &device;
            state->deviceLifetime = device.originalDeviceLifetimeTicket();
            state->descriptor =
                core::LifetimeObserver<const rhi::DescriptorSet>(&object, object.lifetimeTicket());
            state->arena = arena;
            if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
                state->compute = pipeline;
            else
                state->graphics = pipeline;
            state->quotas = quotas;
            if constexpr (general) {
                auto selected = core::makeUnique<OriginalGeneralWriteState>();
                selected->request = request;
                selected->bindings.reserve(count);
                for (std::size_t i = 0u; i < count; ++i) {
                    WGPUBindGroupEntry entry{};
                    std::uint32_t group = 0u;
                    if (const auto* id = WebGpuOriginalGeneralBindingLowering::lower(
                            {request.contractVersion, true},
                            device,
                            pipeline,
                            sources,
                            set,
                            writes[i],
                            entry,
                            group))
                        return fail(id);
                    const auto owner = rhi::OriginalCommandDescriptorDependencies::capture(
                        *sources, writes[i].sourceIndex);
                    if (!owner)
                        return fail("RHI-ORIGINAL-GENERAL-WRITE-SOURCE");
                    OriginalGeneralWriteState::Binding item{
                        writes[i], *owner, WebGpuTransferResourceObservation{}};
                    const auto* id = std::visit(
                        [&](const auto& source) -> const char* {
                            using T = std::remove_const_t<
                                typename std::decay_t<decltype(source)>::element_type>;
                            if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>)
                                return observeOriginalWebGpuResource(*source->originalObject(),
                                    std::get<WebGpuTransferResourceObservation>(item.observation));
                            else {
                                item.observation = WebGpuOrdinaryResourceObservation{};
                                return observeOriginalWebGpuResource(*source->originalObject(),
                                    std::get<WebGpuOrdinaryResourceObservation>(item.observation));
                            }
                        },
                        *owner);
                    if (id)
                        return fail(id);
                    selected->bindings.push_back(std::move(item));
                }
                state->request = std::move(selected);
            } else
                state->request = request;
            state->logicalSet = set;
            state->revision = object.contentRevision();
            state->declarations.reserve(count);
            state->candidateWrites.reserve(count);
            if constexpr (!general)
                for (std::size_t i = 0u; i < count; ++i)
                    state->declarations.push_back(
                        {writes[i].write, writes[i].buffer, writes[i].dynamicOffset});
            state->entries.reserve(count);
            state->dynamicOffsets.reserve(count);
            for (std::size_t i = 0u; i < count; ++i) {
                auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
                std::uint32_t group = 0u, displacement = 0u;
                if constexpr (general) {
                    if (const auto* id = WebGpuOriginalGeneralBindingLowering::lowerFrozen(
                            {request.contractVersion, true},
                            device,
                            pipeline,
                            state->general()->bindings[i].owner,
                            set,
                            writes[i],
                            entry,
                            group))
                        return fail(id);
                } else if constexpr (whole) {
                    if (const auto* id = WebGpuOriginalWholeBufferBindingLowering::lower(device,
                            pipeline,
                            writes[i].buffer,
                            set,
                            writes[i].write,
                            writes[i].dynamicOffset,
                            entry,
                            group,
                            displacement))
                        return fail(id);
                } else if constexpr (targetMemory) {
                    if (const auto* id = WebGpuOriginalRuntimeLengthIntervalLowering::
                            lowerTargetMemoryBindingDiagnostic({1u, true},
                                device,
                                pipeline,
                                writes[i].buffer,
                                set,
                                writes[i].write.binding,
                                writes[i].write.bufferBindingRange,
                                writes[i].dynamicOffset,
                                entry,
                                group,
                                displacement))
                        return fail(id);
                } else if (const auto* id =
                               WebGpuOriginalRuntimeLengthIntervalLowering::lowerBindingDiagnostic(
                                   {1u, true},
                                   device,
                                   pipeline,
                                   writes[i].buffer,
                                   set,
                                   writes[i].write.binding,
                                   writes[i].write.bufferBindingRange,
                                   writes[i].dynamicOffset,
                                   entry,
                                   group,
                                   displacement))
                    return fail(id);
                if (i && group != state->physicalGroup)
                    return fail("RHI-ORIGINAL-FINITE-WRITE-MIXED-GROUP");
                state->physicalGroup = group;
                state->entries.push_back(entry);
                state->candidateWrites.push_back(writes[i].write);
            }
        }
        if (const auto* id = state->validate())
            return fail(id);
        std::shared_ptr<WebGpuNativeDescriptorGroupPayload> payload;
        {
            core::CpuMemoryQuotaScope scope(quotas[1]);
            payload = core::makeShared<WebGpuNativeDescriptorGroupPayload>();
            payload->bindings.reserve(count);
            payload->dynamicOffsetBindings.reserve(count);
            for (std::size_t i = 0u; i < count; ++i) {
                const auto* binding = rhi::findDescriptorSetBinding(
                    object.layout(), writes[i].write.binding); // proved exact scalar declaration
                if constexpr (general) {
                    WebGpuNativeDescriptorBindingPayload item;
                    item.binding = *binding;
                    item.write = writes[i].write;
                    std::visit(
                        [&](const auto& source) {
                            using T = std::remove_const_t<
                                typename std::decay_t<decltype(source)>::element_type>;
                            if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>)
                                item.buffer = webGpuNativePayloadFor(*source->originalObject());
                            else if constexpr (std::is_same_v<T,
                                                   rhi::OriginalCommandTextureViewSource>)
                                item.textureView =
                                    webGpuNativePayloadFor(*source->originalObject());
                            else
                                item.sampler = webGpuNativePayloadFor(*source->originalObject());
                        },
                        state->general()->bindings[i].owner);
                    payload->bindings.push_back(std::move(item));
                } else
                    payload->bindings.push_back({*binding,
                        writes[i].write,
                        0u,
                        webGpuNativePayloadFor(*writes[i].buffer->originalObject()),
                        nullptr,
                        nullptr});
                if (binding->dynamicOffset)
                    payload->dynamicOffsetBindings.push_back(state->entries[i].binding);
            }
            std::sort(payload->dynamicOffsetBindings.begin(), payload->dynamicOffsetBindings.end());
            payload->dynamicOffsetCount =
                static_cast<std::uint32_t>(payload->dynamicOffsetBindings.size());
        }
        {
            core::CpuMemoryQuotaScope scope(quotas[0]);
            for (const auto binding : payload->dynamicOffsetBindings)
                for (std::size_t i = 0u; i < count; ++i)
                    if (state->entries[i].binding == binding)
                        state->dynamicOffsets.push_back(writes[i].dynamicOffset);
        }
        state->callbacks.prepare(quotas[2]);
        const auto native = webGpuNativePayloadFor(*pipeline);
        state->nativeDevice = static_cast<WGPUDevice>(native->issuedContract()->nativeDevice());
        payload->originalFiniteWrite_ = std::move(state);
        auto& prepared = *payload->originalFiniteWrite_;
        (void)prepared.lifecycle.begin();
        WebGpuNativeErrorScopeBatch::push(prepared.nativeDevice);
        if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
            payload->layout =
                wgpuComputePipelineGetBindGroupLayout(native->pipeline, prepared.physicalGroup);
        else
            payload->layout =
                wgpuRenderPipelineGetBindGroupLayout(native->pipeline, prepared.physicalGroup);
        if (payload->layout) {
            auto desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            desc.layout = payload->layout;
            desc.entryCount = prepared.entries.size();
            desc.entries = prepared.entries.data();
            payload->group = wgpuDeviceCreateBindGroup(prepared.nativeDevice, &desc);
        }
        prepared.originalGroup = payload->group;
        prepared.originalLayout = payload->layout;
        if (!payload->layout || !payload->group)
            prepared.lifecycle.reject("RHI-ORIGINAL-FINITE-WRITE-NATIVE-PAYLOAD");
        prepared.callbacks.pop(prepared.nativeDevice, payload);
        diagnostic = prepared.lifecycle.failure();
        return payload; // retained even on native failure until callbacks drain
    } catch (const std::bad_alloc&) {
        return fail("RHI-ORIGINAL-FINITE-WRITE-ALLOCATION");
    } catch (const std::length_error&) {
        return fail("RHI-ORIGINAL-FINITE-WRITE-ALLOCATION");
    }
#else
    return fail("RHI-ORIGINAL-FINITE-WRITE-NATIVE-METHODS");
#endif
}

template <class Pipeline>
std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalFiniteImpl(const rhi::GpuDevice& device,
    const rhi::DescriptorSet& object,
    const std::shared_ptr<const rhi::DescriptorArena>& arena,
    const std::shared_ptr<Pipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const rhi::OriginalFiniteDescriptorWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const char*& diagnostic)
{
    return prepareOriginalScopedImpl(
        device, object, arena, pipeline, set, writes, count, request, quotas, diagnostic);
}

bool WebGpuNativeDescriptorGroupPayload::finiteScopeObserved(
    std::uint32_t participant) const noexcept
{
    return originalFiniteWrite_
        && originalFiniteWrite_->lifecycle.evidence().hasReceived(participant);
}

bool WebGpuNativeDescriptorGroupPayload::finiteScopesDrained() const noexcept
{
    return originalFiniteWrite_ && originalFiniteWrite_->lifecycle.evidence().allReceived();
}

const char* WebGpuNativeDescriptorGroupPayload::finiteCandidateDiagnostic() const noexcept
{
    if (!originalFiniteWrite_ || originalFiniteWrite_->committed)
        return "RHI-ORIGINAL-FINITE-WRITE-UNPREPARED";
    const auto& state = *originalFiniteWrite_;
    if (const auto* id = state.validate())
        return id;
    if (state.lifecycle.phase() != decltype(state.lifecycle)::Phase::Ready)
        return state.lifecycle.failure() ? state.lifecycle.failure()
                                         : "RHI-ORIGINAL-FINITE-WRITE-PENDING";
    if (!state.lifecycle.evidence().proven())
        return "RHI-ORIGINAL-FINITE-WRITE-SCOPE-EVIDENCE";
    return finiteNativeIdentityDiagnostic();
}

core::pmr::Vector<rhi::DescriptorBindingWrite>&
WebGpuNativeDescriptorGroupPayload::finiteCandidateWrites() noexcept
{
    return originalFiniteWrite_->candidateWrites;
}

void WebGpuNativeDescriptorGroupPayload::commitOriginalFinite() noexcept
{
    originalFiniteWrite_->committed = true;
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalFinite(const rhi::GpuDevice& device,
    const rhi::DescriptorSet& object,
    const std::shared_ptr<const rhi::DescriptorArena>& arena,
    const std::shared_ptr<rhi::ComputePipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const rhi::OriginalFiniteDescriptorWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const char*& diagnostic)
{
    return prepareOriginalFiniteImpl(
        device, object, arena, pipeline, set, writes, count, request, quotas, diagnostic);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalFinite(const rhi::GpuDevice& device,
    const rhi::DescriptorSet& object,
    const std::shared_ptr<const rhi::DescriptorArena>& arena,
    const std::shared_ptr<rhi::RenderPipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const rhi::OriginalFiniteDescriptorWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const char*& diagnostic)
{
    return prepareOriginalFiniteImpl(
        device, object, arena, pipeline, set, writes, count, request, quotas, diagnostic);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalFinite(const rhi::GpuDevice& device,
    const rhi::DescriptorSet& object,
    const std::shared_ptr<const rhi::DescriptorArena>& arena,
    const std::shared_ptr<rhi::ComputePipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const rhi::OriginalFiniteTargetMemoryWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const char*& diagnostic)
{
    return prepareOriginalScopedImpl(
        device, object, arena, pipeline, set, writes, count, request, quotas, diagnostic);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalFinite(const rhi::GpuDevice& device,
    const rhi::DescriptorSet& object,
    const std::shared_ptr<const rhi::DescriptorArena>& arena,
    const std::shared_ptr<rhi::RenderPipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalFiniteDescriptorBufferWrite* writes,
    std::size_t count,
    const rhi::OriginalFiniteTargetMemoryWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const char*& diagnostic)
{
    return prepareOriginalScopedImpl(
        device, object, arena, pipeline, set, writes, count, request, quotas, diagnostic);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalWhole(const rhi::GpuDevice& device,
    const rhi::DescriptorSet& object,
    const std::shared_ptr<const rhi::DescriptorArena>& arena,
    const std::shared_ptr<rhi::ComputePipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalWholeDescriptorBufferWrite* writes,
    std::size_t count,
    const rhi::OriginalWholeDescriptorWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const char*& diagnostic)
{
    return prepareOriginalScopedImpl(
        device, object, arena, pipeline, set, writes, count, request, quotas, diagnostic);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
WebGpuNativeDescriptorGroupPayload::prepareOriginalWhole(const rhi::GpuDevice& device,
    const rhi::DescriptorSet& object,
    const std::shared_ptr<const rhi::DescriptorArena>& arena,
    const std::shared_ptr<rhi::RenderPipeline>& pipeline,
    std::uint32_t set,
    const rhi::OriginalWholeDescriptorBufferWrite* writes,
    std::size_t count,
    const rhi::OriginalWholeDescriptorWriteRequest& request,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
    const char*& diagnostic)
{
    return prepareOriginalScopedImpl(
        device, object, arena, pipeline, set, writes, count, request, quotas, diagnostic);
}

} // namespace arp::rhi_webgpu
