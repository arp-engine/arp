#include "animation/skinned_mesh_player.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "resource/asset_package_closure.h"
#include "resource/asset_package_payload.h"
#include "tests/fixtures/prefab_runtime_package_fixture.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <thread>

namespace {
using namespace arp;
using namespace resource;
namespace fixture = tests::prefab_runtime_package_fixture;
constexpr std::size_t maxBytes = 16u * 1024u * 1024u;
const std::filesystem::path logicalRoot = "package-preparation-source-only";

void check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

enum class Mutation { None, MissingBlob, CorruptBlob };

ResourceSourceSet snapshot(
    const std::filesystem::path& nativeRoot, Mutation mutation = Mutation::None)
{
    check(!std::filesystem::exists(logicalRoot), "source-only path unexpectedly exists on host");
    ResourceSourceSet sources;
    for (const auto& file : std::filesystem::recursive_directory_iterator(nativeRoot)) {
        if (!file.is_regular_file())
            continue;
        const auto relative = file.path().lexically_relative(nativeRoot);
        const bool blob = relative == "blobs/prefab-runtime.arpa.bin";
        if (blob && mutation == Mutation::MissingBlob)
            continue;
        auto read = core::readBinaryFile(file.path(), maxBytes);
        check(read.success, "fixture source read failed");
        if (blob && mutation == Mutation::CorruptBlob) {
            check(!read.bytes.empty(), "empty fixture blob");
            read.bytes.back() ^= 1;
        }
        ResourceSourceBuffer source;
        source.pathScope = core::io::FilePathScope::HostFilesystem;
        source.path = core::FilePath::utf8(logicalRoot / relative);
        source.bytes = std::move(read.bytes);
        check(sources.add(std::move(source)), "duplicate fixture source");
    }
    return sources;
}

AssetPackageSourceContext context(const ResourceSourceSet& sources)
{
    AssetPackageSourceContext value;
    value.resources.sources = &sources;
    value.resources.requirePreloadedSources = true;
    value.resources.maxDecodedBytes = maxBytes;
    return value;
}

AssetPackagePreparationResult prepare(const ResourceSourceSet& sources)
{
    return InternalAssetPackageReader().prepareSources(logicalRoot, context(sources), maxBytes, 32);
}

void failed(AssetPackagePreparationResult result, const char* expected = nullptr)
{
    check(!result.success && !result.diagnostics.empty() && result.resources.empty()
            && result.package.entries.empty() && result.maxDecodedBytes == 0,
        "failed preparation exposed partial candidates or metadata");
    if (expected)
        check(result.diagnostics.find(expected) != core::pmr::String::npos,
            result.diagnostics.c_str());
}

template <class T> const T* candidate(const AssetPackagePreparationResult& prepared)
{
    for (const auto& value : prepared.resources)
        if (const auto* typed = dynamic_cast<const T*>(value.get()))
            return typed;
    return nullptr;
}

void publishCycle(const ResourceSourceSet& sources, bool worker)
{
    ResourceManager manager;
    const auto owner = std::this_thread::get_id();
    AssetPackagePreparationResult prepared;
    std::thread::id preparedOn;
    std::exception_ptr error;
    const auto work = [&] {
        try {
            preparedOn = std::this_thread::get_id();
            prepared = prepare(sources);
        } catch (...) {
            error = std::current_exception();
        }
    };
    if (worker) {
        std::thread thread(work);
        thread.join();
    } else
        work();
    if (error)
        std::rethrow_exception(error);
    check(prepared.success, prepared.diagnostics.c_str());
    check(!worker || preparedOn != owner, "preparation did not execute on the worker");
    check(prepared.resources.size() == 3 && prepared.package.entries.size() == 3
            && prepared.maxDecodedBytes && prepared.maxDecodedBytes <= maxBytes,
        "typed graph preparation lost package metadata or budget");
    const auto* runtime = candidate<PrefabRuntimePayloadResource>(prepared);
    const auto* animator = candidate<AnimatorControllerResource>(prepared);
    check(runtime && runtime->runtimeAsset && runtime->controllerAsset && animator
            && animator->controllerAsset == runtime->controllerAsset,
        "runtime/controller candidates do not share one decoded payload");
    core::WeakPtr<const assets::AnimatedMeshAsset> mesh = runtime->runtimeAsset;
    core::WeakPtr<const animation::AnimatorControllerAsset> controller = runtime->controllerAsset;
    core::pmr::Vector<core::pmr::String> ordered;
    for (const auto& value : prepared.resources) {
        check(!manager.find<ResourceBase>(value->reference().id).ready(),
            "preparation admitted a Resource before owner publication");
        for (const auto& dependency : value->dependencies())
            check(std::find(ordered.begin(), ordered.end(), dependency.id) != ordered.end(),
                "prepared graph is not in dependency order");
        ordered.push_back(value->reference().id);
    }
    auto load = context(sources).resources;
    load.maxDecodedBytes = prepared.maxDecodedBytes;
    check(manager.loadResourcesAtomically(std::move(prepared.resources), load).success,
        "prepared typed graph could not publish atomically");
    const auto handle = manager.find<PrefabRuntimePayloadResource>(fixture::kRuntimePayloadId);
    check(handle.ready() && handle.get() == runtime
            && manager.find<PrefabResource>(fixture::kPrefabId).ready(),
        "publication copied/replaced the typed candidates or omitted the root");
    {
        animation::SkinnedMeshPlayer player(*runtime->runtimeAsset);
        check(
            player.valid() && player.update("run", .25f), "prepared binary animation cannot play");
    }
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it)
        check(manager.unload(*it).success, "prepared graph unload failed");
    check(!handle.ready() && mesh.expired() && controller.expired(),
        "candidate metadata or manager retained an unloaded Asset");
}

