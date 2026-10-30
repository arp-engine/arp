#include "editor/workspace/editor_scene_script_codec.h"

#include "core/hash/sha256.h"
#include "core/io/filesystem_path.h"
#include "editor/viewport/editor_scene_transform_frame.h"
#include "editor/workspace/editor_scene_component_registry.h"
#include "editor/workspace/editor_scene_presence.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace arp::editor {
namespace {
using Y = core::YamlContract;
using Node = Y::Node;
using Presence = EditorScenePresence;
using State = EditorScriptComponentState;
using Transaction = scene::Scene::IdentityRestoration;
using Limits = EditorSceneAuthoringCodec;

void require(bool value, const char* diagnostic)
{
    if (!value)
        throw std::invalid_argument(diagnostic);
}

void mapping(const Node& node, std::initializer_list<core::StringView> keys)
{
    Y::onlyKeys(node, keys);
    require(node.is_mapping() && node.size() == keys.size(), "EDITOR-SCENE-V4-RECORD-SHAPE");
}

std::uint64_t identity(const Node& node)
{
    const auto value = Y::uint64(node);
    require(value && value <= std::uint64_t(INT64_MAX), "EDITOR-SCENE-V4-IDENTITY");
    return value;
}

void checkIdentity(std::uint64_t value)
{
    require(value && value <= std::uint64_t(INT64_MAX), "EDITOR-SCENE-V4-IDENTITY");
}

enum class SourceVerification { RetainedObservation, ColdAdmission };

void verifyProject(const EditorScriptComponentCatalog& catalog,
    const EditorSceneScriptInputs& input,
    SourceVerification sources = SourceVerification::ColdAdmission)
{
    require(catalog.modules().matchesManifest(input.project),
        "EDITOR-SCENE-V4-PROJECT-MODULE-CONFLICT");
    core::pmr::String diagnostic;
    if (sources == SourceVerification::ColdAdmission) {
        if (input.sources) {
            const char* issue = nullptr;
            const bool valid = input.sources->validate(issue);
            require(valid, issue ? issue : "PROJECT-SOURCE-CONTEXT");
            require(input.projectRoot
                    == std::filesystem::u8path(input.sources->snapshot->location().root),
                "EDITOR-SCRIPT-SOURCE-ROOT");
            if (!catalog.modules().verifySources(*input.sources->snapshot,
                    *input.sources->files,
                    input.sources->epoch,
                    diagnostic))
                throw std::invalid_argument(diagnostic.c_str());
        } else if (!catalog.modules().verifySources(input.projectRoot, diagnostic))
            throw std::invalid_argument(diagnostic.c_str());
    }
}

Node writeScript(const State& state)
{
    Node output = Node::mapping();
    output["contractVersion"] = State::contractVersion;
    output["resourceId"] = state.identity.scriptResourceId;
    output["schemaVersion"] = state.identity.moduleSchemaVersion;
    output["scriptType"] = state.identity.scriptType;
    output["moduleId"] = state.moduleId;
    output["sourcePath"] = state.sourcePath;
    output["sourceFingerprint"] = state.sourceFingerprint;
    output["enabled"] = state.enabled;
    output["fields"] = Y::parseSingleDocument(state.fields);
    output["imports"] = Node::sequence();
    for (const auto& import : state.imports) {
        Node item = Node::mapping();
        item["id"] = import.id;
        item["version"] = import.version;
        item["exportContractHash"] = import.exportContractHash;
        item["reflectionRegistryHash"] = import.reflectionRegistryHash;
        output["imports"].get_value_ref<Node::sequence_type&>().push_back(std::move(item));
    }
    output["references"] = Node::sequence();
    for (const auto& ref : state.references) {
        Node item = Node::mapping();
        item["owner"] = ref.owner.value();
        item["component"] = ref.component.value();
        output["references"].get_value_ref<Node::sequence_type&>().push_back(std::move(item));
    }
    return output;
}

State readScript(const Node& node, const EditorScriptArchiveBudget& budget)
{
    mapping(node,
        {"contractVersion",
            "resourceId",
            "schemaVersion",
            "scriptType",
            "moduleId",
            "sourcePath",
            "sourceFingerprint",
            "enabled",
            "fields",
            "imports",
            "references"});
    require(Y::uint32(node, "contractVersion") == State::contractVersion,
        "EDITOR-SCENE-V4-SCRIPT-VERSION");
    State state;
    state.identity = {
        Y::text(node, "resourceId"), Y::text(node, "scriptType"), Y::uint32(node, "schemaVersion")};
    state.moduleId = Y::text(node, "moduleId");
    state.sourcePath = Y::text(node, "sourcePath");
    state.sourceFingerprint = Y::text(node, "sourceFingerprint");
    state.enabled = Y::boolean(node, "enabled");
    const auto& fields = Y::required(node, "fields");
    require(fields.is_mapping(), "EDITOR-SCENE-V4-SCRIPT-FIELDS");
    state.fields = Y::serialize(fields);
    require(state.fields.size() <= budget.maxYamlBytes, "EDITOR-SCENE-V4-SCRIPT-FIELDS-BUDGET");
    const auto& imports = Y::sequence(node, "imports");
    require(imports.size() <= ProjectManifestBudget{}.maxModuleDependencies,
        "EDITOR-SCENE-V4-IMPORT-BUDGET");
    for (const auto& item : imports) {
        mapping(item, {"id", "version", "exportContractHash", "reflectionRegistryHash"});
        state.imports.push_back({Y::text(item, "id"),
            Y::text(item, "exportContractHash"),
            Y::text(item, "reflectionRegistryHash"),
            Y::uint32(item, "version")});
    }
    const auto& references = Y::sequence(node, "references");
    require(references.size() <= budget.maxReferences, "EDITOR-SCENE-V4-REFERENCE-BUDGET");
    for (const auto& ref : references) {
        mapping(ref, {"owner", "component"});
        const auto component = Y::uint64(Y::required(ref, "component"));
        require(component <= std::uint64_t(INT64_MAX), "EDITOR-SCENE-V4-IDENTITY");
        state.references.push_back(
            {scene::GameObjectId::fromValue(identity(Y::required(ref, "owner"))),
                scene::ComponentId::fromValue(component)});
    }
    return state;
}

Node writeObject(const EditorSceneDocument& document,
    const scene::GameObject& object,
    const EditorSceneScriptInputs& input,
    std::size_t childIndex,
    std::size_t& componentCount)
{
    checkIdentity(object.id().value());
    checkIdentity(object.transform().id().value());
    require(EditorSceneDocument::validObjectName(object.name()) && !object.pendingDestroy()
            && EditorSceneTransformFrame::validAuthoring(object.local_transform()),
        "EDITOR-SCENE-V4-OBJECT-STATE");
    Node node = Node::mapping();
    node["id"] = object.id().value();
    node["transformId"] = object.transform().id().value();
    node["name"] = object.name();
    node["active"] = object.active();
    node["parent"] = object.parentId().value();
    node["childIndex"] = std::uint64_t(childIndex);
    node["transformEnabled"] = object.transform().enabled();
    reflection::SerializationArchive transform;
    require(transform.writeFields(reflection::object(object.local_transform()), node["transform"]),
        "EDITOR-SCENE-V4-TRANSFORM");
    node["components"] = Node::sequence();
    require(++componentCount <= Limits::maxComponents, "EDITOR-SCENE-V4-COMPONENT-BUDGET");
    EditorScriptComponentArchive scripts(document, input.fields);
    for (std::size_t index = 1; index < object.components().size(); ++index) {
        require(++componentCount <= Limits::maxComponents, "EDITOR-SCENE-V4-COMPONENT-BUDGET");
        const auto& component = *object.components()[index];
        checkIdentity(component.id().value());
        Node encoded = Node::mapping();
        encoded["id"] = component.id().value();
        if (!component.scriptInstance().is_undefined()) {
            encoded["kind"] = "script";
            encoded["state"] = writeScript(scripts.capture(component));
        } else {
            encoded["kind"] = "native";
            encoded["type"] = component.scriptTypeName();
            const auto snapshot = Presence::captureComponent(component, index);
            require(snapshot.properties.size() <= Limits::maxPropertiesPerComponent,
                "EDITOR-SCENE-V4-PROPERTY-BUDGET");
            encoded["properties"] = Node::mapping();
            for (const auto& field : snapshot.properties) {
                Node value;
                require(
                    EditorScenePropertyCodec::encode(field, value, field.sceneObjectReferenceValue),
                    "EDITOR-SCENE-V4-NATIVE-FIELD");
                encoded["properties"][core::toStdString(field.name)] = std::move(value);
            }
        }
        node["components"].get_value_ref<Node::sequence_type&>().push_back(std::move(encoded));
    }
    return node;
}

struct ObjectRecord {
    Presence::Object object;
    const Node* node = nullptr;
};

ObjectRecord readObject(
    const Node& node, const EditorScriptArchiveBudget& budget, std::size_t& componentCount)
{
    mapping(node,
        {"id",
            "transformId",
            "name",
            "active",
            "parent",
            "childIndex",
            "transformEnabled",
            "transform",
            "components"});
    ObjectRecord result;
    result.node = &node;
    auto& object = result.object;
    object.id = scene::GameObjectId::fromValue(identity(Y::required(node, "id")));
    object.transformId = scene::ComponentId::fromValue(identity(Y::required(node, "transformId")));
    object.parent = scene::GameObjectId::fromValue(Y::uint64(Y::required(node, "parent")));
    object.childIndex = Y::uint64(Y::required(node, "childIndex"));
    require(object.childIndex <= Limits::maxObjects, "EDITOR-SCENE-V4-CHILD-INDEX");
    object.name = Y::text(node, "name");
    object.active = Y::boolean(node, "active");
    object.transformEnabled = Y::boolean(node, "transformEnabled");
    require(EditorSceneDocument::validObjectName(object.name), "EDITOR-SCENE-V4-OBJECT-NAME");
    const auto& transform = Y::required(node, "transform");
    reflection::SerializationArchive fields;
    require(fields.readFields(transform, reflection::object(&object.transform))
            && EditorSceneTransformFrame::validAuthoring(object.transform),
        "EDITOR-SCENE-V4-TRANSFORM");
    const auto& components = Y::sequence(node, "components");
    require(++componentCount <= Limits::maxComponents
            && components.size() <= Limits::maxComponents - componentCount,
        "EDITOR-SCENE-V4-COMPONENT-BUDGET");
    for (const auto& component : components) {
        Presence::Component value;
        value.id = scene::ComponentId::fromValue(identity(Y::required(component, "id")));
        value.index = object.components.size() + 1;
        const auto kind = Y::text(component, "kind");
        if (kind == "script") {
            mapping(component, {"id", "kind", "state"});
            value.script =
                core::makeShared<State>(readScript(Y::required(component, "state"), budget));
            value.type = value.script->identity.scriptType;
        } else {
            require(kind == "native", "EDITOR-SCENE-V4-COMPONENT-KIND");
            mapping(component, {"id", "kind", "type", "properties"});
            value.type = Y::text(component, "type");
            require(
                editorSceneComponentIdentityRestorable(value.type), "EDITOR-SCENE-V4-NATIVE-TYPE");
            const auto& properties = Y::required(component, "properties");
            require(
                properties.is_mapping() && properties.size() <= Limits::maxPropertiesPerComponent,
                "EDITOR-SCENE-V4-NATIVE-PROPERTIES");
        }
        object.components.push_back(std::move(value));
        ++componentCount;
    }
    return result;
}

void readWireRecords(const Node& node,
    const EditorScriptArchiveBudget& budget,
    core::pmr::Vector<ObjectRecord>& records)
{
    mapping(node, {"type", "contractVersion", "runtimeSceneId", "selected", "root", "objects"});
    require(Y::text(node, "type") == "arp.editor-scene"
            && Y::uint32(node, "contractVersion") == EditorSceneScriptCodec::contractVersion,
        "EDITOR-SCENE-V4-CONTRACT");
    const auto& runtime = Y::required(node, "runtimeSceneId");
    require(
        runtime.is_null() || (runtime.is_string() && resource::isResourceUuid(Y::text(runtime))),
        "EDITOR-SCENE-V4-RUNTIME-IDENTITY");
    const auto& objects = Y::sequence(node, "objects");
    require(objects.size() <= Limits::maxObjects, "EDITOR-SCENE-V4-OBJECT-BUDGET");
    std::size_t count = 0;
    records.push_back(readObject(Y::required(node, "root"), budget, count));
    for (const auto& object : objects)
        records.push_back(readObject(object, budget, count));
    core::pmr::Set<std::uint64_t> objectIds, componentIds;
    const auto selected = identity(Y::required(node, "selected"));
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& object = records[i].object;
        require(objectIds.insert(object.id.value()).second
                && componentIds.insert(object.transformId.value()).second,
            "EDITOR-SCENE-V4-DUPLICATE-IDENTITY");
        if (!i)
            require(
                !object.parent.valid() && object.childIndex == 0, "EDITOR-SCENE-V4-ROOT-IDENTITY");
        else {
            require(objectIds.count(object.parent.value()) && object.parent != object.id,
                "EDITOR-SCENE-V4-PARENT-ORDER");
            const auto siblings =
                std::count_if(records.begin() + 1, records.begin() + i, [&](const auto& record) {
                    return record.object.parent == object.parent;
                });
            require(object.childIndex == std::size_t(siblings), "EDITOR-SCENE-V4-SIBLING-ORDER");
        }
        for (const auto& component : object.components) {
            require(componentIds.insert(component.id.value()).second,
                "EDITOR-SCENE-V4-DUPLICATE-IDENTITY");
            if (!component.script)
                continue;
            const auto& state = *component.script;
            require(resource::isResourceUuid(state.identity.scriptResourceId)
                    && state.identity.moduleSchemaVersion && !state.identity.scriptType.empty()
                    && !state.moduleId.empty()
                    && core::FilePath::isSafeRelativePath(state.sourcePath)
                    && Limits::isContentHash(state.sourceFingerprint),
                "EDITOR-SCENE-V4-SCRIPT-IDENTITY");
        }
    }
    require(objectIds.count(selected), "EDITOR-SCENE-V4-SELECTION");
    for (const auto& record : records)
        for (const auto& component : record.object.components) {
            if (!component.script)
                continue;
            for (const auto& reference : component.script->references) {
                const auto found =
                    std::find_if(records.begin(), records.end(), [&](const auto& owner) {
                        return owner.object.id == reference.owner;
                    });
                require(found != records.end(), "EDITOR-SCENE-V4-REFERENCE-OWNER");
                require(!reference.component.valid()
                        || reference.component == found->object.transformId
                        || std::any_of(found->object.components.begin(),
                            found->object.components.end(),
                            [&](const auto& target) {
                                return target.id == reference.component;
                            }),
                    "EDITOR-SCENE-V4-REFERENCE-COMPONENT");
            }
        }
}

