#include "core/encoding/binary_stream.h"
#include "core/io/blocking_file_reader.h"
#include "core/io/file_io.h"
#include "core/io/thread_pool_file_backend.h"
#include "resource/cooked_resource.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_source_io.h"
#include "tests/fixtures/prefab_runtime_package_fixture.h"
#include "tests/fixtures/ui_atlas_pixels.h"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace arp;
using namespace resource;

namespace {
void check(bool b, const char* error)
{
    if (!b)
        throw std::runtime_error(error);
}

constexpr std::array<const char*, 5> ids = {"165b5ee8-2514-46e7-8472-1bfa17c1e226",
    "94d1a54f-2719-41d1-b37c-5b5d266feb52",
    "932adff7-c6cb-4aaf-a4c2-a54e1a299f12",
    "62365463-2481-49c6-a196-5e68ad9f5a45",
    "0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1"};

struct Probe : ResourceBase {
    static constexpr const char* resourceTypeName() { return "CookedProbe"; }

    int seed = 0;
    CookedBytes data{&core::defaultCpuMemoryResource()};

    std::size_t processedByteCount() const noexcept override { return data.size(); }
};

struct Counts {
    std::array<std::atomic<unsigned>, 5> prepared{};
};

class Codec final : public ReflectedCookedResourceCodec<Probe> {
public:
    explicit Codec(Counts& counts) : counts_(counts) {}

    CookedBytes cookPayload(const ResourceBase& base,
        const std::filesystem::path&,
        const CookedResourceBudget&) const override
    {
        const auto seed = dynamic_cast<const Probe&>(base).seed;
        return CookedBytes(
            65536, static_cast<std::uint8_t>(seed), &core::defaultCpuMemoryResource());
    }

    CookedResourceResult preparePayload(
        ResourceBase& base, core::ArrayView<const std::uint8_t> b, std::size_t limit) const override
    {
        auto& p = dynamic_cast<Probe&>(base);
        if (b.size() > limit || b.size() != 65536 || b[0] != p.seed || b[b.size() - 1] != p.seed)
            return {false, "probe payload"};
        p.data.assign(b.begin(), b.end());
        ++counts_.prepared[p.seed];
        return {true};
    }

private:
    Counts& counts_;
};

class ObservedBackend final : public core::io::ManagedFileBackend {
public:
    core::io::FileBackendCapability capability() const noexcept override
    {
        return {core::io::FileBackendKind::ThreadPoolFallbackFileBackend,
            core::io::FileBackendStatus::PrototypeComplete,
            false,
            true,
            true,
            false,
            false,
            false,
            !closed_};
    }

    core::io::FileReadResult validateRead(const core::io::FileReadRequest& r) const override
    {
        return core::io::BlockingFileReader().validateRead(r);
    }

    core::io::FileIoStatus readAsync(const core::io::FileReadRequest& r,
        core::io::FileReadCompletion c,
        core::runtime::CompletionQueue& q) noexcept override
    {
        try {
            if (closed_)
                return core::io::FileIoStatus::ServiceClosed;
            if (!workers_) {
                workers_ = core::makeUnique<core::runtime::WorkService>(
                    core::runtime::WorkServiceDesc{&q, 2, 64});
                files_ = core::makeUnique<core::io::ThreadPoolFallbackFileBackend>(*workers_);
            }
            reads.push_back(r);
            return files_->readAsync(r, c, q);
        } catch (...) {
            return core::io::FileIoStatus::BudgetExceeded;
        }
    }

    void shutdown() noexcept override
    {
        closed_ = true;
        if (workers_)
            workers_->shutdown();
    }

