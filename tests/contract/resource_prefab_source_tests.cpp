#include "core/io/file_io.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "resource/internal_asset_package_loader.h"
#include "tests/fixtures/prefab_runtime_package_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace arp::tests::prefab_runtime_package_fixture;
constexpr std::size_t maxBytes = 1024 * 1024;
constexpr const char* blobPath = "blobs/prefab-runtime.arpa.bin";
constexpr const char* sentinelId = "9a6ff1f5-737b-4ae9-9892-85e36bb7656e";

class PrefabSourceFixture final {
public:
    enum class Mutation { None, Missing, Corrupt, Truncated };
    enum class Action { None, Cancel, Remount, DecodeBudget };

    static core::SharedPtr<core::io::MemoryFileBackend> memory(
        const std::filesystem::path& root, Mutation mutation = Mutation::None)
    {
        const auto package = resource::InternalAssetPackageReader().read(root);
        assert(package.success);
        core::pmr::Vector<core::pmr::String> paths{"manifest.arpkg.yaml", blobPath};
        for (const auto& entry : package.package.entries)
            paths.push_back(entry.envelopePath);
        auto backend = core::makeShared<core::io::MemoryFileBackend>(
            core::io::MemoryFileBackendDesc{paths.size() + 3, maxBytes, 4, maxBytes, 1024});
        for (const auto* directory : {"snapshot", "snapshot/blobs", "snapshot/resources"})
            assert(backend->registerDirectory(directory) == core::io::FileIoStatus::Success);
        for (const auto& path : paths) {
            if (path == blobPath && mutation == Mutation::Missing)
                continue;
            auto bytes = core::readBinaryFile(
                root / std::filesystem::u8path(path.begin(), path.end()), maxBytes);
            assert(bytes.success);
            if (path == blobPath && mutation == Mutation::Corrupt)
                bytes.bytes[0] ^= 1;
            if (path == blobPath && mutation == Mutation::Truncated)
                bytes.bytes.pop_back();
            const auto logical = core::pmr::String("snapshot/") + path;
            assert(backend->registerFile(logical, bytes.bytes.data(), bytes.bytes.size())
                == core::io::FileIoStatus::Success);
        }
        return backend;
    }

    static core::SharedPtr<core::io::HttpFileBackend> http(const char* url)
    {
        core::io::HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 2;
        desc.maxBytesInFlight = maxBytes;
        desc.maxPathBytes = 1024;
        desc.maxHeaderBytes = desc.ioChunkBytes = 4096;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 2;
        return core::makeShared<core::io::HttpFileBackend>(desc);
    }

