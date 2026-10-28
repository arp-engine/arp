#include "editor/tests/fixtures/editor_script_document_fixture.h"
#include "editor/workspace/editor_scene_presence.h"
#include "editor/workspace/editor_script_component_archive.h"
#include "editor/workspace/editor_script_component_form.h"
#include "reflection/instance.h"

#include <stdexcept>

namespace {
using namespace arp;
using namespace arp::editor;
using namespace arp::editor::testfixtures;
using Transaction = scene::Scene::IdentityRestoration;
using Presence = EditorScenePresence;
using State = EditorScriptComponentState;
using Y = core::YamlContract;
// clang-format off
constexpr const char* primary = R"ARP(from arp_scene import GameObject, Component, main_scene


# Every field is explicit; neither constructor nor initializer may execute.
class Mover(ScriptComponent):
    amount: int
    speed: float
    visible: bool
    labels: [string]
    targets: [GameObject]
    target: GameObject
    native: Component
    optional: GameObject?
    peer: Mover?

    def __init__(self):
        main_scene.set_object_name("ConstructorExecuted")


class LightComponent(ScriptComponent):
    message: string


class Marker(ScriptComponent):
    def marker(self) -> bool:
        return true


main_scene.set_object_name("InitializerExecuted")
)ARP";
constexpr const char* secondary = R"ARP(from arp_scene import main_scene


# Same name, independent module/schema and fields.
class Mover(ScriptComponent):
    message: string


# This spelling must consume a script receiver, never a native control.
class Component(ScriptComponent):
    peer: Component?


main_scene.set_object_name("InitializerExecuted")
)ARP";
// clang-format on

template <class F> void rejects(std::string_view diagnostic, F&& action)
{
    bool rejected = false;
    try {
        action();
    } catch (const core::YamlContractError& error) {
        rejected = std::string_view(error.diagnostic).find(diagnostic) != std::string_view::npos;
    } catch (const std::exception& error) {
        rejected = std::string_view(error.what()).find(diagnostic) != std::string_view::npos;
        if (!rejected)
            std::fprintf(stderr,
                "expected %.*s, got %s\n",
                int(diagnostic.size()),
                diagnostic.data(),
                error.what());
    }
    check(rejected, "expected rejection");
}

EditorScriptArchiveBudget budget()
{
    return {256 * 1024,
        64,
        4 * 1024 * 1024,
        {reflection::SerializationProfile::Authoring, 16, 1024, 64}};
}

EditorScriptComponentArchive archive(const EditorSceneDocument& document)
{
    return EditorScriptComponentArchive(document, budget());
}

scene::Component& stage(EditorSceneDocument& document,
    Transaction& transaction,
    scene::GameObjectId owner,
    const char* resource = primaryId,
    const char* type = "Mover")
{
    const auto catalog = document.scriptCatalog();
    auto* module = catalog->modules().find({resource, type, 1});
    check(module != nullptr);
    const auto* object = transaction.resolveObject(owner);
    check(object != nullptr);
    return transaction.createScriptComponent(
        owner, *module, {resource, type, 1}, object->components().size());
}

void set(const scene::Component& component, std::string_view field, const reflection::object& value)
{
    component.scriptInstance().ptr<reflection::instance>()->slot(field) = value;
}

struct Graph {
    scene::GameObjectId root, a, b, c;
    scene::ComponentId first, second, third;
};