    core::pmr::Vector<core::io::FileReadRequest> reads{&core::defaultCpuMemoryResource()};

private:
    bool closed_ = false;
    core::UniquePtr<core::runtime::WorkService> workers_;
    core::UniquePtr<core::io::ThreadPoolFallbackFileBackend> files_;
};

ResourceDomainBudget domainBudget()
{
    return {32, 32, 32, 4, {32, 32, 4 * 1024 * 1024, 4 * 1024 * 1024, 32}};
}

std::size_t readyCount(const ResourceManager& manager)
{
    std::size_t count = 0;
    for (const auto* id : ids)
        count += manager.stateOf(id) == ResourceState::Ready;
    return count;
}

void write(const std::filesystem::path& path, const CookedBytes& bytes)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    check(bool(f), "write fixture");
}

void run(const std::filesystem::path& root)
{
    Counts counts;
    auto codecs = core::makeShared<CookedResourceCodecRegistry>(1);
    check(codecs->add(core::makeShared<Codec>(counts)) && codecs->freeze(), "registry");
    core::pmr::Vector<ResourceCookInput> inputs{&core::defaultCpuMemoryResource()};
    for (unsigned i = 0; i < ids.size(); ++i) {
        ResourceCookInput input;
        auto p = core::makeUnique<Probe>();
        p->seed = i;
        p->configureMetadata({ids[i], "CookedProbe", {}, "Contract Complete", {}});
        input.resource = std::move(p);
        if (i == 1 || i == 2)
            input.dependencies.push_back({{ids[0], "shared"}, "CookedProbe"});
        if (i == 3) {
            input.dependencies.push_back({{ids[1], "a"}, "CookedProbe"});
            input.dependencies.push_back({{ids[2], "b"}, "CookedProbe"});
        }
        inputs.push_back(std::move(input));
    }
    auto cooked = ResourceCookService().cook({inputs.data(), inputs.size()}, root / "a", codecs);
    check(cooked.success, cooked.diagnostics.c_str());
    auto repeat = ResourceCookService().cook({inputs.data(), inputs.size()}, root / "b", codecs);
    check(repeat.success, repeat.diagnostics.c_str());
    for (const auto& file : std::filesystem::directory_iterator(root / "a")) {
        auto a = core::readBinaryFile(file.path()),
             b = core::readBinaryFile(root / "b" / file.path().filename());
        check(a.success && b.success && a.bytes == b.bytes, "nondeterministic cook");
    }
    for (auto& count : counts.prepared)
        count = 0;
    // Native observation wrapper uses an inline read on the runtime's file producer;
    // planning must not depend on payload accessibility.
    auto observer = core::makeShared<ObservedBackend>();
    core::runtime::AsyncRuntimeDesc desc{128, 4, 64, 32};
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = observer;
    core::runtime::AsyncRuntime runtime(desc);
    auto plan = CookedResourceLoader().plan(root / "a", runtime, codecs);
    check(plan.success, plan.diagnostics.c_str());
    check(plan.metadataReadBytes < 65536, "phase one read payload");
    check(observer->reads.size() == 1 + plan.catalog.entries().size() * 2,
        "unexpected phase one reads");
    for (const auto& request : observer->reads)
        if (std::filesystem::path(request.path).filename() != "catalog.arpbin") {
            check(request.range.mode == core::io::FileReadRangeMode::ExactRange,
                "metadata whole-file read");
            for (const auto& e : plan.catalog.entries())
                if (std::filesystem::path(request.path).filename() == std::filesystem::path(e.file))
                    check(request.range.offset + request.range.size <= 104 + e.metadataBytes,
                        "payload touched in phase one");
        }
    for (auto& count : counts.prepared)
        check(count == 0, "planning decoded payload");
    ResourceManager manager;
    ResourceDomain domain(manager, runtime, domainBudget());
    check(CookedResourceLoader().registerPlan(plan, domain).success, "register");
    check(domain.stateOf(ids[0]) == ResourceState::Declared && readyCount(manager) == 0,
        "phase one readiness");
    ResourceLease first, second;
    unsigned done = 0;
    const auto identity = plan.catalog.find(ids[3])->identity();
    check(domain
              .acquireAsync(identity,
                  [&](ResourceAcquireResult r) {
                      check(r.result.success(), r.diagnostics.c_str());
                      first = std::move(r.lease);
                      ++done;
                  })
              .success(),
        "request one");
    check(domain
              .acquireAsync(identity,
                  [&](ResourceAcquireResult r) {
                      check(r.result.success(), r.diagnostics.c_str());
                      second = std::move(r.lease);
                      ++done;
                  })
              .success(),
        "request two");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (done != 2) {
        check(std::chrono::steady_clock::now() < deadline, "acquire timeout");
        domain.dispatch();
        std::this_thread::yield();
    }
    check(first.ready() && second.ready() && first.get<Probe>()->data.size() == 65536,
        "payload missing");
    for (unsigned i = 0; i < 4; ++i)
        check(counts.prepared[i] == 1, "diamond duplicate load");
    check(counts.prepared[4] == 0, "unrelated payload loaded");
    first.reset();
    domain.collect();
    check(second.ready(), "shared lease invalidated");
    second.reset();
    domain.collect();
    check(readyCount(manager) == 0 && domain.stateOf(ids[3]) == ResourceState::Declared,
        "lease did not release closure");
    auto again = domain.acquire(identity);
    check(again.result.success() && counts.prepared[0] == 2, "reacquire");
    again.lease.reset();
    domain.collect();
    // Cancellation before publication must leave no residency behind.
    core::runtime::CancellationSource cancel;
    bool cancelled = false;
    check(domain
              .acquireAsync(
                  identity,
                  [&](ResourceAcquireResult r) {
                      cancelled = !r.result.success();
                  },
                  cancel.token())
              .success(),
        "cancel submit");
    cancel.cancel();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!cancelled) {
        check(std::chrono::steady_clock::now() < deadline, "cancel timeout");
        domain.dispatch();
        std::this_thread::yield();
    }
    check(readyCount(manager) == 0, "cancelled residency published");
    // A corrupt payload is invisible in stage one, and fails lazily without a Ready root.
    auto entry = plan.catalog.find(ids[3]);
    auto bytes = core::readBinaryFile(root / "a" / std::filesystem::path(entry->file));
    bytes.bytes.back() ^= 1;
    write(root / "a" / std::filesystem::path(entry->file), bytes.bytes);
    auto corruptPlan = CookedResourceLoader().plan(root / "a", runtime, codecs);
    check(corruptPlan.success, "metadata read corrupt payload");
    auto failed = domain.acquire(identity);
    check(!failed.result.success() && !failed.lease.ready() && readyCount(manager) == 0,
        "corrupt payload admitted");
    // Registration validates a complete DAG before publishing definitions.
    inputs[0].dependencies.push_back({{ids[3], "cycle"}, "CookedProbe"});
    auto cycle = ResourceCookService().cook({inputs.data(), inputs.size()}, root / "cycle", codecs);
    check(!cycle.success && !std::filesystem::exists(root / "cycle"), "cycle published");
    inputs[0].dependencies.clear();
    inputs[0].dependencies.push_back(
        {{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "missing"}, "CookedProbe"});
    auto missing =
        ResourceCookService().cook({inputs.data(), inputs.size()}, root / "missing", codecs);
    check(!missing.success && !std::filesystem::exists(root / "missing"),
        "missing dependency published");
    inputs[0].dependencies.clear();
    const auto previous = core::readBinaryFile(root / "b/catalog.arpbin");
    inputs[1].dependencies[0].expectedType = "WrongTargetType";
    auto mismatch = ResourceCookService().cook({inputs.data(), inputs.size()}, root / "b", codecs);
    check(!mismatch.success
            && core::readBinaryFile(root / "b/catalog.arpbin").bytes == previous.bytes,
        "failed replacement changed the delivery");
    inputs[1].dependencies[0].expectedType = "CookedProbe";
    inputs[0].resource->configureMetadata({ids[0], "UnknownCodec", {}, "Contract Complete", {}});
    auto unknown =
        ResourceCookService().cook({inputs.data(), inputs.size()}, root / "unknown", codecs);
    check(
        !unknown.success && !std::filesystem::exists(root / "unknown"), "unknown codec published");
    inputs[0].resource->configureMetadata({ids[0], "CookedProbe", {}, "Contract Complete", {}});
    auto existing = ResourceCookService().cook({inputs.data(), inputs.size()}, root / "b", codecs);
    check(existing.success && std::filesystem::exists(root / "b/catalog.arpbin"),
        "incremental delivery failed");
    // Changed data changes authoritative versions.
    dynamic_cast<Probe&>(*inputs[4].resource).seed = 0;
    auto changed =
        ResourceCookService().cook({inputs.data(), inputs.size()}, root / "changed", codecs);
    check(changed.success, changed.diagnostics.c_str());
    auto updated = CookedResourceLoader().plan(root / "changed", runtime, codecs);
    check(updated.success
            && updated.catalog.find(ids[4])->contentVersion
                != plan.catalog.find(ids[4])->contentVersion,
        "content change aliased");
}

