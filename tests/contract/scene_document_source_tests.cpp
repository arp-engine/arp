#include "core/io/file_io.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "resource/resource_graph_executor.h"
#include "scene/scene_asset_resource.h"
#include "scene/scene_component_persistence.h"
#include "scene/script_component_persistence.h"
#include "tests/fixtures/script_component_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>
#include <fstream>

namespace {
using namespace arp;
using namespace resource;
using namespace core::io;

constexpr const char* sceneId = "00000000-0000-4000-8000-0000000000a1";
constexpr const char* prefabId = "00000000-0000-4000-8000-0000000000a2";
constexpr const char* moduleId = "00000000-0000-4000-8000-000000000096";
constexpr std::size_t bytesBudget = 128u * 1024u;
constexpr std::uint32_t objectBudget = 8;

const char* id(bool prefab)
{
    return prefab ? prefabId : sceneId;
}

const char* path(bool prefab)
{
    return prefab ? "prefab.yaml" : "scene.yaml";
}

core::UniquePtr<ResourceBase> candidate(bool prefab,
    ResourceFileOrigin origin,
    core::pmr::String sourcePath = {},
    std::size_t budget = bytesBudget,
    std::uint32_t objects = objectBudget)
{
    if (sourcePath.empty())
        sourcePath = path(prefab);
    if (prefab)
        return core::makeUnique<scene::ScenePrefabResource>(
            id(prefab), std::move(sourcePath), budget, objects, origin);
    return core::makeUnique<scene::SceneAssetResource>(
        id(prefab), std::move(sourcePath), budget, objects, origin);
}

ResourceSourceSet acquired(
    bool prefab, core::StringView text, ResourceFileOrigin origin, core::StringView sourcePath = {})
{
    ResourceSourceSet sources;
    ResourceSourceBuffer source;
    source.path = sourcePath.empty() ? path(prefab) : sourcePath;
    source.pathScope = origin.pathScope;
    source.mount = origin.mount;
    source.bytes.assign(text.begin(), text.end());
    assert(sources.add(std::move(source)));
    return sources;
}

ResourceLoadContext context(const ResourceSourceSet& sources)
{
    ResourceLoadContext result;
    result.sources = &sources;
    result.requirePreloadedSources = true;
    return result;
}

// Use the real ScriptComponent domain fixture and scene encoder. Neither the
// schema nor a script receiver is synthesized by the IO fixture.
class Fixture final {
public:
    Fixture()
    {
        assert(domains.install());
        const auto source = core::readTextFile(ARP_SCENE_SCRIPT_IO_FIXTURE, bytesBudget);
        assert(source.success);
        module = core::makeShared<scriptvm::ScriptComponentModule>();
        assert(module->load(source.text,
            "scene_source",
            moduleId,
            1,
            domains.scope.modules(),
            {"arp_scene"},
            tests::ScriptComponentFixture::budget(),
            objectBudget));
        assert(scene::registerScenePersistence(factory));
        assert(scriptvm::registerScriptComponentPersistence(factory, module));
        {
            scene::Scene authored(
                "source world", scene::SceneId::fromValue(3), scene::SceneExecutionMode::Passive);
            auto& parent = authored.createObject(u8"父节点");
            auto& child = authored.createObject("actor");
            assert(authored.reparent(child.id(), parent.id()).ok());
            parent.addComponent<scene::CameraComponent>(
                scene::CameraComponentDesc::perspective(73, 1.75f, .23f, 321));
            auto component = module->create(child, "Mover", false);
            assert(component);
            auto* fields = component->scriptInstance().ptr<reflection::instance>();
            fields->slot("amount") = scriptvm::makeInt(7);
            fields->slot("speed") = scriptvm::makeFloat(2.5);
            fields->slot("labels") = reflection::object(
                reflection::hlist{scriptvm::makeString(core::StringView("authored"))});
            fields->slot("target") = parent.nativeReflectionObject();
            fields->slot("native") = parent.transform().nativeReflectionObject();
            fields->slot("optional") = scriptvm::makeNil();
            child.addComponent(std::move(component));
            assert(scene::SceneDocumentResource::encode(
                authored, factory, "arp.scene-asset", sceneId, sceneText, diagnostic));
            assert(scene::SceneDocumentResource::encode(
                authored, factory, "arp.scene-prefab", prefabId, prefabText, diagnostic));
        }
        assert(module->liveInstances() == 0 && probes() == 0);
    }

