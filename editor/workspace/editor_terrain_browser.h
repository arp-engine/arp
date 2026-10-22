#pragma once

#include "editor/workspace/editor_terrain_preview.h"

namespace arp::editor {

// A value snapshot for presentation; never retains ResourceHandle/raw payloads.
struct EditorTerrainBrowserEntry final {
    core::pmr::String previewId, terrainId;
    resource::ResourceGeneration previewGeneration = 0, terrainGeneration = 0;
    std::uint32_t tilesX = 0, tilesZ = 0, samplesPerEdge = 0, layerCount = 0;
    std::size_t sourceBytes = 0;
    std::uint64_t vegetationInstances = 0;
    core::Vec3 minimum{}, maximum{};
};

class EditorTerrainBrowser final {
public:
    static constexpr std::size_t maxMaterialReferences = 256;
    static constexpr std::size_t maxPreviews = 16;
    // All-or-nothing snapshot. Invalid/stale preview roots are diagnosed.
    bool refresh(const resource::ResourceManager& resources);

    const core::pmr::Vector<EditorTerrainBrowserEntry>& entries() const noexcept
    {
        return entries_;
    }

    const core::pmr::String& diagnostic() const noexcept { return diagnostic_; }

private:
    core::pmr::Vector<EditorTerrainBrowserEntry> entries_;
    core::pmr::String diagnostic_;
};

} // namespace arp::editor
