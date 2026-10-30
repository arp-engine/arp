#include "2d/components.h"
#include "2d/physics_components.h"
#include "3d/components.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/tests/fixtures/editor_script_document_fixture.h"
#include "editor/workspace/editor_scene_presence.h"
#include "editor/workspace/editor_scene_script_codec.h"
#include "reflection/instance.h"
#include "tests/fixtures/unified_io_fixture.h"

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

// The mounted root is deliberately unrelated to Fixture::root. All domain and
// codec work below must consume acquired bytes, including after native files
// have become invalid. Actual IO is isolated here, outside cold preparation.
class SourceFixture final {
public:
    static constexpr std::size_t fileBytes = 65536;
    static constexpr std::uint64_t epoch = 41;

    explicit SourceFixture(Fixture& files) : files_(files)
    {
        ProjectDiagnostic issue;
        check(ProjectManifestCodec::encode(files.manifest, manifest_, issue));
        primary_ = "\xef\xbb\xbf";
        primary_ += primary;
        std::filesystem::create_directories(files.root / root);
        files.write("workspace/project.arp.yaml", manifest_);
        files.write("workspace/primary.arp", primary_);
        files.write("workspace/secondary.arp", secondary);
    }

    core::SharedPtr<core::io::ManagedFileBackend> backend(unsigned mode, std::string_view url)
    {
        using namespace core::io;
        if (mode == 1) {
            auto desc = workspace_.descriptor();
            desc.rootPath = core::FilePath::utf8(files_.root);
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = fileBytes;
            desc.maxScanEntries = 32;
            return core::makeShared<NativeDirectoryFileBackend>(desc);
        }
        if (mode == 2) {
            HttpFileBackendDesc desc;
            desc.baseUrl = url;
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = fileBytes;
            desc.maxPathBytes = 512;
            desc.maxHeaderBytes = 8192;
            desc.ioChunkBytes = 4096;
            desc.timeoutMs = 3000;
            desc.cancellationPollMs = 2;
            return core::makeShared<HttpFileBackend>(desc);
        }
        auto result = core::makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{4, 3 * fileBytes, 1, fileBytes, 512});
        check(result->registerDirectory("workspace") == FileIoStatus::Success);
        const auto add = [&](const char* name, std::string_view bytes) {
            check(result->registerFile(
                      name, reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())
                    == FileIoStatus::Success,
                result->diagnostic() ? result->diagnostic() : "");
        };
        add("workspace/project.arp.yaml", manifest_);
        add("workspace/primary.arp", primary_);
        add("workspace/secondary.arp", secondary);
        return result;
    }

    ProjectSourceContext acquire(core::io::FileService& files,
        core::runtime::CompletionQueue& queue,
        core::PoolHandle mount,
        bool complete = true)
    {
        ProjectSourceSession source(
            files, {mount, "workspace", epoch}, {3, fileBytes, 3 * fileBytes});
        for (const auto* name : {"project.arp.yaml", "primary.arp", "secondary.arp"}) {
            if (!complete && std::string_view(name) == "secondary.arp")
                break;
            check(source.read(name, fileBytes) == core::io::FileIoStatus::Accepted);
            unified_io_fixture::wait(queue, [&] {
                return source.state() != ProjectSourceState::Pending;
            });
            check(source.state() == ProjectSourceState::Ready,
                source.diagnostic() ? source.diagnostic() : "");
        }
        auto snapshot = source.finish(epoch);
        check(bool(snapshot));
        return {std::move(snapshot), &files, epoch};
    }

    void emit(const std::filesystem::path& destination)
    {
        std::filesystem::copy(
            files_.root / root, destination / root, std::filesystem::copy_options::recursive);
    }

    const std::filesystem::path root{"workspace"};

private:
    Fixture& files_;
    unified_io_fixture::Workspace workspace_;
    core::pmr::String manifest_, primary_;
};

