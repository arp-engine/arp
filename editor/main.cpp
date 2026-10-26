#include "editor/app/editor_application.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    bool hidden = false, after = false, hover = false, viewportPipelineSmoke = false;
    bool viewportRuntimeSmoke = false, terrainResourceSmoke = false;
    bool terrainAssets = false;
    bool remoteBindAddressSpecified = false, legacyWoccSpecified = false;
    std::uint16_t remotePort = 0;
    std::string_view remoteBindAddress = "127.0.0.1";
    std::filesystem::path projectRoot, openScript, legacyWoccExecutable;
    std::string_view openTerrain;
    std::string_view transformGestureSmoke;
    std::string_view inspectorSafetySmoke;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0)
            hidden = true;
        else if (std::strcmp(argv[i], "--after") == 0)
            after = true;
        else if (std::strcmp(argv[i], "--hover") == 0)
            hover = true;
        else if (std::strcmp(argv[i], "--inspector-safety-smoke") == 0 && i + 1 < argc)
            inspectorSafetySmoke = argv[++i];
        else if (std::strcmp(argv[i], "--transform-gesture-smoke") == 0 && i + 1 < argc)
            transformGestureSmoke = argv[++i];
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
        else if (std::strcmp(argv[i], "--legacy-wocc-executable") == 0 && i + 1 < argc) {
            if (legacyWoccSpecified) {
                std::fprintf(
                    stderr, "EDITOR-LEGACY-PREVIEW-OPTIONS: duplicate executable option.\n");
                return 2;
            }
            legacyWoccSpecified = true;
            legacyWoccExecutable = std::filesystem::u8path(argv[++i]);
        } else if (std::strcmp(argv[i], "--open-terrain") == 0 && i + 1 < argc)
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
                "[--viewport-runtime-smoke] [--terrain-resource-smoke] [--inspector-safety-smoke drag_hover|drag_assigned|drag_rejected|drag_cancelled|nav_valid|nav_current|nav_missing|nav_unmapped|nav_failed|nav_retry|focus_valid|focus_current|focus_missing|focus_failed|focus_retry|normal|failed|retry|ref_unassigned|ref_missing|ref_type|ref_repaired|res_unassigned|res_missing|res_type|res_not_ready|res_capacity|res_repaired|res_stale|picker_first|picker_last|picker_repaired|picker_invalid|picker_objects|picker_object_repaired|selection_same|selection_mixed|selection_incompatible|selection_ambiguous|selection_failed|selection_retry|selection_single] [--transform-gesture-smoke before|move|scale|rotate|undo|redo|world_axes|local_axes|local_move|local_scale|world_scale_rejected|local_rotate|world_rotate|local_undo|local_redo|snap_before|snap_move|snap_negative|snap_scale|snap_rotate|snap_undo|snap_redo|batch_move|batch_undo|batch_redo|group_move|group_rotate|group_scale|group_individual|group_undo|group_redo|settings_open|settings_draft|settings_invalid|settings_failed|settings_applied|settings_cancel|input_tool|input_text|input_modal|input_recovery] [--project-root PATH] "
                "[--open-script PATH] [--open-terrain PREVIEW_MATERIAL_UUID] [--terrain-assets] [--remote-port PORT] "
                "[--remote-bind-address IPv4] [--legacy-wocc-executable ABSOLUTE_PATH]\n");
            return 2;
        }
    }
    if (legacyWoccSpecified
        && (!legacyWoccExecutable.is_absolute() || !projectRoot.is_absolute())) {
        std::fprintf(stderr,
            "EDITOR-LEGACY-PREVIEW-OPTIONS: --legacy-wocc-executable requires absolute executable and --project-root paths.\n");
        return 2;
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
    if (!transformGestureSmoke.empty()
        && (!hidden || after || hover || viewportPipelineSmoke || viewportRuntimeSmoke
            || remotePort != 0 || !projectRoot.empty() || !openScript.empty()
            || !openTerrain.empty() || terrainAssets || legacyWoccSpecified)) {
        std::fprintf(stderr, "--transform-gesture-smoke requires an isolated --smoke run\n");
        return 2;
    }
    if (!inspectorSafetySmoke.empty()
        && (!hidden || after || hover || viewportPipelineSmoke || viewportRuntimeSmoke
            || !transformGestureSmoke.empty() || remotePort != 0 || !projectRoot.empty()
            || !openScript.empty() || !openTerrain.empty() || terrainAssets
            || legacyWoccSpecified)) {
        std::fprintf(stderr, "--inspector-safety-smoke requires an isolated --smoke run\n");
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
        terrainAssets,
        legacyWoccExecutable,
        transformGestureSmoke,
        inspectorSafetySmoke);
}
