#pragma once

#include "core/ownership/lifetime_epoch.h"
#include "editor/workspace/editor_script_component_archive.h"

#include <optional>

namespace arp::editor {

// An Editor draft, not a script instance. Only schema ownership and explicit
// YAML values survive between edits; no receiver, reference control or lease is
// retained. UI controls may inspect each formal descriptor without executing VM
// getters. The command layer still owns live conflict checks and publication.
class EditorScriptComponentForm final {
public:
    static EditorScriptComponentForm create(const EditorSceneDocument&,
        const scriptvm::ScriptComponentIdentity&,
        bool enabled,
        EditorScriptArchiveBudget);
    static EditorScriptComponentForm fromState(
        const EditorSceneDocument&, const EditorScriptComponentState&, EditorScriptArchiveBudget);

    const scriptvm::ScriptComponentIdentity& identity() const noexcept { return identity_; }

    const core::pmr::String& displayName() const noexcept;

    std::size_t size() const noexcept { return values_.size(); }

    const reflection::SerializationDescriptor::Field& field(std::size_t) const;
    // nullopt means unassigned, even for optional fields. Explicit YAML null,
    // zero, false, empty string and empty sequence remain assigned values.
    const std::optional<core::pmr::String>& value(std::size_t) const;
    bool complete() const noexcept;

    bool enabled() const noexcept { return enabled_; }

    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }

    // All validation/allocation finishes before changing a cell. A failed
    // assignment preserves the draft, including its unassigned/assigned state.
    void assign(const EditorSceneDocument&, std::size_t, std::string_view valueYaml);
    void clear(std::size_t);
    EditorScriptComponentState prepare(const EditorSceneDocument&) const;

private:
    EditorScriptComponentForm(const EditorSceneDocument&,
        const scriptvm::ScriptComponentIdentity&,
        bool,
        EditorScriptArchiveBudget);
    void requireOwner(const EditorSceneDocument&) const;
    core::SharedPtr<const EditorScriptComponentCatalog> catalog_;
    core::LifetimeEpoch::Ticket owner_;
    scriptvm::ScriptComponentIdentity identity_;
    EditorScriptArchiveBudget budget_;
    core::pmr::Vector<std::optional<core::pmr::String>> values_;
    std::size_t valueBytes_ = 0;
    bool enabled_;
};

} // namespace arp::editor