void admissionFailure(const ResourceSourceSet& sources)
{
    auto prepared = prepare(sources);
    check(prepared.success, prepared.diagnostics.c_str());
    const auto* runtime = candidate<PrefabRuntimePayloadResource>(prepared);
    core::WeakPtr<const assets::AnimatedMeshAsset> mesh = runtime->runtimeAsset;
    ResourceManager manager;
    auto original = core::makeUnique<ResourceBase>(ResourceReference{
        "37bc120a-d984-4811-8625-0c2cfed5ef88", "test.original", {}, "Contract Complete", {}});
    const auto originalId = original->reference().id;
    check(manager.loadResource(std::move(original)).success, "sentinel admission failed");
    const auto originalHandle = manager.find<ResourceBase>(originalId);
    const auto* originalPointer = originalHandle.get();
    auto load = context(sources).resources;
    load.commitGate.allow = [](void*) noexcept {
        return false;
    };
    const auto result = manager.loadResourcesAtomically(std::move(prepared.resources), load);
    check(!result.success && mesh.expired() && originalHandle.get() == originalPointer
            && !manager.find<ResourceBase>(fixture::kRuntimePayloadId).ready(),
        "rejected publication retained a candidate or changed the registry");

    prepared = prepare(sources);
    check(prepared.success, prepared.diagnostics.c_str());
    mesh = candidate<PrefabRuntimePayloadResource>(prepared)->runtimeAsset;
    std::size_t gates = 0;
    load.commitGate.userData = &gates;
    load.commitGate.allow = [](void* state) noexcept {
        return ++*static_cast<std::size_t*>(state) < 3;
    };
    const auto rollback = manager.loadResourcesAtomically(std::move(prepared.resources), load);
    check(!rollback.success && rollback.rolledBack && mesh.expired()
            && originalHandle.get() == originalPointer,
        "mid-publication rollback left candidates or damaged existing ownership");
    for (const auto* id :
        {fixture::kRuntimePayloadId, fixture::kAnimatorControllerId, fixture::kPrefabId})
        check(!manager.find<ResourceBase>(id).ready(), "rollback exposed a partial package");
}

void failures(const std::filesystem::path& nativeRoot, const ResourceSourceSet& sources)
{
    failed(prepare(snapshot(nativeRoot, Mutation::MissingBlob)));
    failed(prepare(snapshot(nativeRoot, Mutation::CorruptBlob)));
    auto invalid = context(sources);
    invalid.resources.requirePreloadedSources = false;
    failed(InternalAssetPackageReader().prepareSources(logicalRoot, invalid, maxBytes, 32),
        "PACKAGE-IO-BOUNDED-SOURCE-CONFIG");
    failed(InternalAssetPackageReader().prepareSources(logicalRoot, context(sources), 8, 32));
    failed(InternalAssetPackageReader().prepareSources(logicalRoot, context(sources), maxBytes, 2));
    invalid = context(sources);
    invalid.resources.maxDecodedBytes = 1;
    failed(InternalAssetPackageReader().prepareSources(logicalRoot, invalid, maxBytes, 32));
    auto mutableCodecs = core::makeShared<AssetPackageCodecRegistry>(1);
    failed(InternalAssetPackageReader().prepareSources(
               logicalRoot, context(sources), maxBytes, 32, mutableCodecs),
        "NOT-FROZEN");

    const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    bool refused = false;
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto ignored = prepare(sources);
        refused = !ignored.success;
    } catch (const std::bad_alloc&) {
        refused = true;
    }
    check(refused && quota->liveBytes() == 0, "failed preparation leaked core quota allocations");
}

