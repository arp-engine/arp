#include "2d/physics_components.h"
#include "core/io/file_io.h"
#include "editor/tests/fixtures/editor_script_document_fixture.h"
#include "editor/workspace/editor_scene_persistence.h"
#include "reflection/instance.h"

#include <thread>

namespace {
using namespace arp;
using namespace arp::editor;
using namespace arp::editor::testfixtures;
using Preparation = EditorSceneScriptFilePreparation;
using Transaction = scene::Scene::IdentityRestoration;

EditorSceneScriptInputs inputs(const Fixture& files)
{
    return {files.root,
        files.manifest,
        Fixture::budget(),
        {256 * 1024, 64, 4 * 1024 * 1024, reflection::SerializationOptions{}},
        64 * 1024 * 1024};
}

core::pmr::String text(const core::pmr::Vector<std::uint8_t>& bytes)
{
    return {bytes.begin(), bytes.end()};
}

core::io::AtomicFileResult finish(EditorScenePersistence& files, core::PoolHandle handle)
{
    check(bool(handle), "request must be accepted");
    core::io::AtomicFileResult result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (files.take(handle, result))
            return result;
        std::this_thread::yield();
    }
    check(false, "file completion deadline");
    return result;
}

struct Graph {
    scene::GameObjectId child;
    scene::ComponentId rootScript, link, body;
};

Graph populate(Fixture& files, EditorSceneDocument& document)
{
    core::pmr::String diagnostic;
    check(document.bindServiceSession(Fixture::services(), diagnostic), diagnostic);
    check(files.prepare(document, diagnostic), diagnostic);
    document.scene().setName("file authoring root");
    Graph graph;
    graph.child = document.createObject("physics link");
    check(document.addComponent("RigidBody2DComponent", document.scene().rootId()));
    const auto* body = document.scene().getComponent<physics2d::RigidBody2DComponent>();
    check(body != nullptr);
    graph.body = body->id();
    Transaction transaction(document.scene(), {0, 2});
    auto* primary = document.scriptCatalog()->modules().find({primaryId, "Mover", 1});
    auto* secondary = document.scriptCatalog()->modules().find({secondaryId, "PhysicsLink", 1});
    check(primary && secondary);
    auto& root = transaction.createScriptComponent(document.scene().rootId(),
        *primary,
        {primaryId, "Mover", 1},
        document.scene().components().size());
    auto& link = transaction.createScriptComponent(
        graph.child, *secondary, {secondaryId, "PhysicsLink", 1}, 1);
    root.scriptInstance().ptr<reflection::instance>()->slot("amount") = reflection::object(7);
    link.scriptInstance().ptr<reflection::instance>()->slot("body") =
        body->nativeReflectionObject();
    graph.rootScript = root.id();
    graph.link = link.id();
    transaction.prepare();
    transaction.commit();
    return graph;
}

void retired(const core::SharedPtr<const EditorScriptComponentCatalog>& catalog)
{
    for (const auto& entry : catalog->modules().entries())
        check(entry.module->liveInstances() == 0);
}

