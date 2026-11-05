#include "core/io/file_io.h"
#include "core/io/file_save_contract.h"
#include "core/io/filesystem_path.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "resource/package_resource_domain.h"
#include "tests/fixtures/prefab_runtime_package_fixture.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
using namespace arp;
using namespace resource;
namespace fixture = tests::prefab_runtime_package_fixture;
constexpr std::size_t maxBytes = 16u * 1024u * 1024u;
constexpr const char* secondId = "932adff7-c6cb-4aaf-a4c2-a54e1a299f12";
constexpr const char* stagedId = "94d1a54f-2719-41d1-b37c-5b5d266feb52";
constexpr const char* absentId = "165b5ee8-2514-46e7-8472-1bfa17c1e226";
constexpr const char* textureId = "62365463-2481-49c6-a196-5e68ad9f5a45";
constexpr const char* texturePath = "images/texture.png";

void check(bool value, const char* diagnostic)
{
    if (!value)
        throw std::runtime_error(diagnostic);
}

ResourceDomainBudget budget()
{
    return {64, 64, 64, 4, {64, 64, maxBytes, maxBytes, 32}};
}

template <class Predicate> void until(ResourceDomain& domain, Predicate done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        check(std::chrono::steady_clock::now() < deadline, "domain timeout");
        check(domain.dispatch().success(), "domain dispatch");
        std::this_thread::yield();
    }
}

struct Sources {
    core::SharedPtr<core::io::MemoryFileBackend> memory =
        core::makeShared<core::io::MemoryFileBackend>(
            core::io::MemoryFileBackendDesc{32, maxBytes, 8, maxBytes, 1024, true});
    core::SharedPtr<core::io::MountedFileBackend> router =
        core::makeShared<core::io::MountedFileBackend>(
            core::io::MountedFileBackendDesc{2, 8, maxBytes, 32, 1024});
    core::PoolHandle mount = router->mount("packages", memory);

    ResourceSourceSet add(
        const std::filesystem::path& root, const char* prefix, bool corrupt = false)
    {
        using core::io::FileIoStatus;
        check(bool(mount), "mount failed");
        for (const char* suffix : {"", "/blobs", "/resources", "/images"})
            check(memory->registerDirectory(core::pmr::String(prefix) + suffix)
                    == FileIoStatus::Success,
                "fixture directory");
        ResourceSourceSet sources;
        for (const auto& file : std::filesystem::recursive_directory_iterator(root)) {
            if (!file.is_regular_file())
                continue;
            auto read = core::readBinaryFile(file.path(), maxBytes);
            check(read.success, "fixture read");
            const auto relative = file.path().lexically_relative(root);
            if (corrupt && relative == "blobs/prefab-runtime.arpa.bin")
                read.bytes.back() ^= 1;
            ResourceSourceBuffer source;
            source.path = core::FilePath::utf8(std::filesystem::path(prefix) / relative);
            source.pathScope = core::io::FilePathScope::MountedResource;
            source.mount = mount;
            source.bytes = std::move(read.bytes);
            core::io::FileSaveRequest sealed;
            sealed.bytes = source.bytes;
            source.version = core::io::FileSaveContract::contentVersion(sealed);
            check(memory->registerFile(source.path, source.bytes.data(), source.bytes.size())
                    == FileIoStatus::Success,
                "fixture file");
            check(sources.add(std::move(source)), "fixture duplicate");
        }
        return sources;
    }

    core::runtime::AsyncRuntimeDesc runtimeDesc() const
    {
        core::runtime::AsyncRuntimeDesc desc{128, 4, 64, 32};
        desc.enableThreadPoolFileBackend = false;
        desc.fileBackend = router;
        return desc;
    }

