#pragma once

#include "core/encoding/yaml_contract.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "resource/asset_package_closure.h"
#include "resource/asset_package_payload.h"
#include "resource/prefab_asset_resources.h"
#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_resource.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <algorithm>
#include <cstdio>

namespace arp::tests {

class PackageSourceFixture final {
public:
    enum class Mutation {
        None,
        Missing,
        Corrupt,
        BadUtf8,
        UnknownVersion,
        WrongOrigin,
        UnclosedCorrupt
    };
    enum class Backend { Memory, Native, Http };
    static constexpr std::size_t maxBytes = 1024 * 1024;
    static constexpr const char* terrainId = "3f6fbcca-17d2-46a6-9817-2af357399301";
    static constexpr const char* catalogId = "3ea4c197-d012-4e64-8f5f-6a415ec2c109";

    static core::pmr::Vector<core::pmr::String> sourcePaths(
        const resource::InternalAssetPackageValidationResult& package,
        const resource::AssetPackageClosureResult& closure)
    {
        core::pmr::Vector<core::pmr::String> paths;
        paths.push_back("manifest.arpkg.yaml");
        paths.push_back(package.package.closurePath);
        for (const auto& entry : package.package.entries)
            paths.push_back(entry.envelopePath);
        for (const auto& file : closure.files)
            paths.push_back(file.path);
        return paths;
    }

    static core::SharedPtr<core::io::MemoryFileBackend> memory(
        const std::filesystem::path& root, Mutation mutation = Mutation::None)
    {
        const auto package = resource::InternalAssetPackageReader().read(root, maxBytes, 32);
        const auto closure = resource::AssetPackageClosureService().inspect(root);
        assert(package.success && closure.success);
        return registeredMemory(root, sourcePaths(package, closure), package, mutation);
    }

    static core::SharedPtr<core::io::MemoryFileBackend> registeredMemory(
        const std::filesystem::path& nativeRoot,
        const core::pmr::Vector<core::pmr::String>& paths,
        const resource::InternalAssetPackageValidationResult& package,
        Mutation mutation)
    {
        using namespace core;
        using namespace core::io;
        const auto logicalRoot = std::filesystem::path("snapshot");
        auto memory = makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{64, 4 * maxBytes, 2, 2 * maxBytes, 1024});
        pmr::Vector<pmr::String> directories;
        directories.push_back("snapshot");
        for (const auto& relative : paths) {
            auto parent = (logicalRoot / std::filesystem::u8path(relative.begin(), relative.end()))
                              .parent_path();
            while (!parent.empty()) {
                const auto path = FilePath::utf8(parent);
                if (std::find(directories.begin(), directories.end(), path) == directories.end())
                    directories.push_back(path);
                parent = parent.parent_path();
            }
        }
        std::sort(directories.begin(), directories.end());
        for (const auto& path : directories)
            assert(memory->registerDirectory(path) == FileIoStatus::Success);
        for (const auto& relative : paths) {
            if (mutation == Mutation::Missing && relative == "catalog/geometry.bin")
                continue;
            auto bytes = readBinaryFile(
                nativeRoot / std::filesystem::u8path(relative.begin(), relative.end()), maxBytes);
            assert(bytes.success);
            if ((mutation == Mutation::Corrupt || mutation == Mutation::UnclosedCorrupt)
                && relative == "catalog/geometry.bin")
                bytes.bytes[0] ^= 1;
            if (mutation == Mutation::BadUtf8
                && relative == package.package.entries.front().envelopePath)
                bytes.bytes.push_back(0xFF);
            if (mutation == Mutation::UnknownVersion && relative == "manifest.arpkg.yaml") {
                auto document = YamlContract::parseSingleDocument(
                    {reinterpret_cast<const char*>(bytes.bytes.data()), bytes.bytes.size()});
                document["version"] = 99;
                const auto text = YamlContract::serialize(document);
                bytes.bytes.assign(text.begin(), text.end());
            }
            if (mutation == Mutation::UnclosedCorrupt && relative == "manifest.arpkg.yaml") {
                auto document = YamlContract::parseSingleDocument(
                    {reinterpret_cast<const char*>(bytes.bytes.data()), bytes.bytes.size()});
                document.get_value_ref<YamlContract::Node::mapping_type&>().erase(
                    YamlContract::Node("closure"));
                const auto text = YamlContract::serialize(document);
                bytes.bytes.assign(text.begin(), text.end());
            }
            const auto path = FilePath::utf8(
                logicalRoot / std::filesystem::u8path(relative.begin(), relative.end()));
            assert(memory->registerFile(path, bytes.bytes.data(), bytes.bytes.size())
                == FileIoStatus::Success);
        }
        return memory;
    }