void applyObject(scene::GameObject& object, const Presence::Object& state)
{
    object.setName(state.name);
    object.setActive(state.active);
    object.set_local_transform(state.transform);
    object.transform().setEnabled(state.transformEnabled);
}

void readNativeFields(EditorSceneDocument& document,
    const Transaction& transaction,
    Presence::Component& snapshot,
    const Node& encoded,
    scene::Component& component)
{
    const auto& properties = Y::required(encoded, "properties");
    require(properties.is_mapping() && properties.size() <= Limits::maxPropertiesPerComponent,
        "EDITOR-SCENE-V4-NATIVE-PROPERTIES");
    const auto schema = EditorSceneDocument::componentProperties(component);
    std::size_t expected = 0;
    for (const auto& field : schema)
        expected += field.serializable;
    require(expected == properties.size(), "EDITOR-SCENE-V4-NATIVE-FIELDS-INCOMPLETE");
    const EditorScenePropertyCodec::ObjectResolver resolve = [&transaction](std::uint64_t id) {
        const auto* object = transaction.resolveObject(scene::GameObjectId::fromValue(id));
        return object && !object->pendingDestroy() ? id : 0;
    };
    for (const auto& [name, value] : properties.get_value_ref<const Node::mapping_type&>()) {
        const auto text = Y::text(name);
        const auto found = std::find_if(schema.begin(), schema.end(), [&](const auto& field) {
            return field.name == text && field.serializable && field.editable;
        });
        require(found != schema.end(), "EDITOR-SCENE-V4-NATIVE-FIELD-UNKNOWN");
        SceneInspectorProperty parsed;
        auto* resources =
            document.resourceManagerProvider() ? document.resourceManagerProvider()() : nullptr;
        require(EditorScenePropertyCodec::decode(
                    *found, value, parsed, &Limits::isResourceReady, resources, resolve),
            "EDITOR-SCENE-V4-NATIVE-FIELD");
        snapshot.properties.push_back(std::move(parsed));
    }
}
} // namespace