    const core::pmr::String& text(bool prefab) const { return prefab ? prefabText : sceneText; }

    int probes()
    {
        scriptvm::Value value;
        assert(module->session().call("read_probes", {}, value));
        return value.cast<int>();
    }

    void inspect(scene::Scene& world, bool changed = false)
    {
        auto* parent = world.findObjectByName(u8"父节点");
        auto* child = world.findObjectByName("actor");
        assert(parent && child && child->parentId() == parent->id());
        assert(parent->getComponent<scene::CameraComponent>()->desc().verticalFovDegrees == 73);
        const auto receiver = child->get_component("Mover");
        assert(receiver.is<reflection::instance>());
        auto* fields = receiver.ptr<reflection::instance>();
        assert(fields && fields->slot("amount").cast<int>() == 7);
        assert(fields->slot("speed").cast<double>() == 2.5);
        const auto& labels = fields->slot("labels").cast<const reflection::hlist&>();
        assert(labels.size() == 1
            && labels[0].cast<core::pmr::String>() == (changed ? "changed" : "authored"));
        assert(fields->slot("target").cast<scene::GameObject*>() == parent);
        assert(fields->slot("native").cast<scene::Component*>() == &parent->transform());
        assert(scriptvm::isNil(fields->slot("optional")));
    }

    void instantiate(ResourceManager& manager, bool prefab, bool changed = false)
    {
        const auto asset = manager.find<scene::SceneDocumentResource>(id(prefab));
        assert(asset.ready());
        const auto before = probes();
        {
            auto passive = asset.get()->instantiate(
                manager, factory, diagnostic, scene::SceneExecutionMode::Passive);
            if (!passive)
                std::fprintf(stderr, "Passive scene: %s\n", diagnostic.c_str());
            assert(passive);
            inspect(*passive, changed);
            assert(probes() == before && !passive->update({}).ok());
            core::pmr::String encoded;
            assert(scene::SceneDocumentResource::encode(*passive,
                factory,
                prefab ? "arp.scene-prefab" : "arp.scene-asset",
                id(prefab),
                encoded,
                diagnostic));
            // Both documents were produced by this same formal encoder.
            assert(encoded == asset.get()->yaml());
        }
        assert(module->liveInstances() == 0 && probes() == before);
        if (prefab) {
            scene::Scene target(
                "target", scene::SceneId::fromValue(4), scene::SceneExecutionMode::Passive);
            const auto retained = target.createObject("retained").id();
            const auto instance =
                asset.get()->instantiateInto(target, target.rootId(), manager, factory, diagnostic);
            assert(instance.valid() && target.findObject(retained));
            inspect(target, changed);
            assert(probes() == before);
        } else {
            auto runtime = asset.get()->instantiate(manager, factory, diagnostic);
            assert(runtime);
            inspect(*runtime, changed);
            assert(probes() == before); // Restored authored fields skip __init__.
            auto& fresh = runtime->createObject("fresh constructor probe");
            assert(fresh.add_component("Mover").is<reflection::instance>());
            assert(probes() == before + 101);
            assert(runtime->update({0.1f, 0, 32}).ok());
            assert(probes() == before + 121);
        }
        assert(module->liveInstances() == 0);
    }