void cycle(Fixture& files)
{
    std::filesystem::remove(files.root / "scene.yaml");
    EditorSceneDocument document(false);
    const auto graph = populate(files, document);
    EditorScenePersistence persistence;
    core::pmr::String diagnostic;
    core::UniquePtr<Preparation::Prepared> prepared;
    check(Preparation::prepare(
              document, inputs(files), {"scene.yaml", false, {}}, prepared, diagnostic),
        diagnostic);
    const auto originalHash = prepared->contentHash();
    auto result =
        finish(persistence, persistence.saveScript(document, inputs(files), *prepared, diagnostic));
    check(result.success && result.committed && result.bytes == prepared->replacementBytes(),
        result.diagnostic);
    const auto original = text(result.bytes);
    check(document.scene().name() == "file authoring root");
    // Reopen into an independent actual document/registry/domain.
    EditorSceneDocument reopened(false);
    check(reopened.bindServiceSession(Fixture::services(), diagnostic), diagnostic);
    const auto disk = finish(persistence, persistence.readScript(files.root, "scene.yaml"));
    check(disk.success && disk.bytes == result.bytes);
    check(EditorSceneScriptCodec::decode(text(disk.bytes), reopened, inputs(files), diagnostic),
        diagnostic);
    check(reopened.scene().name() == "file authoring root");
    check(reopened.scene().findObject(graph.child) != nullptr);
    const auto* restored = reopened.scene().findComponent(graph.rootScript);
    check(restored
        && restored->scriptInstance().ptr<reflection::instance>()->slot("amount").cast<int>() == 7);
    core::pmr::String roundtrip, hash;
    check(
        EditorSceneScriptCodec::encode(reopened, inputs(files), roundtrip, diagnostic), diagnostic);
    check(roundtrip == original);
    check(
        EditorSceneScriptCodec::contentHash(reopened, inputs(files), hash, diagnostic), diagnostic);
    check(hash == originalHash);
    check(&reopened.scene() != &document.scene());
    const auto* reopenedLink = reopened.scene().findObject(graph.child)->findComponent(graph.link);
    const auto* originalLink = document.scene().findObject(graph.child)->findComponent(graph.link);
    check(reopenedLink && originalLink);
    check(reopenedLink->scriptInstance().ptr<reflection::instance>()
        != originalLink->scriptInstance().ptr<reflection::instance>());
    // Existing files are fully cold-validated before requesting a byte match.
    document.scene()
        .findComponent(graph.rootScript)
        ->scriptInstance()
        .ptr<reflection::instance>()
        ->slot("amount") = reflection::object(9);
    check(Preparation::prepare(
              document, inputs(files), {"scene.yaml", true, original}, prepared, diagnostic),
        diagnostic);
    files.write("scene.yaml", original + "\n# external writer\n");
    result =
        finish(persistence, persistence.saveScript(document, inputs(files), *prepared, diagnostic));
    check(!result.success && !result.committed
        && result.diagnostic == "ARP-FILE-PRECONDITION-CHANGED");
    check(core::readTextFile(files.root / "scene.yaml").text == original + "\n# external writer\n");
    files.write("scene.yaml", original);
    result =
        finish(persistence, persistence.saveScript(document, inputs(files), *prepared, diagnostic));
    check(result.success && result.committed);
    const auto saved = text(result.bytes);
    check(saved != original);
    // A new-file token must not overwrite a file created after preparation.
    std::filesystem::remove(files.root / "new.yaml");
    core::UniquePtr<Preparation::Prepared> create;
    check(
        Preparation::prepare(document, inputs(files), {"new.yaml", false, {}}, create, diagnostic),
        diagnostic);
    files.write("new.yaml", "foreign");
    result =
        finish(persistence, persistence.saveScript(document, inputs(files), *create, diagnostic));
    check(!result.success && !result.committed
        && result.diagnostic == "ARP-FILE-PRECONDITION-EXISTS");
    check(core::readTextFile(files.root / "new.yaml").text == "foreign");
    // Corrupt/unknown files and stale source cannot reach a worker write.
    const auto oldEpoch = document.ownerTicket();
    const auto* token = prepared.get();
    for (const auto* invalid : {"invalid: [", "type: arp.editor-scene\ncontractVersion: 999\n"}) {
        files.write("scene.yaml", invalid);
        const auto raw = finish(persistence, persistence.readScript(files.root, "scene.yaml"));
        check(raw.success);
        check(!Preparation::prepare(
            document, inputs(files), {"scene.yaml", true, text(raw.bytes)}, prepared, diagnostic));
        check(prepared.get() == token && oldEpoch.isCurrent());
        check(
            !EditorSceneScriptCodec::decode(text(raw.bytes), document, inputs(files), diagnostic));
        check(
            oldEpoch.isCurrent() && core::readTextFile(files.root / "scene.yaml").text == invalid);
    }
    files.write("scene.yaml", original);
    files.write("primary.arp", core::pmr::String(Fixture::primarySource) + "\n# changed\n");
    check(!persistence.saveScript(document, inputs(files), *prepared, diagnostic));
    check(!diagnostic.empty() && oldEpoch.isCurrent());
    files.write("primary.arp", Fixture::primarySource);
    auto tiny = inputs(files);
    tiny.preparationBytes = 1;
    check(!persistence.saveScript(document, tiny, *prepared, diagnostic));
    // Owned request copies must fail before acceptance, leaving no live slot.
    diagnostic.reserve(1024);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        check(!persistence.saveScript(document, inputs(files), *prepared, diagnostic));
    }
    check(quota->liveBytes() == 0 && diagnostic == "EDITOR-SCRIPT-FILE-SUBMIT-EXCEPTION");
    std::array<core::PoolHandle, 4> occupied;
    for (auto& handle : occupied) {
        handle = persistence.readScript(files.root, "scene.yaml");
        check(bool(handle));
    }
    check(!persistence.readScript(files.root, "scene.yaml"));
    check(std::string_view(persistence.diagnostic()) == "ARP-FILE-REQUEST-CAPACITY");
    check(!persistence.saveScript(document, inputs(files), *prepared, diagnostic));
    check(diagnostic == "ARP-FILE-REQUEST-CAPACITY");
    for (const auto handle : occupied)
        check(finish(persistence, handle).success);
    check(core::readTextFile(files.root / "scene.yaml").text == original);
    result =
        finish(persistence, persistence.saveScript(document, inputs(files), *prepared, diagnostic));
    check(result.success && result.committed && text(result.bytes) == saved);
    // The legacy v3 store remains explicit and cannot overwrite a v4 file.
    EditorSceneDocument native(false);
    core::pmr::String nativeYaml;
    check(EditorSceneAuthoringCodec::encode(native, nativeYaml, diagnostic), diagnostic);
    result = finish(persistence, persistence.save(files.root, "scene.yaml", nativeYaml));
    check(!result.success && !result.committed
        && result.diagnostic == "ARP-SAVE-UNSUPPORTED-CONTRACT");
    check(core::readTextFile(files.root / "scene.yaml").text == saved);
    const auto oldCatalog = document.scriptCatalog();
    const auto reopenedCatalog = reopened.scriptCatalog();
    auto receiver = document.scene().findComponent(graph.rootScript)->scriptInstance();
    check(document.replaceWith(EditorSceneDocument(false)));
    check(!prepared->current(document, inputs(files), diagnostic));
    check(diagnostic == "EDITOR-SCRIPT-FILE-DOCUMENT-STALE");
    check(!persistence.saveScript(document, inputs(files), *prepared, diagnostic));
    check(!receiver.ptr<reflection::instance>()->alive());
    check(reopened.replaceWith(EditorSceneDocument(false)));
    retired(oldCatalog);
    retired(reopenedCatalog);
    check(core::readTextFile(files.root / "scene.yaml").text == saved);
    for (const auto& entry : std::filesystem::directory_iterator(files.root))
        check(entry.path().filename().string().find(".arp-tmp-") == std::string::npos);
}
} // namespace

int main(int argc, char** argv)
{
    Fixture files;
    cycle(files);
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned round = 0; round < 20; ++round) {
            cycle(files);
            const auto after = core::defaultCpuAllocator().stats();
            check(after.liveBytes == before.liveBytes);
            check(after.allocationCount - after.deallocationCount
                == before.allocationCount - before.deallocationCount);
        }
        std::printf(
            "ordinary_cycles=20 live_bytes_delta=0 live_count_delta=0 retired_instances=0 core_process_peak_bytes=%llu\n",
            static_cast<unsigned long long>(core::defaultCpuAllocator().stats().peakBytes));
    }
}
