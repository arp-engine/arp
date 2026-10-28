#pragma once

#include "editor/workspace/editor_script_modules.h"

namespace arp::editor {

// Explicit cold-path limits supplied by the Editor caller. No implicit runtime
// instance allocation or gameplay defaults are derived from a catalog row.
struct EditorScriptCatalogBudget {
    std::size_t maxEntries = 0;
    std::uint64_t preparationBytes = 0;
};

class EditorScriptComponentCatalog final {
public:
    struct Entry {
        scriptvm::ScriptComponentIdentity identity;
        core::pmr::String moduleId, source, displayName, diagnostic;
        core::SharedPtr<const reflection::SerializationDescriptor> fields;

        bool available() const noexcept { return bool(fields); }
    };

    // Membership and schemas retain the exact prepared source/module owner.
    // Domain services borrowed by that owner remain the caller's responsibility.
    // Failure preserves destination, including its previous source membership.
    // This does not read disk or publish an active document; the eventual
    // document transaction must verify the retained source bindings explicitly.
    static bool prepare(core::SharedPtr<const EditorScriptModules>,
        EditorScriptCatalogBudget,
        core::SharedPtr<const EditorScriptComponentCatalog>& destination,
        core::pmr::String& diagnostic);

    const core::pmr::Vector<Entry>& entries() const noexcept { return entries_; }

    const Entry* find(const scriptvm::ScriptComponentIdentity&) const noexcept;

    const EditorScriptModules& modules() const noexcept { return *modules_; }

private:
    core::SharedPtr<const EditorScriptModules> modules_;
    core::pmr::Vector<Entry> entries_;
};

} // namespace arp::editor