static bool encodeSnapshot(const EditorSceneDocument& document,
    const EditorSceneScriptInputs& input,
    core::pmr::String& yaml,
    core::pmr::String& diagnostic,
    SourceVerification sources = SourceVerification::ColdAdmission)
{
    diagnostic.clear();
    try {
        require(input.preparationBytes, "EDITOR-SCENE-V4-PREPARATION-BUDGET");
        core::CpuMemoryQuotaScope quota(
            core::makeShared<core::CpuMemoryQuota>(input.preparationBytes));
        const auto catalog = document.scriptCatalog();
        require(bool(catalog), "EDITOR-SCENE-V4-DOMAIN-UNAVAILABLE");
        verifyProject(*catalog, input, sources);
        const auto entries = document.hierarchyEntries();
        require(!entries.empty() && entries.size() - 1 <= Limits::maxObjects,
            "EDITOR-SCENE-V4-OBJECT-BUDGET");
        Node root = Node::mapping();
        root["type"] = "arp.editor-scene";
        root["contractVersion"] = EditorSceneScriptCodec::contractVersion;
        root["runtimeSceneId"] =
            document.runtimeSceneId().empty() ? Node{} : Node(document.runtimeSceneId());
        root["selected"] = document.selectedObject().value();
        require(
            document.scene().findObject(document.selectedObject()), "EDITOR-SCENE-V4-SELECTION");
        std::size_t count = 0;
        root["root"] = writeObject(document, document.scene(), input, 0, count);
        root["objects"] = Node::sequence();
        for (std::size_t i = 1; i < entries.size(); ++i) {
            const auto* object = document.scene().findObject(entries[i].objectId);
            const auto* parent = object ? document.scene().findObject(object->parentId()) : nullptr;
            require(parent && object, "EDITOR-SCENE-V4-HIERARCHY");
            const auto child =
                std::find(parent->childIds().begin(), parent->childIds().end(), object->id());
            require(child != parent->childIds().end(), "EDITOR-SCENE-V4-HIERARCHY");
            root["objects"].get_value_ref<Node::sequence_type&>().push_back(writeObject(
                document, *object, input, std::size_t(child - parent->childIds().begin()), count));
        }
        auto result = Y::serialize(root);
        require(result.size() <= Limits::maxDocumentBytes, "EDITOR-SCENE-V4-DOCUMENT-BUDGET");
        yaml = std::move(result);
        return true;
    } catch (const core::YamlContractError& error) {
        diagnostic = error.diagnostic;
        return false;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        return false;
    }
}

