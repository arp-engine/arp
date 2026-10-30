#include "editor/workspace/editor_scene_script_bindings.h"

#include "editor/workspace/editor_scene_document.h"

#include <exception>

bool arp_add_arp_scene_static_plugin(arp::scriptvm::StaticPluginCatalog&);
bool arp_add_arp_scene2d_static_plugin(arp::scriptvm::StaticPluginCatalog&);

namespace arp::editor {
core::SharedPtr<EditorSceneScriptBindings> EditorSceneScriptBindings::prepare(
    const std::filesystem::path& root,
    const ProjectManifest& manifest,
    scene::Scene& scene,
    core::SharedPtr<scene::MainSceneRegistry> scenes,
    core::SharedPtr<EditorSceneServiceSession> services,
    EditorScriptBindingBudget budget,
    core::pmr::String& diagnostic,
    const ProjectSourceContext* sources)
{
    diagnostic.clear();
    if (!scenes || !scenes->contains(scene) || scenes->active() != &scene || !services
        || !services->resources() || !services->ownerTicket().isCurrent()
        || !budget.domainPreparationBytes || !budget.catalog.maxEntries
        || !budget.catalog.preparationBytes) {
        diagnostic = "EDITOR-SCRIPT-DOMAIN-SERVICES-OR-BUDGET";
        return {};
    }
    try {
        if (sources) {
            const char* issue = nullptr;
            if (!sources->validate(issue)) {
                diagnostic = issue;
                return {};
            }
            if (root != std::filesystem::u8path(sources->snapshot->location().root)) {
                diagnostic = "EDITOR-SCRIPT-SOURCE-ROOT";
                return {};
            }
        }
        ProjectDiagnostic issue;
        core::pmr::String validated;
        if (!ProjectManifestCodec::encode(manifest, validated, issue)) {
            diagnostic = issue.code;
            return {};
        }
        core::SharedPtr<EditorSceneScriptBindings> candidate;
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.domainPreparationBytes);
        {
            core::CpuMemoryQuotaScope scope(quota);
            candidate = core::makeShared<EditorSceneScriptBindings>();
            candidate->root_ = root;
            if (sources)
                candidate->sources_ = *sources;
            candidate->scene_ = &scene;
            candidate->scenes_ = std::move(scenes);
            candidate->services_ = std::move(services);
            candidate->serviceEpoch_ = candidate->services_->ownerTicket();
            candidate->world_ = candidate->services_->physics();
            auto& native = candidate->scope_;
            if (!native.services().provide(scene::kMainSceneService, 1, &scene)
                || !native.services().provide(
                    scene::kMainSceneRegistryService, 1, candidate->scenes_.get())
                || (candidate->world_
                    && (!candidate->world_->valid()
                        || !native.services().provide(
                            "arp.physics2d.world", 1, candidate->world_.get())))
                || !arp_add_arp_scene_static_plugin(native.staticCatalog())
                || !arp_add_arp_scene2d_static_plugin(native.staticCatalog())) {
                diagnostic = "EDITOR-SCRIPT-DOMAIN-SERVICE-REGISTRATION";
                return {};
            }
            native.modules().setStaticPluginCatalog(&native.staticCatalog(), &native.services());
            // Native registration is charged to the explicit domain quota.
            // Only declared imports are loaded, against real supplied services.
            for (const auto& module : manifest.scriptModules) {
                if (!module.authoring) {
                    diagnostic = "EDITOR-SCRIPT-AUTHORING-CONFIGURATION-REQUIRED";
                    return {};
                }
                for (const auto& import : module.authoring->imports)
                    if (!native.modules().loadModule(import)) {
                        diagnostic = "EDITOR-SCRIPT-DOMAIN-IMPORT-UNAVAILABLE: ";
                        diagnostic += import;
                        return {};
                    }
            }
        }
        core::SharedPtr<const EditorScriptModules> modules;
        const bool prepared = sources
            ? EditorScriptModules::prepareSources(*sources->snapshot,
                  *sources->files,
                  sources->epoch,
                  manifest,
                  candidate->scope_.modules(),
                  modules,
                  diagnostic)
            : EditorScriptModules::prepare(
                  root, manifest, candidate->scope_.modules(), modules, diagnostic);
        if (!prepared
            || !EditorScriptComponentCatalog::prepare(
                modules, budget.catalog, candidate->catalog_, diagnostic)
            || !candidate->verifySources(diagnostic))
            return {};
        if (!candidate->matches(scene, candidate->services_, candidate->scenes_)) {
            diagnostic = "EDITOR-SCRIPT-DOMAIN-STALE";
            return {};
        }
        return candidate;
    } catch (const std::bad_alloc&) {
        diagnostic = "EDITOR-SCRIPT-DOMAIN-MEMORY-BUDGET";
        return {};
    } catch (const std::exception& error) {
        diagnostic = "EDITOR-SCRIPT-DOMAIN-PREPARATION-FAILED: ";
        diagnostic += error.what();
        return {};
    }
}

