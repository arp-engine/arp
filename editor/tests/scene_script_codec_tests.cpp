#include "2d/components.h"
#include "2d/physics_components.h"
#include "3d/components.h"
#include "editor/tests/fixtures/editor_script_document_fixture.h"
#include "editor/workspace/editor_scene_presence.h"
#include "editor/workspace/editor_scene_script_codec.h"
#include "reflection/instance.h"

#include <limits>

namespace {
using namespace arp;
using namespace arp::editor;
using namespace arp::editor::testfixtures;
using Codec = EditorSceneScriptCodec;
using Y = core::YamlContract;
using Transaction = scene::Scene::IdentityRestoration;
// clang-format off
constexpr const char* primary = R"ARP(from arp_scene import GameObject, Component, main_scene


# Explicit fields exercise exact root, component and mutual references.
class Link(ScriptComponent):
    count: int
    text: string
    target: GameObject
    native: Component
    peer: Link?
    targets: [GameObject]

    def __init__(self):
        main_scene.set_object_name("ConstructorExecuted")


def read_name() -> string:
    return main_scene.object_name


main_scene.set_object_name("InitializerExecuted")
)ARP";
constexpr const char* secondary = R"ARP(from arp_scene import main_scene


# Independent module identity despite the same class name.
class Link(ScriptComponent):
    text: string


main_scene.set_object_name("InitializerExecuted")
)ARP";
// clang-format on

EditorSceneScriptInputs inputs(const Fixture& files)
{
    return {files.root,
        files.manifest,
        Fixture::budget(),
        {256 * 1024,
            64,
            4 * 1024 * 1024,
            {reflection::SerializationProfile::Authoring, 16, 1024, 64}},
        64 * 1024 * 1024};
}

void set(const scene::Component& component, const char* name, reflection::object value)
{
    component.scriptInstance().ptr<reflection::instance>()->slot(name) = std::move(value);
}

scene::Component& stage(EditorSceneDocument& document,
    Transaction& transaction,
    scene::GameObjectId owner,
    const char* resource = primaryId)
{
    auto* module = document.scriptCatalog()->modules().find({resource, "Link", 1});
    check(module != nullptr);
    return transaction.createScriptComponent(owner,
        *module,
        {resource, "Link", 1},
        transaction.resolveObject(owner)->components().size());
}

struct Graph {
    scene::GameObjectId a, b;
    scene::ComponentId rootScript, aScript, bScript, light;
};

Graph populate(EditorSceneDocument& document)
{
    document.scene().setName("Actual authoring root");
    document.scene().setActive(false);
    document.scene().transform().setEnabled(false);
    auto transform = document.scene().local_transform();
    transform.translation = {3, 4, 0};
    document.scene().set_local_transform(transform);
    check(document.setRuntimeSceneId("00000000-0000-4000-8000-000000000066"));
    Graph graph;
    graph.a = document.createObject("first");
    graph.b = document.createObject("second");
    check(document.reparentObject(graph.b, graph.a, 0));
    auto& light = document.scene().addComponent<scene3d::LightComponent>();
    graph.light = light.id();
    // Root is a live owner even inside a cold document. Its dependencies must
    // resolve through the transaction, not ordinary root component lookup.
    check(document.addComponent("RigidBody2DComponent", document.scene().rootId()));
    check(document.addComponent("Collider2DComponent", document.scene().rootId()));
    Transaction transaction(document.scene(), {0, 3});
    auto& root = stage(document, transaction, document.scene().rootId());
    auto& a = stage(document, transaction, graph.a);
    auto& b = stage(document, transaction, graph.b, secondaryId);
    graph.rootScript = root.id();
    graph.aScript = a.id();
    graph.bScript = b.id();
    for (auto* item : {&root, &a}) {
        set(*item, "count", reflection::object(9));
        set(*item, "text", reflection::object(core::pmr::String("")));
        set(*item, "target", document.scene().nativeReflectionObject());
        set(*item, "native", light.nativeReflectionObject());
        set(*item, "peer", (item == &root ? a : root).scriptInstance());
        reflection::hlist targets;
        targets.push_back(document.scene().nativeReflectionObject());
        targets.push_back(document.scene().findObject(graph.b)->nativeReflectionObject());
        set(*item, "targets", reflection::object(std::move(targets)));
    }
    a.setEnabled(false);
    set(b, "text", reflection::object(core::pmr::String("other module\nquoted: \"x\"")));
    transaction.prepare();
    transaction.commit();
    check(document.select(graph.b));
    return graph;
}

