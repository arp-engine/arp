#include "core/encoding/yaml_contract.h"
#include "editor/tests/fixtures/editor_script_document_fixture.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_scene_script_bindings.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace {
using namespace arp;
using namespace arp::editor;
using namespace arp::editor::testfixtures;

void contracts(Fixture& files)
{
    EditorSceneDocument document(false);
    core::pmr::String diagnostic;
    const auto session = Fixture::services();
    const auto initialEpoch = document.ownerTicket();
    check(!files.prepare(document, diagnostic) && !diagnostic.empty());
    check(initialEpoch.sameCurrentEpoch(document.ownerTicket()) && !document.scriptCatalog());
    check(document.bindServiceSession(session, diagnostic), diagnostic);
    const auto ticket = document.ownerTicket();
    const auto generation = document.scene().main_scene_generation();
    const auto name = document.scene().name();
    const auto good = files.manifest;
    const auto rejected = [&] {
        check(!files.prepare(document, diagnostic) && !diagnostic.empty());
        check(!document.scriptCatalog() && ticket.sameCurrentEpoch(document.ownerTicket()));
        check(document.scene().name() == name && document.scene().is_main_scene()
            && document.scene().main_scene_generation() == generation);
        check(document.sceneServiceSession() == session);
        files.manifest = good;
    };
    files.manifest.scriptModules[0].authoring->imports = {"missing"};
    rejected();
    files.manifest.scriptModules[0].dependencies = {"secondary"};
    rejected();
    files.manifest.scriptModules[0].authoring->preparationBytes = 1;
    rejected();
    check(!document.prepareScriptAuthoring(
        files.root, files.manifest, {1, {4, 1024 * 1024}}, diagnostic));
    check(ticket.sameCurrentEpoch(document.ownerTicket()) && !document.scriptCatalog());
    check(!document.prepareScriptAuthoring(
        files.root, files.manifest, {16 * 1024 * 1024, {1, 1024 * 1024}}, diagnostic));
    check(ticket.sameCurrentEpoch(document.ownerTicket()) && !document.scriptCatalog());
    files.write("primary.arp", "invalid syntax !!!\n");
    rejected();
    files.write("primary.arp", Fixture::primarySource);
    check(files.prepare(document, diagnostic), diagnostic);
    check(!ticket.isCurrent() && document.scene().name() == name);
    const auto catalog = document.scriptCatalog();
    check(catalog && catalog->entries().size() == 2);
    for (const auto& entry : catalog->modules().entries())
        check(entry.module->liveInstances() == 0);
    auto* module = catalog->modules().find({primaryId, "Mover", 1});
    check(module != nullptr);
    scriptvm::Value result;
    check(module->session().call("read_name", {}, result), module->diagnostic());
    check(result.cast<core::pmr::String>() == "InitializerExecuted"
        && document.scene().name() == "InitializerExecuted");
    const auto publishedEpoch = document.ownerTicket();
    check(!files.prepare(document, diagnostic) && diagnostic == "EDITOR-SCRIPT-DOCUMENT-NOT-COLD");
    check(publishedEpoch.sameCurrentEpoch(document.ownerTicket())
        && document.scriptCatalog() == catalog);
    // Rebinding services invalidates use, while the old exact world stays owned
    // until the retired script scope and any retained catalog finish teardown.
    std::weak_ptr<physics2d::PhysicsWorld2D> oldWorld = session->physics();
    check(document.bindServiceSession(Fixture::services(), diagnostic), diagnostic);
    check(!document.scriptCatalog() && !oldWorld.expired());

    EditorSceneDocument missingWorld(false);
    check(missingWorld.bindServiceSession(Fixture::services(false), diagnostic));
    check(!files.prepare(missingWorld, diagnostic) && !diagnostic.empty());
    check(!missingWorld.scriptCatalog());
}

