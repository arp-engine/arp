#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "editor/project/project_scene_authoring.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/workspace/editor_scene_publication_preparation.h"
#include "resource/resource_contract.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

namespace {
using namespace arp;
using namespace arp::editor;
using Preparation = EditorScenePublicationPreparation;

void write(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    assert(file.good());
}

// Real scene packages and a live Passive draft. Preparation never owns files.
class Fixture final {
public:
    Fixture()
    {
        root = std::filesystem::temp_directory_path()
            / ("arp-publication-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        root = std::filesystem::canonical(root);
        ProjectDiagnostic issue;
        assert(ProjectManifestCodec::decode(
            tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"),
            manifest,
            issue));
        manifest.entryScene = resource::deriveResourceUuid("publication-test", "entry");
        manifest.physics2d =
            physics2d::WorldConfig{{0, -9.8f}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f};
        assert(ProjectSceneAuthoring::prepareTemplate(
            root, manifest, ProjectTemplateKind::Game2D, diagnostic));
        assert(ProjectManifestCodec::encode(manifest, originalManifest, issue));
        write(root / "project.arp.yaml", originalManifest);
        assert(live.load(root).success);
        factory = ProjectSceneAuthoring::factory(live.manager(), live.physicsWorld2D(), diagnostic);
        assert(factory);
        const auto scene = live.manager().find<scene::SceneAssetResource>(manifest.entryScene);
        originalScene = scene.get()->yaml();
        assert(scene.ready() && originalScene.size());
        assert(Preparation::inspectPackage(root,
            manifest.assetPackages.front(),
            manifest.entryScene,
            *factory,
            source,
            diagnostic));
    }

    ~Fixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        assert(!error);
    }

    core::UniquePtr<EditorSceneDocument> draft(std::string_view id)
    {
        auto document = core::makeUnique<EditorSceneDocument>(false);
        document->setResourceManagerProvider([this] {
            return &live.manager();
        });
        document->setPhysicsWorld2DProvider([this] {
            return live.physicsWorld2D();
        });
        const auto entry = live.manager().find<scene::SceneAssetResource>(manifest.entryScene);
        assert(document->loadRuntimeScene(*entry.get(), live.manager(), *factory, diagnostic));
        assert(document->setRuntimeSceneId(id));
        auto& parent = document->scene().createObject("unsaved parent");
        auto& child = document->scene().createObject("unsaved child");
        assert(document->scene().reparent(child.id(), parent.id()).ok());
        parent.addComponent<scene::ObjectLinksComponent>().links.push_back({"child", child.id()});
        assert(document->addComponent("RigidBody2DComponent", child.id()));
        assert(document->addComponent("Collider2DComponent", child.id()));
        assert(document->select(child.id()));
        return document;
    }

    Preparation::Request request(
        EditorSceneDocument& document, Preparation::Intent intent, std::string_view relative)
    {
        const auto output = root / std::filesystem::u8path(relative);
        assert(ProjectSceneAuthoring::exportScene(
            document.scene(), *factory, document.runtimeSceneId(), output, diagnostic));
        // Draft preservation is independent of which immutable asset was exported.
        document.scene().createObject("later unsaved content");
        Preparation::Request result;
        result.root = root;
        result.projectId = manifest.id;
        result.manifestSnapshot = originalManifest;
        result.intent = intent;
        if (intent == Preparation::Intent::Update)
            result.source = source;
        assert(Preparation::inspectPackage(
            root, relative, document.runtimeSceneId(), *factory, result.target, diagnostic));
        assert(Preparation::captureDraft(document, 7, result.draft, diagnostic));
        return result;
    }