    void replace(core::runtime::AsyncRuntime& runtime,
        const ResourceSourceBuffer& previous,
        const core::pmr::Vector<std::uint8_t>& next) const
    {
        core::io::FileSaveRequest request;
        request.pathScope = core::io::FilePathScope::MountedResource;
        request.mount = mount;
        request.path = previous.path;
        request.precondition = core::io::FileSavePrecondition::ContentSha256;
        core::io::FileSaveRequest baseline;
        baseline.bytes = previous.bytes;
        request.expectedVersion = core::io::FileSaveContract::contentVersion(baseline);
        request.bytes = next;
        request.maxWriteBytes = maxBytes / 2;
        request.maxBaselineBytes = previous.bytes.size();
        bool done = false;
        check(runtime.fileService().save(std::move(request),
                  {[](void* context, core::io::FileSaveResult result) {
                       check(result.status == core::io::FileIoStatus::Success,
                           "source replacement failed");
                       *static_cast<bool*>(context) = true;
                   },
                      &done})
                == core::io::FileIoStatus::Accepted,
            "source replacement submit");
        while (!done)
            runtime.dispatch();
    }

    AssetPackageDomainPlan plan(
        const char* root, const ResourceSourceSet& sources, bool inventory = false) const
    {
        AssetPackageSourceContext context;
        context.origin = {core::io::FilePathScope::MountedResource, mount};
        context.resources.sources = &sources;
        context.resources.requirePreloadedSources = true;
        context.resources.maxDecodedBytes = maxBytes;
        core::pmr::Vector<AssetPackageFileSeal> seals;
        ResourceSourceSet metadata;
        for (std::size_t i = 0; inventory && i != sources.size(); ++i) {
            const auto& source = *sources.sourceAt(i);
            AssetPackageFileSeal seal;
            seal.relativePath =
                core::FilePath::utf8(std::filesystem::path(source.path).lexically_relative(root));
            seal.sha256 = core::Sha256::hexDigest(
                {reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size()});
            seal.maxBytes = source.bytes.size();
            if (seal.relativePath != "blobs/prefab-runtime.arpa.bin"
                && seal.relativePath != texturePath)
                check(metadata.add(source), "metadata duplicate");
            seals.push_back(std::move(seal));
        }
        if (inventory)
            context.resources.sources = &metadata;
        auto result = inventory
            ? AssetPackageDomainPlanner().planSealedSources(
                  root, context, maxBytes, 32, {seals.data(), seals.size()})
            : AssetPackageDomainPlanner().planSources(root, context, maxBytes, 32);
        check(result.success, result.diagnostics.c_str());
        return result;
    }
};

const ResourceIdentity& identity(const AssetPackageDomainPlan& plan, core::StringView id)
{
    for (const auto& item : plan.registrations)
        if (item.identity.id == id)
            return item.identity;
    throw std::runtime_error("identity missing");
}