void createColdComponent(
    EditorSceneDocument& document, scene::GameObjectId owner = {}, const char* scriptType = "Mover")
{
    const auto catalog = document.scriptCatalog();
    auto* module = catalog->modules().find({primaryId, scriptType, 1});
    const auto* row = catalog->find({primaryId, scriptType, 1});
    check(module && row && row->available());
    const auto name = document.scene().name();
    scene::Scene::IdentityRestoration transaction(document.scene(), {0, 1});
    auto& component = transaction.createScriptComponent(
        owner.valid() ? owner : document.scene().rootId(), *module, {primaryId, scriptType, 1}, 1);
    auto receiver = component.scriptInstance();
    reflection::SerializationArchive archive;
    check(archive.readValue(
              *row->fields, core::YamlContract::parseSingleDocument("amount: 7\n"), receiver),
        archive.diagnostics());
    transaction.prepare();
    const auto before = core::defaultCpuAllocator().stats();
    transaction.commit();
    check(core::defaultCpuAllocator().stats().allocationCount == before.allocationCount);
    check(row->fields->get(receiver, "amount").cast<int>() == 7);
    check(module->liveInstances() == 1 && document.scene().name() == name);
}

void persistenceBoundary(Fixture& files)
{
    core::pmr::String diagnostic, output("unchanged");
    EditorSceneDocument native(false);
    check(EditorSceneAuthoringCodec::encode(native, output, diagnostic), diagnostic);
    check(native.scene().createComponentByName(native.scene(), "LightComponent") != nullptr);
    const auto nativeEpoch = native.ownerTicket();
    output = "unchanged";
    check(!EditorSceneAuthoringCodec::encode(native, output, diagnostic),
        "root component was silently omitted by the native-only codec");
    check(diagnostic == "EDITOR-SCENE-ROOT-COMPONENT-UNSUPPORTED" && output == "unchanged"
        && nativeEpoch.sameCurrentEpoch(native.ownerTicket()));
    for (bool root : {true, false}) {
        EditorSceneDocument document(false);
        check(document.bindServiceSession(Fixture::services(), diagnostic));
        check(files.prepare(document, diagnostic), diagnostic);
        const auto owner = root ? document.scene().rootId() : document.createObject("Script owner");
        createColdComponent(document, owner);
        const auto ticket = document.ownerTicket();
        output = "unchanged";
        check(!EditorSceneAuthoringCodec::encode(document, output, diagnostic));
        check(diagnostic
            == (root ? "EDITOR-SCENE-ROOT-COMPONENT-UNSUPPORTED"
                     : "EDITOR-SCENE-SCRIPT-COMPONENT-UNSUPPORTED"));
        check(output == "unchanged" && ticket.sameCurrentEpoch(document.ownerTicket()));
        core::pmr::String hash("unchanged hash");
        check(!EditorSceneAuthoringCodec::contentHash(document, hash, diagnostic));
        check(hash == "unchanged hash");
        for (const auto& entry : document.scriptCatalog()->modules().entries())
            check(entry.module->liveInstances()
                == (entry.declaration.resourceId == primaryId ? 1u : 0u));
    }
    // A user class may have the same spelling as a native catalog row.
    // Its runtime identity must never be guessed from that spelling.
    auto source = core::pmr::String(Fixture::primarySource);
    const auto classOffset = source.find("class Mover(");
    check(classOffset != core::pmr::String::npos);
    source.replace(classOffset, std::string_view("class Mover").size(), "class LightComponent");
    files.write("primary.arp", source);
    {
        EditorSceneDocument collision(false);
        check(collision.bindServiceSession(Fixture::services(), diagnostic));
        check(files.prepare(collision, diagnostic), diagnostic);
        const auto owner = collision.createObject("Native name collision");
        createColdComponent(collision, owner, "LightComponent");
        output = "unchanged";
        const bool encoded = EditorSceneAuthoringCodec::encode(collision, output, diagnostic);
        std::fprintf(stderr,
            "native_name_collision_encoded=%d diagnostic=%s\n",
            encoded,
            diagnostic.c_str());
        check(!encoded, "script was saved as a native component by name");
        check(output == "unchanged" && diagnostic == "EDITOR-SCENE-SCRIPT-COMPONENT-UNSUPPORTED");
    }
    files.write("primary.arp", Fixture::primarySource);
}

