#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/math/math.h"
#include "core/runtime/async_runtime.h"
#include "resource/environment_asset_resources.h"
#include "resource/resource_graph_executor.h"
#include "tests/fixtures/named_glb_source_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using namespace assets;
using namespace resource;
using namespace core::io;
using Fixture = tests::NamedGlbSourceFixture;

void check(const NamedVariant& variant, bool rootRelative = false)
{
    assert(variant.name == "leaf\xf0\x9f\x8c\xbf" && variant.mesh.vertices.size() == 3
        && variant.mesh.indices.size() == 3 && variant.hasWindData
        && variant.windWeights.size() == 12 && variant.windWeights[0] == 1
        && variant.windWeights[1] == 0 && variant.windWeights[3] == 1);
    assert(variant.mesh.vertices[0].position[0] == (rootRelative ? 2.5f : 12.5f));
    assert(variant.mesh.vertices[2].position[2] == (rootRelative ? -3.f : 27.f));
    // Non-symmetric matrix confirms the explicit row-major import boundary.
    assert(variant.worldTransform[0] == 2 && variant.worldTransform[5] == 3
        && variant.worldTransform[10] == 4 && variant.worldTransform[3] == 10
        && variant.worldTransform[7] == 20 && variant.worldTransform[11] == 30
        && variant.worldTransform[12] == 0 && variant.worldTransform[15] == 1);
    core::Mat4 matrix;
    std::copy_n(variant.worldTransform, 16, matrix.m.data());
    const auto transformed = core::transformPoint(matrix, {1.25f, 1.f, -.75f});
    assert(transformed.x == 12.5f && transformed.y == 23.f && transformed.z == 27.f);
}

NamedMeshGlbSourceDesc descriptor(ResourceFileOrigin origin)
{
    return {"model.glb", origin, Fixture::limits(), NamedVariantGeometrySpace::Scene};
}

core::UniquePtr<NamedMeshVariantSet> candidate(ResourceFileOrigin origin)
{
    return core::makeUnique<NamedMeshVariantSet>(
        Fixture::id, "named/source", core::pmr::String("fixture"), descriptor(origin), 1);
}