    static void load(core::SharedPtr<core::io::ManagedFileBackend> backend,
        bool expected = true,
        Action action = Action::None)
    {
        using namespace core::io;
        auto router =
            core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 4, maxBytes, 32, 1024});
        const auto mount = router->mount("prefab", backend);
        assert(mount);
        core::runtime::AsyncRuntimeDesc desc;
        desc.enableWork = desc.enableThreadPoolFileBackend = false;
        desc.fileBackend = router;
        desc.completionQueueCapacity = desc.completionBudget = 8;
        core::runtime::AsyncRuntime runtime(desc);
        assert(runtime.valid());
        resource::ResourceManager manager;
        assert(manager
                .loadResource(core::makeUnique<resource::ResourceBase>(resource::ResourceReference{
                    sentinelId, "Sentinel", "retained", "Contract Complete", {}}))
                .success);
        const auto generation = manager.generationOf(sentinelId);
        resource::AssetPackageLoadRequest request;
        request.directory = "snapshot";
        assert(!std::filesystem::exists(request.directory));
        request.origin = {FilePathScope::MountedResource, mount};
        request.budget = {65536, 8, 16, maxBytes, maxBytes, maxBytes};
        if (action == Action::DecodeBudget)
            request.budget.maxDecodedBytes = 1;
        core::runtime::CancellationSource cancel;
        request.cancellation = cancel.token();

        struct Probe {
            unsigned calls = 0;
            resource::AssetPackageLoadResult result;
        } probe;

        const auto status = resource::InternalAssetPackageLoader(
            manager, runtime.fileService(), runtime.completionQueue())
                                .submit(std::move(request),
                                    {[](void* context, resource::AssetPackageLoadResult result) {
                                         auto& probe = *static_cast<Probe*>(context);
                                         ++probe.calls;
                                         probe.result = std::move(result);
                                     },
                                        &probe});
        assert(status == FileIoStatus::Accepted && probe.calls == 0);
        if (action == Action::Cancel || action == Action::Remount) {
            runtime.dispatch(1);
            assert(probe.calls == 0);
            if (action == Action::Cancel)
                cancel.cancel();
            else {
                assert(router->unmount(mount));
                assert(router->mount("prefab", backend));
            }
        }
        unified_io_fixture::wait(runtime.completionQueue(), [&] {
            return probe.calls != 0;
        });
        if (probe.result.resource.success != expected)
            std::fprintf(stderr,
                "prefab: %s / %s\n",
                probe.result.diagnosticId ? probe.result.diagnosticId : "no diagnostic",
                probe.result.resource.diagnostics.c_str());
        assert(probe.calls == 1 && probe.result.telemetry.completions == 1);
        assert(probe.result.resource.success == expected
            && probe.result.telemetry.committed == expected);
        assert(manager.find<resource::ResourceBase>(sentinelId).ready()
            && manager.generationOf(sentinelId) == generation);
        if (expected) {
            assert(probe.result.telemetry.submittedReads == 5);
            const auto payload =
                manager.find<resource::PrefabRuntimePayloadResource>(kRuntimePayloadId);
            const auto controller =
                manager.find<resource::AnimatorControllerResource>(kAnimatorControllerId);
            assert(manager.find<resource::PrefabResource>(kPrefabId).ready());
            assert(payload.ready() && controller.ready() && payload.get()->runtimeAsset);
            assert(payload.get()->runtimeAsset->mesh.validForIndexedDraw());
            assert(payload.get()->runtimeAsset->clips.size() == 2);
            assert(
                payload.get()->runtimeAsset->materials.front().baseColorTextureCoordinateSet == 2);
            assert(payload.get()->controllerAsset == controller.get()->controllerAsset);
            assert(controller.get()->controllerAsset->states.size() == 2
                && controller.get()
                        ->controllerAsset->transitions.front()
                        .conditions.front()
                        .parameter
                    == "moving");
        } else {
            assert(!manager.recordSnapshot(kRuntimePayloadId));
            assert(!manager.recordSnapshot(kAnimatorControllerId));
            assert(!manager.recordSnapshot(kPrefabId));
            if (action == Action::DecodeBudget)
                assert(probe.result.resource.diagnostics == "PACKAGE-PREFAB-DECODED-BUDGET");
        }
        runtime.shutdown();
        assert(
            runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
        assert(router->telemetry().inFlight == 0);
        if (const auto* http = dynamic_cast<const HttpFileBackend*>(backend.get()))
            assert(http->telemetry().inFlight == 0 && http->telemetry().activeNativeTransfers == 0);
        if (const auto* native = dynamic_cast<const NativeDirectoryFileBackend*>(backend.get()))
            assert(native->telemetry().openNativeObjects == 0);
    }

    static void sourceContracts(const std::filesystem::path& root)
    {
        const auto package = resource::InternalAssetPackageReader().read(root);
        assert(package.success);
        auto entry = package.package.entries.front();
        auto payload = resource::createPrefabResourceFromEnvelope(entry.envelopeYaml, entry.status);
        assert(payload);
        auto& metadata = *dynamic_cast<resource::PrefabRuntimePayloadResource*>(payload.get());
        const auto original = core::readBinaryFile(root / blobPath, maxBytes);
        assert(original.success);
        const core::PoolHandle mount{&package, 0, 1};
        const resource::ResourceFileOrigin origin{core::io::FilePathScope::MountedResource, mount};
        auto test = [&](const core::pmr::Vector<std::uint8_t>& bytes,
                        bool expected,
                        const char* diagnostic,
                        bool wrongOrigin = false,
                        std::size_t budget = maxBytes) {
            resource::ResourceSourceSet set;
            resource::ResourceSourceBuffer source;
            source.path = "snapshot/blobs/prefab-runtime.arpa.bin";
            source.pathScope = origin.pathScope;
            source.mount = wrongOrigin ? core::PoolHandle{&package, 0, 2} : mount;
            source.bytes = bytes;
            assert(set.add(std::move(source)));
            resource::AssetPackageSourceContext context{{{}, &set, true, budget}, origin};
            const auto result = resource::PrefabRuntimePayloadSourceReader().readSources(
                entry, "snapshot", context);
            assert(result.success == expected);
            if (diagnostic)
                assert(result.diagnostics.find(diagnostic) != core::pmr::String::npos);
            return result.decodedBytes;
        };
        const auto decodedBytes = test(original.bytes, true, nullptr);
        assert(decodedBytes > 1 && decodedBytes < maxBytes);
        test(original.bytes, true, nullptr, false, decodedBytes);
        test(original.bytes, false, "PACKAGE-PREFAB-DECODED-BUDGET", false, decodedBytes - 1);
        test(original.bytes, false, "ARP-RESOURCE-SOURCE-LOCATION-MISMATCH", true);
        test(original.bytes, false, "PACKAGE-PREFAB-SOURCE-CONTEXT", false, 0);

        // Full candidate replacement must preserve existing Ready objects when
        // decoding fails. The exact budget also proves shared controller data
        // is charged once, rather than once per referencing resource.
        resource::ResourceSourceSet complete;
        core::pmr::Vector<core::pmr::String> paths{"manifest.arpkg.yaml", blobPath};
        for (const auto& e : package.package.entries)
            paths.push_back(e.envelopePath);
        for (const auto& path : paths) {
            auto bytes = core::readBinaryFile(
                root / std::filesystem::u8path(path.begin(), path.end()), maxBytes);
            assert(bytes.success);
            resource::ResourceSourceBuffer source;
            source.path = core::pmr::String("snapshot/") + path;
            source.pathScope = origin.pathScope;
            source.mount = mount;
            source.bytes = std::move(bytes.bytes);
            assert(complete.add(std::move(source)));
        }
        resource::AssetPackageSourceContext completeContext{
            {{}, &complete, true, decodedBytes}, origin};
        resource::ResourceManager manager;
        assert(resource::InternalAssetPackageReader()
                .loadSources("snapshot", manager, completeContext)
                .success);
        const auto old = manager.find<resource::PrefabRuntimePayloadResource>(kRuntimePayloadId);
        const auto oldController =
            manager.find<resource::AnimatorControllerResource>(kAnimatorControllerId);
        --completeContext.resources.maxDecodedBytes;
        assert(!resource::InternalAssetPackageReader()
                .loadSources("snapshot", manager, completeContext)
                .success);
        assert(old.ready() && manager.generationOf(kRuntimePayloadId) == old.generation());
        assert(manager.find<resource::PrefabRuntimePayloadResource>(kRuntimePayloadId).get()
            == old.get());
        assert(oldController.ready()
            && manager.generationOf(kAnimatorControllerId) == oldController.generation());
        completeContext.resources.maxDecodedBytes = maxBytes;
        resource::ResourceManager limitedManager;
        resource::ResourceBudgetConfig limits;
        limits.maxDecodedBytes = decodedBytes - 1;
        limitedManager.setBudgetConfig(limits);
        assert(!resource::InternalAssetPackageReader()
                .loadSources("snapshot", limitedManager, completeContext)
                .success);
        assert(!limitedManager.recordSnapshot(kRuntimePayloadId));

        metadata.defaultClipName = "missing";
        entry = makeEntry(kRuntimePayloadId,
            "PrefabRuntimePayloadResource",
            "runtime",
            "resources/runtime.arpres.yaml",
            {},
            metadata);
        test(original.bytes, false, "default clip metadata mismatch");
        metadata.defaultClipName = "idle";

        auto malformed = original.bytes;
        const std::uint32_t unknownVersion = UINT32_MAX;
        std::memcpy(
            malformed.data() + sizeof(std::uint32_t), &unknownVersion, sizeof(unknownVersion));
        std::uint32_t hash = 2166136261u;
        for (const auto byte : malformed) {
            hash ^= byte;
            hash *= 16777619u;
        }
        metadata.blobHash = static_cast<int>(hash);
        entry = makeEntry(kRuntimePayloadId,
            "PrefabRuntimePayloadResource",
            "runtime",
            "resources/runtime.arpres.yaml",
            {},
            metadata);
        test(malformed, false, "version is unsupported");
        malformed = original.bytes;
        // Header schema: magic/version, length-prefixed default state, count.
        const auto countOffset = 3 * sizeof(std::uint32_t) + metadata.defaultStateName.size();
        std::memcpy(malformed.data() + countOffset, &unknownVersion, sizeof(unknownVersion));
        hash = 2166136261u;
        for (const auto byte : malformed) {
            hash ^= byte;
            hash *= 16777619u;
        }
        metadata.blobHash = static_cast<int>(hash);
        entry = makeEntry(kRuntimePayloadId,
            "PrefabRuntimePayloadResource",
            "runtime",
            "resources/runtime.arpres.yaml",
            {},
            metadata);
        test(malformed, false, "state bindings are invalid");
        metadata.blobPath = "../outside.bin";
        entry = makeEntry(kRuntimePayloadId,
            "PrefabRuntimePayloadResource",
            "runtime",
            "resources/runtime.arpres.yaml",
            {},
            metadata);
        assert(!resource::PrefabRuntimePayloadSourceReader()
                .planSourceReads(entry, "snapshot", origin)
                .success);
    }
};
} // namespace

