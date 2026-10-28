#pragma once

#include "editor/workspace/editor_scene_service_session.h"
#include "editor/workspace/editor_script_component_catalog.h"
#include "scene/main_scene_registry.h"
#include "scriptvm/plugin/plugin_runtime_scope.h"

namespace arp::editor {

struct EditorScriptBindingBudget {
    std::uint64_t domainPreparationBytes = 0;
    EditorScriptCatalogBudget catalog;
};

// Editor-owned cold module/service lifetime, not a gameplay host. Only the
// document may publish it; its Scene remains the canonical document Scene.
class EditorSceneScriptBindings final {
private:
    friend class EditorSceneDocument;
    static core::SharedPtr<EditorSceneScriptBindings> prepare(const std::filesystem::path&,
        const ProjectManifest&,
        scene::Scene&,
        core::SharedPtr<scene::MainSceneRegistry>,
        core::SharedPtr<EditorSceneServiceSession>,
        EditorScriptBindingBudget,
        core::pmr::String& diagnostic);
    bool matches(const scene::Scene&,
        const core::SharedPtr<EditorSceneServiceSession>&,
        const core::SharedPtr<scene::MainSceneRegistry>&) const noexcept;
    bool verifySources(core::pmr::String& diagnostic) const;

    std::filesystem::path root_;
    core::SharedPtr<EditorSceneServiceSession> services_;
    core::LifetimeEpoch::Ticket serviceEpoch_;
    // Preserve the exact native world even if a service session later changes.
    // Stale bindings are retained for teardown and rejected for authoring use.
    core::SharedPtr<physics2d::PhysicsWorld2D> world_;
    core::SharedPtr<scene::MainSceneRegistry> scenes_;
    scriptvm::ScriptPluginRuntimeScope scope_;
    core::SharedPtr<const EditorScriptComponentCatalog> catalog_;
    const scene::Scene* scene_ = nullptr;
};

} // namespace arp::editor
