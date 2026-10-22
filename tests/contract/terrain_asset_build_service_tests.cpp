#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/runtime/async_runtime.h"
#include "resource/internal_asset_package.h"
#include "resource/resource_graph_executor.h"
#include "resource/terrain_asset_build_service.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

namespace {
constexpr const char* id = "3f6fbcca-17d2-46a6-9817-2af357399301";
constexpr const char* yaml =
    "type: TerrainAssetAuthoring\ncontractVersion: terrain.authoring/v1\n"
    "resourceId: 3f6fbcca-17d2-46a6-9817-2af357399301\nalias: terrain/standard\n"
    "minX: 11.0\nminZ: -7.0\ntileCountX: 2\ntileCountZ: 1\n"
    "samplesPerEdge: 2\ntileWorldSize: 2.0\nmaxDecodedBytes: 32\n";

void loadPackage(const std::filesystem::path& path)
{
    arp::resource::ResourceManager resources;
    const auto loaded = arp::resource::InternalAssetPackageReader().load(path, resources);
    if (!loaded.success)
        std::fprintf(stderr, "%s\n", loaded.diagnostics.c_str());
    assert(loaded.success);
    const auto terrain = resources.find<arp::resource::TerrainAssetResource>(id);
    assert(terrain.ready() && terrain.get()->dependencies().empty());
    const auto old = terrain.get()->snapshotSurface();
    assert(old->sample(12.0f, -6.0f).sample.height == 3.5f);
    assert(resources.unload(id).success);
    assert(old->sample(15.0f, -5.0f).sample.height == 3.5f);
    assert(arp::resource::InternalAssetPackageReader().load(path, resources).success);
}

void testGraph(const std::filesystem::path& path)
{
    using namespace arp;
    resource::ResourceReference ref;
    ref.id = id;
    ref.type = "TerrainAssetResource";
    ref.status = "Contract Complete";
    core::runtime::AsyncRuntime runtime({16u, 1u, 4u, 4u});
    resource::ResourceManager resources;
    resource::ResourceGraphExecutor executor(
        resources, runtime.fileService(), runtime.workService(), runtime.completionQueue());
    resource::ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = id;
    resource::ResourceGraphNode node;
    node.resource =
        core::makeUnique<resource::TerrainAssetResource>(ref, path / "terrain.asset.yaml");
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1u, 3u, 1024u * 1024u, 32u, 4u};

    struct Probe {
        bool complete = false;
        resource::ResourceGraphExecutionResult result;
    } probe;