Graph createGraph(EditorSceneDocument& document)
{
    Graph graph;
    graph.root = document.createObject("script subtree");
    graph.a = document.createObject("first");
    check(document.reparentObject(graph.a, graph.root, 0));
    graph.b = document.createObject("second");
    check(document.reparentObject(graph.b, graph.root, 1));
    graph.c = document.createObject("same name from another module");
    check(document.reparentObject(graph.c, graph.root, 2));
    Transaction transaction(document.scene(), {0, 3});
    auto& a = stage(document, transaction, graph.a);
    auto& b = stage(document, transaction, graph.b);
    auto& c = stage(document, transaction, graph.c, secondaryId);
    graph.first = a.id();
    graph.second = b.id();
    graph.third = c.id();
    for (auto* item : {&a, &b}) {
        set(*item, "amount", reflection::object(7));
        set(*item, "speed", reflection::object(2.5));
        set(*item, "visible", reflection::object(true));
        reflection::hlist labels;
        labels.push_back(reflection::object(core::pmr::String("one")));
        labels.push_back(reflection::object(core::pmr::String("中文\nquote: \"x\"")));
        set(*item, "labels", reflection::object(std::move(labels)));
        reflection::hlist targets;
        targets.push_back(document.scene().nativeReflectionObject());
        targets.push_back(item->gameObject()->nativeReflectionObject());
        set(*item, "targets", reflection::object(std::move(targets)));
        set(*item, "target", document.scene().nativeReflectionObject());
        set(*item, "native", c.nativeReflectionObject());
        set(*item,
            "optional",
            item == &a ? reflection::object{} : document.scene().nativeReflectionObject());
        set(*item, "peer", (item == &a ? b : a).scriptInstance());
    }
    b.setEnabled(false);
    set(c, "message", reflection::object(core::pmr::String("other module")));
    // Capture and round-trip an unpublished component on a live owner. Both
    // targets in peer/native are also unpublished in this same transaction.
    const auto state = archive(document).capture(a, &transaction);
    check(EditorScriptComponentArchive::equal(
        archive(document).prepare(state.identity, state.enabled, state.fields, &transaction),
        state));
    archive(document).apply(a, state, transaction);
    transaction.prepare();
    const auto before = core::defaultCpuAllocator().stats();
    transaction.commit();
    check(before.allocationCount == core::defaultCpuAllocator().stats().allocationCount);
    check(document.scene().name() != "InitializerExecuted"
        && document.scene().name() != "ConstructorExecuted");
    return graph;
}

void prepare(Fixture& files, EditorSceneDocument& document)
{
    core::pmr::String diagnostic;
    check(document.bindServiceSession(Fixture::services(), diagnostic), diagnostic);
    auto bindingBudget = Fixture::budget();
    // This fixture declares three primary and two secondary component types.
    bindingBudget.catalog.maxEntries = 5;
    check(document.prepareScriptAuthoring(files.root, files.manifest, bindingBudget, diagnostic),
        diagnostic);
}

void identitiesAndReferences(Fixture& files)
{
    EditorSceneDocument document(false);
    prepare(files, document);
    const auto graph = createGraph(document);
    auto* first = document.scene().findObject(graph.a)->findComponent(graph.first);
    auto* second = document.scene().findObject(graph.b)->findComponent(graph.second);
    const auto state = archive(document).capture(*first);
    check(state.references.size() == 5 && !state.fields.empty());
    check(state.moduleId == "primary" && state.sourcePath == "primary.arp");
    check(EditorScriptComponentArchive::storageBytes(state) > state.fields.size());
    Presence::validateRestoration(
        document, graph.a, Presence::captureComponent(document, *first, 1));
    check(archive(document).equal(archive(document).capture(*first), state));
    const auto epoch = document.ownerTicket();
    rejects("EDITOR-SCENE-HISTORY-FACTORY-UNSUPPORTED", [&] {
        Presence::captureComponent(*first, 1);
    });
    rejects("EDITOR-SCENE-HISTORY-FACTORY-UNSUPPORTED", [&] {
        Presence::captureSubtree(document, graph.root);
    });
    rejects("EDITOR-SCENE-HISTORY-COMPONENT-REFERENCED", [&] {
        Presence::validateRemoval(document, *first);
    });
    {
        Transaction transaction(document.scene(), {0, 1});
        rejects("EDITOR-SCRIPT-ARCHIVE-LIVE-DESTINATION", [&] {
            archive(document).apply(*first, state, transaction);
        });
    }
    const auto snapshot =
        Presence::captureSubtree(document, graph.root, Presence::ScriptPolicy::Archive);
    check(snapshot.cost.references == 11 && snapshot.cost.components == 7);
    const auto oldObject = first->gameObject()->nativeReflectionObject();
    const auto oldNative = first->nativeReflectionObject();
    const auto oldReceiver = first->scriptInstance();
    const auto otherReceiver = second->scriptInstance();
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        Presence::destroySubtree(document, snapshot);
        check(!document.scene().findObject(graph.root));
        check(!oldNative.native_reference_alive() && !oldObject.native_reference_alive());
        check(!oldReceiver.ptr<reflection::instance>()->alive()
            && !otherReceiver.ptr<reflection::instance>()->alive());
        for (const auto& module : document.scriptCatalog()->modules().entries())
            check(module.module->liveInstances() == 0);
        Presence::restoreSubtree(document, snapshot);
        check(Presence::matchesSubtree(document, snapshot));
        const auto* restored = document.scene().findObject(graph.a)->findComponent(graph.first);
        check(restored->scriptInstance().ptr<reflection::instance>()
            != oldReceiver.ptr<reflection::instance>());
        check(!document.scene().findObject(graph.b)->findComponent(graph.second)->enabled());
    }
    check(epoch.sameCurrentEpoch(
        document.ownerTicket())); // Presence does not publish Editor history/epochs itself.
}