    tests::ScriptComponentFixture domains;
    core::SharedPtr<scriptvm::ScriptComponentModule> module;
    scene::SceneComponentFactory factory;
    core::pmr::String sceneText, prefabText, diagnostic;
};

struct Completion {
    unsigned calls = 0;
    ResourceGraphExecutionResult result;

    static void complete(void* user, ResourceGraphExecutionResult result)
    {
        auto& self = *static_cast<Completion*>(user);
        ++self.calls;
        self.result = std::move(result);
    }
};

void graph(Fixture& fixture, core::SharedPtr<ManagedFileBackend> backend)
{
    auto mounts =
        core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, bytesBudget, 32, 128});
    const ResourceFileOrigin origin{
        FilePathScope::MountedResource, mounts->mount("scene", backend)};
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    desc.completionQueueCapacity = desc.completionBudget = 4;
    core::runtime::AsyncRuntime runtime(desc);
    assert(runtime.valid());
    ResourceManager manager;
    for (bool prefab : {false, true}) {
        const auto before = fixture.probes();
        Completion completion;
        assert(manager.loadAsyncResource(runtime,
                   candidate(prefab, origin),
                   {1, 1, bytesBudget, bytesBudget, 4},
                   {Completion::complete, &completion})
            == ResourceGraphSubmitStatus::Accepted);
        assert(completion.calls == 0 && !manager.recordSnapshot(id(prefab)));
        unified_io_fixture::wait(runtime.completionQueue(), [&] {
            return completion.calls == 1;
        });
        if (!completion.result.resource.success)
            std::fprintf(stderr,
                "Scene source: %s / %s\n",
                completion.result.resource.diagnostics.c_str(),
                completion.result.diagnosticId ? completion.result.diagnosticId : "none");
        assert(completion.result.resource.success);
        assert(completion.result.telemetry.submittedFileReads == 1);
        assert(completion.result.telemetry.bytesRead == fixture.text(prefab).size());
        assert(fixture.module->liveInstances() == 0 && fixture.probes() == before);
        const auto document = manager.find<scene::SceneDocumentResource>(id(prefab));
        assert(document.get()->yaml() == fixture.text(prefab));
        assert(document.get()->processedByteCount() == fixture.text(prefab).size());
        ResourceManager acquiredManager;
        auto sources = acquired(prefab, fixture.text(prefab), origin);
        assert(acquiredManager.loadResource(candidate(prefab, origin), context(sources)).success);
        assert(acquiredManager.recordSnapshot(id(prefab))->contentHash
            == manager.recordSnapshot(id(prefab))->contentHash);
        fixture.instantiate(manager, prefab);
    }
    runtime.shutdown();
    assert(runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
    assert(mounts->telemetry().inFlight == 0);
}

core::SharedPtr<MemoryFileBackend> memory(Fixture& fixture)
{
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{2, 2 * bytesBudget, 1, bytesBudget, 128});
    for (bool prefab : {false, true}) {
        const auto& text = fixture.text(prefab);
        assert(backend->registerFile(
                   path(prefab), reinterpret_cast<const std::uint8_t*>(text.data()), text.size())
            == FileIoStatus::Success);
    }
    return backend;
}