    void unchanged(const EditorSceneDocument& document, const Preparation::Request& request)
    {
        assert(core::readTextFile(root / "project.arp.yaml").text == originalManifest);
        assert(live.manifestSnapshot() == originalManifest
            && live.manifest()->assetPackages.size() == 1);
        assert(live.manager().find<scene::SceneAssetResource>(manifest.entryScene).get()->yaml()
            == originalScene);
        assert(core::readTextFile(source.directory / "scene.yaml").text == originalScene);
        assert(std::filesystem::is_regular_file(request.target.directory / "manifest.arpkg.yaml"));
        core::pmr::String hash;
        assert(EditorSceneAuthoringCodec::contentHash(document, hash, diagnostic));
        assert(hash == request.draft.contentHash);
    }

    std::filesystem::path root;
    ProjectManifest manifest;
    EditorProjectResources live;
    core::SharedPtr<scene::SceneComponentFactory> factory;
    Preparation::PackageSnapshot source;
    core::pmr::String originalManifest, originalScene, diagnostic;
};

void inspectFailures(Fixture& fixture)
{
    Preparation::PackageSnapshot retained = fixture.source;
    const auto reject = [&](std::string_view path, std::string_view id) {
        assert(!Preparation::inspectPackage(
            fixture.root, path, id, *fixture.factory, retained, fixture.diagnostic));
        assert(retained.fingerprint == fixture.source.fingerprint);
    };
    reject("../outside", fixture.source.resourceId);
    reject("missing", fixture.source.resourceId);
    reject(fixture.source.relativeDirectory, "invalid");
    reject(fixture.source.relativeDirectory, resource::deriveResourceUuid("missing", "id"));
    reject(core::pmr::String(Preparation::maxRelativeDirectoryBytes + 1u, 'p'),
        fixture.source.resourceId);
    EditorSceneDocument empty(false);
    Preparation::DraftSnapshot draft;
    draft.yaml = "retained";
    assert(!Preparation::captureDraft(empty, 0, draft, fixture.diagnostic)
        && draft.yaml == "retained");
    assert(!EditorSceneAuthoringCodec::isResourceReady(
        nullptr, "SceneAssetResource", fixture.source.resourceId));
    assert(!EditorSceneAuthoringCodec::isResourceReady(
        &fixture.live.manager(), "SpriteResource", fixture.source.resourceId));
    assert(EditorSceneAuthoringCodec::isResourceReady(
        &fixture.live.manager(), "SceneAssetResource", fixture.source.resourceId));
}

void restoreAndFreshness(Fixture& fixture, Preparation::Intent intent, std::string_view path)
{
    const auto id = intent == Preparation::Intent::Update
        ? fixture.manifest.entryScene
        : resource::deriveResourceUuid("publication-test", "independent");
    auto document = fixture.draft(id);
    const auto request = fixture.request(*document, intent, path);
    const auto* activeManager = &fixture.live.manager();
    const auto liveWorld = fixture.live.physicsWorld2D();
    const auto liveTelemetry = liveWorld->telemetry();
    core::UniquePtr<Preparation::Candidate> candidate;
    core::pmr::String issue;
    std::thread worker([&] {
        candidate = Preparation::prepareResources(request, issue);
    });
    worker.join();
    if (!candidate)
        std::fprintf(stderr, "Resource preparation failed: %s\n", issue.c_str());
    assert(candidate && issue.empty() && !candidate->document);
    assert(&candidate->resources.manager() != activeManager
        && candidate->resources.physicsWorld2D() != liveWorld);
    assert(!Preparation::validateCurrent(
        *candidate, fixture.root, fixture.manifest.id, request.draft, issue));
    assert(Preparation::prepareDocument(*candidate, issue) && issue.empty());
    assert(!Preparation::prepareDocument(*candidate, issue));
    assert(issue == "EDITOR-SCENE-PUBLICATION-DOCUMENT-ALREADY-PREPARED");
    const auto& restored = *candidate->document;
    assert(restored.scene().executionMode() == scene::SceneExecutionMode::Passive);
    assert(restored.scene().findObjectByName("later unsaved content"));
    const auto* child = restored.scene().findObjectByName("unsaved child");
    const auto* parent = restored.scene().findObjectByName("unsaved parent");
    assert(child && parent && child->parentId() == parent->id());
    const auto* links = parent->getComponent<scene::ObjectLinksComponent>();
    assert(links && links->links.size() == 1 && links->links.front().target == child->id());
    assert(restored.selectedObject() == child->id());
    const auto target = candidate->resources.manager().find<scene::SceneAssetResource>(id);
    assert(target.ready()
        && target.get()->yaml().find("later unsaved content") == core::pmr::String::npos);
    assert(candidate->resources.manifest()->entryScene == fixture.manifest.entryScene);
    assert(candidate->resources.manifest()->assetPackages.size()
        == (intent == Preparation::Intent::Update ? 1u : 2u));
    if (intent == Preparation::Intent::Update)
        assert(candidate->resources.manifest()->assetPackages.front() == path);
    assert(Preparation::validateCurrent(
        *candidate, fixture.root, fixture.manifest.id, request.draft, issue));
    const auto stale = [&](std::string_view project, std::uint64_t epoch, std::string_view hash) {
        auto current = request.draft;
        current.documentEpoch = epoch;
        current.contentHash = hash;
        assert(!Preparation::validateCurrent(*candidate, fixture.root, project, current, issue));
        assert(issue == "EDITOR-SCENE-PUBLICATION-CANDIDATE-STALE");
    };
    stale("different project", request.draft.documentEpoch, request.draft.contentHash);
    stale(fixture.manifest.id, request.draft.documentEpoch + 1, request.draft.contentHash);
    stale(fixture.manifest.id, request.draft.documentEpoch, core::Sha256::hexDigest("later edit"));
    assert(document->select(document->scene().rootId()));
    Preparation::DraftSnapshot selectionChanged;
    assert(
        Preparation::captureDraft(*document, request.draft.documentEpoch, selectionChanged, issue));
    assert(selectionChanged.contentHash == request.draft.contentHash
        && selectionChanged.yaml != request.draft.yaml);
    assert(!Preparation::validateCurrent(
               *candidate, fixture.root, fixture.manifest.id, selectionChanged, issue)
        && issue == "EDITOR-SCENE-PUBLICATION-CANDIDATE-STALE");
    assert(document->select(document->scene().findObjectByName("unsaved child")->id()));
    write(fixture.root / "project.arp.yaml", fixture.originalManifest + "# external change\n");
    assert(!Preparation::validateCurrent(
        *candidate, fixture.root, fixture.manifest.id, request.draft, issue));
    assert(issue == "EDITOR-SCENE-PUBLICATION-MANIFEST-CONFLICT");
    write(fixture.root / "project.arp.yaml", fixture.originalManifest);
    const auto metadataPath = request.target.directory / "manifest.arpkg.yaml";
    const auto metadata = core::readTextFile(metadataPath).text;
    write(metadataPath, metadata + "# same semantic package, different exact snapshot\n");
    assert(!Preparation::validateCurrent(
        *candidate, fixture.root, fixture.manifest.id, request.draft, issue));
    assert(issue == "EDITOR-SCENE-PUBLICATION-PACKAGE-CONFLICT");
    write(metadataPath, metadata);
    assert(Preparation::validateCurrent(
        *candidate, fixture.root, fixture.manifest.id, request.draft, issue));
    assert(&fixture.live.manager() == activeManager && fixture.live.physicsWorld2D() == liveWorld);
    assert(liveWorld->telemetry().bodies == liveTelemetry.bodies
        && liveWorld->telemetry().colliders == liveTelemetry.colliders);
    fixture.unchanged(*document, request);
    std::weak_ptr<resource::ResourceManager> frozenManager = candidate->resources.retainManager();
    const auto* frozenWorld = candidate->resources.physicsWorld2D().get();
    auto frozenDocument = std::move(candidate->document);
    candidate.reset();
    assert(!frozenManager.expired()
        && frozenDocument->resourceManagerProvider()() == frozenManager.lock().get()
        && frozenDocument->physicsWorld2DProvider()().get() == frozenWorld);
    frozenDocument.reset();
    assert(frozenManager.expired());
}

void preparationFailures(Fixture& fixture)
{
    auto document = fixture.draft(resource::deriveResourceUuid("publication-test", "failures"));
    const auto request =
        fixture.request(*document, Preparation::Intent::Register, "assets/failures");
    core::pmr::String diagnostic;
    const auto reject = [&](Preparation::Request value) {
        assert(!Preparation::prepareResources(value, diagnostic) && !diagnostic.empty());
        fixture.unchanged(*document, request);
    };
    auto invalid = request;
    invalid.projectId = resource::deriveResourceUuid("different", "project");
    reject(invalid);
    invalid = request;
    invalid.intent = Preparation::Intent::Update;
    reject(invalid);
    invalid = request;
    invalid.source = fixture.source;
    reject(invalid);
    invalid = request;
    invalid.target.fingerprint = core::Sha256::hexDigest("wrong snapshot");
    reject(invalid);
    invalid = request;
    invalid.target.contentHash.erase(0, std::string_view("sha256:").size());
    reject(invalid); // The public scene hash contract uses the tagged digest.
    invalid = request;
    invalid.target = fixture.source;
    reject(invalid); // Repeated UUID is not an implicit update.
    invalid = request;
    invalid.manifestSnapshot += "# changed\n";
    reject(invalid);
    invalid = request;
    invalid.draft.contentHash = core::Sha256::hexDigest("wrong draft");
    auto candidate = Preparation::prepareResources(invalid, diagnostic);
    assert(candidate && !Preparation::prepareDocument(*candidate, diagnostic));
    assert(!candidate->document && diagnostic == "EDITOR-SCENE-PUBLICATION-DRAFT-MISMATCH");
    invalid = request;
    invalid.draft.yaml = "type: unsupported\n";
    candidate = Preparation::prepareResources(invalid, diagnostic);
    assert(
        candidate && !Preparation::prepareDocument(*candidate, diagnostic) && !candidate->document);
    // A syntactically valid UUID of the wrong Ready type is not a sprite.
    auto badDocument = fixture.draft(request.target.resourceId);
    const auto child = badDocument->scene().findObjectByName("unsaved child")->id();
    assert(badDocument->addComponent("SpriteRendererComponent", child));
    invalid = request;
    assert(Preparation::captureDraft(*badDocument, 7, invalid.draft, diagnostic));
    auto yaml = core::YamlContract::parseSingleDocument(invalid.draft.yaml);
    bool changed = false;
    using Node = core::YamlContract::Node;
    for (auto& object : yaml["objects"].get_value_ref<Node::sequence_type&>()) {
        for (auto& component : object["components"].get_value_ref<Node::sequence_type&>()) {
            if (core::YamlContract::text(component, "type") == "SpriteRendererComponent") {
                assert(component["properties"].contains("spriteResourceId"));
                component["properties"]["spriteResourceId"]["value"] = fixture.source.resourceId;
                changed = true;
            }
        }
    }
    assert(changed);
    invalid.draft.yaml = core::YamlContract::serialize(yaml);
    candidate = Preparation::prepareResources(invalid, diagnostic);
    assert(candidate && !Preparation::prepareDocument(*candidate, diagnostic)
        && !candidate->document && diagnostic != "EDITOR-SCENE-PUBLICATION-DRAFT-MISMATCH");
    fixture.unchanged(*document, request);
    const auto runtimePath = request.target.directory / "scene.yaml";
    const auto runtime = core::readTextFile(runtimePath).text;
    write(runtimePath, runtime + "# changed after closure creation\n");
    assert(!Preparation::prepareResources(request, diagnostic));
    assert(std::filesystem::is_regular_file(runtimePath));
    write(runtimePath, runtime);
    fixture.unchanged(*document, request);
}

void updateSourceFailures(Fixture& fixture)
{
    auto document = fixture.draft(fixture.manifest.entryScene);
    const auto request =
        fixture.request(*document, Preparation::Intent::Update, "assets/source-check");
    core::pmr::String diagnostic;
    auto invalid = request;
    invalid.source = request.target;
    assert(!Preparation::prepareResources(invalid, diagnostic)
        && diagnostic == "EDITOR-SCENE-PUBLICATION-REQUEST-INVALID");
    // An equivalent package is not the explicitly registered source directory.
    const auto copy = fixture.root / "assets/unregistered-source";
    std::filesystem::copy(fixture.source.directory, copy, std::filesystem::copy_options::recursive);
    Preparation::PackageSnapshot unregistered;
    assert(Preparation::inspectPackage(fixture.root,
        "assets/unregistered-source",
        request.target.resourceId,
        *fixture.factory,
        unregistered,
        diagnostic));
    invalid = request;
    invalid.source = unregistered;
    assert(!Preparation::prepareResources(invalid, diagnostic)
        && diagnostic == "EDITOR-SCENE-PUBLICATION-SOURCE-NOT-REGISTERED");
    const auto path = fixture.source.directory / "manifest.arpkg.yaml";
    const auto original = core::readTextFile(path).text;
    write(path, original + "# changed source snapshot\n");
    assert(!Preparation::prepareResources(request, diagnostic)
        && diagnostic == "EDITOR-SCENE-PUBLICATION-PACKAGE-CONFLICT");
    write(path, original);
    auto candidate = Preparation::prepareResources(request, diagnostic);
    assert(candidate && Preparation::prepareDocument(*candidate, diagnostic));
    write(path, original + "# changed source before commit\n");
    assert(!Preparation::validateCurrent(
               *candidate, fixture.root, fixture.manifest.id, request.draft, diagnostic)
        && diagnostic == "EDITOR-SCENE-PUBLICATION-PACKAGE-CONFLICT");
    write(path, original);
    fixture.unchanged(*document, request);
}

void lifetime(Fixture& fixture)
{
    auto document = fixture.draft(resource::deriveResourceUuid("publication-test", "lifetime"));
    const auto request =
        fixture.request(*document, Preparation::Intent::Register, "assets/lifetime");
    core::pmr::String diagnostic;
    const auto cycle = [&] {
        auto candidate = Preparation::prepareResources(request, diagnostic);
        assert(candidate && Preparation::prepareDocument(*candidate, diagnostic));
        assert(Preparation::validateCurrent(
            *candidate, fixture.root, fixture.manifest.id, request.draft, diagnostic));
        auto stale = request.draft;
        ++stale.documentEpoch;
        assert(!Preparation::validateCurrent(
            *candidate, fixture.root, fixture.manifest.id, stale, diagnostic));
    };
    cycle();
    const auto baseline = core::defaultCpuAllocator().stats();
    const auto physics = fixture.live.physicsWorld2D()->telemetry();
    for (unsigned i = 0; i < 20; ++i) {
        cycle();
        const auto stats = core::defaultCpuAllocator().stats();
        assert(stats.liveBytes == baseline.liveBytes
            && stats.allocationCount - stats.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        assert(fixture.live.physicsWorld2D()->telemetry().bodies == physics.bodies
            && fixture.live.physicsWorld2D()->telemetry().colliders == physics.colliders);
    }
    fixture.unchanged(*document, request);
    std::puts(
        "PASS publication preparation CPU smoke: 20 warmed candidate/draft/stale/destroy cycles; core live bytes/count delta = 0; no disk/live publication");
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        lifetime(fixture);
        return 0;
    }
    assert(argc == 1);
    inspectFailures(fixture);
    restoreAndFreshness(fixture, Preparation::Intent::Register, "assets/registered-candidate");
    restoreAndFreshness(fixture, Preparation::Intent::Update, "assets/update-candidate");
    preparationFailures(fixture);
    updateSourceFailures(fixture);
    std::puts(
        "PASS publication preparation: explicit register/update, complete independent resources, unsaved draft/local references, stale/conflict rejection, immutable source/output");
}
