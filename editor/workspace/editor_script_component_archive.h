#pragma once

#include "editor/workspace/editor_script_component_catalog.h"

namespace arp::editor {
class EditorSceneDocument;

struct EditorScriptArchiveBudget {
    std::size_t maxYamlBytes = 0, maxReferences = 0;
    std::uint64_t preparationBytes = 0;
    reflection::SerializationOptions fields;
};

// Plain authoring/history values. No receiver, module, native handle or service
// is retained; the destination document supplies the current domain on restore.
struct EditorScriptComponentState {
    struct Reference {
        scene::GameObjectId owner;
        scene::ComponentId component;

        bool operator==(const Reference& other) const noexcept
        {
            return owner == other.owner && component == other.component;
        }
    };

    static constexpr std::uint32_t contractVersion = 1;
    scriptvm::ScriptComponentIdentity identity;
    core::pmr::String moduleId, sourcePath, sourceFingerprint, fields;
    core::pmr::Vector<EditorScriptModules::Import> imports;
    core::pmr::Vector<Reference> references;
    bool enabled = true;
};

// Editor identity-reference contract: {owner, component}; component=0 denotes
// an object. References resolve only through the supplied document/transaction,
// never through names, ordinals or saved reflection controls.
class EditorScriptComponentArchive final {
public:
    EditorScriptComponentArchive(const EditorSceneDocument&, EditorScriptArchiveBudget);
    EditorScriptComponentState capture(const scene::Component&,
        const scene::Scene::IdentityRestoration* transaction = nullptr) const;
    // Complete explicit input is decoded into plain inspection storage. No
    // receiver, instance lease, initializer or implicit default is created.
    EditorScriptComponentState prepare(const scriptvm::ScriptComponentIdentity&,
        bool enabled,
        std::string_view fields,
        const scene::Scene::IdentityRestoration* transaction = nullptr) const;
    // A form cell is validated by the same descriptor and exact resolver as a
    // complete archive. This does not manufacture values for any other field.
    core::pmr::String prepareField(const scriptvm::ScriptComponentIdentity&,
        std::string_view name,
        std::string_view valueYaml) const;
    // A presentation can compose ordinary lists/optional values without
    // implementing YAML traversal or retaining native reference controls.
    // Depth follows only formally declared Optional/Sequence element links.
    reflection::object readFieldElement(const scriptvm::ScriptComponentIdentity&,
        std::string_view name,
        std::size_t elementDepth,
        std::string_view valueYaml) const;
    core::pmr::String writeField(const scriptvm::ScriptComponentIdentity&,
        std::string_view name,
        const reflection::object&) const;
    core::pmr::String writeFieldElement(const scriptvm::ScriptComponentIdentity&,
        std::string_view name,
        std::size_t elementDepth,
        const reflection::object&) const;
    // Owner-thread operation: all decoding, conflict checks and slot lookup
    // finish before the first live write. Receiver and native identity survive.
    bool replaceFields(scene::Component&,
        const EditorScriptComponentState& expected,
        const EditorScriptComponentState& replacement) const;
    // Any staging/apply exception requires abandoning the caller-owned transaction.
    // Only unpublished candidates are writable; no live rollback is attempted.
    void apply(scene::Component&,
        const EditorScriptComponentState&,
        const scene::Scene::IdentityRestoration&) const;
    scene::Component& stage(scene::Scene::IdentityRestoration&,
        scene::GameObjectId,
        scene::ComponentId,
        std::size_t index,
        const EditorScriptComponentState&,
        bool allocateIdentity = false) const;
    static bool equal(const EditorScriptComponentState&, const EditorScriptComponentState&);
    static std::size_t storageBytes(const EditorScriptComponentState&) noexcept;

private:
    const reflection::SerializationDescriptor& fieldDescriptor(const EditorScriptComponentCatalog&,
        const scriptvm::ScriptComponentIdentity&,
        std::string_view,
        std::size_t elementDepth) const;
    core::SharedPtr<const EditorScriptComponentCatalog> catalog() const;
    const EditorScriptModules::Entry& module(
        const EditorScriptComponentCatalog&, const scriptvm::ScriptComponentIdentity&) const;
    void validateBinding(
        const EditorScriptComponentCatalog&, const EditorScriptComponentState&) const;
    EditorScriptComponentState binding(const scriptvm::ScriptComponentIdentity&, bool) const;
    EditorScriptComponentState prepareValues(const scriptvm::ScriptComponentIdentity&,
        bool,
        std::string_view,
        reflection::object&,
        const scene::Scene::IdentityRestoration*) const;
    const EditorSceneDocument& document_;
    EditorScriptArchiveBudget budget_;
};
} // namespace arp::editor