bool EditorSceneScriptCodec::encode(const EditorSceneDocument& document,
    const EditorSceneScriptInputs& input,
    core::pmr::String& yaml,
    core::pmr::String& diagnostic)
{
    core::pmr::String snapshot;
    if (!encodeSnapshot(document, input, snapshot, diagnostic))
        return false;
    core::UniquePtr<EditorSceneDocument> probe;
    if (!prepare(snapshot, document, input, probe, diagnostic))
        return false;
    core::pmr::String restored;
    if (!encodeSnapshot(*probe, input, restored, diagnostic))
        return false;
    if (snapshot != restored) {
        diagnostic = "EDITOR-SCENE-V4-ROUNDTRIP-MISMATCH";
        return false;
    }
    yaml = std::move(snapshot);
    return true;
}

static bool snapshotHash(const EditorSceneDocument& document,
    const EditorSceneScriptInputs& input,
    core::pmr::String& hash,
    core::pmr::String& diagnostic,
    SourceVerification sources)
{
    core::pmr::String yaml;
    if (!encodeSnapshot(document, input, yaml, diagnostic, sources))
        return false;
    try {
        auto node = Y::parseSingleDocument(yaml);
        node["selected"] = 0;
        auto result = core::Sha256::hexDigest(Y::serialize(node));
        hash = std::move(result);
        return true;
    } catch (const core::YamlContractError& error) {
        diagnostic = error.diagnostic;
        return false;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        return false;
    }
}