bool EditorSceneScriptBindings::matches(const scene::Scene& scene,
    const core::SharedPtr<EditorSceneServiceSession>& services,
    const core::SharedPtr<scene::MainSceneRegistry>& scenes) const noexcept
{
    return scene_ == &scene && scenes_ == scenes && scenes && scenes->contains(scene)
        && services_ == services && services
        && serviceEpoch_.sameCurrentEpoch(services->ownerTicket());
}

bool EditorSceneScriptBindings::verifySources(core::pmr::String& diagnostic) const
{
    if (sources_)
        return catalog_->modules().verifySources(
            *sources_->snapshot, *sources_->files, sources_->epoch, diagnostic);
    return catalog_->modules().verifySources(root_, diagnostic);
}

bool EditorSceneDocument::prepareScriptAuthoring(const std::filesystem::path& root,
    const ProjectManifest& manifest,
    EditorScriptBindingBudget budget,
    core::pmr::String& diagnostic)
{
    return prepareScriptAuthoringUsing(root, manifest, budget, diagnostic, nullptr);
}

bool EditorSceneDocument::prepareScriptAuthoringSources(const ProjectSourceContext& sources,
    const ProjectManifest& manifest,
    EditorScriptBindingBudget budget,
    core::pmr::String& diagnostic)
{
    const char* issue = nullptr;
    if (!sources.validate(issue)) {
        diagnostic = issue;
        return false;
    }
    try {
        return prepareScriptAuthoringUsing(
            std::filesystem::u8path(sources.snapshot->location().root),
            manifest,
            budget,
            diagnostic,
            &sources);
    } catch (const std::bad_alloc&) {
        diagnostic = "EDITOR-SCRIPT-DOMAIN-MEMORY-BUDGET";
        return false;
    }
}

bool EditorSceneDocument::prepareScriptAuthoringUsing(const std::filesystem::path& root,
    const ProjectManifest& manifest,
    EditorScriptBindingBudget budget,
    core::pmr::String& diagnostic,
    const ProjectSourceContext* sources)
{
    diagnostic.clear();
    // Recompilation prepares a separate document. Do not rebind an existing
    // domain, registered factories or live receivers in place.
    if (!scene_ || scriptBindings_ || !ownerTicket().isCurrent()
        || scene_->executionMode() != scene::SceneExecutionMode::Passive) {
        diagnostic = "EDITOR-SCRIPT-DOCUMENT-NOT-COLD";
        return false;
    }
    const auto hasScripts = [](const scene::GameObject& object) {
        for (const auto& component : object.components())
            if (!component->scriptInstance().is_undefined())
                return true;
        return false;
    };
    if (hasScripts(*scene_)) {
        diagnostic = "EDITOR-SCRIPT-DOCUMENT-HAS-INSTANCES";
        return false;
    }
    for (const auto& object : scene_->objects())
        if (hasScripts(*object)) {
            diagnostic = "EDITOR-SCRIPT-DOCUMENT-HAS-INSTANCES";
            return false;
        }
    auto prepared = EditorSceneScriptBindings::prepare(root,
        manifest,
        *scene_,
        scriptSceneRegistry_,
        sceneServiceSession(),
        budget,
        diagnostic,
        sources);
    if (!prepared)
        return false;
    scriptBindings_ = std::move(prepared);
    ownerEpoch_->advance();
    return true;
}

core::SharedPtr<const EditorScriptComponentCatalog>
EditorSceneDocument::scriptCatalog() const noexcept
{
    return scene_ && scriptBindings_
            && scriptBindings_->matches(*scene_, sceneServiceSession(), scriptSceneRegistry_)
        ? core::SharedPtr<const EditorScriptComponentCatalog>(
              scriptBindings_, scriptBindings_->catalog_.get())
        : nullptr;
}

} // namespace arp::editor