void contracts(Fixture& fixture)
{
    auto mounts =
        core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, bytesBudget, 32, 128});
    const ResourceFileOrigin origin{
        FilePathScope::MountedResource, mounts->mount("contracts", memory(fixture))};
    for (bool prefab : {false, true}) {
        ResourceManager manager;
        auto sources = acquired(prefab, fixture.text(prefab), origin);
        assert(manager.loadResource(candidate(prefab, origin), context(sources)).success);
        const auto old = manager.find<scene::SceneDocumentResource>(id(prefab));
        const auto* record = manager.recordSnapshot(id(prefab));
        auto same = manager.reloadResource(candidate(prefab, origin), context(sources));
        assert(same.success && same.unchangedContent && old.ready());
        core::pmr::String bom = "\xEF\xBB\xBF";
        bom += fixture.text(prefab);
        auto bomSources = acquired(prefab, bom, origin);
        same = manager.reloadResource(candidate(prefab, origin), context(bomSources));
        assert(same.success && same.unchangedContent);
        for (const auto bytes : {core::StringView{},
                 core::StringView{"\xC0\xAF"},
                 core::StringView{"bad\0yaml", 8},
                 core::StringView{"\xEF\xBB\xBF"},
                 core::StringView{"type: invalid\n"}}) {
            auto bad = acquired(prefab, bytes, origin);
            auto failed = manager.reloadResource(candidate(prefab, origin), context(bad));
            assert(!failed.success && !failed.diagnostics.empty() && old.ready());
            assert(manager.recordSnapshot(id(prefab)) == record);
            assert(manager.generationOf(id(prefab)) == old.generation());
        }
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            auto document = core::YamlContract::parseSingleDocument(fixture.text(prefab));
            if (scenario < 2)
                document["contractVersion"] = scenario == 0 ? 1 : 3;
            if (scenario == 2)
                document["resourceId"] = id(!prefab);
            if (scenario == 3)
                document["type"] = prefab ? "arp.scene-asset" : "arp.scene-prefab";
            auto bad = acquired(prefab, core::YamlContract::serialize(document), origin);
            const auto rejected = manager.reloadResource(candidate(prefab, origin), context(bad));
            assert(
                !rejected.success && old.ready() && manager.recordSnapshot(id(prefab)) == record);
            if (scenario == 0)
                assert(rejected.diagnostics.find("ARP-SCENE-MIGRATION-REQUIRED")
                    != core::pmr::String::npos);
        }
        core::pmr::String changed = fixture.text(prefab);
        const auto at = changed.find("authored");
        assert(at != core::pmr::String::npos);
        changed.replace(at, 8, "changed");
        auto edited = acquired(prefab, changed, origin);
        ResourceBudgetConfig limit;
        limit.maxRetainedRecoveryBytes = fixture.text(prefab).size() - 1;
        manager.setBudgetConfig(limit);
        assert(!manager.reloadResource(candidate(prefab, origin), context(edited)).success);
        assert(old.ready() && record->contentHash == same.telemetry.oldContentHash);
        manager.setBudgetConfig({});
        const auto published = manager.reloadResource(candidate(prefab, origin), context(edited));
        assert(published.success && !published.unchangedContent && old.stale() && !old.get());
        assert(manager.recordSnapshot(id(prefab)) == record
            && manager.generationOf(id(prefab)) == old.generation() + 1);
        fixture.instantiate(manager, prefab, true);
        manager.releaseRetiredResources();
        assert(manager.retainedResourceCount() == 0);
        // Exact provenance is required, even if bytes exist at the same path.
        ResourceManager wrong;
        assert(!wrong.loadResource(candidate(prefab, {}), context(sources)).success);
        for (unsigned scenario = 0; scenario < 6; ++scenario) {
            auto item = candidate(prefab,
                origin,
                scenario == 0 ? "../escape.yaml" : path(prefab),
                scenario == 1       ? 0
                    : scenario == 2 ? SIZE_MAX
                    : scenario == 3 ? 1
                                    : bytesBudget,
                scenario == 4       ? 0
                    : scenario == 5 ? 1
                                    : objectBudget);
            ResourceManager invalid;
            assert(!invalid.loadResource(std::move(item), context(sources)).success);
        }
        for (const auto invalidPath :
            {core::StringView{"scene.yaml\0suffix", 17}, core::StringView{"bad\xFF.yaml", 9}}) {
            auto badSource = acquired(prefab, fixture.text(prefab), origin, invalidPath);
            ResourceManager invalid;
            assert(!invalid
                    .loadResource(candidate(prefab, origin, core::pmr::String(invalidPath)),
                        context(badSource))
                    .success);
        }
        const core::pmr::String unicodePath = u8"文档/场景.yaml";
        auto unicode = acquired(prefab, fixture.text(prefab), origin, unicodePath);
        ResourceManager named;
        assert(
            named.loadResource(candidate(prefab, origin, unicodePath), context(unicode)).success);
        core::pmr::String crlf;
        for (const auto byte : fixture.text(prefab)) {
            if (byte == '\n')
                crlf += '\r';
            crlf += byte;
        }
        auto lines = acquired(prefab, crlf, origin);
        ResourceManager newlines;
        assert(newlines.loadResource(candidate(prefab, origin), context(lines)).success);
        assert(newlines.find<scene::SceneDocumentResource>(id(prefab)).get()->yaml() == crlf);
        assert(newlines.recordSnapshot(id(prefab))->contentHash != same.telemetry.oldContentHash);
    }
    // Unknown script identity is an instantiation failure, never a native or
    // bare-name substitute. Transaction teardown preserves the target scene.
    core::pmr::String unknown = fixture.sceneText;
    const auto moduleAt = unknown.find(moduleId);
    assert(moduleAt != core::pmr::String::npos);
    unknown.replace(
        moduleAt, core::StringView(moduleId).size(), "00000000-0000-4000-8000-000000000098");
    auto unknownSources = acquired(false, unknown, origin);
    ResourceManager unknownManager;
    assert(unknownManager.loadResource(candidate(false, origin), context(unknownSources)).success);
    scene::Scene target(
        "preserved", scene::SceneId::fromValue(5), scene::SceneExecutionMode::Passive);
    const auto retained = target.createObject("retained").id();
    const auto probes = fixture.probes();
    assert(!unknownManager.find<scene::SceneDocumentResource>(sceneId)
            .get()
            ->instantiateInto(
                target, target.rootId(), unknownManager, fixture.factory, fixture.diagnostic)
            .valid());
    assert(
        !fixture.diagnostic.empty() && target.objects().size() == 1 && target.findObject(retained));
    assert(fixture.probes() == probes && fixture.module->liveInstances() == 0);
    mounts->shutdown();
}