void sourceRangeIdentity()
{
    ResourceSourceSet sources;
    ResourceFileReadIntent metadata;
    metadata.path = "resource.arpres.bin";
    metadata.pathScope = core::io::FilePathScope::HostFilesystem;
    metadata.range = {core::io::FileReadRangeMode::ExactRange, 104, 3};
    metadata.contentVersion = "metadata-v1";
    metadata.maxBytes = 3;
    ResourceSourceBuffer bytes;
    bytes.path = metadata.path;
    bytes.pathScope = metadata.pathScope;
    bytes.range = metadata.range;
    bytes.contentVersion = metadata.contentVersion;
    bytes.bytes = {1, 2, 3};
    check(sources.add(bytes) && !sources.add(bytes), "duplicate section accepted");
    check(sources.find(metadata) && !sources.find(metadata.path), "range exposed as whole file");
    auto payload = metadata;
    payload.range.offset = 107;
    payload.contentVersion = "payload-v1";
    auto changed = metadata;
    changed.contentVersion = "metadata-v2";
    auto location = metadata;
    location.pathScope = core::io::FilePathScope::MountedResource;
    check(!sources.find(payload) && !sources.find(changed) && !sources.find(location),
        "section cache aliased a different identity");
    core::pmr::Vector<ResourceFileReadIntent> reads{&core::defaultCpuMemoryResource()};
    reads = {metadata, payload, changed};
    check(pendingResourceReads(reads, sources, 64).status == ResourceSourcePlanStatus::Valid
            && reads.size() == 2 && reads[0].range.offset == 107
            && reads[1].contentVersion == "metadata-v2",
        "range pending plan reused stale section");
    reads = {payload, payload};
    check(pendingResourceReads(reads, sources, 64).status == ResourceSourcePlanStatus::Ambiguous,
        "duplicate range plan accepted");
}