void replacement(Fixture& files)
{
    core::pmr::String diagnostic;
    EditorSceneDocument destination(false), candidate(false);
    check(destination.bindServiceSession(Fixture::services(), diagnostic));
    check(candidate.bindServiceSession(Fixture::services(), diagnostic));
    check(files.prepare(destination, diagnostic), diagnostic);
    auto token = destination.prepareReplacement(
        candidate, diagnostic, EditorSceneDocument::ServicePolicy::AdoptCandidate);
    check(bool(token), diagnostic);
    check(files.prepare(candidate, diagnostic), diagnostic);
    check(destination.replacementReadiness(*token, candidate)
        == EditorSceneDocument::ReplacementResult::Stale);
    files.write("primary.arp", "invalid syntax !!!\n");
    const auto unchanged = destination.ownerTicket();
    check(!destination.prepareReplacement(
              candidate, diagnostic, EditorSceneDocument::ServicePolicy::AdoptCandidate)
        && !diagnostic.empty());
    check(unchanged.sameCurrentEpoch(destination.ownerTicket()));
    files.write("primary.arp", Fixture::primarySource);
    createColdComponent(destination);
    createColdComponent(candidate);
    const auto oldCatalog = destination.scriptCatalog(), incoming = candidate.scriptCatalog();
    auto* oldScene = &destination.scene();
    auto* incomingScene = &candidate.scene();
    check(!destination.prepareReplacement(candidate, diagnostic));
    token = destination.prepareReplacement(
        candidate, diagnostic, EditorSceneDocument::ServicePolicy::AdoptCandidate);
    check(bool(token), diagnostic);
    const auto before = core::defaultCpuAllocator().stats();
    check(destination.commitReplacement(*token, candidate)
        == EditorSceneDocument::ReplacementResult::Committed);
    check(core::defaultCpuAllocator().stats().allocationCount == before.allocationCount);
    check(&destination.scene() == incomingScene && &candidate.scene() == oldScene);
    check(destination.scriptCatalog() == incoming && candidate.scriptCatalog() == oldCatalog);
    check(destination.scene().is_main_scene() && candidate.scene().is_main_scene());
    check(destination.commitReplacement(*token, candidate)
        == EditorSceneDocument::ReplacementResult::Consumed);

    EditorSceneDocument sameOwner(false);
    check(sameOwner.bindServiceSession(destination.sceneServiceSession(), diagnostic));
    check(files.prepare(sameOwner, diagnostic), diagnostic);
    token = destination.prepareReplacement(sameOwner, diagnostic);
    check(bool(token), diagnostic);
    check(destination.commitReplacement(*token, sameOwner)
        == EditorSceneDocument::ReplacementResult::Committed);
    check(destination.scriptCatalog() && sameOwner.scriptCatalog());
}

void lifetimes(Fixture& files)
{
    core::pmr::String diagnostic;
    core::SharedPtr<const EditorScriptComponentCatalog> retained;
    std::weak_ptr<physics2d::PhysicsWorld2D> world;
    reflection::object receiver;
    {
        EditorSceneDocument source(false);
        check(source.bindServiceSession(Fixture::services(), diagnostic));
        world = source.physicsWorld2DProvider()();
        check(files.prepare(source, diagnostic), diagnostic);
        createColdComponent(source);
        retained = source.scriptCatalog();
        receiver = source.scene().components()[1]->scriptInstance();
        EditorSceneDocument moved(std::move(source));
        check(moved.scriptCatalog() == retained && moved.scene().is_main_scene());
        EditorSceneDocument assigned(false);
        check(assigned.bindServiceSession(Fixture::services(), diagnostic));
        check(files.prepare(assigned, diagnostic), diagnostic);
        createColdComponent(assigned);
        assigned = std::move(moved);
        check(assigned.scriptCatalog() == retained && assigned.scene().is_main_scene());
    }
    check(!receiver.ptr<reflection::instance>()->alive());
    for (const auto& entry : retained->modules().entries())
        check(entry.module->liveInstances() == 0);
    check(!world.expired());
    receiver = {};
    retained.reset();
    check(world.expired());
}

