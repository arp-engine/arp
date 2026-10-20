#pragma once

#include "editor/viewport/editor_scene_viewport_resource_resolver.h"
#include "editor/viewport/editor_scene_viewport_shader_binding_contract.h"
#include "editor/viewport/editor_scene_viewport_texture_cache.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace arp::editor {

enum class EditorSceneViewportMaterialTextureSemantic : std::uint8_t {
    BaseColor,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
};

inline constexpr std::size_t kEditorSceneViewportMaterialTextureSlotCount = 5u;

struct EditorSceneViewportMaterialTextureBinding {
    EditorSceneViewportMaterialTextureSemantic semantic =
        EditorSceneViewportMaterialTextureSemantic::BaseColor;
    core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    resource::ResourceGeneration generation = 0u;
    EditorSceneViewportTextureColorSpace colorSpace = EditorSceneViewportTextureColorSpace::Linear;
    rhi::GpuTextureView* view = nullptr;
    rhi::GpuSampler* sampler = nullptr;

    bool present() const noexcept
    {
        return !resourceId.empty() && generation != 0u && view != nullptr && sampler != nullptr;
    }
};

struct EditorSceneViewportMaterialPayload {
    core::pmr::String materialResourceId{&core::defaultCpuMemoryResource()};
    resource::ResourceGeneration materialGeneration = 0u;
    core::pmr::String sourceResourceId{&core::defaultCpuMemoryResource()};
    resource::ResourceGeneration sourceGeneration = 0u;
    core::pmr::String shaderPackageId{&core::defaultCpuMemoryResource()};
    resource::ResourceGeneration shaderGeneration = 0u;
    std::array<float, 4> baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> emissiveFactor{0.0f, 0.0f, 0.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    core::pmr::String alphaMode{&core::defaultCpuMemoryResource()};
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    EditorSceneViewportMaterialShaderBindings shaderBindings;
    std::array<EditorSceneViewportMaterialTextureBinding,
        kEditorSceneViewportMaterialTextureSlotCount>
        textures;

    bool valid() const noexcept;
};

enum class EditorSceneViewportDescriptorReadiness : std::uint8_t {
    Ready,
    InvalidPayload,
    MissingTexture,
};

struct EditorSceneViewportDescriptorReadinessResult {
    EditorSceneViewportDescriptorReadiness readiness =
        EditorSceneViewportDescriptorReadiness::InvalidPayload;
    std::size_t failedSlot = 0u;

    bool ready() const noexcept
    {
        return readiness == EditorSceneViewportDescriptorReadiness::Ready;
    }
};

// The CPU payload allows absent optional textures, but the current shader
// interface still requires all five texture descriptors at draw submission.
ARP_API EditorSceneViewportDescriptorReadinessResult validateEditorSceneViewportDescriptorReadiness(
    const EditorSceneViewportMaterialPayload& payload) noexcept;

struct EditorSceneViewportDescriptorWrite {
    std::uint32_t set = 0u;
    rhi::DescriptorBindingWrite write;
};

enum class EditorSceneViewportDescriptorWritesDiagnostic : std::uint8_t {
    None,
    MaterialNotReady,
};

struct EditorSceneViewportDescriptorWritesResult {
    EditorSceneViewportDescriptorWritesDiagnostic diagnostic =
        EditorSceneViewportDescriptorWritesDiagnostic::None;
    core::pmr::Vector<EditorSceneViewportDescriptorWrite> writes{&core::defaultCpuMemoryResource()};

    bool valid() const noexcept
    {
        return diagnostic == EditorSceneViewportDescriptorWritesDiagnostic::None
            && writes.size() == kEditorSceneViewportMaterialTextureSlotCount + 1u;
    }
};

// Produces logical descriptor writes from verified shader bindings. This
// neither allocates a descriptor set nor changes resource state.
ARP_API EditorSceneViewportDescriptorWritesResult buildEditorSceneViewportDescriptorWrites(
    const EditorSceneViewportMaterialPayload& payload);

enum class EditorSceneViewportMaterialPayloadDiagnostic : std::uint8_t {
    None,
    InvalidResolvedItem,
    InvalidMaterialMetadata,
    ShaderBindingContractFailure,
    ResourceChanged,
    TextureCacheFailure,
};

struct EditorSceneViewportMaterialPayloadResult {
    EditorSceneViewportMaterialPayloadDiagnostic diagnostic =
        EditorSceneViewportMaterialPayloadDiagnostic::None;
    EditorSceneViewportTextureCacheDiagnostic textureDiagnostic =
        EditorSceneViewportTextureCacheDiagnostic::None;
    core::pmr::String detail{&core::defaultCpuMemoryResource()};
    EditorSceneViewportMaterialPayload payload;

    bool valid() const noexcept
    {
        return diagnostic == EditorSceneViewportMaterialPayloadDiagnostic::None && payload.valid();
    }
};

// Texture views and samplers are borrowed from textureCache and remain valid
// only while that cache and its device live. The caller must keep both alive
// until every submitted draw using this payload has completed.
ARP_API EditorSceneViewportMaterialPayloadResult prepareEditorSceneViewportMaterialPayload(
    const EditorSceneViewportResolvedItem& item,
    const shader::ShaderReflection& reflection,
    rhi::GpuDevice& device,
    const resource::ResourceManager& resources,
    EditorSceneViewportTextureCache& textureCache);

ARP_API const char* editorSceneViewportMaterialPayloadDiagnosticCode(
    EditorSceneViewportMaterialPayloadDiagnostic diagnostic) noexcept;

} // namespace arp::editor
