#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace arp::editor {

class EditorApplication final {
public:
    int run(bool hidden,
        bool after,
        bool hover,
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& openScript,
        std::uint16_t remotePort = 0,
        std::string_view remoteBindAddress = "127.0.0.1",
        bool viewportPipelineSmoke = false,
        bool viewportRuntimeSmoke = false,
        bool terrainResourceSmoke = false,
        std::string_view openTerrain = {},
        bool terrainAssets = false,
        const std::filesystem::path& legacyWoccExecutable = {},
        std::string_view transformGestureSmoke = {},
        std::string_view inspectorSafetySmoke = {});
};

} // namespace arp::editor