void componentAndCollision(Fixture& files)
{
    EditorSceneDocument document(false);
    prepare(files, document);
    for (const auto& identity : {scriptvm::ScriptComponentIdentity{primaryId, "LightComponent", 1},
             scriptvm::ScriptComponentIdentity{secondaryId, "Component", 1}}) {
        const auto owner = document.createObject("component restore");
        scene::ComponentId id;
        {
            Transaction transaction(document.scene(), {0, 1});
            auto& component = stage(document,
                transaction,
                owner,
                identity.scriptResourceId.c_str(),
                identity.scriptType.c_str());
            id = component.id();
            if (identity.scriptType == "Component")
                set(component, "peer", component.scriptInstance());
            else
                set(component,
                    "message",
                    reflection::object(core::pmr::String("native-name collision")));
            transaction.prepare();
            transaction.commit();
        }
        const auto* original = document.scene().findObject(owner)->findComponent(id);
        const auto old = original->scriptInstance();
        auto snapshot = Presence::captureComponent(document, *original, 1);
        Presence::validateRemoval(document, *original);
        Presence::validateRestoration(document, owner, snapshot);
        check(document.scene().findObject(owner)->removeComponent(id));
        {
            Transaction transaction(document.scene(), {0, 1});
            auto& candidate = Presence::stageComponent(document, transaction, owner, snapshot);
            Presence::applyComponent(candidate, snapshot, document, transaction);
            transaction.prepare();
            transaction.commit();
        }
        check(!old.ptr<reflection::instance>()->alive());
        check(Presence::matchesComponent(
            document, *document.scene().findObject(owner)->findComponent(id), snapshot));
    }
}