void packageSmoke(const std::filesystem::path& root)
{
    auto codecs = makePrefabCookedResourceCodecs();
    auto asset = tests::prefab_runtime_package_fixture::makeRuntimeAsset();
    auto controller = tests::prefab_runtime_package_fixture::makeController();
    auto written = writePrefabRuntimePayload(tests::prefab_runtime_package_fixture::describe(
        root / "source/package", asset, controller));
    check(written.success, written.diagnostics.c_str());
    auto package = tests::prefab_runtime_package_fixture::makePackage(written.resource);
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    auto prefab = createPrefabResourceFromEnvelope(package.entries.back().envelopeYaml, diagnostic);
    check(bool(prefab), diagnostic.c_str());
    auto& p = dynamic_cast<PrefabResource&>(*prefab);
    p.animatorControllerId = tests::prefab_runtime_package_fixture::kAnimatorControllerId;
    package.entries.back() = tests::prefab_runtime_package_fixture::makeEntry(
        tests::prefab_runtime_package_fixture::kPrefabId,
        "PrefabResource",
        "prefab/runtime",
        "resources/prefab.arpres.yaml",
        p.dependencies(),
        p);
    auto published = InternalAssetPackageWriter().write(package, root / "source/package");
    check(published.success, published.diagnostics.c_str());
    {
        std::ofstream manifest(root / "manifest.yaml");
        manifest
            << "type: ArpResourceCook\ncontractVersion: resource.cook/v1\npackages:\n  - package\n";
    }
    auto cooked = ResourceCookService().cookManifest(
        root / "manifest.yaml", root / "source", root / "delivery", codecs);
    check(cooked.success, cooked.diagnostics.c_str());
    std::filesystem::rename(root / "source", root / "authoring-unavailable");
    core::runtime::AsyncRuntime runtime({128, 2, 64, 32});
    auto plan = CookedResourceLoader().plan(root / "delivery", runtime, codecs);
    check(plan.success, plan.diagnostics.c_str());
    ResourceManager manager;
    ResourceDomain domain(manager, runtime, domainBudget());
    check(CookedResourceLoader().registerPlan(plan, domain).success, "package register");
    auto loaded = domain.acquire(
        plan.catalog.find(tests::prefab_runtime_package_fixture::kPrefabId)->identity());
    check(loaded.result.success(), loaded.diagnostics.c_str());
    auto loadedAsset = manager.find<PrefabRuntimePayloadResource>(
        tests::prefab_runtime_package_fixture::kRuntimePayloadId);
    auto loadedController = manager.find<AnimatorControllerResource>(
        tests::prefab_runtime_package_fixture::kAnimatorControllerId);
    check(loadedAsset.ready() && loadedAsset.get()->runtimeAsset && loadedController.ready()
            && loadedController.get()->controllerAsset == loadedAsset.get()->controllerAsset,
        "cooked runtime/controller binding");
}