void bind(Fixture& files, EditorSceneDocument& document)
{
    core::pmr::String diagnostic;
    check(document.bindServiceSession(Fixture::services(), diagnostic), diagnostic);
    check(files.prepare(document, diagnostic), diagnostic);
}

void roundTrip(Fixture& files)
{
    EditorSceneDocument document(false);
    bind(files, document);
    const auto graph = populate(document);
    const auto scope = document.scriptCatalog();
    auto old = document.scene().findComponent(graph.rootScript)->scriptInstance();
    const auto epoch = document.ownerTicket();
    core::pmr::String yaml, diagnostic, hash;
    check(Codec::encode(document, inputs(files), yaml, diagnostic), diagnostic);
    check(epoch.isCurrent());
    check(!EditorSceneAuthoringCodec::validateStructure(yaml, diagnostic));
    check(Codec::contentHash(document, inputs(files), hash, diagnostic), diagnostic);
    check(document.select(document.scene().rootId()));
    core::pmr::String otherHash;
    check(Codec::contentHash(document, inputs(files), otherHash, diagnostic), diagnostic);
    check(hash == otherHash);
    check(document.select(graph.b));
    check(Codec::decode(yaml, document, inputs(files), diagnostic), diagnostic);
    check(!epoch.isCurrent());
    check(!old.ptr<reflection::instance>()->alive());
    for (const auto& entry : scope->modules().entries())
        check(entry.module->liveInstances() == 0);
    check(document.scene().name() == "Actual authoring root");
    check(!document.scene().active() && !document.scene().transform().enabled());
    check(document.selectedObject() == graph.b);
    check(document.scene().findObject(graph.b)->parentId() == graph.a);
    check(document.scene().findComponent(graph.rootScript) != nullptr);
    core::pmr::String restored;
    check(Codec::encode(document, inputs(files), restored, diagnostic), diagnostic);
    check(yaml == restored);
    // Explicit execution starts the initializer only now, against the actual
    // newly published Scene rather than the destroyed source or a fake domain.
    auto* module = document.scriptCatalog()->modules().find({primaryId, "Link", 1});
    scriptvm::Value result;
    check(module->session().call("read_name", {}, result), module->diagnostic());
    check(result.cast<core::pmr::String>() == "InitializerExecuted"
        && document.scene().name() == "InitializerExecuted");
}