void sourceCycle(Fixture& files, SourceFixture& fixture, unsigned mode, std::string_view url)
{
    using namespace core::io;
    core::runtime::CompletionQueue queue{{8}};
    MountedFileBackend mounts{{1, 1, SourceFixture::fileBytes, 32, 512}};
    FileService io{queue, mounts};
    const auto mount = mounts.mount("fixture", fixture.backend(mode, url));
    check(bool(mount));
    const auto source = fixture.acquire(io, queue, mount);
    const auto count = io.telemetry().submitted;
    check(count == 3);
    EditorSceneDocument document(false);
    core::pmr::String diagnostic;
    check(document.bindServiceSession(Fixture::services(), diagnostic), diagnostic);
    const auto epoch = document.ownerTicket();
    auto invalid = source;
    invalid.epoch += 1;
    check(!document.prepareScriptAuthoringSources(
        invalid, files.manifest, Fixture::budget(), diagnostic));
    check(epoch.sameCurrentEpoch(document.ownerTicket()) && !document.scriptCatalog());
    invalid = {};
    check(!document.prepareScriptAuthoringSources(
        invalid, files.manifest, Fixture::budget(), diagnostic));
    check(epoch.sameCurrentEpoch(document.ownerTicket()) && !document.scriptCatalog());
    const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope limited(quota);
        try {
            check(!document.prepareScriptAuthoringSources(
                source, files.manifest, Fixture::budget(), diagnostic));
        } catch (const std::bad_alloc&) {
            // A bounded diagnostic string can itself exhaust this injected quota.
        }
    }
    check(quota->liveBytes() == 0 && epoch.sameCurrentEpoch(document.ownerTicket())
        && !document.scriptCatalog());
    check(document.prepareScriptAuthoringSources(
              source, files.manifest, Fixture::budget(), diagnostic),
        diagnostic);
    const auto name = document.scene().name();
    check(name != "InitializerExecuted" && name != "ConstructorExecuted");
    const auto catalog = document.scriptCatalog();
    check(catalog && catalog->entries().size() == 2);
    for (const auto& entry : catalog->modules().entries())
        check(entry.module->liveInstances() == 0);
    const auto graph = populate(document);
    const auto old = document.scene().findComponent(graph.rootScript)->scriptInstance();
    auto input = inputs(files);
    // The logical root has no relation to the physical fixture directory.
    EditorSceneScriptInputs mounted{
        fixture.root, files.manifest, input.modules, input.fields, input.preparationBytes, &source};
    core::pmr::String yaml, restored;
    check(Codec::encode(document, mounted, yaml, diagnostic), diagnostic);
    check(document.scene().name() == "Actual authoring root");
    auto withoutSource = mounted;
    withoutSource.sources = nullptr;
    restored = "unchanged";
    check(!Codec::encode(document, withoutSource, restored, diagnostic) && restored == "unchanged");
    const std::filesystem::path wrongRoot("different-source-root");
    EditorSceneScriptInputs wrong{
        wrongRoot, files.manifest, input.modules, input.fields, input.preparationBytes, &source};
    check(!Codec::encode(document, wrong, restored, diagnostic) && restored == "unchanged");
    check(Codec::decode(yaml, document, mounted, diagnostic), diagnostic);
    check(!old.ptr<reflection::instance>()->alive());
    check(
        document.scene().name() == "Actual authoring root" && document.selectedObject() == graph.b);
    for (const auto& entry : catalog->modules().entries())
        check(entry.module->liveInstances() == 0);
    check(Codec::encode(document, mounted, restored, diagnostic) && restored == yaml, diagnostic);
    check(io.telemetry().submitted == count); // No parser/domain rereads.
    const auto incomplete = fixture.acquire(io, queue, mount, false);
    auto missing = mounted;
    missing.sources = &incomplete;
    core::UniquePtr<EditorSceneDocument> candidate;
    check(Codec::prepare(yaml, document, mounted, candidate, diagnostic), diagnostic);
    const auto* kept = candidate.get();
    check(
        !Codec::prepare(yaml, document, missing, candidate, diagnostic) && candidate.get() == kept);
    auto stale = source;
    stale.epoch += 1;
    missing.sources = &stale;
    check(
        !Codec::prepare(yaml, document, missing, candidate, diagnostic) && candidate.get() == kept);
    const auto liveEpoch = document.ownerTicket();
    check(mounts.unmount(mount));
    restored = "unchanged";
    check(!Codec::encode(document, mounted, restored, diagnostic) && restored == "unchanged");
    check(!document.prepareReplacement(*candidate, diagnostic));
    check(liveEpoch.sameCurrentEpoch(document.ownerTicket())
        && document.scene().name() == "Actual authoring root");
    check(Codec::observeSnapshot(document, mounted, restored, diagnostic), diagnostic);
    check(restored == yaml); // Retained observation stays IO-free after unmount.
    check(io.telemetry().submitted == count + 2);
}
} // namespace

int main(int argc, char** argv)
{
    Fixture files;
    files.write("primary.arp", primary);
    files.write("secondary.arp", secondary);
    const auto option = argc >= 2 ? std::string_view(argv[1]) : std::string_view{};
    const bool source = option == "--source" || option == "--source-smoke"
        || option == "--source-native" || option == "--source-http" || option == "--source-emit";
    if (source) {
        SourceFixture fixture(files);
        if (std::string_view(argv[1]) == "--source-emit") {
            check(argc == 3);
            fixture.emit(std::filesystem::u8path(argv[2]));
            return 0;
        }
        const unsigned mode = std::string_view(argv[1]) == "--source-native" ? 1
            : std::string_view(argv[1]) == "--source-http"                   ? 2
                                                                             : 0;
        check(mode != 2 || argc == 3);
        const auto run = [&] {
            sourceCycle(files, fixture, mode, mode == 2 ? argv[2] : "");
        };
        if (std::string_view(argv[1]) == "--source") {
            run();
            std::puts(
                "PASS explicit source domains/v4 identities/references; no IO or user execution during cold prepare");
            return 0;
        }
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto after = core::defaultCpuAllocator().stats();
        std::printf(
            "Source v4 cold preparation 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        check(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        return 0;
    }
    check(argc == 1 || (argc == 2 && option == "--smoke"));
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
