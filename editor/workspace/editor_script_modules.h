#pragma once

#include "editor/project/project_input_snapshot.h"
#include "editor/project/project_manifest.h"
#include "scene/script_component_persistence.h"

namespace arp::editor {

// Immutable Editor authoring membership/source snapshot. Components remain the
// engine's ScriptBehaviorComponent; no Editor gameplay host or runtime facade.
class EditorScriptModules final {
public:
    struct Import {
        core::pmr::String id, exportContractHash, reflectionRegistryHash;
        std::uint32_t version = 0;
    };

    struct Entry {
        ProjectScriptModule declaration;
        core::pmr::String source;
        core::pmr::Vector<Import> imports;
        core::SharedPtr<scriptvm::ScriptComponentModule> module;
    };

    // Caller supplies an actual prepared domain registry. No filesystem import
    // resolver, guessed engine bindings, source concatenation or script execution.
    // root is canonical; borrowed domain services must outlive all prepared
    // sessions/components. Retaining exports does not retain their Scene owner.
    static bool prepare(const std::filesystem::path& root,
        const ProjectManifest&,
        scriptvm::ModuleRegistry&,
        core::SharedPtr<const EditorScriptModules>& destination,
        core::pmr::String& diagnostic);

    const core::pmr::Vector<Entry>& entries() const noexcept { return entries_; }

    const core::pmr::Vector<ProjectInputBinding>& sources() const noexcept { return sources_; }

    scriptvm::ScriptComponentModule* find(const scriptvm::ScriptComponentIdentity&) const noexcept;
    // Retained declaration comparison only; source freshness is a separate cold
    // admission. Reuse the Project codec for every module and budget field.
    bool matchesManifest(const ProjectManifest&) const;
    bool verifySources(const std::filesystem::path& root, core::pmr::String& diagnostic) const;

private:
    core::pmr::Vector<Entry> entries_;
    core::pmr::Vector<ProjectInputBinding> sources_;
};

} // namespace arp::editor