ResourceSourceSet replaceMetadata(
    const InternalAssetPackageDesc& package, const ResourceSourceSet& original)
{
    const AssetPackageOutputBudget budget{1, 32, maxBytes, maxBytes, 64, maxBytes, 4 * maxBytes};
    auto output = InternalAssetPackageWriter().prepare(package, budget);
    check(output.success, output.diagnostics.c_str());
    ResourceSourceSet result;
    for (auto& file : output.files) {
        ResourceSourceBuffer source;
        source.pathScope = core::io::FilePathScope::HostFilesystem;
        source.path = core::FilePath::utf8(logicalRoot / std::filesystem::path(file.path));
        source.bytes = std::move(file.bytes);
        check(result.add(std::move(source)), "duplicate output metadata");
    }
    for (std::size_t index = 0; index < original.size(); ++index) {
        const auto& source = *original.sourceAt(index);
        if (!result.find(source.path))
            check(result.add(source), "duplicate original payload");
    }
    return result;
}

void graphFailures(const ResourceSourceSet& sources)
{
    auto read =
        InternalAssetPackageReader().readSources(logicalRoot, context(sources), maxBytes, 32);
    check(read.success, read.diagnostics.c_str());
    auto prepared = prepare(sources);
    check(prepared.success, prepared.diagnostics.c_str());
    const auto* runtime = candidate<PrefabRuntimePayloadResource>(prepared);
    const auto rewriteRuntime = [&](core::pmr::Vector<ResourceDependency> dependencies) {
        auto package = read.package;
        package.entries.front() = fixture::makeEntry(fixture::kRuntimePayloadId,
            PrefabRuntimePayloadResource::resourceTypeName(),
            "prefab/runtime/runtime-payload",
            "resources/runtime-payload.arpres.yaml",
            dependencies,
            *runtime);
        return replaceMetadata(package, sources);
    };
    failed(prepare(rewriteRuntime({fixture::dependency(fixture::kPrefabId, "cycle")})));
    auto unknown = read.package;
    ExternalAssetSourceResource source;
    source.name = "unsupported-type";
    unknown.entries.push_back(fixture::makeEntry("90c3ed7f-3c3d-483f-9e47-a6e62371ac25",
        "test.unsupported-package-type",
        "test/unsupported-package-type",
        "resources/unsupported.arpres.yaml",
        {},
        source));
    failed(prepare(replaceMetadata(unknown, sources)), "TYPED-FACTORY-REQUIRED");
}

void closureBudget()
{
    const auto nativeRoot = std::filesystem::current_path() / "package-preparation-closed-fixture";
    InternalAssetPackageDesc package;
    package.name = "closed-budget-metadata";
    package.closurePath = "closure.yaml";
    package.status = "Contract Complete";
    ExternalAssetSourceResource source;
    source.name = "closed-budget-metadata";
    source.sourcePath = "authoring/synthetic";
    package.entries.push_back(fixture::makeEntry("90c3ed7f-3c3d-483f-9e47-a6e62371ac25",
        ExternalAssetSourceResource::resourceTypeName(),
        "source/closed-budget",
        "resources/source.arpres.yaml",
        {},
        source));
    for (auto& entry : package.entries) {
        auto envelope = core::YamlContract::parseSingleDocument(entry.envelopeYaml);
        envelope["status"] = "Contract Complete";
        entry.status = "Contract Complete";
        entry.envelopeYaml = core::YamlContract::serialize(envelope);
    }
    const auto closure = AssetPackageClosureService().prepareIndex(
        package, nativeRoot, {32, 64, maxBytes, maxBytes, maxBytes});
    check(closure.success, closure.diagnostics.c_str());
    const auto sources = snapshot(nativeRoot);
    auto prepared = prepare(sources);
    check(prepared.success && prepared.maxDecodedBytes == maxBytes,
        "valid closed package preparation failed");
    auto limited = context(sources);
    limited.resources.maxDecodedBytes = 1;
    auto smaller = InternalAssetPackageReader().prepareSources(logicalRoot, limited, maxBytes, 32);
    check(smaller.success && smaller.maxDecodedBytes == 1,
        "closure replaced the caller decode budget during preparation");
}
} // namespace

int main(int argc, char** argv)
{
    try {
        registerPrefabAssetReflection();
        (void)prefabResourceFactory();
        const auto nativeRoot = std::filesystem::current_path() / "package-preparation-fixture";
        fixture::write(nativeRoot);
        const auto sources = snapshot(nativeRoot);
        publishCycle(sources, false);
        failures(nativeRoot, sources);
        graphFailures(sources);
        admissionFailure(sources);
        const auto baseline = core::defaultCpuAllocator().stats().liveBytes;
        const bool smoke = argc == 2 && core::StringView(argv[1]) == "--smoke";
        for (unsigned cycle = 0; cycle < (smoke ? 20u : 1u); ++cycle) {
            publishCycle(sources, true);
            check(core::defaultCpuAllocator().stats().liveBytes == baseline,
                "warmed prepare/publish/unload cycle leaked core allocations");
        }
        closureBudget();
        std::puts(
            "PASS source-only worker typed preparation, delayed atomic publication/rollback, "
            "single runtime decode, failed budgets/sources/codecs and warmed allocation baseline");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
