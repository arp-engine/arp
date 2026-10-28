#include "editor/workspace/editor_script_component_archive.h"

#include "core/hash/sha256.h"
#include "editor/workspace/editor_scene_document.h"
#include "reflection/instance.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace arp::editor {
namespace {
using State = EditorScriptComponentState;
using Descriptor = reflection::SerializationDescriptor;
using Y = core::YamlContract;

void require(bool condition, const char* diagnostic)
{
    if (!condition)
        throw std::invalid_argument(diagnostic);
}

bool sameImports(const core::pmr::Vector<EditorScriptModules::Import>& a,
    const core::pmr::Vector<EditorScriptModules::Import>& b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) {
               return x.id == y.id && x.version == y.version
                   && x.exportContractHash == y.exportContractHash
                   && x.reflectionRegistryHash == y.reflectionRegistryHash;
           });
}

class ExactReferences final : public reflection::SerializationContext {
public:
    ExactReferences(const scene::Scene& scene,
        const scriptvm::ScriptComponentIdentity& identity,
        std::size_t maximum,
        const scene::Scene::IdentityRestoration* transaction = nullptr)
        : scene_(scene), identity_(identity), maximum_(maximum), transaction_(transaction)
    {}

    bool writeReference(const reflection::property&,
        const reflection::object&,
        Y::Node&,
        core::pmr::String& error) const override
    {
        error = "EDITOR-SCRIPT-ARCHIVE-NATIVE-PROPERTY-UNSUPPORTED";
        return false;
    }

    bool readReference(const reflection::property&,
        const Y::Node&,
        reflection::object&,
        core::pmr::String& error) const override
    {
        error = "EDITOR-SCRIPT-ARCHIVE-NATIVE-PROPERTY-UNSUPPORTED";
        return false;
    }

    bool writeReference(const Descriptor& type,
        const reflection::object& value,
        Y::Node& output,
        core::pmr::String& error) const override
    {
        if (value.is_native_reference() && value.native_reference_alive()
            && value.can_safe_cast<scene::GameObject*>()) {
            const auto* object = value.cast<scene::GameObject*>();
            if (object && !object->pendingDestroy() && resolveObject(object->id()) == object
                && objectType(type))
                return write({object->id(), {}}, output, error);
        }
        const scene::Component* component = nullptr;
        if (type.referenceClass && value.is_native_reference() && value.native_reference_alive()
            && value.can_safe_cast<scene::Component*>())
            component = value.cast<scene::Component*>();
        else if (!type.referenceClass && value.is<reflection::instance>()) {
            const auto* instance = value.ptr<reflection::instance>();
            if (instance && instance->alive()) {
                const auto found = instance->slots().find("component");
                if (found != instance->slots().end()) {
                    const auto& control = found->second;
                    if (control.is_native_reference() && control.native_reference_alive()
                        && control.can_safe_cast<scene::Component*>()) {
                        const auto* actual = control.cast<scene::Component*>();
                        const auto receiver =
                            actual ? actual->scriptInstance() : reflection::object{};
                        if (receiver.is<reflection::instance>()
                            && receiver.ptr<reflection::instance>() == instance)
                            component = actual;
                    }
                }
            }
        }
        // Only the canonical receiver/native control and exact scoped identity
        // are accepted. This includes references between unpublished candidates.
        if (component && !component->pendingDestroy() && component->gameObject()
            && !component->gameObject()->pendingDestroy()
            && resolveObject(component->ownerId()) == component->gameObject()
            && resolveComponent(component->ownerId(), component->id()) == component
            && componentType(type, *component))
            return write({component->ownerId(), component->id()}, output, error);
        error = "EDITOR-SCRIPT-ARCHIVE-REFERENCE-EXTERNAL-DANGLING-OR-TYPE";
        return false;
    }

