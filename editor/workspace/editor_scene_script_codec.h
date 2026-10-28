#pragma once

#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_script_bindings.h"
#include "editor/workspace/editor_script_component_archive.h"

namespace arp::editor {
// Borrowed only during synchronous preparation. Actual document services own
// the destination domain; neither this input nor a file can supply native exports.
struct EditorSceneScriptInputs {
    const std::filesystem::path& projectRoot;
    const ProjectManifest& project;
    EditorScriptBindingBudget modules;
    EditorScriptArchiveBudget fields;
    std::uint64_t preparationBytes = 0;
};

// Explicit v4 authoring path. The v3 codec and file-store gates are preserved.
// Editor object/component IDs are serialized; runtime resource IDs stay separate.
class EditorSceneScriptCodec final {
public:
    static constexpr std::uint32_t contractVersion = 4;
    static bool encode(const EditorSceneDocument&,
        const EditorSceneScriptInputs&,
        core::pmr::String& yaml,
        core::pmr::String& diagnostic);
    // Cold snapshot hash rechecks source files, without preparing another domain.
    // It is not a persistence-admission token; encode verifies a full cold reload.
    static bool contentHash(const EditorSceneDocument&,
        const EditorSceneScriptInputs&,
        core::pmr::String&,
        core::pmr::String& diagnostic);
    // Ordinary in-memory observation: retained schema/source declarations and
    // slots only. No file IO, domain preparation or user code. Saving still uses
    // the separate cold admission path, even when this hash matches a baseline.
    static bool observeContentHash(const EditorSceneDocument&,
        const EditorSceneScriptInputs&,
        core::pmr::String&,
        core::pmr::String& diagnostic);
    // Bounded wire observation/structure only; no disk, service or script owner
    // is created. Native/script field semantics still require prepare() below.
    static bool observeSnapshot(const EditorSceneDocument&,
        const EditorSceneScriptInputs&,
        core::pmr::String& yaml,
        core::pmr::String& diagnostic);
    static bool validateStructure(std::string_view yaml,
        EditorScriptArchiveBudget,
        std::uint64_t preparationBytes,
        core::pmr::String& diagnostic);
    // Returns a fully prepared actual Passive document. Failure preserves the
    // caller's previous candidate and the destination document. No script executes.
    static bool prepare(std::string_view yaml,
        const EditorSceneDocument& destination,
        const EditorSceneScriptInputs&,
        core::UniquePtr<EditorSceneDocument>& candidate,
        core::pmr::String& diagnostic);
    static bool decode(std::string_view yaml,
        EditorSceneDocument& destination,
        const EditorSceneScriptInputs&,
        core::pmr::String& diagnostic);
};
} // namespace arp::editor