void failures(Fixture& files)
{
    EditorSceneDocument document(false), foreign(false);
    prepare(files, document);
    const auto graph = createGraph(document);
    auto* first = document.scene().findObject(graph.a)->findComponent(graph.first);
    auto* second = document.scene().findObject(graph.b)->findComponent(graph.second);
    const auto state = archive(document).capture(*first);
    const auto ticket = document.ownerTicket();
    const auto liveCount =
        document.scriptCatalog()->modules().find(state.identity)->liveInstances();
    const auto badRestore = [&](State bad, std::string_view diagnostic) {
        rejects(diagnostic, [&] {
            Transaction transaction(document.scene(), {0, 1});
            auto& candidate = archive(document).stage(transaction, graph.a, {}, 2, bad, true);
            archive(document).apply(candidate, bad, transaction);
        });
        check(ticket.sameCurrentEpoch(document.ownerTicket()));
        check(
            document.scriptCatalog()->modules().find(state.identity)->liveInstances() == liveCount);
        check(EditorScriptComponentArchive::equal(archive(document).capture(*first), state));
    };
    auto bad = state;
    bad.moduleId = "renamed";
    badRestore(bad, "SOURCE-OR-IMPORT-CONFLICT");
    bad = state;
    bad.sourcePath = "renamed.arp";
    badRestore(bad, "SOURCE-OR-IMPORT-CONFLICT");
    bad = state;
    bad.sourceFingerprint = "changed";
    badRestore(bad, "SOURCE-OR-IMPORT-CONFLICT");
    bad = state;
    bad.imports.front().version++;
    badRestore(bad, "SOURCE-OR-IMPORT-CONFLICT");
    bad = state;
    bad.imports.front().exportContractHash = "changed";
    badRestore(bad, "SOURCE-OR-IMPORT-CONFLICT");
    bad = state;
    bad.imports.front().reflectionRegistryHash = "changed";
    badRestore(bad, "SOURCE-OR-IMPORT-CONFLICT");
    bad = state;
    bad.identity.moduleSchemaVersion++;
    badRestore(bad, "SCHEMA");
    bad = state;
    bad.references.clear();
    badRestore(bad, "REFERENCE-RECORD-MISMATCH");
    bad = state;
    auto fields = Y::parseSingleDocument(bad.fields);
    fields["target"]["owner"] = std::uint64_t(999999);
    bad.fields = Y::serialize(fields);
    badRestore(bad, "REFERENCE-OWNER");
    fields = Y::parseSingleDocument(state.fields);
    fields["native"]["component"] = graph.first.value();
    bad = state;
    bad.fields = Y::serialize(fields);
    badRestore(bad, "REFERENCE-COMPONENT-OR-TYPE");
    fields = Y::parseSingleDocument(state.fields);
    fields["peer"]["owner"] = graph.c.value();
    fields["peer"]["component"] = graph.third.value();
    bad = state;
    bad.fields = Y::serialize(fields);
    badRestore(bad, "REFERENCE-COMPONENT-OR-TYPE");
    fields = Y::parseSingleDocument(state.fields);
    fields["amount"] = "wrong";
    bad = state;
    bad.fields = Y::serialize(fields);
    badRestore(bad, "FIELD-READ");
    fields = Y::parseSingleDocument(state.fields);
    fields["unknown"] = 1;
    bad = state;
    bad.fields = Y::serialize(fields);
    badRestore(bad, "FIELD-READ");
    // An allocation failure after staging must retire the isolated receiver too.
    {
        auto limits = budget();
        limits.preparationBytes = 1;
        const auto before = archive(document).capture(*first);
        rejects("", [&] {
            Transaction transaction(document.scene(), {0, 1});
            auto& candidate = archive(document).stage(transaction, graph.a, {}, 2, state, true);
            EditorScriptComponentArchive(document, limits).apply(candidate, state, transaction);
        });
        check(
            document.scriptCatalog()->modules().find(state.identity)->liveInstances() == liveCount);
        check(EditorScriptComponentArchive::equal(archive(document).capture(*first), before));
    }
    // Wrong Scene, expired controls and wrong receiver kinds never fall back by ID/name.
    set(*first, "target", foreign.scene().nativeReflectionObject());
    rejects("REFERENCE-EXTERNAL-DANGLING-OR-TYPE", [&] {
        archive(document).capture(*first);
    });
    set(*first, "target", document.scene().nativeReflectionObject());
    set(*first, "peer", second->nativeReflectionObject());
    rejects("REFERENCE-EXTERNAL-DANGLING-OR-TYPE", [&] {
        archive(document).capture(*first);
    });
    set(*first, "peer", second->scriptInstance());
    auto limits = budget();
    limits.maxReferences = 1;
    rejects("REFERENCE-BUDGET", [&] {
        EditorScriptComponentArchive(document, limits).capture(*first);
    });
    limits = budget();
    limits.maxYamlBytes = 1;
    rejects("STATE-BUDGET", [&] {
        EditorScriptComponentArchive(document, limits).capture(*first);
    });
    limits = budget();
    limits.preparationBytes = 1;
    rejects("", [&] {
        EditorScriptComponentArchive(document, limits).capture(*first);
    });
    limits = budget();
    limits.fields.maxValues = 1;
    rejects("FIELD-WRITE", [&] {
        EditorScriptComponentArchive(document, limits).capture(*first);
    });
    limits = budget();
    limits.fields.maxCollectionLength = 1;
    rejects("FIELD-WRITE", [&] {
        EditorScriptComponentArchive(document, limits).capture(*first);
    });
    // An external incoming reference cannot be cleared atomically yet. Refuse
    // capture before mutation; an outgoing reference to root remains supported.
    // Native base fields can also reference the mandatory Transform component;
    // pointer equality and reflected inheritance, not its spelling, establish type.
    set(*first, "native", first->gameObject()->transform().nativeReflectionObject());
    const auto nativeState = archive(document).capture(*first);
    check(nativeState.references.size() == state.references.size());
    set(*first,
        "native",
        document.scene().findObject(graph.c)->findComponent(graph.third)->nativeReflectionObject());
    const auto external = document.createObject("outside subtree");
    {
        Transaction transaction(document.scene(), {0, 1});
        auto& link = stage(document, transaction, external, secondaryId, "Component");
        // Base native references in the primary schema detect exact native IDs.
        set(link, "peer", {});
        transaction.prepare();
        transaction.commit();
    }
    const auto externalPrimary = document.createObject("outside primary");
    {
        Transaction transaction(document.scene(), {0, 1});
        auto& link = stage(document, transaction, externalPrimary);
        auto receiver = link.scriptInstance().ptr<reflection::instance>();
        for (const auto& field : first->scriptInstance().ptr<reflection::instance>()->slots())
            if (field.first != "component" && field.first != "game_object")
                receiver->slot(field.first) = field.second;
        transaction.prepare();
        transaction.commit();
    }
    rejects("EXTERNAL-SCRIPT-REFERENCE-UNSUPPORTED", [&] {
        Presence::captureSubtree(document, graph.root, Presence::ScriptPolicy::Archive);
    });
    check(document.scene().findObject(graph.root) != nullptr);
    check(EditorScriptComponentArchive::equal(archive(document).capture(*first), state));
}

