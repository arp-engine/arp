#pragma once

#include "editor/project/project_source_verification.h"
#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/editor_recovery_codec.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/script_document_session.h"

namespace arp::editor {
enum class SourceRecoveryCaptureState { Pending, Ready, Failed, Cancelled };

struct EditorSourceRecoveryCaptureInput {
    ProjectSourceContext sources;
    const EditorProjectResources& resources;
    const ProjectManifest& projectDraft;
    const EditorSceneDocument& document;
    const ScriptDocumentSession& scripts;
    std::string_view alias, sessionId, workspace;
    std::uint64_t sequence = 0, workspaceEpoch = 0;
    std::uint32_t sceneVersion = 3;
    // An empty path means an unbound scene and requires both baselines empty.
    std::string_view scenePath, sceneSavedHash, sceneSavedFileBytes;
};

// Captures owned authoring bytes on the owner thread, then asynchronously
// verifies the retained project inputs. The live Scene/tabs may retire after
// construction. This does not capture mutable resource payloads or publish UI.
class EditorSourceRecoveryCapture final {
public:
    EditorSourceRecoveryCapture(const EditorSourceRecoveryCaptureInput&,
        ProjectInputBudget = {},
        EditorRecoveryBudget = {});
    ~EditorSourceRecoveryCapture();
    EditorSourceRecoveryCapture(const EditorSourceRecoveryCapture&) = delete;
    EditorSourceRecoveryCapture& operator=(const EditorSourceRecoveryCapture&) = delete;

    void advance(std::uint64_t currentEpoch) noexcept;
    void cancel() noexcept;
    SourceRecoveryCaptureState state() const noexcept;
    std::string_view diagnostic() const noexcept;
    const ProjectSourceContext& context() const noexcept;
    const EditorRecoverySnapshot* snapshot(
        std::uint64_t currentEpoch, const char*& diagnostic) const noexcept;

private:
    struct State;
    core::UniquePtr<State> state_;
};
} // namespace arp::editor
