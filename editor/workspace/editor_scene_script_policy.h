#pragma once

#include "editor/workspace/editor_scene_script_codec.h"

namespace arp::editor {

// Initial bounded Editor authoring profile, not a runtime/game budget and not
// inferred from file size. Project modules keep their own explicit budgets.
class EditorSceneScriptPolicy final {
public:
    static EditorScriptArchiveBudget fields()
    {
        return {256 * 1024, 64, 4 * 1024 * 1024, reflection::SerializationOptions{}};
    }

    static constexpr std::uint64_t documentPreparationBytes = 64 * 1024 * 1024;

    static EditorSceneScriptInputs inputs(
        const std::filesystem::path& root, const ProjectManifest& project)
    {
        return {root,
            project,
            {16 * 1024 * 1024, {4, 1024 * 1024}},
            fields(),
            documentPreparationBytes};
    }
};

} // namespace arp::editor