int main(int argc, char** argv)
{
    resource::registerPrefabAssetReflection();
    if (argc == 3 && core::StringView(argv[1]) == "--prepare") {
        write(std::filesystem::u8path(argv[2]));
        return 0;
    }
    if (argc == 3 && core::StringView(argv[1]) == "--http") {
        for (unsigned i = 0; i < 20; ++i)
            PrefabSourceFixture::load(PrefabSourceFixture::http(argv[2]));
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            PrefabSourceFixture::load(PrefabSourceFixture::http(argv[2]));
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::puts("Prefab real HTTP 20 warmup + 20 measured cycles: core bytes/count delta 0");
        return 0;
    }
    unified_io_fixture::Workspace workspace;
    const auto root = workspace.root / "snapshot";
    write(root);
    PrefabSourceFixture::sourceContracts(root);
    for (auto mutation : {PrefabSourceFixture::Mutation::None,
             PrefabSourceFixture::Mutation::Missing,
             PrefabSourceFixture::Mutation::Corrupt,
             PrefabSourceFixture::Mutation::Truncated})
        PrefabSourceFixture::load(PrefabSourceFixture::memory(root, mutation),
            mutation == PrefabSourceFixture::Mutation::None);
    for (auto action : {PrefabSourceFixture::Action::Cancel,
             PrefabSourceFixture::Action::Remount,
             PrefabSourceFixture::Action::DecodeBudget})
        PrefabSourceFixture::load(PrefabSourceFixture::memory(root), false, action);
#if !defined(__EMSCRIPTEN__)
    PrefabSourceFixture::load(
        core::makeShared<core::io::NativeDirectoryFileBackend>(workspace.descriptor()));
#endif
    for (unsigned i = 0; i < 20; ++i)
        PrefabSourceFixture::load(PrefabSourceFixture::memory(root));
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        PrefabSourceFixture::load(PrefabSourceFixture::memory(root));
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::puts(
        "Prefab memory/native source contracts PASS; 20 warmup + 20 measured core bytes/count delta 0");
}
