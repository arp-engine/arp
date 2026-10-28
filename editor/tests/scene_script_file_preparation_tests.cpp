#include "core/io/file_io.h"
#include "editor/tests/fixtures/editor_script_document_fixture.h"
#include "editor/workspace/editor_scene_persistence.h"
#include "editor/workspace/editor_scene_script_file_preparation.h"
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

scene::Component& populate(Fixture& files, EditorSceneDocument& document)
{
    core::pmr::String diagnostic;
    check(document.bindServiceSession(Fixture::services(), diagnostic), diagnostic);
    check(files.prepare(document, diagnostic), diagnostic);
    const auto id = document.createObject("script object");
    Transaction transaction(document.scene(), {0, 1});
    auto* module = document.scriptCatalog()->modules().find({primaryId, "Mover", 1});
    check(module != nullptr);
    auto& component = transaction.createScriptComponent(id, *module, {primaryId, "Mover", 1}, 1);
    component.scriptInstance().ptr<reflection::instance>()->slot("amount") = reflection::object(7);
    transaction.prepare();
    transaction.commit();
    return component;
}

core::pmr::String text(const core::pmr::Vector<std::uint8_t>& bytes)
{
    return core::pmr::String(bytes.begin(), bytes.end());
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

void unchanged(Fixture& files, const Preparation::Prepared& prepared)
{
    const auto disk = core::readTextFile(files.root / "scene.yaml");
    check(disk.success && disk.text == text(prepared.previousBytes()));
}

void cycle(Fixture& files)
{
    EditorSceneDocument document(false);
    auto& component = populate(files, document);
    const auto originalName = document.scene().name();
    const auto epoch = document.ownerTicket();
    core::pmr::String diagnostic, yaml;
    check(EditorSceneScriptCodec::encode(document, inputs(files), yaml, diagnostic), diagnostic);
    files.write("scene.yaml", yaml);
    EditorScenePersistence persistence;
    const auto raw = finish(persistence, persistence.readResolved(files.root / "scene.yaml"));
    check(raw.success && text(raw.bytes) == yaml);
    component.scriptInstance().ptr<reflection::instance>()->slot("amount") = reflection::object(9);
    core::UniquePtr<Preparation::Prepared> prepared;
    check(Preparation::prepare(
              document, inputs(files), {"scene.yaml", true, text(raw.bytes)}, prepared, diagnostic),
        diagnostic);
    check(prepared->targetExisted() && prepared->previousBytes() == raw.bytes);
    check(prepared->replacementBytes() != raw.bytes
        && prepared->current(document, inputs(files), diagnostic));
    check(epoch.isCurrent() && document.scene().name() == originalName);
    unchanged(files, *prepared);
    const auto* retained = prepared.get();
    const auto reject = [&](Preparation::Request request) {
        check(!Preparation::prepare(document, inputs(files), request, prepared, diagnostic));
        check(!diagnostic.empty() && prepared.get() == retained && epoch.isCurrent());
        unchanged(files, *prepared);
    };
    reject({"../outside.yaml", true, yaml});
    reject({"scene.yaml", false, yaml});
    reject({"scene.yaml", true, {}});
    reject({"scene.yaml", true, "invalid: [yaml"});
    auto unknown = core::YamlContract::parseSingleDocument(yaml);
    unknown["contractVersion"] = 999;
    const auto unknownYaml = core::YamlContract::serialize(unknown);
    reject({"scene.yaml", true, unknownYaml});
    EditorSceneDocument native(false);
    core::pmr::String nativeYaml;
    check(EditorSceneAuthoringCodec::encode(native, nativeYaml, diagnostic), diagnostic);
    reject({"scene.yaml", true, nativeYaml});
    auto tiny = inputs(files);
    tiny.preparationBytes = 1;
    check(!Preparation::prepare(document, tiny, {"scene.yaml", true, yaml}, prepared, diagnostic));
    check(prepared.get() == retained && epoch.isCurrent());
    check(document.select(document.scene().rootId()));
    check(prepared->current(document, inputs(files), diagnostic), diagnostic);
    component.scriptInstance().ptr<reflection::instance>()->slot("amount") = reflection::object(10);
    check(!prepared->current(document, inputs(files), diagnostic));
    check(diagnostic == "EDITOR-SCRIPT-FILE-CONTENT-STALE");
    component.scriptInstance().ptr<reflection::instance>()->slot("amount") = reflection::object(9);
    check(prepared->current(document, inputs(files), diagnostic), diagnostic);
    auto manifest = files.manifest;
    manifest.name = "Changed project";
    const auto input = inputs(files);
    const EditorSceneScriptInputs context{
        files.root, manifest, input.modules, input.fields, input.preparationBytes};
    check(!prepared->current(document, context, diagnostic));
    check(diagnostic == "EDITOR-SCRIPT-FILE-CONTEXT-STALE");
    files.write("primary.arp", core::pmr::String(Fixture::primarySource) + "\n# changed\n");
    check(!prepared->current(document, inputs(files), diagnostic));
    files.write("primary.arp", Fixture::primarySource);
    check(prepared->current(document, inputs(files), diagnostic), diagnostic);
    unchanged(files, *prepared);
    // Owner freshness deliberately is not a disk-CAS claim. Preserve the exact
    // old bytes for the future worker precondition rather than accepting edits.
    files.write("scene.yaml", yaml + "\n# external disk edit\n");
    check(prepared->current(document, inputs(files), diagnostic), diagnostic);
    check(text(prepared->previousBytes()) == yaml);
    files.write("scene.yaml", yaml);
    // No approval is bypassed: both new-v4 and existing-v4 routes remain closed
    // in the native v3 asynchronous store, preserving the original bytes.
    auto load = finish(persistence, persistence.load(files.root, "scene.yaml"));
    check(!load.success && !load.committed && load.diagnostic == "ARP-SAVE-UNSUPPORTED-CONTRACT");
    auto save = finish(persistence, persistence.save(files.root, "scene.yaml", nativeYaml));
    check(!save.success && !save.committed && save.diagnostic == "ARP-SAVE-UNSUPPORTED-CONTRACT");
    unchanged(files, *prepared);
    files.write("native.yaml", nativeYaml);
    auto nativeSave = finish(persistence,
        persistence.save(files.root, "native.yaml", text(prepared->replacementBytes())));
    check(!nativeSave.success && !nativeSave.committed);
    const auto nativeDisk = core::readTextFile(files.root / "native.yaml");
    check(nativeDisk.success && nativeDisk.text == nativeYaml);
    auto newSave = finish(persistence,
        persistence.save(files.root, "new-v4.yaml", text(prepared->replacementBytes())));
    check(!newSave.success && !newSave.committed
        && !std::filesystem::exists(files.root / "new-v4.yaml"));
    core::UniquePtr<Preparation::Prepared> create;
    check(Preparation::prepare(
              document, inputs(files), {"new-v4.yaml", false, {}}, create, diagnostic),
        diagnostic);
    check(!create->targetExisted() && create->previousBytes().empty());
    check(!std::filesystem::exists(files.root / "new-v4.yaml"));
    const auto catalog = document.scriptCatalog();
    check(document.replaceWith(EditorSceneDocument(false)));
    check(!prepared->current(document, inputs(files), diagnostic));
    check(diagnostic == "EDITOR-SCRIPT-FILE-DOCUMENT-STALE");
    for (const auto& entry : catalog->modules().entries())
        check(entry.module->liveInstances() == 0);
    unchanged(files, *prepared);
    for (const auto& entry : std::filesystem::directory_iterator(files.root))
        check(entry.path().filename().string().find(".arp-tmp-") == std::string::npos);
}
} // namespace

int main(int argc, char** argv)
{
    Fixture files;
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
            "PASS 20 warmed ordinary v4 file-preparation/raw-read/refusal/close cycles: core bytes/count delta=0; retired instances=0; no file writes");
    } else
        std::puts(
            "PASS owned v4 file snapshots, full cold revalidation, exact freshness and asynchronous v3 gate preservation");
}
