#pragma once

#include "core/runtime/cancellation_token.h"
#include "editor/viewport/editor_scene_viewport_shader_package.h"
#include "renderer/shader_package_reflection.h"
#include "resource/resource_graph_executor.h"

#include <array>

namespace arp::editor {

// NativeViewportSources/1. WebGL1 uses its separate ordered program contract.
struct EditorViewportShaderSources final {
    static constexpr std::array<const char*, 3> publications{
        "editor_viewport_pbr.artifact-set.yaml",
        "editor_viewport_sprite.artifact-set.yaml",
        "editor_environment/editor_viewport_environment.artifact-set.yaml"};
    static constexpr std::size_t maxFileBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t maxSourceBytes = 32u * 1024u * 1024u;
    static constexpr std::size_t maxPayloadBytes = 64u * 1024u * 1024u;
    static constexpr std::size_t maxFiles = 32;
    std::array<renderer::ShaderPackagePreparationDesc, 3> programs;
    std::array<std::array<core::pmr::String, 5>, 3> paths;
    bool valid() const;
    EditorSceneViewportShaderArtifacts artifacts(std::size_t index) const;
    // The host retains manager and callback storage until its IO has drained.
    // ResourceGraphExecutor owns acquisition, cancellation and admission.
    static resource::ResourceGraphExecutionRequest request(
        core::PoolHandle, core::runtime::CancellationToken = {});
    static EditorViewportShaderSources capture(const resource::ResourceManager&);
};
} // namespace arp::editor