    // Acquisition in this fixture is intentionally driven by an already sealed
    // explicit closure. It does not stand in for a production async package planner.
    static void load(const std::filesystem::path& nativeRoot,
        Backend kind,
        Mutation mutation = Mutation::None,
        const char* url = nullptr)
    {
        using namespace core;
        using namespace core::io;
        using namespace resource;
        const auto package = InternalAssetPackageReader().read(nativeRoot, maxBytes, 32);
        const auto closure = AssetPackageClosureService().inspect(nativeRoot);
        assert(package.success && closure.success);
        const auto paths = sourcePaths(package, closure);
        const auto logicalRoot = std::filesystem::path("snapshot");
        assert(!std::filesystem::exists(logicalRoot));
        SharedPtr<ManagedFileBackend> backend;
        if (kind == Backend::Http) {
            HttpFileBackendDesc desc;
            desc.baseUrl = url;
            desc.maxInFlight = 2;
            desc.maxBytesInFlight = 4 * maxBytes;
            desc.maxPathBytes = 1024;
            desc.maxHeaderBytes = 4096;
            desc.ioChunkBytes = 4096;
            desc.timeoutMs = 3000;
            desc.cancellationPollMs = 2;
            backend = makeShared<HttpFileBackend>(desc);
        } else if (kind == Backend::Native) {
            NativeDirectoryFileBackendDesc desc;
            desc.rootPath = FilePath::utf8(nativeRoot.parent_path());
            desc.maxInFlight = 2;
            desc.maxBytesInFlight = 2 * maxBytes;
            desc.maxPathBytes = 1024;
            desc.ioChunkBytes = 4096;
            desc.directoryBufferBytes = 4096;
            desc.maxScanEntries = 64;
            desc.maxScanNameBytes = 4096;
            backend = makeShared<NativeDirectoryFileBackend>(desc);
        } else {
            backend = registeredMemory(nativeRoot, paths, package, mutation);
        }
        auto router =
            makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 2, 2 * maxBytes, 32, 1024});
        const auto mount = router->mount("package", backend);
        assert(mount);
        runtime::AsyncRuntimeDesc runtimeDesc;
        runtimeDesc.enableWork = false;
        runtimeDesc.enableThreadPoolFileBackend = false;
        runtimeDesc.fileBackend = router;
        runtimeDesc.completionQueueCapacity = runtimeDesc.completionBudget = 8;
        runtime::AsyncRuntime runtime(runtimeDesc);
        assert(runtime.valid());
        ResourceSourceSet acquired;
        bool acquisitionFailed = false;
        for (const auto& relative : paths) {
            FileReadRequest request;
            request.contractVersion = 1;
            request.pathScope = FilePathScope::MountedResource;
            request.mount = mount;
            request.path = FilePath::utf8(
                logicalRoot / std::filesystem::u8path(relative.begin(), relative.end()));
            request.maxBytes = maxBytes;
            if (kind == Backend::Native) {
                // Native reads only return a strong version when explicitly
                // conditioned. The fixture supplies the writer's known bytes.
                const auto expected = readBinaryFile(
                    nativeRoot / std::filesystem::u8path(relative.begin(), relative.end()),
                    maxBytes);
                assert(expected.success);
                Sha256 hash;
                hash.update(
                    {reinterpret_cast<const char*>(expected.bytes.data()), expected.bytes.size()});
                request.expectedVersion =
                    FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
                request.maxVersionBytes = maxBytes;
            }
            unified_io_fixture::Probe<FileReadResult> probe;
            assert(runtime.fileService().read(request, {decltype(probe)::complete, &probe})
                == FileIoStatus::Accepted);
            unified_io_fixture::wait(runtime.completionQueue(), [&] {
                return probe.calls != 0;
            });
            assert(probe.calls == 1);
            if (probe.result.status != FileIoStatus::Success) {
                acquisitionFailed = true;
                continue;
            }
            Sha256 hash;
            hash.update({reinterpret_cast<const char*>(probe.result.bytes.data()),
                probe.result.bytes.size()});
            assert(probe.result.version
                == FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256));
            ResourceSourceBuffer source;
            source.path = request.path;
            source.pathScope = request.pathScope;
            source.mount = mount;
            source.version = probe.result.version;
            source.bytes = std::move(probe.result.bytes);
            assert(acquired.add(std::move(source)));
        }
        assert(!acquisitionFailed || mutation == Mutation::Missing);
        AssetPackageSourceContext context;
        context.resources.sources = &acquired;
        context.resources.requirePreloadedSources = true;
        context.resources.maxDecodedBytes = maxBytes;
        context.origin = {FilePathScope::MountedResource, mount};
        if (mutation == Mutation::WrongOrigin)
            context.origin = {};
        ResourceManager manager;
        constexpr const char* sentinelId = "1c88da51-195d-42ee-a98c-576f1b1cd114";
        auto sentinel = makeUnique<ResourceBase>(
            ResourceReference{sentinelId, "Sentinel", "sentinel", "Contract Complete", {}});
        assert(manager.loadResource(std::move(sentinel)).success);
        const auto sentinelGeneration = manager.generationOf(sentinelId);
        const auto loaded = InternalAssetPackageReader().loadSources(logicalRoot, manager, context);
        if (mutation == Mutation::None && !loaded.success)
            std::fprintf(stderr, "preloaded package: %s\n", loaded.diagnostics.c_str());
        assert(loaded.success == (mutation == Mutation::None));
        assert(manager.generationOf(sentinelId) == sentinelGeneration
            && manager.find<ResourceBase>(sentinelId).ready());
        if (loaded.success) {
            const auto terrain = manager.find<TerrainAssetResource>(terrainId);
            assert(terrain.ready() && terrain.get()->sample(1, 1).sample.height == 4);
            const auto catalog = manager.find<VegetationCatalogResource>(catalogId);
            assert(catalog.ready() && catalog.get()->data->findTemplate("grass-a"));
            const auto checked = AssetPackageClosureService().inspectSources(logicalRoot, context);
            assert(checked.success && checked.files.size() == closure.files.size());
        } else {
            for (const auto& entry : package.package.entries)
                assert(!manager.recordSnapshot(entry.resourceId));
        }
        runtime.shutdown();
        assert(
            runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
        assert(router->telemetry().inFlight == 0);
        if (const auto* native = dynamic_cast<const NativeDirectoryFileBackend*>(backend.get()))
            assert(native->telemetry().openNativeObjects == 0);
        if (const auto* http = dynamic_cast<const HttpFileBackend*>(backend.get()))
            assert(http->telemetry().inFlight == 0 && http->telemetry().activeNativeTransfers == 0);
    }

    static void codecContracts()
    {
        class LegacyOnly final : public resource::AssetPackageCodec {
        public:
            resource::NameView resourceType() const noexcept override
            {
                return "FixtureLegacyCodec";
            }

            resource::AssetPackagePayloadIndex inspect(const resource::InternalAssetPackageEntry&,
                const std::filesystem::path&) const override
            {
                assert(false);
                return {};
            }

            core::UniquePtr<resource::ResourceBase> prepare(
                const resource::InternalAssetPackageEntry&,
                const std::filesystem::path&,
                core::pmr::String&) const override
            {
                assert(false);
                return {};
            }
        } codec;

        resource::InternalAssetPackageEntry entry;
        resource::AssetPackageSourceContext context;
        core::pmr::String diagnostic;
        const auto checked = codec.inspectSources(entry, {}, context);
        assert(!checked.success
            && checked.diagnostics == "PACKAGE-CODEC-PRELOADED-SOURCES-UNSUPPORTED");
        assert(!codec.prepareSources(entry, {}, context, diagnostic));
        assert(diagnostic == "PACKAGE-CODEC-PRELOADED-SOURCES-UNSUPPORTED");
    }
};

} // namespace arp::tests