void parserContracts()
{
    const auto good = Fixture::bytes();
    for (const auto space :
        {NamedVariantGeometrySpace::Scene, NamedVariantGeometrySpace::RootRelative}) {
        auto bytes = good;
        const auto imported =
            importNamedVariantsFromGlbBytes({bytes.data(), bytes.size()}, space, Fixture::limits());
        assert(imported.success && imported.transformLayout == NamedVariantTransformLayout::RowMajor
            && imported.importPeakBytes > bytes.size() && imported.variants.size() == 1
            && imported.materials.size() == 1
            && imported.materials[0].name == "external-slot-zero");
        assert(imported.materials[0].baseColorFactor[1] == .4f
            && imported.materials[0].alphaMode == ImportedAlphaMode::Blend
            && imported.materials[0].emissiveStrength == 2 && imported.images.size() == 1
            && imported.textures.size() == 1
            && imported.images[0].encodedBytes.size() == sizeof(tests::uiAtlasPngBytes));
        for (std::size_t i = 0; i < sizeof(tests::uiAtlasPngBytes); ++i)
            assert(imported.images[0].encodedBytes[i] == tests::uiAtlasPngBytes[i]);
        bytes.clear();
        check(imported.variants[0], space == NamedVariantGeometrySpace::RootRelative);
    }
    unified_io_fixture::Workspace legacyWorkspace;
    legacyWorkspace.write("model.glb", {reinterpret_cast<const char*>(good.data()), good.size()});
    const auto legacy = importNamedVariantsFromGlbFile(legacyWorkspace.root / "model.glb");
    assert(legacy.success
        && legacy.transformLayout == NamedVariantTransformLayout::LegacyColumnMajor
        && legacy.variants[0].worldTransform[12] == 10 && legacy.variants[0].worldTransform[3] == 0
        && legacy.variants[0].mesh.vertices[0].position[0] == 12.5f);
    for (unsigned test = 0; test < 24; ++test) {
        auto json = Fixture::document();
        auto limits = Fixture::limits();
        auto space = NamedVariantGeometrySpace::Scene;
        if (test == 0)
            limits.maxVertices = 2;
        if (test == 1)
            limits.maxIndices = 2;
        if (test == 2)
            limits.maxVariants = 0;
        if (test == 3)
            limits.maxPrimitives = 0;
        if (test == 4)
            limits.maxNodeVisits = 0;
        if (test == 5)
            limits.maxNodeDepth = 0;
        if (test == 6)
            limits.maxJsonValues = 1;
        if (test == 7)
            limits.maxJsonDepth = 1;
        if (test == 8)
            limits.maxImportBytes = 1;
        if (test == 9)
            limits.maxSourceBytes = 1;
        if (test == 10)
            limits.colorPolicy = NamedVariantColorPolicy::Unspecified;
        if (test == 11)
            limits.colorPolicy = NamedVariantColorPolicy::RejectColor0;
        if (test == 12)
            space = static_cast<NamedVariantGeometrySpace>(99);
        if (test == 13)
            Fixture::replace(json, "\"COLOR_0\":3", "\"COLOR_0\":2");
        if (test == 14)
            Fixture::replace(json, "\"count\":3", "\"count\":0");
        if (test == 15)
            Fixture::replace(json, "\"byteLength\":36", "\"byteLength\":4");
        if (test == 16)
            Fixture::replace(json, "\"mesh\":0", "\"mesh\":99");
        if (test == 17)
            Fixture::replace(json, "\"mesh\":0", "\"mesh\":0,\"children\":[0]");
        if (test == 18)
            Fixture::replace(json, "\"material\":0", "\"material\":0.5");
        if (test == 19)
            Fixture::replace(json, "\"COLOR_0\":3", "\"COLOR_0\":3,\"COLOR_0\":3");
        if (test == 20)
            Fixture::replace(json, "\"buffer\":0", "\"buffer\":1");
        if (test == 21)
            Fixture::replace(json, "\"byteOffset\":0", "\"byteOffset\":4294967295");
        if (test == 22)
            Fixture::replace(json, "\"KHR_materials_emissive_strength\":", "\"ARP_unknown\":");
        auto bytes = tests::StaticGlbGeometryFixture::bytes(json);
        if (test == 23)
            bytes.pop_back();
        const auto failed =
            importNamedVariantsFromGlbBytes({bytes.data(), bytes.size()}, space, limits);
        if (failed.success)
            std::fprintf(stderr, "unexpected named success: %u\n", test);
        assert(!failed.success && !failed.diagnostics.empty() && failed.variants.empty()
            && failed.materials.empty() && failed.images.empty());
    }
    assert(!importNamedVariantsFromGlbBytes({}, NamedVariantGeometrySpace::Scene, Fixture::limits())
            .success);
    auto absent = Fixture::document();
    Fixture::replace(absent, ",\"COLOR_0\":3", "");
    auto absentBytes = tests::StaticGlbGeometryFixture::bytes(absent);
    auto noColor = Fixture::limits();
    noColor.colorPolicy = NamedVariantColorPolicy::RejectColor0;
    const auto withoutWind = importNamedVariantsFromGlbBytes(
        {absentBytes.data(), absentBytes.size()}, NamedVariantGeometrySpace::Scene, noColor);
    assert(withoutWind.success && !withoutWind.variants[0].hasWindData
        && withoutWind.variants[0].windWeights.empty());

    auto pair = Fixture::document();
    Fixture::replace(pair, "\"nodes\":[{", "\"nodes\":[{\"children\":[1],");
    Fixture::replace(
        pair, "\"scale\":[2,3,4]}]", "\"scale\":[2,3,4]},{\"name\":\"second\",\"mesh\":0}]");
    auto pairBytes = tests::StaticGlbGeometryFixture::bytes(pair);
    const auto two = importNamedVariantsFromGlbBytes(
        {pairBytes.data(), pairBytes.size()}, NamedVariantGeometrySpace::Scene, Fixture::limits());
    assert(two.success && two.variants.size() == 2);
    auto oneVariant = Fixture::limits();
    oneVariant.maxVariants = 1;
    const auto exceeded = importNamedVariantsFromGlbBytes(
        {pairBytes.data(), pairBytes.size()}, NamedVariantGeometrySpace::Scene, oneVariant);
    assert(!exceeded.success && exceeded.diagnostics == "ASSET-NAMED-VARIANT-COUNT-BUDGET"
        && exceeded.variants.empty());
    Fixture::replace(pair, "\"name\":\"second\"", "\"name\":\"leaf\\uD83C\\uDF3F\"");
    pairBytes = tests::StaticGlbGeometryFixture::bytes(pair);
    const auto duplicate = importNamedVariantsFromGlbBytes(
        {pairBytes.data(), pairBytes.size()}, NamedVariantGeometrySpace::Scene, Fixture::limits());
    assert(!duplicate.success && duplicate.diagnostics == "ASSET-NAMED-VARIANT-NAME-DUPLICATE");
    auto multi = Fixture::document();
    Fixture::replace(multi,
        "\"indices\":4,\"material\":0}]}]",
        "\"indices\":4,\"material\":0},{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"TANGENT\":3,\"COLOR_0\":3},\"indices\":4,\"material\":0}]}]");
    auto multiBytes = tests::StaticGlbGeometryFixture::bytes(multi);
    const auto multiple = importNamedVariantsFromGlbBytes({multiBytes.data(), multiBytes.size()},
        NamedVariantGeometrySpace::Scene,
        Fixture::limits());
    assert(multiple.success && multiple.variants[0].mesh.vertices.size() == 6
        && multiple.variants[0].windWeights.size() == 24
        && multiple.variants[0].materialSlots.size() == 2);
    auto limited = Fixture::limits();
    limited.maxPrimitives = 1;
    const auto overPrimitives = importNamedVariantsFromGlbBytes(
        {multiBytes.data(), multiBytes.size()}, NamedVariantGeometrySpace::Scene, limited);
    assert(!overPrimitives.success
        && overPrimitives.diagnostics == "ASSET-NAMED-VARIANT-GEOMETRY-BUDGET");
    Fixture::replace(multi, ",\"COLOR_0\":3", "");
    multiBytes = tests::StaticGlbGeometryFixture::bytes(multi);
    const auto mixed = importNamedVariantsFromGlbBytes({multiBytes.data(), multiBytes.size()},
        NamedVariantGeometrySpace::Scene,
        Fixture::limits());
    assert(!mixed.success && mixed.diagnostics == "ASSET-NAMED-VARIANT-COLOR-MIXED");

    // Interleaved normalized U8 RGBA shares the explicit tangent buffer view.
    auto normalized = Fixture::document();
    Fixture::replace(normalized,
        "\"type\":\"SCALAR\"}]",
        "\"type\":\"SCALAR\"},{\"bufferView\":3,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\",\"normalized\":true}]");
    Fixture::replace(normalized, "\"COLOR_0\":3", "\"COLOR_0\":5");
    Fixture::replace(normalized,
        "\"byteOffset\":96,\"byteLength\":48",
        "\"byteOffset\":96,\"byteLength\":48,\"byteStride\":16");
    auto normalizedBytes = tests::StaticGlbGeometryFixture::bytes(normalized);
    const auto colors =
        importNamedVariantsFromGlbBytes({normalizedBytes.data(), normalizedBytes.size()},
            NamedVariantGeometrySpace::Scene,
            Fixture::limits());
    assert(colors.success && colors.variants[0].windWeights.size() == 12
        && colors.variants[0].windWeights[2] == 128.f / 255.f);
    Fixture::replace(normalized, "\"normalized\":true", "\"normalized\":false");
    normalizedBytes = tests::StaticGlbGeometryFixture::bytes(normalized);
    assert(!importNamedVariantsFromGlbBytes({normalizedBytes.data(), normalizedBytes.size()},
        NamedVariantGeometrySpace::Scene,
        Fixture::limits())
            .success);

    auto dag = Fixture::document();
    Fixture::replace(dag, "\"nodes\":[{", "\"nodes\":[{\"children\":[1,2],");
    Fixture::replace(
        dag, "\"scale\":[2,3,4]}]", "\"scale\":[2,3,4]},{\"children\":[3]},{\"children\":[3]},{}]");
    auto dagBytes = tests::StaticGlbGeometryFixture::bytes(dag);
    assert(importNamedVariantsFromGlbBytes(
        {dagBytes.data(), dagBytes.size()}, NamedVariantGeometrySpace::Scene, Fixture::limits())
            .success);
    limited = Fixture::limits();
    limited.maxNodeVisits = 4;
    const auto overVisits = importNamedVariantsFromGlbBytes(
        {dagBytes.data(), dagBytes.size()}, NamedVariantGeometrySpace::Scene, limited);
    assert(!overVisits.success && overVisits.diagnostics == "ASSET-NAMED-VARIANT-NODE-BUDGET");
    // Graph depth and occurrence budgets are independent of shallow JSON nesting.
    auto chain = Fixture::document();
    Fixture::replace(chain, "\"nodes\":[{", "\"nodes\":[{\"children\":[1],");
    Fixture::replace(chain,
        "\"scale\":[2,3,4]}]",
        "\"scale\":[2,3,4]},{\"children\":[2]},{\"children\":[3]},{}]");
    auto bytes = tests::StaticGlbGeometryFixture::bytes(chain);
    for (bool depth : {false, true}) {
        auto limits = Fixture::limits();
        if (depth)
            limits.maxNodeDepth = 2;
        else
            limits.maxNodeVisits = 2;
        const auto failed = importNamedVariantsFromGlbBytes(
            {bytes.data(), bytes.size()}, NamedVariantGeometrySpace::Scene, limits);
        assert(!failed.success && failed.diagnostics == "ASSET-NAMED-VARIANT-NODE-BUDGET");
    }
}