void retainedHistory(Fixture& files)
{
    core::SharedPtr<const EditorScriptComponentCatalog> retired;
    std::optional<State> state;
    reflection::object oldReceiver;
    {
        EditorSceneDocument document(false);
        prepare(files, document);
        const auto graph = createGraph(document);
        const auto* first = document.scene().findObject(graph.a)->findComponent(graph.first);
        state = archive(document).capture(*first);
        retired = document.scriptCatalog();
        oldReceiver = first->scriptInstance();
    }
    // Retaining a plain history value never retains a live script instance.
    // The old catalog is kept only to inspect counters, never to execute code.
    check(state && !state->fields.empty());
    check(!oldReceiver.ptr<reflection::instance>()->alive());
    for (const auto& module : retired->modules().entries())
        check(module.module->liveInstances() == 0);
}

void liveFieldPreparation(Fixture& files)
{
    core::SharedPtr<const EditorScriptComponentCatalog> retired;
    reflection::object retiredReceiver;
    {
        EditorSceneDocument document(false);
        // Both primary instance slots will be occupied. Field preparation must
        // neither allocate another receiver lease nor replace existing components.
        const auto originalLimit = files.manifest.scriptModules.front().authoring->maxInstances;
        files.manifest.scriptModules.front().authoring->maxInstances = 2;
        prepare(files, document);
        retired = document.scriptCatalog();
        files.manifest.scriptModules.front().authoring->maxInstances = originalLimit;
        const auto graph = createGraph(document);
        auto* firstComponent = document.scene().findObject(graph.a)->findComponent(graph.first);
        check(firstComponent != nullptr);
        auto& first = *firstComponent;
        const auto before = archive(document).capture(first);
        auto* module = document.scriptCatalog()->modules().find(before.identity);
        check(module->liveInstances() == 2);
        const auto receiver = first.scriptInstance();
        retiredReceiver = receiver;
        const auto native = first.nativeReflectionObject();
        const auto owner = first.gameObject()->nativeReflectionObject();
        const auto builtinComponent = receiver.ptr<reflection::instance>()->slot("component");
        const auto builtinOwner = receiver.ptr<reflection::instance>()->slot("game_object");
        const auto other = archive(document).capture(
            *document.scene().findObject(graph.b)->findComponent(graph.second));
        const auto ticket = document.ownerTicket();
        const auto unchanged = [&]() {
            check(EditorScriptComponentArchive::equal(archive(document).capture(first), before));
            check(first.scriptInstance() == receiver && first.nativeReflectionObject() == native
                && first.gameObject()->nativeReflectionObject() == owner
                && receiver.ptr<reflection::instance>()->slot("component") == builtinComponent
                && receiver.ptr<reflection::instance>()->slot("game_object") == builtinOwner
                && module->liveInstances() == 2 && ticket.sameCurrentEpoch(document.ownerTicket()));
            check(EditorScriptComponentArchive::equal(
                archive(document).capture(
                    *document.scene().findObject(graph.b)->findComponent(graph.second)),
                other));
        };
        check(EditorScriptComponentArchive::equal(
            archive(document).prepare(before.identity, before.enabled, before.fields), before));
        check(!archive(document).replaceFields(first, before, before));
        unchanged();

        auto fields = Y::parseSingleDocument(before.fields);
        fields["amount"] = 42;
        fields["speed"] = 4.25;
        fields["visible"] = false;
        fields["labels"] = Y::Node::sequence();
        fields["labels"].get_value_ref<Y::Node::sequence_type&>().emplace_back("new 中文");
        fields["optional"] = fields["target"];
        fields["peer"]["owner"] = graph.a.value();
        fields["peer"]["component"] = graph.first.value();
        const auto after = archive(document).prepare(before.identity, false, Y::serialize(fields));
        unchanged();
        const auto rejectedReplacement = [&](State bad, std::string_view diagnostic) {
            rejects(diagnostic, [&] {
                archive(document).replaceFields(first, before, bad);
            });
            unchanged();
        };
        auto bad = after;
        bad.references.clear();
        rejectedReplacement(bad, "ROUNDTRIP-MISMATCH");
        bad = after;
        bad.sourceFingerprint = "changed";
        rejectedReplacement(bad, "SOURCE-OR-IMPORT-CONFLICT");
        auto invalid = fields;
        invalid["amount"] = "wrong";
        bad = after;
        bad.fields = Y::serialize(invalid);
        rejectedReplacement(bad, "FIELD-READ");
        invalid = fields;
        invalid["unknown"] = 1;
        bad.fields = Y::serialize(invalid);
        rejectedReplacement(bad, "FIELD-READ");
        invalid = fields;
        invalid.get_value_ref<Y::Node::mapping_type&>().erase("amount");
        bad.fields = Y::serialize(invalid);
        rejectedReplacement(bad, "FIELD-READ");
        invalid = fields;
        invalid["target"]["owner"] = std::uint64_t(999999);
        bad.fields = Y::serialize(invalid);
        rejectedReplacement(bad, "REFERENCE-OWNER");
        invalid = fields;
        invalid["peer"]["owner"] = graph.c.value();
        invalid["peer"]["component"] = graph.third.value();
        bad.fields = Y::serialize(invalid);
        rejectedReplacement(bad, "REFERENCE-COMPONENT-OR-TYPE");
        {
            auto limits = budget();
            limits.preparationBytes = 1;
            rejects("", [&] {
                EditorScriptComponentArchive(document, limits).replaceFields(first, before, after);
            });
            unchanged();
            limits = budget();
            limits.maxReferences = 1;
            rejects("", [&] {
                EditorScriptComponentArchive(document, limits)
                    .prepare(after.identity, after.enabled, after.fields);
            });
            unchanged();
            limits = budget();
            limits.maxYamlBytes = 1;
            rejects("STATE-BUDGET", [&] {
                EditorScriptComponentArchive(document, limits)
                    .prepare(after.identity, after.enabled, after.fields);
            });
            unchanged();
        }
        check(archive(document).replaceFields(first, before, after));
        check(EditorScriptComponentArchive::equal(archive(document).capture(first), after));
        check(first.scriptInstance() == receiver && first.nativeReflectionObject() == native
            && module->liveInstances() == 2);
        check(receiver.ptr<reflection::instance>()->slot("peer") == receiver);
        rejects("FIELD-CONFLICT", [&] {
            archive(document).replaceFields(first, before, after);
        });
        check(EditorScriptComponentArchive::equal(archive(document).capture(first), after));
        check(archive(document).replaceFields(first, after, before));
        unchanged();
        check(document.scene().name() != "InitializerExecuted"
            && document.scene().name() != "ConstructorExecuted");
    }
    check(!retiredReceiver.ptr<reflection::instance>()->alive());
    for (const auto& entry : retired->modules().entries())
        check(entry.module->liveInstances() == 0);
}