    bool readReference(const Descriptor& type,
        const Y::Node& input,
        reflection::object& value,
        core::pmr::String& error) const override
    {
        Y::onlyKeys(input, {"owner", "component"});
        if (input.size() != 2) {
            error = "EDITOR-SCRIPT-ARCHIVE-REFERENCE-SHAPE";
            return false;
        }
        const State::Reference target{
            scene::GameObjectId::fromValue(Y::uint64(Y::required(input, "owner"))),
            scene::ComponentId::fromValue(Y::uint64(Y::required(input, "component")))};
        const auto* owner = resolveObject(target.owner);
        if (!owner || owner->pendingDestroy() || !target.owner.valid()) {
            error = "EDITOR-SCRIPT-ARCHIVE-REFERENCE-OWNER";
            return false;
        }
        if (objectType(type) && !target.component.valid())
            value = owner->nativeReflectionObject();
        else {
            const auto* component = resolveComponent(target.owner, target.component);
            if (!target.component.valid() || !component || !componentType(type, *component)) {
                error = "EDITOR-SCRIPT-ARCHIVE-REFERENCE-COMPONENT-OR-TYPE";
                return false;
            }
            // A declared native Component (including a derived native type)
            // consumes its native control; a declared script type its receiver.
            value = type.referenceClass ? component->nativeReflectionObject()
                                        : component->scriptInstance();
        }
        return record(target, error);
    }

    const core::pmr::Vector<State::Reference>& references() const { return references_; }

private:
    const scene::Component* resolveComponent(
        scene::GameObjectId ownerId, scene::ComponentId id) const
    {
        const auto* owner = resolveObject(ownerId);
        return transaction_ ? transaction_->resolveComponent(ownerId, id)
            : owner         ? owner->findComponent(id)
                            : nullptr;
    }

    const scene::GameObject* resolveObject(scene::GameObjectId id) const
    {
        return transaction_ ? transaction_->resolveObject(id) : scene_.findObject(id);
    }

    static bool objectType(const Descriptor& type)
    {
        return type.referenceClass == reflection::class_of<scene::GameObject>();
    }

    bool componentType(const Descriptor& type, const scene::Component& value) const
    {
        if (value.pendingDestroy())
            return false;
        if (type.referenceClass) {
            const auto* componentClass = reflection::class_of<scene::Component>();
            const auto* actual = value.nativeReflectionObject().class_();
            // class_info::is_base_of excludes equality. Both exact types and
            // registered native derivations are valid reference targets.
            return (componentClass == type.referenceClass
                       || componentClass->is_base_of(type.referenceClass))
                && actual
                && (type.referenceClass == actual || type.referenceClass->is_base_of(actual));
        }
        const auto* script = dynamic_cast<const scene::ScriptBehaviorComponent*>(&value);
        const auto receiver = value.scriptInstance();
        return script && receiver.is<reflection::instance>()
            && receiver.ptr<reflection::instance>()->alive()
            && script->scriptResourceId() == identity_.scriptResourceId
            && script->schemaVersion() == identity_.moduleSchemaVersion
            && type.referenceType == script->scriptTypeName();
    }

    bool record(State::Reference target, core::pmr::String& error) const
    {
        if (references_.size() == maximum_) {
            error = "EDITOR-SCRIPT-ARCHIVE-REFERENCE-BUDGET";
            return false;
        }
        references_.push_back(target);
        return true;
    }

    bool write(State::Reference target, Y::Node& output, core::pmr::String& error) const
    {
        if (!record(target, error))
            return false;
        output = Y::Node::mapping();
        output["owner"] = target.owner.value();
        output["component"] = target.component.value();
        return true;
    }

    const scene::Scene& scene_;
    const scriptvm::ScriptComponentIdentity& identity_;
    std::size_t maximum_;
    const scene::Scene::IdentityRestoration* transaction_;
    mutable core::pmr::Vector<State::Reference> references_;
};

scriptvm::ScriptComponentIdentity identity(const scene::Component& value)
{
    const auto* script = dynamic_cast<const scene::ScriptBehaviorComponent*>(&value);
    require(
        script && !value.scriptInstance().is_undefined(), "EDITOR-SCRIPT-ARCHIVE-COMPONENT-TYPE");
    return {script->scriptResourceId(), script->scriptTypeName(), script->schemaVersion()};
}

struct PreparedValue {
    core::pmr::String yaml;
    core::pmr::Vector<State::Reference> references;
};