void resourceContracts()
{
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{1, Fixture::sourceBudget, 1, Fixture::sourceBudget, 256});
    MountedFileBackend mounts({1, 1, Fixture::sourceBudget, 32, 256});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts.mount("source", backend)};
    ResourceSourceBuffer input;
    input.path = "model.glb";
    input.pathScope = origin.pathScope;
    input.mount = origin.mount;
    input.bytes = Fixture::bytes();
    ResourceSourceSet sources;
    assert(sources.add(std::move(input)));
    ResourceLoadContext context;
    context.sources = &sources;
    context.requirePreloadedSources = true;
    context.maxDecodedBytes = 128 * 1024;
    ResourceManager manager;
    assert(manager.loadResource(candidate(origin), context).success);
    const auto prior = manager.find<NamedMeshVariantSet>(Fixture::id);
    assert(prior.get()->images().size() == 1 && prior.get()->textures().size() == 1);
    check(*prior.get()->findVariant("leaf\xf0\x9f\x8c\xbf"));
    auto wrong = origin;
    wrong.mount.generation += 1;
    const auto failed = manager.reloadResource(candidate(wrong), context);
    assert(!failed.success && failed.telemetry.rolledBack && prior.ready()
        && manager.find<NamedMeshVariantSet>(Fixture::id).get() == prior.get());
    sources.clear();
    const auto missing = manager.reloadResource(candidate(origin), context);
    assert(!missing.success && missing.telemetry.rolledBack && prior.ready());
    auto legacy = core::makeUnique<NamedMeshVariantSet>(
        Fixture::id, "legacy", std::string("fixture"), std::filesystem::path("missing.glb"), 1, 1);
    const auto refused = manager.reloadResource(std::move(legacy), context);
    assert(!refused.success && refused.diagnostics == "NAMED-MESH-EXPLICIT-SOURCE-REQUIRED");
    ResourceSourceBuffer changed;
    changed.path = "model.glb";
    changed.pathScope = origin.pathScope;
    changed.mount = origin.mount;
    auto json = Fixture::document();
    Fixture::replace(json, "\"translation\":[10,20,30]", "\"translation\":[11,20,30]");
    changed.bytes = tests::StaticGlbGeometryFixture::bytes(json);
    assert(sources.add(std::move(changed)));
    const auto generation = prior.generation();
    const auto reloaded = manager.reloadResource(candidate(origin), context);
    assert(reloaded.success && reloaded.telemetry.committed && !reloaded.unchangedContent
        && manager.generationOf(Fixture::id) == generation + 1 && !prior.ready());
    auto current = manager.find<NamedMeshVariantSet>(Fixture::id);
    assert(manager.reloadResource(candidate(origin), context).unchangedContent && current.ready());
    auto adjusted = descriptor(origin);
    adjusted.limits.maxNodeVisits += 1;
    auto limitsCandidate = core::makeUnique<NamedMeshVariantSet>(
        Fixture::id, "named/source", core::pmr::String("fixture"), adjusted, 1);
    assert(manager.reloadResource(std::move(limitsCandidate), context).telemetry.committed
        && !current.ready());
    current = manager.find<NamedMeshVariantSet>(Fixture::id);
    assert(mounts.unmount(origin.mount));
    origin.mount = mounts.mount("source", backend);
    assert(origin.mount);
    sources.clear();
    ResourceSourceBuffer rebound;
    rebound.path = "model.glb";
    rebound.pathScope = origin.pathScope;
    rebound.mount = origin.mount;
    rebound.bytes = tests::StaticGlbGeometryFixture::bytes(json);
    assert(sources.add(std::move(rebound)));
    adjusted.origin = origin;
    auto reboundCandidate = core::makeUnique<NamedMeshVariantSet>(
        Fixture::id, "named/source", core::pmr::String("fixture"), adjusted, 1);
    assert(manager.reloadResource(std::move(reboundCandidate), context).telemetry.committed
        && !current.ready());
    assert(manager.find<NamedMeshVariantSet>(Fixture::id).get()->sourceDescriptor()->origin.mount
        == origin.mount);
}

