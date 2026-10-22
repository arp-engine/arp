#include "editor/app/editor_application.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    bool hidden = false, after = false, hover = false, viewportPipelineSmoke = false;
    bool viewportRuntimeSmoke = false, terrainResourceSmoke = false;
    bool terrainAssets = false;
    bool remoteBindAddressSpecified = false;
    std::uint16_t remotePort = 0;
    std::string_view remoteBindAddress = "127.0.0.1";
    std::filesystem::path projectRoot, openScript;
    std::string_view openTerrain;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0)
            hidden = true;
        else if (std::strcmp(argv[i], "--after") == 0)
            after = true;
        else if (std::strcmp(argv[i], "--hover") == 0)
            hover = true;
        else if (std::strcmp(argv[i], "--viewport-pipeline-smoke") == 0)
            viewportPipelineSmoke = true;
        else if (std::strcmp(argv[i], "--terrain-resource-smoke") == 0) {
            terrainResourceSmoke = true;
            viewportPipelineSmoke = true;
        } else if (std::strcmp(argv[i], "--terrain-assets") == 0) {
            terrainAssets = true;
        } else if (std::strcmp(argv[i], "--viewport-runtime-smoke") == 0)
            viewportRuntimeSmoke = true;
        else if (std::strcmp(argv[i], "--project-root") == 0 && i + 1 < argc)
            projectRoot = argv[++i];
        else if (std::strcmp(argv[i], "--open-script") == 0 && i + 1 < argc)
            openScript = argv[++i];
        else if (std::strcmp(argv[i], "--open-terrain") == 0 && i + 1 < argc)
            openTerrain = argv[++i];
        else if (std::strcmp(argv[i], "--remote-port") == 0 && i + 1 < argc) {
            char* end = nullptr;
            const auto parsed = std::strtoul(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed == 0 || parsed > 65535) {
                std::fprintf(stderr, "Invalid --remote-port; expected 1..65535\n");
                return 2;
            }
            remotePort = static_cast<std::uint16_t>(parsed);
        } else if (std::strcmp(argv[i], "--remote-bind-address") == 0 && i + 1 < argc) {
            remoteBindAddress = argv[++i];
            remoteBindAddressSpecified = true;
        } else {
            std::fprintf(stderr,
                "Usage: arp_editor [--smoke] [--after] [--hover] [--viewport-pipeline-smoke] "
                "[--viewport-runtime-smoke] [--terrain-resource-smoke] [--project-root PATH] "
                "[--open-script PATH] [--open-terrain PREVIEW_MATERIAL_UUID] [--terrain-assets] [--remote-port PORT] "
                "[--remote-bind-address IPv4]\n");
            return 2;
        }
    }
    if (remoteBindAddressSpecified && remotePort == 0) {
        std::fprintf(stderr, "--remote-bind-address requires --remote-port\n");
        return 2;
    }
    if (viewportPipelineSmoke && !hidden) {
        std::fprintf(stderr, "--viewport-pipeline-smoke requires --smoke\n");
        return 2;
    }
    if (viewportRuntimeSmoke && !hidden) {
        std::fprintf(stderr, "--viewport-runtime-smoke requires --smoke\n");
        return 2;
    }
    if (viewportPipelineSmoke && viewportRuntimeSmoke) {
        std::fprintf(stderr, "viewport smoke modes are mutually exclusive\n");
        return 2;
    }
    return arp::editor::EditorApplication().run(hidden,
        after,
        hover,
        projectRoot,
        openScript,
        remotePort,
        remoteBindAddress,
        viewportPipelineSmoke,
        viewportRuntimeSmoke,
        terrainResourceSmoke,
        openTerrain,
        terrainAssets);
}
