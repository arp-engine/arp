#pragma once

#include "editor/workspace/editor_scene_script_codec.h"

namespace arp::editor {

// Explicit cold reload candidate, then one bounded original-file admission
// proof. It never writes files and never retains the retired domain after publish.
class EditorSceneScriptReload final {
public:
    EditorSceneScriptReload() = default;
    static core::UniquePtr<EditorSceneScriptReload> prepare(const EditorSceneDocument&,
        const EditorSceneScriptInputs& next,
        std::string_view relative,
        std::string_view previousBytes,
        const EditorSceneScriptReload* previousProof,
        core::pmr::String& diagnostic);

    EditorSceneDocument& candidate() const noexcept { return *candidate_; }

    bool allowsPrevious(const EditorSceneDocument&,
        const EditorSceneScriptInputs&,
        std::string_view relative,
        std::string_view previousBytes) const;

private:
    friend class EditorShell;
    // Only after the original Scene replacement commits; never renew a proof.
    void published(const EditorSceneDocument&) noexcept;
    core::UniquePtr<EditorSceneDocument> candidate_;
    core::LifetimeEpoch::Ticket owner_;
    const scene::Scene* scene_ = nullptr;
    const EditorScriptComponentCatalog* catalog_ = nullptr;
    core::io::FileService* files_ = nullptr;
    ProjectSourceLocation location_;
    core::pmr::String relative_, previousHash_;
    bool published_ = false;
};
} // namespace arp::editor