void failures(Fixture& files)
{
    EditorSceneDocument document(false);
    bind(files, document);
    const auto graph = populate(document);
    const auto epoch = document.ownerTicket();
    const auto* scene = &document.scene();
    core::pmr::String yaml, diagnostic;
    check(Codec::encode(document, inputs(files), yaml, diagnostic), diagnostic);
    core::UniquePtr<EditorSceneDocument> candidate;
    check(Codec::prepare(yaml, document, inputs(files), candidate, diagnostic), diagnostic);
    const auto* retained = candidate.get();
    const auto reject = [&](const core::pmr::String& invalid) {
        check(!Codec::prepare(invalid, document, inputs(files), candidate, diagnostic));
        check(!diagnostic.empty() && retained == candidate.get());
        check(epoch.isCurrent() && scene == &document.scene()
            && document.selectedObject() == graph.b);
    };
    const auto mutate = [&](auto fn) {
        auto node = Y::parseSingleDocument(yaml);
        fn(node);
        reject(Y::serialize(node));
    };
    mutate([](auto& node) {
        node["contractVersion"] = 999;
    });
    mutate([](auto& node) {
        node["unexpected"] = true;
    });
    mutate([](auto& node) {
        node["root"]["id"] = 99;
    });
    mutate([](auto& node) {
        node["root"]["transform"]["translation"] = "invalid";
    });
    mutate([](auto& node) {
        node["selected"] = 99999;
    });
    mutate([](auto& node) {
        node["objects"][0]["parent"] = 99999;
    });
    mutate([](auto& node) {
        node["objects"][1]["id"] = node["objects"][0]["id"];
    });
    mutate([](auto& node) {
        node["objects"][0]["childIndex"] = 2;
    });
    mutate([](auto& node) {
        node["root"]["components"][0]["kind"] = "unknown";
    });
    mutate([](auto& node) {
        node["root"]["components"][0]["properties"]["unknown"] = true;
    });
    mutate([](auto& node) {
        node["objects"][0]["components"][0]["state"]["sourceFingerprint"] = "bad";
    });
    mutate([](auto& node) {
        node["objects"][0]["components"][0]["state"]["schemaVersion"] = 2;
    });
    mutate([](auto& node) {
        node["objects"][0]["components"][0]["state"]["fields"]["count"] = "bad";
    });
    mutate([](auto& node) {
        node["objects"][0]["components"][0]["state"]["fields"]["peer"]["component"] = 99999;
    });
    reject("type: arp.editor-scene\n");
    auto tiny = inputs(files);
    tiny.preparationBytes = 1;
    check(!Codec::prepare(yaml, document, tiny, candidate, diagnostic));
    check(candidate.get() == retained && epoch.isCurrent());
    auto references = inputs(files);
    references.fields.maxReferences = 1;
    check(!Codec::prepare(yaml, document, references, candidate, diagnostic));
    check(candidate.get() == retained && epoch.isCurrent());
    auto fields = inputs(files);
    fields.fields.maxYamlBytes = 1;
    check(!Codec::prepare(yaml, document, fields, candidate, diagnostic));
    check(candidate.get() == retained && epoch.isCurrent());
    auto changed = files.manifest;
    changed.scriptModules[0].authoring->maxInstances++;
    auto wrongProject = inputs(files);
    EditorSceneScriptInputs conflict{files.root,
        changed,
        wrongProject.modules,
        wrongProject.fields,
        wrongProject.preparationBytes};
    core::pmr::String unchanged = "unchanged";
    check(!Codec::encode(document, conflict, unchanged, diagnostic));
    check(unchanged == "unchanged");
    core::pmr::String observed, retainedHash;
    check(Codec::observeContentHash(document, inputs(files), retainedHash, diagnostic));
    files.write("primary.arp", core::pmr::String(primary) + "\n# changed\n");
    check(Codec::observeContentHash(document, inputs(files), observed, diagnostic));
    check(observed == retainedHash);
    std::filesystem::remove(files.root / "primary.arp");
    check(Codec::observeContentHash(document, inputs(files), observed, diagnostic));
    check(observed == retainedHash);

    check(!Codec::prepare(yaml, document, inputs(files), candidate, diagnostic));
    check(candidate.get() == retained && epoch.isCurrent());
    check(!Codec::encode(document, inputs(files), unchanged, diagnostic));
    check(unchanged == "unchanged");
    files.write("primary.arp", primary);
    // Missing Ready resource IDs cannot be serialized into apparently valid saves.
    auto* sprite = dynamic_cast<scene2d::SpriteRendererComponent*>(
        document.addComponent("SpriteRendererComponent", graph.b));
    check(sprite != nullptr);
    sprite->setSpriteResource("00000000-0000-4000-8000-000000000099");
    check(!Codec::encode(document, inputs(files), unchanged, diagnostic));
    check(unchanged == "unchanged");
    sprite->setSpriteResource({});
    // A direct invalid native write cannot produce a successful durable save.
    document.scene().getComponent<scene3d::LightComponent>()->setIntensity(
        std::numeric_limits<float>::quiet_NaN());
    check(!Codec::encode(document, inputs(files), unchanged, diagnostic));
    check(unchanged == "unchanged");
}

void cycle(Fixture& files)
{
    roundTrip(files);
    failures(files);
}
} // namespace

int main(int argc, char** argv)
{
    Fixture files;
    files.write("primary.arp", primary);
    files.write("secondary.arp", secondary);
    cycle(files);
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i) {
            cycle(files);
            const auto current = core::defaultCpuAllocator().stats();
            check(current.liveBytes == baseline.liveBytes);
            check(current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        }
        std::puts(
            "PASS 20 warmed ordinary v4 cold round-trip/failure/close cycles: core bytes/count delta=0; retired module instances=0");
    } else
        std::puts(
            "PASS v4 exact identities/root/dependencies/mutual references, cold domains and atomic rejections");
}