void sharing(const std::filesystem::path& firstRoot, const std::filesystem::path& secondRoot)
{
    Sources files;
    auto sourcesA = files.add(firstRoot, "package-a");
    auto sourcesB = files.add(secondRoot, "package-b");
    auto a = files.plan("package-a", sourcesA, true);
    auto b = files.plan("package-b", sourcesB);
    check(identity(a, fixture::kRuntimePayloadId).version
                == identity(b, fixture::kRuntimePayloadId).version
            && identity(a, fixture::kAnimatorControllerId).version
                == identity(b, fixture::kAnimatorControllerId).version,
        "package location changed shared child content identity");
    ResourceManager manager;
    core::runtime::AsyncRuntime runtime(files.runtimeDesc());
    ResourceDomain domain(manager, runtime, budget());
    check(domain.registerResources({a.registrations.data(), a.registrations.size()}).success(),
        "register first typed package");
    check(domain.registerResources({b.registrations.data(), b.registrations.size()}).success(),
        "register shared typed package");
    const auto idA = identity(a, fixture::kPrefabId);
    const auto idB = identity(b, secondId);
    a = {};
    b = {};
    sourcesA.clear();
    sourcesB.clear();
    for (unsigned cycle = 0; cycle != 2; ++cycle) {
        unsigned done = 0;
        ResourceLease first, join, second;
        check(domain
                  .acquireAsync(idA,
                      [&](ResourceAcquireResult result) {
                          if (!result.result.success())
                              std::fprintf(stderr,
                                  "acquire failed: %s / %s\n",
                                  result.result.diagnosticId ? result.result.diagnosticId : "none",
                                  result.diagnostics.c_str());
                          check(result.result.success(), "acquire failed");
                          first = std::move(result.lease);
                          ++done;
                      })
                  .success(),
            "acquire first root");
        check(domain
                  .acquireAsync(idA,
                      [&](ResourceAcquireResult result) {
                          check(result.result.success(), "join failed");
                          join = std::move(result.lease);
                          ++done;
                      })
                  .success(),
            "join first root");
        check(domain
                  .prewarmAsync(idB,
                      [&](ResourceAcquireResult result) {
                          check(result.result.success(), "prewarm shared root failed");
                          second = std::move(result.lease);
                          ++done;
                      })
                  .success(),
            "prewarm second root");
        until(domain, [&] {
            return done == 3;
        });
        check(first.get<PrefabResource>() == join.get<PrefabResource>() && second.ready(),
            "root canonical identity");
        const auto payload = manager.find<PrefabRuntimePayloadResource>(fixture::kRuntimePayloadId);
        const auto controller =
            manager.find<AnimatorControllerResource>(fixture::kAnimatorControllerId);
        check(payload.ready() && payload.get()->runtimeAsset && controller.ready()
                && payload.get()->controllerAsset == controller.get()->controllerAsset,
            "typed controller decoded twice or failed to bind runtime");
        const auto* usage = dynamic_cast<const AssetPackageSourceUsage*>(payload.get());
        check(usage && usage->sourceUsage().size() == 1
                && usage->sourceUsage()[0].relativePath == "blobs/prefab-runtime.arpa.bin"
                && usage->sourceUsage()[0].bytes
                    == static_cast<std::size_t>(payload.get()->blobByteSize),
            "typed source accounting lost the actual acquired byte count");
        check(domain.telemetry().loads == 4u * (cycle + 1)
                && runtime.telemetry().fileService.submitted == cycle + 1,
            "shared binary read or typed load duplicated");
        core::WeakPtr<const assets::AnimatedMeshAsset> asset = payload.get()->runtimeAsset;
        core::WeakPtr<const animation::AnimatorControllerAsset> animated =
            controller.get()->controllerAsset;
        first.reset();
        join.reset();
        domain.collect();
        check(second.ready() && payload.ready() && !asset.expired(),
            "first parent released shared dependency");
        second.reset();
        domain.collect();
        check(manager.stateOf(fixture::kRuntimePayloadId) == ResourceState::Unloaded
                && manager.stateOf(fixture::kAnimatorControllerId) == ResourceState::Unloaded
                && asset.expired() && animated.expired(),
            "factory retained decoded Assets after last release");
    }
    unsigned completed = 0;
    core::runtime::CancellationSource cancel;
    ResourceLease survivor;
    check(domain
              .acquireAsync(
                  idA,
                  [&](ResourceAcquireResult result) {
                      check(result.result.status == ResourceAcquireStatus::Cancelled,
                          "cancelled consumer got Ready");
                      ++completed;
                  },
                  cancel.token())
              .success(),
        "cancelled submit");
    check(domain
              .acquireAsync(idB,
                  [&](ResourceAcquireResult result) {
                      check(result.result.success(), "consumer cancellation poisoned shared work");
                      survivor = std::move(result.lease);
                      ++completed;
                  })
              .success(),
        "survivor submit");
    cancel.cancel();
    until(domain, [&] {
        return completed == 2;
    });
    check(survivor.ready(), "shared consumer not Ready");
}

ResourceRegistration probe(const char* id, const char* child = nullptr, const char* version = "v1")
{
    return {{id, "BatchProbe", version}, [id, child]() -> core::UniquePtr<ResourceBase> {
                auto result = core::makeUnique<ResourceBase>(
                    ResourceReference{id, "BatchProbe", {}, "Contract Complete", {}});
                if (child)
                    result->addDependency({child, "strong"});
                return result;
            }};
}