// Both whole-component archives and form cells use this single domain
// normalization path. Only SerializationArchive walks nested values/types.
PreparedValue prepareValue(const scene::Scene& scene,
    const scriptvm::ScriptComponentIdentity& identity,
    const EditorScriptArchiveBudget& budget,
    const Descriptor& descriptor,
    const Y::Node& node,
    reflection::object& values,
    const scene::Scene::IdentityRestoration* transaction)
{
    ExactReferences input(scene, identity, budget.maxReferences, transaction);
    reflection::SerializationArchive reader(budget.fields, &input);
    if (!reader.readValue(descriptor, node, values))
        throw std::invalid_argument(
            (core::pmr::String("EDITOR-SCRIPT-ARCHIVE-FIELD-READ: ") + reader.diagnostics())
                .c_str());
    ExactReferences output(scene, identity, budget.maxReferences, transaction);
    reflection::SerializationArchive writer(budget.fields, &output);
    Y::Node normalized;
    if (!writer.writeValue(descriptor, values, normalized))
        throw std::invalid_argument(
            (core::pmr::String("EDITOR-SCRIPT-ARCHIVE-FIELD-WRITE: ") + writer.diagnostics())
                .c_str());
    require(input.references() == output.references(),
        "EDITOR-SCRIPT-ARCHIVE-REFERENCE-RECORD-MISMATCH");
    auto yaml = Y::serialize(normalized);
    require(yaml.size() <= budget.maxYamlBytes, "EDITOR-SCRIPT-ARCHIVE-STATE-BUDGET");
    return {std::move(yaml), output.references()};
}
} // namespace

EditorScriptComponentArchive::EditorScriptComponentArchive(
    const EditorSceneDocument& document, EditorScriptArchiveBudget budget)
    : document_(document), budget_(budget)
{
    require(budget_.maxYamlBytes && budget_.maxReferences && budget_.preparationBytes
            && budget_.fields.maxDepth && budget_.fields.maxValues
            && budget_.fields.maxCollectionLength,
        "EDITOR-SCRIPT-ARCHIVE-BUDGET");
}

core::SharedPtr<const EditorScriptComponentCatalog> EditorScriptComponentArchive::catalog() const
{
    auto result = document_.scriptCatalog();
    require(bool(result), "EDITOR-SCRIPT-ARCHIVE-DOMAIN-STALE-OR-UNAVAILABLE");
    return result;
}

const EditorScriptModules::Entry& EditorScriptComponentArchive::module(
    const EditorScriptComponentCatalog& catalog,
    const scriptvm::ScriptComponentIdentity& identity) const
{
    require(catalog.find(identity) && catalog.find(identity)->available(),
        "EDITOR-SCRIPT-ARCHIVE-SCHEMA");
    for (const auto& entry : catalog.modules().entries())
        if (entry.declaration.resourceId == identity.scriptResourceId
            && entry.declaration.schemaVersion == identity.moduleSchemaVersion)
            return entry;
    throw std::invalid_argument("EDITOR-SCRIPT-ARCHIVE-MODULE");
}

void EditorScriptComponentArchive::validateBinding(
    const EditorScriptComponentCatalog& catalog, const State& state) const
{
    const auto& entry = module(catalog, state.identity);
    require(entry.declaration.id == state.moduleId && entry.declaration.source == state.sourcePath
            && core::Sha256::hexDigest(core::StringView(entry.source)) == state.sourceFingerprint
            && sameImports(entry.imports, state.imports),
        "EDITOR-SCRIPT-ARCHIVE-SOURCE-OR-IMPORT-CONFLICT");
    require(!state.fields.empty() && state.fields.size() <= budget_.maxYamlBytes
            && state.references.size() <= budget_.maxReferences,
        "EDITOR-SCRIPT-ARCHIVE-STATE-BUDGET");
}