void native(Fixture& fixture)
{
    unified_io_fixture::Workspace workspace;
    for (bool prefab : {false, true})
        workspace.write(path(prefab), fixture.text(prefab));
    graph(fixture, core::makeShared<NativeDirectoryFileBackend>(workspace.descriptor()));
    ResourceManager manager;
    for (bool prefab : {false, true}) {
        auto input = candidate(prefab, {}, core::FilePath::utf8(workspace.root / path(prefab)));
        assert(manager.loadResource(std::move(input)).success);
        const ResourceFileOrigin origin{};
        auto source = acquired(prefab, fixture.text(prefab), origin, "literal.yaml");
        ResourceManager prepared;
        assert(prepared.loadResource(candidate(prefab, origin, "literal.yaml"), context(source))
                .success);
        assert(prepared.recordSnapshot(id(prefab))->contentHash
            == manager.recordSnapshot(id(prefab))->contentHash);
        ResourceSourceSet missing;
        ResourceManager noFallback;
        assert(!noFallback
                .loadResource(
                    candidate(prefab, {}, core::FilePath::utf8(workspace.root / path(prefab))),
                    context(missing))
                .success);
    }
}

void admissionFailure(Fixture& fixture)
{
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        auto mounts = core::makeShared<MountedFileBackend>(
            MountedFileBackendDesc{1, 1, bytesBudget, 32, 128});
        const ResourceFileOrigin origin{
            FilePathScope::MountedResource, mounts->mount("source", memory(fixture))};
        core::runtime::AsyncRuntimeDesc desc;
        desc.enableWork = desc.enableThreadPoolFileBackend = false;
        desc.fileBackend = mounts;
        desc.completionQueueCapacity = desc.completionBudget = 4;
        core::runtime::AsyncRuntime runtime(desc);
        ResourceManager manager;
        assert(manager.declareResource(sceneId, "SceneAssetResource").success);
        const auto* before = manager.recordSnapshot(sceneId);
        const auto probes = fixture.probes();
        core::runtime::CancellationSource cancel;
        Completion completion;
        assert(manager.loadAsyncResource(runtime,
                   candidate(false, origin),
                   {1, 1, bytesBudget, bytesBudget, 4},
                   {Completion::complete, &completion},
                   {},
                   cancel.token())
            == ResourceGraphSubmitStatus::Accepted);
        if (scenario < 2) {
            runtime.dispatch(1); // Begin acquisition, with owner processing still queued.
            if (scenario == 0)
                cancel.cancel();
            else
                assert(mounts->unmount(origin.mount));
        } else {
            auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            {
                core::CpuMemoryQuotaScope scope(quota);
                runtime.dispatch(1);
            }
            assert(quota->liveBytes() == 0);
        }
        unified_io_fixture::wait(runtime.completionQueue(), [&] {
            return completion.calls == 1;
        });
        assert(!completion.result.resource.success && completion.calls == 1);
        assert(
            manager.recordSnapshot(sceneId) == before && before->state == ResourceState::Declared);
        assert(manager.generationOf(sceneId) == 0);
        assert(fixture.module->liveInstances() == 0 && fixture.probes() == probes);
        assert(completion.result.fileStatus
            == (scenario == 0       ? FileIoStatus::Cancelled
                    : scenario == 1 ? FileIoStatus::StaleLocation
                                    : FileIoStatus::BudgetExceeded));
        runtime.shutdown();
        assert(
            runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
        assert(!mounts->telemetry().inFlight);
    }
}