void graph(core::SharedPtr<ManagedFileBackend> backend, unsigned failure = 0)
{
    auto mounts = core::makeShared<MountedFileBackend>(
        MountedFileBackendDesc{1, 1, Fixture::sourceBudget, 32, 256});
    ResourceFileOrigin origin{FilePathScope::MountedResource, mounts->mount("source", backend)};
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    desc.completionQueueCapacity = desc.completionBudget = 4;
    core::runtime::AsyncRuntime runtime(desc);
    ResourceManager manager;
    ResourceGraphExecutor executor(manager, runtime.fileService(), runtime.completionQueue());
    ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = Fixture::id;
    ResourceGraphNode node;
    node.resource = candidate(origin);
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, 1, Fixture::sourceBudget, 128 * 1024, 4};
    core::runtime::CancellationSource cancellation;
    request.cancellation = cancellation.token();

    struct Completion {
        bool done = false;
        ResourceGraphExecutionResult result;
    } completion;

    assert(executor.submit(std::move(request),
               {[](void* pointer, ResourceGraphExecutionResult result) {
                    auto& out = *static_cast<Completion*>(pointer);
                    out.result = std::move(result);
                    out.done = true;
                },
                   &completion})
        == ResourceGraphSubmitStatus::Accepted);
    if (failure == 1)
        mounts->unmount(origin.mount);
    if (failure == 2)
        cancellation.cancel();
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.done;
    });
    if (!failure && !completion.result.resource.success)
        std::fprintf(stderr, "%s\n", completion.result.resource.diagnostics.c_str());
    assert(completion.result.resource.success == !failure);
    if (!failure) {
        const auto handle = manager.find<NamedMeshVariantSet>(Fixture::id);
        assert(handle.ready()
            && handle.get()->transformLayout() == NamedVariantTransformLayout::RowMajor);
        check(*handle.get()->findVariant("leaf\xf0\x9f\x8c\xbf"));
        assert(completion.result.telemetry.submittedFileReads == 1);
    }
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved()
        && !mounts->telemetry().inFlight && !mounts->telemetry().reservedReadBytes);
}