State EditorScriptComponentArchive::capture(
    const scene::Component& component, const scene::Scene::IdentityRestoration* transaction) const
{
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget_.preparationBytes));
    const auto owner = catalog();
    require(component.gameObject() && component.gameObject()->scene() == &document_.scene()
            && !component.pendingDestroy() && !component.gameObject()->pendingDestroy()
            && (transaction ? transaction->resolveObject(component.ownerId())
                            : document_.scene().findObject(component.ownerId()))
                == component.gameObject()
            && (transaction ? transaction->resolveComponent(component.ownerId(), component.id())
                            : component.gameObject()->findComponent(component.id()))
                == &component,
        "EDITOR-SCRIPT-ARCHIVE-COMPONENT-OWNER");
    auto state = binding(identity(component), component.enabled());
    ExactReferences context(document_.scene(), state.identity, budget_.maxReferences, transaction);
    reflection::SerializationArchive archive(budget_.fields, &context);
    Y::Node fields;
    const auto* row = owner->find(state.identity);
    if (!archive.writeValue(*row->fields, component.scriptInstance(), fields))
        throw std::invalid_argument(
            (core::pmr::String("EDITOR-SCRIPT-ARCHIVE-FIELD-WRITE: ") + archive.diagnostics())
                .c_str());
    state.fields = Y::serialize(fields);
    state.references = context.references();
    validateBinding(*owner, state);
    return state;
}

State EditorScriptComponentArchive::binding(
    const scriptvm::ScriptComponentIdentity& identity, bool enabled) const
{
    const auto prepared = catalog();
    const auto& entry = module(*prepared, identity);
    State state;
    state.identity = identity;
    state.moduleId = entry.declaration.id;
    state.sourcePath = entry.declaration.source;
    state.sourceFingerprint = core::Sha256::hexDigest(core::StringView(entry.source));
    state.imports = entry.imports;
    state.enabled = enabled;
    return state;
}

State EditorScriptComponentArchive::prepareValues(const scriptvm::ScriptComponentIdentity& identity,
    bool enabled,
    std::string_view fields,
    reflection::object& values,
    const scene::Scene::IdentityRestoration* transaction) const
{
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget_.preparationBytes));
    require(!fields.empty() && fields.size() <= budget_.maxYamlBytes,
        "EDITOR-SCRIPT-ARCHIVE-STATE-BUDGET");
    auto state = binding(identity, enabled);
    const auto owner = catalog();
    const auto& descriptor = *owner->find(identity)->fields;
    const auto node = Y::parseSingleDocument(fields);
    values = reflection::object(scriptvm::ScriptComponentInspectionValues{});
    auto normalized =
        prepareValue(document_.scene(), identity, budget_, descriptor, node, values, transaction);
    state.fields = std::move(normalized.yaml);
    state.references = std::move(normalized.references);
    validateBinding(*owner, state);
    return state;
}

State EditorScriptComponentArchive::prepare(const scriptvm::ScriptComponentIdentity& identity,
    bool enabled,
    std::string_view fields,
    const scene::Scene::IdentityRestoration* transaction) const
{
    reflection::object values;
    return prepareValues(identity, enabled, fields, values, transaction);
}

core::pmr::String EditorScriptComponentArchive::prepareField(
    const scriptvm::ScriptComponentIdentity& identity,
    std::string_view name,
    std::string_view valueYaml) const
{
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget_.preparationBytes));
    require(!valueYaml.empty() && valueYaml.size() <= budget_.maxYamlBytes,
        "EDITOR-SCRIPT-ARCHIVE-STATE-BUDGET");
    const auto owner = catalog();
    const auto& descriptor = fieldDescriptor(*owner, identity, name, 0);
    const auto node = Y::parseSingleDocument(valueYaml);
    reflection::object value;
    return prepareValue(document_.scene(), identity, budget_, descriptor, node, value, nullptr)
        .yaml;
}

const Descriptor& EditorScriptComponentArchive::fieldDescriptor(
    const EditorScriptComponentCatalog& owner,
    const scriptvm::ScriptComponentIdentity& identity,
    std::string_view name,
    std::size_t depth) const
{
    module(owner, identity);
    const auto& fields = owner.find(identity)->fields->fields;
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const auto& field) {
        return field.name == name;
    });
    require(found != fields.end() && found->type, "EDITOR-SCRIPT-ARCHIVE-FIELD-NAME");
    const auto* descriptor = found->type.get();
    require(depth < budget_.fields.maxDepth, "EDITOR-SCRIPT-ARCHIVE-FIELD-DEPTH");
    for (std::size_t i = 0; i < depth; ++i) {
        require((descriptor->kind == Descriptor::Kind::Optional
                    || descriptor->kind == Descriptor::Kind::Sequence)
                && descriptor->element,
            "EDITOR-SCRIPT-ARCHIVE-FIELD-ELEMENT");
        descriptor = descriptor->element.get();
    }
    return *descriptor;
}