    assert(executor.submit(std::move(request),
               {[](void* context, resource::ResourceGraphExecutionResult result) {
                    auto& p = *static_cast<Probe*>(context);
                    p.result = std::move(result);
                    p.complete = true;
                },
                   &probe})
        == resource::ResourceGraphSubmitStatus::Accepted);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!probe.complete && std::chrono::steady_clock::now() < deadline) {
        runtime.dispatch();
        std::this_thread::yield();
    }
    if (probe.complete && !probe.result.resource.success)
        std::fprintf(stderr, "%s\n", probe.result.resource.diagnostics.c_str());
    assert(probe.complete && probe.result.resource.success
        && probe.result.telemetry.completedNodes == 1u);
    assert(resources.find<resource::TerrainAssetResource>(id).get()->sample(12.0f, -6.0f).ready());
    assert(resources.find<resource::TerrainAssetResource>(id).get()->processedByteCount() == 32u);
    runtime.shutdown(core::runtime::AsyncRuntimeShutdownPolicy::Drain);
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    if (argc == 2) {
        loadPackage(argv[1]);
        return 0;
    }
    const auto root = std::filesystem::temp_directory_path()
        / ("arp_terrain_standard_build_"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    resource::TerrainAssetBuildService service;
    core::pmr::String authoring(yaml, &core::defaultCpuMemoryResource());
    authoring += "input:\n  type: constant/v1\n  height: 3.5\n";
    const auto prepared = service.prepare(authoring, root);
    assert(prepared.success && prepared.asset.tiles.size() == 2u);
    const auto output = root / "constant";
    const auto packaged = service.packageGeometry(prepared.asset, output);
    if (!packaged.success)
        std::fprintf(stderr, "%s\n", packaged.diagnostics.c_str());
    assert(packaged.success);
    loadPackage(output);
    testGraph(output);
    const auto before = core::readBinaryFile(output / "terrain.asset.yaml");
    assert(!service.packageGeometry(prepared.asset, output).success);
    assert(core::readBinaryFile(output / "terrain.asset.yaml").bytes == before.bytes);
    assert(service.packageGeometry(prepared.asset, root / "repeated").success);
    assert(core::readBinaryFile(root / "repeated/terrain.asset.yaml").bytes == before.bytes);
    auto reversed = prepared.asset;
    std::reverse(reversed.tiles.begin(), reversed.tiles.end());
    assert(service.packageGeometry(reversed, root / "reversed").success);
    assert(core::readBinaryFile(root / "reversed/terrain.asset.yaml").bytes == before.bytes);
    // Strings with YAML syntax must round trip as a single scalar.
    auto quoted = prepared.asset;
    quoted.status = "Contract: Complete # literal";
    const auto quotedOutput = root / "quoted";
    // The writer can serialize arbitrary scalars; the standard build must
    // reject a status outside its resource policy during staged reload.
    assert(resource::TerrainAssetWriter().write(quoted, root / "quoted-writer").success);
    const auto quotedManifest = core::readTextFile(root / "quoted-writer/terrain.asset.yaml");
    const auto document = core::YamlContract::parseSingleDocument(quotedManifest.text);
    assert(core::YamlContract::text(document, "status") == quoted.status);
    assert(!service.packageGeometry(quoted, quotedOutput).success);
    assert(!std::filesystem::exists(quotedOutput));
    assert(!std::filesystem::exists(root / "quoted.arp-staging"));
    // Metadata corruption rejects a replacement without changing the Ready generation.
    resource::ResourceManager resident;
    assert(resource::InternalAssetPackageReader().load(output, resident).success);
    const auto stableGeneration = resident.find<resource::TerrainAssetResource>(id).generation();
    auto corrupted = core::YamlContract::parseSingleDocument(
        core::readTextFile(output / "terrain.asset.yaml").text);
    corrupted["tiles"][0]["minimumHeight"] = 999.0;
    {
        std::ofstream file(output / "terrain.asset.yaml");
        file << core::YamlContract::serialize(corrupted);
    }
    assert(!resource::InternalAssetPackageReader().load(output, resident).success);
    assert(resident.find<resource::TerrainAssetResource>(id).ready());
    assert(resident.find<resource::TerrainAssetResource>(id).generation() == stableGeneration);
    // Repair is an explicit low-level writer operation, never implicit CLI overwrite.
    assert(resource::TerrainAssetWriter().write(prepared.asset, output).success);
    auto invalid = prepared.asset;
    invalid.bounds.maxX += 1.0f;
    assert(!service.packageGeometry(invalid, output).success);
    assert(core::readBinaryFile(output / "terrain.asset.yaml").bytes == before.bytes);
    assert(!service.packageGeometry(invalid, root / "invalid").success);
    assert(!std::filesystem::exists(root / "invalid"));
    assert(!std::filesystem::exists(root / "invalid.arp-staging"));
    authoring += "unknown: true\n";
    assert(!service.prepare(authoring, root).success);
    core::pmr::String raw(yaml, &core::defaultCpuMemoryResource());
    raw +=
        "input:\n  type: r32-float-le/v1\n  path: height.bin\n  width: 3\n  height: 2\n  rowDirection: decreasing-z\n  scale: 2.0\n  offset: -1.0\n";
    const std::uint8_t bytes[] = {
        0, 0, 128, 63, 0, 0, 0, 64, 0, 0, 64, 64, 0, 0, 128, 64, 0, 0, 160, 64, 0, 0, 192, 64};
    {
        std::ofstream file(root / "height.bin", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }
    const auto imported = service.prepare(raw, root);
    assert(imported.success && imported.asset.tiles.front().heights.front() == 7.0f);
    assert(imported.asset.tiles.front().heights.back() == 3.0f);
    assert(imported.asset.tiles.back().heights.back() == 5.0f);
    assert(service.packageGeometry(imported.asset, root / "raw").success);
    raw.replace(raw.find("r32-float-le/v1"), sizeof("r32-float-le/v1") - 1u, "guess/v1");
    assert(!service.prepare(raw, root).success);
    std::puts("Terrain standard build/import/package/query/source-graph contracts passed");
}