void cycle(Fixture& fixture, const char* url)
{
    // Repeat identical gameplay values. A monotonically increasing probe counter
    // otherwise warms different entries of the VM's documented uint16 cache.
    scriptvm::Value reset;
    assert(fixture.module->session().call("reset_probes", {}, reset));
    assert(fixture.probes() == 0);
    if (!url) {
        graph(fixture, memory(fixture));
        return;
    }
    HttpFileBackendDesc desc;
    desc.baseUrl = url;
    desc.maxInFlight = 1;
    desc.maxBytesInFlight = 2 * bytesBudget;
    desc.maxPathBytes = 128;
    desc.maxHeaderBytes = 8192;
    desc.ioChunkBytes = 1024;
    desc.timeoutMs = 3000;
    desc.cancellationPollMs = 2;
    auto backend = core::makeShared<HttpFileBackend>(desc);
    graph(fixture, backend);
    assert(!backend->telemetry().inFlight && !backend->telemetry().activeNativeTransfers);
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    if (argc == 3 && core::StringView(argv[1]) == "--write-fixtures") {
        for (bool prefab : {false, true}) {
            std::ofstream out(std::filesystem::path(argv[2]) / path(prefab), std::ios::binary);
            out.write(fixture.text(prefab).data(), fixture.text(prefab).size());
            assert(out.good());
        }
        return 0;
    }
    const bool quick = argc == 2 && core::StringView(argv[1]) == "--quick";
    const char* url = argc == 3 && core::StringView(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(argc == 1 || quick || url);
    if (!url) {
        contracts(fixture);
        admissionFailure(fixture);
#if !defined(__EMSCRIPTEN__)
        native(fixture);
#endif
    }
    if (quick) {
        cycle(fixture, nullptr);
        std::puts(
            "SceneDocumentSource/1 quick: Scene/Prefab, Text/1, reload and Passive script PASS");
        return 0;
    }
    for (unsigned i = 0; i < 20; ++i)
        cycle(fixture, url);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(fixture, url);
    const auto after = core::defaultCpuAllocator().stats();
    std::fprintf(stderr,
        "Scene lifecycle core: bytes %llu -> %llu; live allocations %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf(
        "Scene source: 20 warmup +20 measured; core bytes %llu -> %llu, live count delta 0\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes));
}