reflection::object EditorScriptComponentArchive::readFieldElement(
    const scriptvm::ScriptComponentIdentity& identity,
    std::string_view name,
    std::size_t depth,
    std::string_view valueYaml) const
{
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget_.preparationBytes));
    require(!valueYaml.empty() && valueYaml.size() <= budget_.maxYamlBytes,
        "EDITOR-SCRIPT-ARCHIVE-STATE-BUDGET");
    const auto owner = catalog();
    const auto& descriptor = fieldDescriptor(*owner, identity, name, depth);
    const auto node = Y::parseSingleDocument(valueYaml);
    reflection::object value;
    prepareValue(document_.scene(), identity, budget_, descriptor, node, value, nullptr);
    return value;
}

core::pmr::String EditorScriptComponentArchive::writeField(
    const scriptvm::ScriptComponentIdentity& identity,
    std::string_view name,
    const reflection::object& value) const
{
    return writeFieldElement(identity, name, 0, value);
}

core::pmr::String EditorScriptComponentArchive::writeFieldElement(
    const scriptvm::ScriptComponentIdentity& identity,
    std::string_view name,
    std::size_t depth,
    const reflection::object& value) const
{
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget_.preparationBytes));
    const auto owner = catalog();
    const auto& descriptor = fieldDescriptor(*owner, identity, name, depth);
    ExactReferences context(document_.scene(), identity, budget_.maxReferences);
    reflection::SerializationArchive writer(budget_.fields, &context);
    Y::Node node;
    if (!writer.writeValue(descriptor, value, node))
        throw std::invalid_argument(
            (core::pmr::String("EDITOR-SCRIPT-ARCHIVE-FIELD-WRITE: ") + writer.diagnostics())
                .c_str());
    reflection::object checked;
    return prepareValue(document_.scene(), identity, budget_, descriptor, node, checked, nullptr)
        .yaml;
}

bool EditorScriptComponentArchive::replaceFields(
    scene::Component& component, const State& expected, const State& replacement) const
{
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget_.preparationBytes));
    const auto before = capture(component);
    require(equal(before, expected), "EDITOR-SCRIPT-ARCHIVE-FIELD-CONFLICT");
    require(before.identity.scriptResourceId == replacement.identity.scriptResourceId
            && before.identity.scriptType == replacement.identity.scriptType
            && before.identity.moduleSchemaVersion == replacement.identity.moduleSchemaVersion,
        "EDITOR-SCRIPT-ARCHIVE-IDENTITY-MISMATCH");
    validateBinding(*catalog(), replacement);
    reflection::object values;
    const auto after = prepareValues(
        replacement.identity, replacement.enabled, replacement.fields, values, nullptr);
    require(equal(after, replacement), "EDITOR-SCRIPT-ARCHIVE-ROUNDTRIP-MISMATCH");
    if (equal(before, after))
        return false;
    auto receiver = component.scriptInstance();
    auto* instance = receiver.ptr<reflection::instance>();
    require(instance && instance->alive(), "EDITOR-SCRIPT-ARCHIVE-LIVE-DESTINATION");

    struct Write {
        reflection::object* target;
        reflection::object value;
    };

    const auto& fields = values.ptr<scriptvm::ScriptComponentInspectionValues>()->fields;
    core::pmr::Vector<Write> pending;
    pending.reserve(fields.size());
    for (const auto& field : fields) {
        require(instance->slots().find(field.first) != instance->slots().end(),
            "EDITOR-SCRIPT-ARCHIVE-LIVE-FIELD-MISSING");
        // slot may allocate a lookup key, so even existing slots are located
        // before committing. Plain decoded values own all pending payloads.
        pending.push_back({&instance->slot(field.first), field.second});
    }
    // No callbacks, lookups, parsing or allocation after the first live write.
    // object move assignment is noexcept; canonical built-in slots are untouched.
    static_assert(std::is_nothrow_move_assignable_v<reflection::object>);
    for (auto& write : pending)
        *write.target = std::move(write.value);
    component.setEnabled(after.enabled);
    return true;
}