void texturePackage(const std::filesystem::path& root, bool geometry)
{
    InternalAssetPackageDesc package;
    if (geometry)
        package = fixture::write(root);
    else {
        package.name = "node-only";
        package.status = "Contract Complete";
        package.entries.push_back(fixture::makeEntry(secondId,
            "PrefabResource",
            "prefab/node-only",
            "resources/prefab.arpres.yaml",
            {},
            PrefabResource{}));
    }
    const auto parent = package.entries.back();
    core::pmr::String diagnostic;
    const auto typed = createPrefabResourceFromEnvelope(parent.envelopeYaml, diagnostic);
    auto dependencies = parent.dependencies;
    dependencies.push_back({textureId, "texture"});
    package.entries.back() = fixture::makeEntry(parent.resourceId.c_str(),
        "PrefabResource",
        parent.alias.c_str(),
        parent.envelopePath.c_str(),
        dependencies,
        *dynamic_cast<const PrefabResource*>(typed.get()));
    TextureResource texture;
    texture.name = "encoded";
    texture.sourcePath = texturePath;
    texture.usage = "sealed opaque encoded source";
    package.entries.push_back(fixture::makeEntry(textureId,
        "TextureResource",
        "texture/encoded",
        "resources/texture.arpres.yaml",
        {},
        texture));
    const auto written = InternalAssetPackageWriter().write(package, root);
    check(written.success, written.diagnostics.c_str());
    std::filesystem::create_directories(root / "images");
    // This adapter verifies sealed opaque bytes; PNG decoding belongs to its consumer.
    std::ofstream image(root / texturePath, std::ios::binary);
    image << "encoded-source-lifetime-fixture";
    check(bool(image), "texture source write");
}

void textureSharing(const std::filesystem::path& root, bool nodeFirst)
{
    Sources files;
    auto sourcesA = files.add(root / "geometry", "geometry");
    auto sourcesB = files.add(root / "node", "node");
    auto a = files.plan("geometry", sourcesA, true);
    auto b = files.plan("node", sourcesB, true);
    const auto texture = identity(a, textureId);
    const auto idA = identity(a, fixture::kPrefabId), idB = identity(b, secondId);
    check(texture.version == identity(b, textureId).version, "mixed package texture identity");
    ResourceManager manager;
    core::runtime::AsyncRuntime runtime(files.runtimeDesc());
    ResourceDomain domain(manager, runtime, budget());
    auto& first = nodeFirst ? b : a;
    auto& second = nodeFirst ? a : b;
    check(
        domain.registerResources({first.registrations.data(), first.registrations.size()}).success()
            && domain.registerResources({second.registrations.data(), second.registrations.size()})
                .success(),
        "mixed texture register");
    check(runtime.telemetry().fileService.submitted == 0, "planning read texture");
    const auto expected = sourcesA.find("geometry/images/texture.png")->bytes;
    a = {};
    b = {};
    sourcesA.clear();
    sourcesB.clear();
    for (unsigned cycle = 0; cycle != 2; ++cycle) {
        unsigned done = 0;
        ResourceLease parentA, parentB;
        check(domain
                  .acquireAsync(idA,
                      [&](ResourceAcquireResult result) {
                          if (!result.result.success())
                              std::fprintf(stderr,
                                  "acquire failed: %s / %s\n",
                                  result.result.diagnosticId ? result.result.diagnosticId : "none",
                                  result.diagnostics.c_str());
                          check(result.result.success(), "acquire failed");
                          parentA = std::move(result.lease);
                          ++done;
                      })
                  .success(),
            "texture parent A submit");
        check(domain
                  .prewarmAsync(idB,
                      [&](ResourceAcquireResult result) {
                          if (!result.result.success())
                              std::fprintf(stderr,
                                  "acquire failed: %s / %s\n",
                                  result.result.diagnosticId ? result.result.diagnosticId : "none",
                                  result.diagnostics.c_str());
                          check(result.result.success(), "acquire failed");
                          parentB = std::move(result.lease);
                          ++done;
                      })
                  .success(),
            "texture parent B submit");
        until(domain, [&] {
            return done == 2;
        });
        auto child = domain.acquire(texture);
        const auto* resource = child.lease.get<TextureResource>();
        const auto* source = dynamic_cast<const AssetPackageEncodedTextureSource*>(resource);
        const auto view =
            source ? source->encodedSourceBytes() : core::ArrayView<const std::uint8_t>{};
        check(resource && view.size() == expected.size()
                && std::equal(view.begin(), view.end(), expected.begin())
                && resource->processedByteCount() == expected.size(),
            "typed source content/accounting");
        check(dynamic_cast<const AssetPackageEncodedTextureSource*>(
                  manager.find<TextureResource>(textureId).get())
                    ->encodedSourceBytes()
                    .data()
                == view.data(),
            "canonical source pointer");
        check(runtime.telemetry().fileService.submitted == 2 * (cycle + 1),
            "mixed parents repeated PNG read (one blob plus one texture required)");
        parentA.reset();
        child.lease.reset();
        domain.collect();
        check(parentB.ready() && source->encodedSourceBytes().data() == view.data()
                && std::equal(view.begin(), view.end(), expected.begin()),
            "first parent destroyed source");
        parentB.reset();
        domain.collect();
        check(manager.stateOf(textureId) == ResourceState::Unloaded,
            "last parent did not release source");
    }
    core::runtime::CancellationSource cancel;
    ResourceLease survivor;
    unsigned done = 0;
    check(domain
              .acquireAsync(
                  idA,
                  [&](ResourceAcquireResult result) {
                      check(result.result.status == ResourceAcquireStatus::Cancelled,
                          "texture cancelled acquire");
                      ++done;
                  },
                  cancel.token())
              .success(),
        "texture cancellation submit");
    check(domain
              .acquireAsync(idB,
                  [&](ResourceAcquireResult result) {
                      if (!result.result.success())
                          std::fprintf(stderr,
                              "acquire failed: %s / %s\n",
                              result.result.diagnosticId ? result.result.diagnosticId : "none",
                              result.diagnostics.c_str());
                      check(result.result.success(), "acquire failed");
                      survivor = std::move(result.lease);
                      ++done;
                  })
              .success(),
        "texture survivor submit");
    cancel.cancel();
    until(domain, [&] {
        return done == 2;
    });
    check(survivor.ready() && manager.find<TextureResource>(textureId).ready(),
        "texture cancel poisoned survivor");
}