void texturePayloadBudget(const std::filesystem::path& root)
{
    const auto codecs = makePrefabCookedResourceCodecs();
    const auto codec = codecs->find(TextureResource::resourceTypeName());
    TextureResource texture;
    texture.sourcePath = "budget.png";
    CookedBytes png(std::begin(tests::uiAtlasPngBytes),
        std::end(tests::uiAtlasPngBytes),
        &core::defaultCpuMemoryResource());
    write(root / "budget.png", png);
    CookedResourceBudget budget;
    budget.maxDecodedBytes = 32 * 16 * 4;
    budget.maxPayloadBytes = budget.maxDecodedBytes + 15;
    auto payload = codec->cookPayload(texture, root, budget);
    check(payload.size() == budget.maxPayloadBytes, "RGBA8 header must fit beside decoded pixels");
    check(codec->preparePayload(texture, {payload.data(), payload.size()}, budget.maxDecodedBytes)
                .success
            && texture.decodedWidth == 32 && texture.decodedHeight == 16
            && texture.decodedRgba8.size() == budget.maxDecodedBytes,
        "full decoded image budget rejected");
    --budget.maxPayloadBytes;
    bool rejected = false;
    try {
        codec->cookPayload(texture, root, budget);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    check(rejected, "payload header bypassed encoded byte budget");
}
} // namespace

int main(int argc, char**)
{
    constexpr auto f = static_cast<std::uint8_t>(reflection::PropertyHint::Serialize);
    reflection::registration::class_<Probe>("CookedProbe")
        .construct<>()
        .property("seed", &Probe::seed, {}, f);
    const auto root = std::filesystem::temp_directory_path()
        / ("arp-cooked-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    try {
        sourceRangeIdentity();
        texturePayloadBudget(root);
        run(root);
        if (argc > 1)
            packageSmoke(root);
        std::filesystem::remove_all(root);
        std::cout << "cooked resource contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << " fixture=" << root << '\n';
        return 1;
    }
}