scene::Component& EditorScriptComponentArchive::stage(
    scene::Scene::IdentityRestoration& transaction,
    scene::GameObjectId owner,
    scene::ComponentId id,
    std::size_t index,
    const State& state,
    bool allocateIdentity) const
{
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget_.preparationBytes));
    const auto prepared = catalog();
    validateBinding(*prepared, state);
    const auto* destination = transaction.resolveObject(owner);
    require(
        destination && destination->scene() == &document_.scene() && !destination->pendingDestroy(),
        "EDITOR-SCRIPT-ARCHIVE-CANDIDATE-OWNER");
    auto* compiled = prepared->modules().find(state.identity);
    return allocateIdentity
        ? transaction.createScriptComponent(owner, *compiled, state.identity, index)
        : transaction.restoreScriptComponent(owner, id, *compiled, state.identity, index);
}

void EditorScriptComponentArchive::apply(scene::Component& component,
    const State& state,
    const scene::Scene::IdentityRestoration& transaction) const
{
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget_.preparationBytes));
    const auto prepared = catalog();
    validateBinding(*prepared, state);
    const auto actual = identity(component);
    require(actual.scriptResourceId == state.identity.scriptResourceId
            && actual.moduleSchemaVersion == state.identity.moduleSchemaVersion
            && actual.scriptType == state.identity.scriptType,
        "EDITOR-SCRIPT-ARCHIVE-IDENTITY-MISMATCH");
    require(component.gameObject() && component.gameObject()->scene() == &document_.scene()
            && transaction.resolveComponent(component.ownerId(), component.id()) == &component,
        "EDITOR-SCRIPT-ARCHIVE-CANDIDATE-OWNER");
    // A destination may only be an unpublished candidate; live writes belong to
    // a separate prepared command. Failed parsing cannot modify live receivers.
    require(transaction.findCandidate(component.ownerId()) == component.gameObject()
            || component.gameObject()->findComponent(component.id()) != &component,
        "EDITOR-SCRIPT-ARCHIVE-LIVE-DESTINATION");
    reflection::object values;
    const auto normalized =
        prepareValues(state.identity, state.enabled, state.fields, values, &transaction);
    require(normalized.references == state.references,
        "EDITOR-SCRIPT-ARCHIVE-REFERENCE-RECORD-MISMATCH");
    require(equal(normalized, state), "EDITOR-SCRIPT-ARCHIVE-ROUNDTRIP-MISMATCH");
    auto receiver = component.scriptInstance();
    require(receiver.is<reflection::instance>() && receiver.ptr<reflection::instance>()->alive(),
        "EDITOR-SCRIPT-ARCHIVE-CANDIDATE-OWNER");
    for (const auto& field : values.ptr<scriptvm::ScriptComponentInspectionValues>()->fields)
        receiver.ptr<reflection::instance>()->slot(field.first) = field.second;
    component.setEnabled(state.enabled);
    require(
        equal(capture(component, &transaction), state), "EDITOR-SCRIPT-ARCHIVE-ROUNDTRIP-MISMATCH");
}

bool EditorScriptComponentArchive::equal(const State& a, const State& b)
{
    return a.identity.scriptResourceId == b.identity.scriptResourceId
        && a.identity.moduleSchemaVersion == b.identity.moduleSchemaVersion
        && a.identity.scriptType == b.identity.scriptType && a.moduleId == b.moduleId
        && a.sourcePath == b.sourcePath && a.sourceFingerprint == b.sourceFingerprint
        && sameImports(a.imports, b.imports) && a.fields == b.fields && a.references == b.references
        && a.enabled == b.enabled;
}

std::size_t EditorScriptComponentArchive::storageBytes(const State& state) noexcept
{
    std::size_t result = sizeof(State) + state.identity.scriptResourceId.capacity() + 1
        + state.identity.scriptType.capacity() + 1 + state.sourceFingerprint.capacity() + 1
        + state.moduleId.capacity() + state.sourcePath.capacity() + 2 + state.fields.capacity() + 1
        + state.imports.capacity() * sizeof(EditorScriptModules::Import)
        + state.references.capacity() * sizeof(State::Reference);
    for (const auto& import : state.imports)
        result += import.id.capacity() + import.exportContractHash.capacity()
            + import.reflectionRegistryHash.capacity() + 3;
    return result;
}
} // namespace arp::editor
