#pragma once

#include "editor/viewport/editor_scene_viewport_contract.h"
#include "resource/resource_manager.h"

namespace arp::editor {

struct EditorSceneViewportResolvedItem {
    core::pmr::String meshResourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String materialResourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String sourceResourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String shaderPackageId{&core::defaultCpuMemoryResource()};
    core::pmr::String baseColorTextureId{&core::defaultCpuMemoryResource()};
    core::pmr::String normalTextureId{&core::defaultCpuMemoryResource()};
    core::pmr::String metallicRoughnessTextureId{&core::defaultCpuMemoryResource()};
    core::pmr::String occlusionTextureId{&core::defaultCpuMemoryResource()};
    core::pmr::String emissiveTextureId{&core::defaultCpuMemoryResource()};
    resource::ResourceGeneration baseColorTextureGeneration = 0;
    resource::ResourceGeneration normalTextureGeneration = 0;
    resource::ResourceGeneration metallicRoughnessTextureGeneration = 0;
    resource::ResourceGeneration occlusionTextureGeneration = 0;
    resource::ResourceGeneration emissiveTextureGeneration = 0;
    core::pmr::Vector<float> baseColorFactor{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<float> emissiveFactor{&core::defaultCpuMemoryResource()};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    core::pmr::String alphaMode{&core::defaultCpuMemoryResource()};
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    resource::ResourceGeneration meshGeneration = 0;
    resource::ResourceGeneration materialGeneration = 0;
    resource::ResourceGeneration sourceGeneration = 0;
    resource::ResourceGeneration shaderGeneration = 0;
    EditorSceneViewportGeometry geometry = EditorSceneViewportGeometry::Mesh;
    terrain::TerrainTileCoord tile;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    std::size_t submeshCount = 0;
    EditorSceneViewportDiagnostic diagnostic = EditorSceneViewportDiagnostic::None;
};

struct EditorSceneViewportResourceResolution {
    EditorSceneViewportDiagnostic frameDiagnostic = EditorSceneViewportDiagnostic::None;
    core::pmr::Vector<EditorSceneViewportResolvedItem> items{&core::defaultCpuMemoryResource()};
    bool valid() const noexcept;
};

// Resolves CPU metadata only; handles and GPU bindings never escape the resolver.
ARP_API EditorSceneViewportResourceResolution resolveEditorSceneViewportResources(
    const EditorSceneViewportFrame& frame, const resource::ResourceManager& resources);

} // namespace arp::editor