void formDrafts(Fixture& files)
{
    using Form = EditorScriptComponentForm;
    using Kind = reflection::SerializationDescriptor::Kind;
    core::SharedPtr<const EditorScriptComponentCatalog> retired;
    std::optional<Form> retained;
    {
        EditorSceneDocument document(false);
        prepare(files, document);
        const auto graph = createGraph(document);
        auto& first = *document.scene().findObject(graph.a)->findComponent(graph.first);
        const auto before = archive(document).capture(first);
        const auto count =
            document.scriptCatalog()->modules().find(before.identity)->liveInstances();
        auto form = Form::create(document, before.identity, false, budget());
        check(form.displayName() == "primary / Mover" && !form.enabled() && !form.complete());
        const auto index = [&](std::string_view name) {
            for (std::size_t i = 0; i < form.size(); ++i)
                if (form.field(i).name == name)
                    return i;
            throw std::logic_error("test field missing");
        };
        for (std::size_t i = 0; i < form.size(); ++i)
            check(!form.value(i));
        check(form.field(index("labels")).type->kind == Kind::Sequence
            && form.field(index("optional")).type->kind == Kind::Optional
            && form.field(index("native")).type->kind == Kind::Reference);
        rejects("UNASSIGNED", [&] {
            form.prepare(document);
        });
        // Optional does not mean prefilled null. Every empty/zero value below
        // is a deliberate assignment and remains distinct from clear().
        form.assign(document, index("amount"), "0");
        form.assign(document, index("speed"), "0.0");
        form.assign(document, index("visible"), "false");
        form.assign(document, index("labels"), "[]");
        form.assign(document, index("targets"), "[]");
        form.assign(document, index("optional"), "null");
        form.assign(document, index("peer"), "null");
        check(!form.complete() && form.value(index("optional")));
        const auto nodes = Y::parseSingleDocument(before.fields);
        for (const char* name : {"target", "native"})
            form.assign(document, index(name), Y::serialize(nodes[name]));
        check(form.complete());
        const auto prepared = form.prepare(document);
        for (std::size_t i = 0; i < form.size(); ++i) {
            const auto value = archive(document).readFieldElement(
                before.identity, form.field(i).name, 0, *form.value(i));
            check(archive(document).writeField(before.identity, form.field(i).name, value)
                == *form.value(i));
        }
        const auto leaf = archive(document).readFieldElement(before.identity, "labels", 1, "\"\"");
        check(leaf.is<core::pmr::String>() && leaf.cast<const core::pmr::String&>().empty());
        check(archive(document).writeFieldElement(before.identity, "labels", 1, leaf) == "\"\"\n");
        rejects("FIELD-ELEMENT", [&] {
            archive(document).readFieldElement(before.identity, "amount", 1, "1");
        });
        rejects("FIELD-DEPTH", [&] {
            archive(document).readFieldElement(
                before.identity, "labels", budget().fields.maxDepth, "1");
        });
        rejects("FIELD-NAME", [&] {
            archive(document).writeField(before.identity, "unknown", reflection::object(1));
        });
        const auto fields = Y::parseSingleDocument(prepared.fields);
        check(Y::int32(fields, "amount") == 0 && !Y::boolean(fields, "visible")
            && fields["optional"].is_null() && fields["labels"].is_sequence()
            && fields["labels"].size() == 0 && !prepared.enabled);
        check(EditorScriptComponentArchive::equal(archive(document).capture(first), before));
        check(document.scriptCatalog()->modules().find(before.identity)->liveInstances() == count);
        const auto amount = *form.value(index("amount"));
        for (const char* bad : {"null", "false", "\"7\"", "2147483648", "---\n1\n---\n2"}) {
            rejects("", [&] {
                form.assign(document, index("amount"), bad);
            });
            check(*form.value(index("amount")) == amount);
        }
        rejects("", [&] {
            form.assign(document, form.size(), "1");
        });
        rejects("FIELD-NAME", [&] {
            archive(document).prepareField(before.identity, "game_object", "null");
        });
        rejects("FIELD-NAME", [&] {
            archive(document).prepareField(before.identity, "unknown", "1");
        });
        auto dangling = nodes["target"];
        dangling["owner"] = std::uint64_t(9999999);
        rejects("REFERENCE-OWNER", [&] {
            form.assign(document, index("target"), Y::serialize(dangling));
        });
        // A string spelling a reference is never accepted as an object; nor
        // may a script receiver from another module satisfy the declared peer.
        auto wrongModule = nodes["peer"];
        wrongModule["owner"] = graph.c.value();
        wrongModule["component"] = graph.third.value();
        rejects("REFERENCE-COMPONENT-OR-TYPE", [&] {
            form.assign(document, index("peer"), Y::serialize(wrongModule));
        });
        form.clear(index("optional"));
        check(!form.value(index("optional")) && !form.complete());
        rejects("UNASSIGNED", [&] {
            form.prepare(document);
        });
        form.assign(document, index("optional"), "null");
        auto copy = Form::fromState(document, before, budget());
        check(copy.complete() && copy.enabled());
        check(EditorScriptComponentArchive::equal(copy.prepare(document), before));
        copy.setEnabled(false);
        check(!copy.prepare(document).enabled);
        auto empty = Form::create(document, {primaryId, "Marker", 1}, true, budget());
        check(empty.size() == 0 && empty.complete());
        check(Y::parseSingleDocument(empty.prepare(document).fields).is_mapping());
        // Required strings preserve both the empty string and escaped lines;
        // UI support for multiline text is deliberately not inferred here.
        auto text = Form::create(document, {primaryId, "LightComponent", 1}, true, budget());
        text.assign(document, 0, "\"\"");
        check(Y::text(Y::parseSingleDocument(text.prepare(document).fields), "message").empty());
        text.assign(document, 0, "\"中文\\nquote: \\\"x\\\"\"");
        check(Y::text(Y::parseSingleDocument(text.prepare(document).fields), "message")
            == "中文\nquote: \"x\"");
        auto badState = before;
        badState.sourceFingerprint = "changed";
        rejects("STATE-CONFLICT", [&] {
            Form::fromState(document, badState, budget());
        });
        auto limits = budget();
        limits.fields.maxCollectionLength = 1;
        rejects("FIELD-BUDGET", [&] {
            Form::create(document, before.identity, true, limits);
        });
        limits = budget();
        limits.preparationBytes = 1;
        rejects("", [&] {
            Form::create(document, before.identity, true, limits);
        });
        limits = budget();
        limits.maxYamlBytes = 8;
        auto byteBounded = Form::create(document, before.identity, true, limits);
        byteBounded.assign(document, index("amount"), "1000000");
        rejects("VALUE-BUDGET", [&] {
            byteBounded.assign(document, index("speed"), "0");
        });
        check(!byteBounded.value(index("speed"))
            && *byteBounded.value(index("amount")) == "1000000\n");
        const auto preserved = *form.value(index("amount"));
        bool allocationRejected = false;
        {
            const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            core::CpuMemoryQuotaScope scope(quota);
            try {
                form.assign(document, index("amount"), "9");
            } catch (const std::bad_alloc&) {
                allocationRejected = true;
            }
        }
        check(allocationRejected && *form.value(index("amount")) == preserved);
        // Per-cell reference success cannot bypass the complete form's total
        // quota. Final preparation re-resolves every target against the Scene.
        limits = budget();
        limits.maxReferences = 1;
        auto bounded = Form::create(document, before.identity, true, limits);
        for (std::size_t i = 0; i < form.size(); ++i)
            bounded.assign(document, i, *form.value(i));
        check(bounded.complete());
        rejects("REFERENCE-BUDGET", [&] {
            bounded.prepare(document);
        });
        EditorSceneDocument other(false);
        prepare(files, other);
        rejects("OWNER-STALE", [&] {
            form.prepare(other);
        });
        rejects("OWNER-STALE", [&] {
            form.assign(other, index("amount"), "9");
        });
        check(EditorScriptComponentArchive::equal(form.prepare(document), prepared));
        check(document.scene().name() != "InitializerExecuted"
            && document.scene().name() != "ConstructorExecuted");
        retired = document.scriptCatalog();
        retained = std::move(copy);
    }
    check(retained->complete() && retained->displayName() == "primary / Mover");
    for (const auto& entry : retired->modules().entries())
        check(entry.module->liveInstances() == 0);
    EditorSceneDocument replacement(false);
    rejects("OWNER-STALE", [&] {
        retained->prepare(replacement);
    });
}