void textureRejection(const std::filesystem::path& root)
{
    Sources files;
    auto sources = files.add(root / "node", "node");
    auto plan = files.plan("node", sources, true);
    const auto id = identity(plan, textureId);
    ResourceManager manager;
    core::runtime::AsyncRuntime runtime(files.runtimeDesc());
    auto limited = budget();
    limited.load.maxDecodedBytes = 1;
    {
        ResourceDomain domain(manager, runtime, limited);
        check(domain.registerResources({plan.registrations.data(), plan.registrations.size()})
                  .success(),
            "limited texture register");
        check(!domain.acquire(id).result.success()
                && !manager.find<TextureResource>(textureId).ready(),
            "encoded retained bytes bypassed CPU budget");
    }
    ResourceManager sealedManager;
    ResourceDomain sealed(sealedManager, runtime, budget());
    check(
        sealed.registerResources({plan.registrations.data(), plan.registrations.size()}).success(),
        "sealed texture register");
    const auto& previous = *sources.find("node/images/texture.png");
    auto changed = previous.bytes;
    changed.back() ^= 1;
    files.replace(runtime, previous, changed);
    check(!sealed.acquire(id).result.success()
            && !sealedManager.find<TextureResource>(textureId).ready(),
        "changed texture seal published source");
}

void transactions()
{
    ResourceDomain domain({128, 4, 64, 32}, budget());
    auto initial = probe(fixture::kPrefabId);
    check(domain.registerResources({&initial, 1}).success(), "initial register");
    auto pinned = domain.acquire(initial.identity);
    if (!pinned.lease.ready())
        std::fprintf(stderr,
            "initial acquire: %s / %s\n",
            pinned.result.diagnosticId ? pinned.result.diagnosticId : "none",
            pinned.diagnostics.c_str());
    check(pinned.lease.ready(), "initial acquire");
    const auto* original = pinned.lease.resource();
    const auto rollback = [&](ResourceRegistration failing, ResourceAcquireStatus status) {
        ResourceRegistration batch[]{probe(stagedId), std::move(failing)};
        check(
            domain.registerResources(batch).status == status, "incorrect batch diagnostic status");
        check(domain.acquire(batch[0].identity).result.status
                == ResourceAcquireStatus::MissingDefinition,
            "failed batch left partial definitions");
        check(pinned.lease.resource() == original && pinned.lease.ready(),
            "failed batch changed Ready generation");
    };
    rollback(probe(fixture::kPrefabId, nullptr, "v2"), ResourceAcquireStatus::IdentityConflict);
    auto wrongType = probe(fixture::kPrefabId);
    wrongType.identity.type = "WrongType";
    rollback(std::move(wrongType), ResourceAcquireStatus::IdentityConflict);
    rollback(probe(fixture::kPrefabId, stagedId), ResourceAcquireStatus::IdentityConflict);
    rollback(probe(secondId, absentId), ResourceAcquireStatus::MissingDefinition);
    rollback(probe(secondId, secondId), ResourceAcquireStatus::DependencyCycle);
    auto empty = probe(secondId);
    empty.factory = []() -> core::UniquePtr<ResourceBase> {
        return {};
    };
    rollback(std::move(empty), ResourceAcquireStatus::IdentityConflict);
    auto throwing = probe(secondId);
    throwing.factory = []() -> core::UniquePtr<ResourceBase> {
        throw std::bad_alloc();
    };
    rollback(std::move(throwing), ResourceAcquireStatus::BudgetExceeded);
    ResourceRegistration duplicate[]{probe(stagedId), probe(stagedId)};
    check(!domain.registerResources(duplicate).success(), "duplicate batch accepted");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    auto staged = probe(stagedId);
    {
        core::CpuMemoryQuotaScope scope(quota);
        check(
            domain.registerResources({&staged, 1}).status == ResourceAcquireStatus::BudgetExceeded,
            "allocation refusal was not atomic");
    }
    check(domain.acquire(staged.identity).result.status == ResourceAcquireStatus::MissingDefinition,
        "quota refusal left partial definition");
    auto tiny = budget();
    tiny.maxResources = 1;
    ResourceDomain bounded({128, 4, 64, 32}, tiny);
    check(bounded.registerResources({&initial, 1}).success(), "bounded initial register");
    check(bounded.registerResources({&staged, 1}).status == ResourceAcquireStatus::BudgetExceeded,
        "resource cap exceeded");
}