// Inspect borrowed native services from an actual component destructor. No
// external catalog or strong world reference may conceal an ordering bug.
class TeardownProbe final : public scene::Component {
public:
    TeardownProbe(
        scene::Scene& owner, std::weak_ptr<physics2d::PhysicsWorld2D> world, bool& observed)
        : owner_(owner), world_(std::move(world)), observed_(observed)
    {}

    ~TeardownProbe() override
    {
        check(!world_.expired() && owner_.main_scene_generation() != 0);
        observed_ = true;
    }

private:
    scene::Scene& owner_;
    std::weak_ptr<physics2d::PhysicsWorld2D> world_;
    bool& observed_;
};

void teardown(Fixture& files)
{
    core::pmr::String diagnostic;
    bool firstDestroyed = false, secondDestroyed = false;
    std::weak_ptr<physics2d::PhysicsWorld2D> firstWorld, secondWorld;
    {
        EditorSceneDocument first(false), second(false);
        check(first.bindServiceSession(Fixture::services(), diagnostic));
        check(second.bindServiceSession(Fixture::services(), diagnostic));
        firstWorld = first.physicsWorld2DProvider()();
        secondWorld = second.physicsWorld2DProvider()();
        check(files.prepare(first, diagnostic), diagnostic);
        check(files.prepare(second, diagnostic), diagnostic);
        first.scene().addComponent<TeardownProbe>(first.scene(), firstWorld, firstDestroyed);
        second.scene().addComponent<TeardownProbe>(second.scene(), secondWorld, secondDestroyed);
        first = std::move(second);
        check(firstDestroyed && !secondDestroyed && firstWorld.expired());
        // This goes through Scene's factory rather than the document helper,
        // proving the callback no longer captures the moved-from document.
        check(first.scene().createComponentByName(first.scene(), "LightComponent") != nullptr);
    }
    check(secondDestroyed && secondWorld.expired());
}

void cycle(Fixture& files)
{
    contracts(files);
    replacement(files);
    lifetimes(files);
    teardown(files);
}
} // namespace

int main(int argc, char** argv)
{
    Fixture files;
    if (argc == 2
        && (std::string_view(argv[1]) == "--persistence-boundary"
            || std::string_view(argv[1]) == "--persistence-smoke")) {
        persistenceBoundary(files);
        if (std::string_view(argv[1]) == "--persistence-smoke") {
            const auto baseline = core::defaultCpuAllocator().stats();
            for (unsigned round = 0; round < 20; ++round) {
                persistenceBoundary(files);
                const auto current = core::defaultCpuAllocator().stats();
                check(current.liveBytes == baseline.liveBytes);
                check(current.allocationCount - current.deallocationCount
                    == baseline.allocationCount - baseline.deallocationCount);
            }
            std::puts(
                "PASS 20 warmed ordinary unsupported script/root persistence rejections: core live bytes/count delta=0");
        } else
            std::puts(
                "PASS native-only codec fails closed for root/native/script components without changing output, hash, document or instances");
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
            "PASS warmed 20 ordinary document script bind/failure/replace/move/close: core live bytes/count delta=0, module live instances=0");
    } else
        std::puts(
            "PASS real document cold modules, explicit world/services, atomic failure, typed component/archive, replacement, move and teardown");
}