void cycle(Fixture& files)
{
    identitiesAndReferences(files);
    componentAndCollision(files);
    failures(files);
    retainedHistory(files);
    liveFieldPreparation(files);
}
} // namespace

int main(int argc, char** argv)
{
    Fixture files;
    files.write("primary.arp", primary);
    files.write("secondary.arp", secondary);
    const auto argument = argc == 2 ? std::string_view(argv[1]) : std::string_view{};
    if (argument == "--form" || argument == "--form-smoke") {
        formDrafts(files);
        if (argument == "--form-smoke") {
            const auto baseline = core::defaultCpuAllocator().stats();
            for (unsigned round = 0; round < 20; ++round) {
                formDrafts(files);
                const auto current = core::defaultCpuAllocator().stats();
                check(current.liveBytes == baseline.liveBytes);
                check(current.allocationCount - current.deallocationCount
                    == baseline.allocationCount - baseline.deallocationCount);
            }
            std::printf("ordinary_script_form_cycles=20 live_bytes_delta=0 live_count_delta=0 "
                        "retired_instances=0 peak_bytes=%llu\n",
                static_cast<unsigned long long>(core::defaultCpuAllocator().stats().peakBytes));
        }
        std::puts(
            "PASS explicit script form values, formal schemas, exact references, quotas and cold lifetime");
        return 0;
    }
    cycle(files);
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned round = 0; round < 20; ++round) {
            cycle(files);
            const auto current = core::defaultCpuAllocator().stats();
            check(current.liveBytes == baseline.liveBytes);
            check(current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        }
        std::puts(
            "PASS 20 ordinary warmed archive/delete/restore/reject/close cycles: core bytes/count delta=0; retained history: module live instances=0 after close");
        std::printf("ordinary_script_field_cycles=20 live_bytes_delta=0 live_count_delta=0 "
                    "retired_instances=0 peak_bytes=%llu\n",
            static_cast<unsigned long long>(core::defaultCpuAllocator().stats().peakBytes));
    } else
        std::puts(
            "PASS exact script history/archive, scoped mutual references, provenance, gates and atomic failures");
}