void seals(const std::filesystem::path& root)
{
    Sources files;
    auto original = files.add(root, "original");
    auto changed = files.add(root, "changed", true);
    auto a = files.plan("original", original);
    auto b = files.plan("changed", changed);
    check(a.package.entries.front().contentHash == b.package.entries.front().contentHash
            && identity(a, fixture::kRuntimePayloadId).version
                != identity(b, fixture::kRuntimePayloadId).version
            && identity(a, fixture::kAnimatorControllerId).version
                != identity(b, fixture::kAnimatorControllerId).version,
        "binary change aliased unchanged envelope/controller content version");
    ResourceManager manager;
    core::runtime::AsyncRuntime runtime(files.runtimeDesc());
    ResourceDomain domain(manager, runtime, budget());
    check(domain.registerResources({a.registrations.data(), a.registrations.size()}).success(),
        "sealed register");
    check(domain.registerResources({b.registrations.data(), b.registrations.size()}).status
            == ResourceAcquireStatus::IdentityConflict,
        "changed binary reused canonical definition");
    // Change backing bytes after plan; SHA seals must reject before Ready publication.
    const auto* blob = original.find("original/blobs/prefab-runtime.arpa.bin");
    auto bytes = blob->bytes;
    bytes.back() ^= 1;
    files.replace(runtime, *blob, bytes);
    const auto acquired = domain.acquire(identity(a, fixture::kPrefabId));
    check(!acquired.result.success() && !acquired.lease.ready()
            && !manager.find<PrefabResource>(fixture::kPrefabId).ready(),
        "mutated source was published Ready");
    const auto reads = runtime.telemetry().fileService.submitted;
    check(!domain.acquire(identity(a, fixture::kPrefabId)).result.success()
            && runtime.telemetry().fileService.submitted == reads,
        "sticky failure silently retried");
    ResourceSourceBuffer changedBlob;
    changedBlob.path = blob->path;
    changedBlob.bytes = bytes;
    files.replace(runtime, changedBlob, blob->bytes);
    core::io::FileReadRequest verify;
    verify.contractVersion = 1;
    verify.pathScope = blob->pathScope;
    verify.mount = blob->mount;
    verify.path = blob->path;
    verify.maxBytes = maxBytes;

    struct Readback {
        bool done = false;
        core::io::FileReadResult result;
    } readback;

    check(runtime.fileService().read(verify,
              {[](void* context, core::io::FileReadResult result) {
                   auto& value = *static_cast<Readback*>(context);
                   value.result = std::move(result);
                   value.done = true;
               },
                  &readback})
            == core::io::FileIoStatus::Accepted,
        "readback submit");
    while (!readback.done)
        runtime.dispatch();
    check(readback.result.status == core::io::FileIoStatus::Success
            && readback.result.bytes == blob->bytes,
        "restore did not restore the original bytes");
    ResourceLease recovered;
    bool completed = false;
    check(domain
              .acquireAsync(
                  identity(a, fixture::kPrefabId),
                  [&](ResourceAcquireResult result) {
                      if (!result.result.success())
                          std::fprintf(stderr,
                              "retry: %s / %s\n",
                              result.result.diagnosticId ? result.result.diagnosticId : "none",
                              result.diagnostics.c_str());
                      check(result.result.success(), "explicit retry failed");
                      recovered = std::move(result.lease);
                      completed = true;
                  },
                  {},
                  true)
              .success(),
        "retry submit");
    until(domain, [&] {
        return completed;
    });
    check(recovered.ready(), "retry missing lease");
    recovered.reset();
    domain.collect();
    check(files.router->unmount(files.mount), "unmount failed");
    check(bool(files.router->mount("packages", files.memory)), "remount failed");
    check(!domain.acquire(identity(a, fixture::kPrefabId)).result.success(),
        "stale mount generation was reused");
}
} // namespace