bool EditorSceneScriptCodec::contentHash(const EditorSceneDocument& document,
    const EditorSceneScriptInputs& input,
    core::pmr::String& hash,
    core::pmr::String& diagnostic)
{
    return snapshotHash(document, input, hash, diagnostic, SourceVerification::ColdAdmission);
}

bool EditorSceneScriptCodec::observeContentHash(const EditorSceneDocument& document,
    const EditorSceneScriptInputs& input,
    core::pmr::String& hash,
    core::pmr::String& diagnostic)
{
    return snapshotHash(document, input, hash, diagnostic, SourceVerification::RetainedObservation);
}

bool EditorSceneScriptCodec::observeSnapshot(const EditorSceneDocument& document,
    const EditorSceneScriptInputs& input,
    core::pmr::String& yaml,
    core::pmr::String& diagnostic)
{
    return encodeSnapshot(
        document, input, yaml, diagnostic, SourceVerification::RetainedObservation);
}

bool EditorSceneScriptCodec::validateStructure(std::string_view yaml,
    EditorScriptArchiveBudget budget,
    std::uint64_t preparationBytes,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    try {
        require(preparationBytes && budget.preparationBytes && budget.maxYamlBytes
                && budget.maxReferences,
            "EDITOR-SCENE-V4-PREPARATION-BUDGET");
        core::CpuMemoryQuotaScope scope(core::makeShared<core::CpuMemoryQuota>(preparationBytes));
        require(!yaml.empty() && yaml.size() <= Limits::maxDocumentBytes,
            "EDITOR-SCENE-V4-DOCUMENT-BUDGET");
        const auto node = Y::parseSingleDocument(yaml);
        core::pmr::Vector<ObjectRecord> records;
        readWireRecords(node, budget, records);
        return true;
    } catch (const core::YamlContractError& error) {
        diagnostic = error.diagnostic;
    } catch (const std::exception& error) {
        diagnostic = error.what();
    }
    return false;
}