void cycle(const char* url, bool native)
{
    const auto bytes = Fixture::bytes();
    if (url) {
        HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = 128 * 1024;
        desc.maxPathBytes = 256;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 1024;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 10;
        auto backend = core::makeShared<HttpFileBackend>(desc);
        graph(backend);
        assert(!backend->telemetry().inFlight && !backend->telemetry().activeNativeTransfers);
    } else if (native) {
        unified_io_fixture::Workspace workspace;
        workspace.write("model.glb", {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        auto desc = workspace.descriptor();
        desc.ioChunkBytes = 1024;
        graph(core::makeShared<NativeDirectoryFileBackend>(desc));
    } else
        for (unsigned failure = 0; failure < 3; ++failure) {
            auto backend = core::makeShared<MemoryFileBackend>(
                MemoryFileBackendDesc{1, Fixture::sourceBudget, 1, Fixture::sourceBudget, 256});
            assert(backend->registerFile("model.glb", bytes.data(), bytes.size())
                == FileIoStatus::Success);
            graph(backend, failure);
        }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--emit") {
        const auto bytes = Fixture::bytes();
        std::ofstream out(std::filesystem::u8path(argv[2]) / "model.glb", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        assert(out.good());
        return 0;
    }
    if (argc == 1) {
        parserContracts();
        resourceContracts();
        cycle(nullptr, false);
        return 0;
    }
    const bool native = argc == 2 && std::string_view(argv[1]) == "--native";
    const char* url = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    assert(native || url || (argc == 2 && std::string_view(argv[1]) == "--smoke"));
    for (unsigned i = 0; i < 20; ++i)
        cycle(url, native);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(url, native);
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Named GLB 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