int main(int argc, char** argv)
{
    const auto root = std::filesystem::temp_directory_path()
        / ("arp-package-domain-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        registerPrefabAssetReflection();
        (void)prefabResourceFactory();
        fixture::write(root / "a");
        auto package = fixture::write(root / "b");
        core::pmr::String diagnostic;
        const auto firstPrefab =
            createPrefabResourceFromEnvelope(package.entries.back().envelopeYaml, diagnostic);
        package.entries.back() = fixture::makeEntry(secondId,
            "PrefabResource",
            "prefab/second",
            "resources/prefab.arpres.yaml",
            package.entries.back().dependencies,
            *dynamic_cast<const PrefabResource*>(firstPrefab.get()));
        check(InternalAssetPackageWriter().write(package, root / "b").success,
            "second package write");
        texturePackage(root / "geometry", true);
        texturePackage(root / "node", false);
        transactions();
        textureRejection(root);
        textureSharing(root, false);
        textureSharing(root, true);
        seals(root / "a");
        sharing(root / "a", root / "b");
        const auto baseline = core::defaultCpuAllocator().stats().liveBytes;
        for (unsigned i = 0; i != (argc > 1 ? 20u : 1u); ++i) {
            sharing(root / "a", root / "b");
            textureSharing(root, i % 2 != 0);
        }
        check(core::defaultCpuAllocator().stats().liveBytes == baseline,
            "warmed domain cycle leaked CPU bytes");
        std::filesystem::remove_all(root);
        std::puts(
            "PASS atomic registration, native cross-package sharing, one binary read/load, mixed geometry texture source sharing in both registration orders, encoded byte accounting, cold reload, last-parent release, seals, retry, cancellation, quota and allocation baseline");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        std::filesystem::remove_all(root);
        return 1;
    }
}
