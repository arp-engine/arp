#pragma once

#include "rhi/gpu_pipeline.h"
#include "shader/shader_compiler.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace arp::editor {

inline constexpr std::size_t kEditorSceneViewportShaderTextureSlotCount = 5u;
inline constexpr std::uint32_t kEditorSceneViewportShaderBindingContractVersion = 1u;

struct EditorSceneViewportShaderDescriptorBinding {
    std::uint32_t set = 0u;
    std::uint32_t binding = 0u;
    std::uint32_t arrayCount = 0u;
    std::uint32_t stageMask = 0u;

    bool valid() const noexcept { return arrayCount != 0u && stageMask != 0u; }
};

struct EditorSceneViewportMaterialShaderBindings {
    std::uint32_t contractVersion = 0u;
    std::array<EditorSceneViewportShaderDescriptorBinding,
        kEditorSceneViewportShaderTextureSlotCount>
        textures;
    EditorSceneViewportShaderDescriptorBinding sampler;

    bool valid() const noexcept;
};

enum class EditorSceneViewportShaderBindingDiagnostic : std::uint8_t {
    None,
    ReflectionContractUnsupported,
    MissingBinding,
    DuplicateBinding,
    BindingTypeMismatch,
    ArrayCountMismatch,
    StageMaskMismatch,
    DescriptorAddressCollision,
    UnexpectedBinding,
    RhiLayoutInvalid,
};

struct EditorSceneViewportShaderBindingContractResult {
    EditorSceneViewportShaderBindingDiagnostic diagnostic =
        EditorSceneViewportShaderBindingDiagnostic::None;
    std::size_t failedSlot = 0u;
    EditorSceneViewportMaterialShaderBindings bindings;
    rhi::ShaderReflectionDesc rhiReflection;

    bool valid() const noexcept
    {
        return diagnostic == EditorSceneViewportShaderBindingDiagnostic::None && bindings.valid();
    }
};

ARP_API EditorSceneViewportShaderBindingContractResult inspectEditorSceneViewportShaderBindings(
    const shader::ShaderReflection& reflection);

ARP_API const char* editorSceneViewportShaderBindingDiagnosticCode(
    EditorSceneViewportShaderBindingDiagnostic diagnostic) noexcept;

} // namespace arp::editor