bool EditorSceneScriptCodec::prepare(std::string_view yaml,
    const EditorSceneDocument& destination,
    const EditorSceneScriptInputs& input,
    core::UniquePtr<EditorSceneDocument>& output,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    try {
        require(input.preparationBytes, "EDITOR-SCENE-V4-PREPARATION-BUDGET");
        core::CpuMemoryQuotaScope quota(
            core::makeShared<core::CpuMemoryQuota>(input.preparationBytes));
        require(!yaml.empty() && yaml.size() <= Limits::maxDocumentBytes,
            "EDITOR-SCENE-V4-DOCUMENT-BUDGET");
        const auto node = Y::parseSingleDocument(yaml);
        core::pmr::Vector<ObjectRecord> records;
        readWireRecords(node, input.fields, records);
        std::size_t count = records.size();
        for (const auto& record : records)
            count += record.object.components.size();
        auto candidate = core::makeUnique<EditorSceneDocument>(false);
        candidate->copySceneServicesFrom(destination);
        const auto& root = records.front().object;
        require(root.id == candidate->scene().rootId()
                && root.transformId == candidate->scene().transform().id() && !root.parent.valid()
                && root.childIndex == 0,
            "EDITOR-SCENE-V4-ROOT-IDENTITY");
        const auto& runtimeId = Y::required(node, "runtimeSceneId");
        require(runtimeId.is_null()
                || (runtimeId.is_string() && !Y::text(runtimeId).empty()
                    && candidate->setRuntimeSceneId(Y::text(runtimeId))),
            "EDITOR-SCENE-V4-RUNTIME-IDENTITY");
        const bool scriptsPrepared = input.sources
            ? candidate->prepareScriptAuthoringSources(
                  *input.sources, input.project, input.modules, diagnostic)
            : candidate->prepareScriptAuthoring(
                  input.projectRoot, input.project, input.modules, diagnostic);
        if (!scriptsPrepared)
            return false;
        applyObject(candidate->scene(), root);
        {
            Transaction* scope = nullptr;
            auto factory = candidate->identityRestorationFactory(
                [&records, &scope](
                    scene::GameObjectId owner, std::string_view type) -> scene::Component* {
                    if (!scope)
                        return nullptr;
                    for (const auto& record : records)
                        if (record.object.id == owner)
                            for (const auto& component : record.object.components)
                                if (!component.script && component.type == type)
                                    return scope->resolveComponent(owner, component.id);
                    return nullptr;
                });
            Transaction transaction(
                candidate->scene(), {records.size() - 1, count - 1}, std::move(factory));
            scope = &transaction;
            for (std::size_t i = 1; i < records.size(); ++i) {
                const auto& state = records[i].object;
                auto& object = transaction.restoreObject(
                    state.id, state.transformId, state.name, state.parent, state.childIndex);
                applyObject(object, state);
            }
            EditorScriptComponentArchive scriptFields(*candidate, input.fields);
            for (const auto& record : records)
                for (const auto& component : record.object.components) {
                    if (component.script)
                        scriptFields.stage(transaction,
                            record.object.id,
                            component.id,
                            component.index,
                            *component.script);
                    else
                        Presence::stageComponent(
                            *candidate, transaction, record.object.id, component);
                }
            for (auto& record : records) {
                const auto& encoded = Y::sequence(*record.node, "components");
                for (auto& component : record.object.components) {
                    auto* actual = transaction.resolveComponent(record.object.id, component.id);
                    if (component.script) {
                        scriptFields.apply(*actual, *component.script, transaction);
                        continue;
                    }
                    readNativeFields(
                        *candidate, transaction, component, encoded[component.index - 1], *actual);
                }
            }
            transaction.prepare();
            transaction.commit();
            // This is still an isolated, unpublished document. Native authoring
            // writers may require real owner siblings (notably the root's
            // Collider/Body pair); attach all cold components before applying
            // native fields. Any failure destroys the entire document candidate.
            for (const auto& record : records)
                for (const auto& component : record.object.components)
                    if (!component.script)
                        Presence::applyComponent(
                            *transaction.resolveComponent(record.object.id, component.id),
                            component,
                            *candidate,
                            transaction);
        }
        require(candidate->select(
                    scene::GameObjectId::fromValue(identity(Y::required(node, "selected")))),
            "EDITOR-SCENE-V4-SELECTION");
        // Verify again after preparation. No source IO is deferred to publication.
        verifyProject(*candidate->scriptCatalog(), input);
        output = std::move(candidate);
        return true;
    } catch (const core::YamlContractError& error) {
        diagnostic = error.diagnostic;
        return false;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        return false;
    }
}

bool EditorSceneScriptCodec::decode(std::string_view yaml,
    EditorSceneDocument& destination,
    const EditorSceneScriptInputs& input,
    core::pmr::String& diagnostic)
{
    core::UniquePtr<EditorSceneDocument> candidate;
    if (!prepare(yaml, destination, input, candidate, diagnostic))
        return false;
    auto token = destination.prepareReplacement(*candidate, diagnostic);
    if (!token)
        return false;
    if (destination.commitReplacement(*token, *candidate)
        != EditorSceneDocument::ReplacementResult::Committed) {
        diagnostic = "EDITOR-SCENE-V4-REPLACEMENT";
        return false;
    }
    return true;
}
} // namespace arp::editor
