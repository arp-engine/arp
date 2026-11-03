#include "core/io/file_service.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "tests/fixtures/named_glb_source_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
using namespace arp;
using namespace assets;
using namespace core::io;
using Fixture = tests::NamedGlbSourceFixture;

StaticMeshSceneSourceImportLimits limits()
{
    return {{Fixture::sourceBudget, 12, 12, 4, false, 32, 2048},
        4 * 1024 * 1024,
        128 * 1024,
        16,
        16,
        StaticMeshVertexColorPolicy::DiscardValidatedRgba};
}

std::string document()
{
    auto json = Fixture::document();
    Fixture::replace(json, "\"name\":\"leaf\\uD83C\\uDF3F\",", "");
    Fixture::replace(json, "\"nodes\":[{", "\"nodes\":[{\"children\":[1,1],");
    Fixture::replace(json, "\"scale\":[2,3,4]}]", "\"scale\":[2,3,4]},{\"mesh\":0}]");
    return json;
}

auto bytes(const std::string& json)
{
    return tests::StaticGlbGeometryFixture::bytes(json);
}

void check(const StaticMeshSceneSourceImportResult& imported)
{
    const auto& result = imported.geometry;
    if (!result.success)
        std::fprintf(stderr, "static scene: %s\n", result.diagnostics.c_str());
    assert(result.success && result.mesh.vertices.size() == 9 && result.mesh.indices.size() == 9
        && result.mesh.submeshes.size() == 3);
    assert(result.mesh.vertices[0].position[0] == 12.5f
        && result.mesh.vertices[3].position[0] == 12.5f
        && result.mesh.vertices[6].position[0] == 12.5f
        && result.mesh.vertices[2].position[2] == 27.f && result.mesh.indices[3] == 3
        && result.mesh.indices[6] == 6);
    assert(result.mesh.bounds.valid && result.materials.size() == 1
        && result.materials[0].baseColorFactor[1] == .4f
        && result.materials[0].alphaMode == ImportedAlphaMode::Blend
        && result.materials[0].emissiveStrength == 2 && result.textures.size() == 1
        && result.images.size() == 1
        && result.images[0].encodedBytes.size() == sizeof(tests::uiAtlasPngBytes));
    assert(imported.decodedBytes
        == 9 * sizeof(MeshVertex) + 9 * sizeof(std::uint32_t) + 3 * sizeof(Submesh)
            + sizeof(tests::uiAtlasPngBytes));
    assert(imported.importPeakBytes > imported.decodedBytes);
}

void contracts()
{
    const auto source = bytes(document());
    const auto good = importStaticMeshSceneFromGlbBytes({source.data(), source.size()}, limits());
    check(good);
    unified_io_fixture::Workspace workspace;
    workspace.write("scene.glb", {reinterpret_cast<const char*>(source.data()), source.size()});
    const auto legacy = importStaticMeshFromGlbFile(workspace.root / "scene.glb");
    assert(legacy.success && legacy.mesh.vertices.size() == good.geometry.mesh.vertices.size()
        && legacy.mesh.indices == good.geometry.mesh.indices);
    for (std::size_t i = 0; i < legacy.mesh.vertices.size(); ++i) {
        const auto& a = legacy.mesh.vertices[i];
        const auto& b = good.geometry.mesh.vertices[i];
        assert(std::equal(std::begin(a.position), std::end(a.position), std::begin(b.position))
            && std::equal(std::begin(a.normal), std::end(a.normal), std::begin(b.normal))
            && std::equal(std::begin(a.tangent), std::end(a.tangent), std::begin(b.tangent)));
    }
    for (unsigned test = 0; test < 22; ++test) {
        auto json = document();
        auto budget = limits();
        if (test == 0)
            budget.maxNodeVisits = 2;
        if (test == 1)
            budget.maxNodeDepth = 1;
        if (test == 2)
            budget.geometry.maxVertices = 8;
        if (test == 3)
            budget.geometry.maxIndices = 8;
        if (test == 4)
            budget.geometry.maxPrimitives = 2;
        if (test == 5)
            budget.maxImportBytes = 1;
        if (test == 6)
            budget.maxDecodedBytes = 1;
        if (test == 7)
            budget.geometry.maxJsonDepth = 1;
        if (test == 8)
            budget.geometry.maxJsonValues = 1;
        if (test == 9)
            budget.colorPolicy = StaticMeshVertexColorPolicy::RejectColor0;
        if (test == 10)
            budget.colorPolicy = StaticMeshVertexColorPolicy::Unspecified;
        if (test == 11)
            budget.geometry.strictGeometryOnly = true;
        if (test == 12)
            Fixture::replace(json, "\"children\":[1,1]", "\"children\":[0]");
        if (test == 13)
            Fixture::replace(json, "\"componentType\":5126", "\"componentType\":5126.5");
        if (test == 14)
            Fixture::replace(json, "\"COLOR_0\":3", "\"COLOR_0\":99");
        if (test == 15)
            Fixture::replace(json, "\"scale\":[2,3,4]", "\"scale\":[2,3]");
        if (test == 16)
            Fixture::replace(json, "\"scene\":0", "\"scene\":99");
        if (test == 17)
            budget.geometry.maxSourceBytes = 1;
        if (test == 18)
            Fixture::replace(json, "\"byteLength\":36", "\"byteLength\":4");
        auto raw = bytes(json);
        if (test == 19)
            raw.pop_back();
        if (test == 20 || test == 21) {
            std::uint32_t jsonSize;
            std::memcpy(&jsonSize, raw.data() + 12, sizeof(jsonSize));
            const float value = test == 20 ? 2.f : std::numeric_limits<float>::quiet_NaN();
            std::memcpy(raw.data() + 28 + jsonSize + 96, &value, sizeof(value));
        }
        const auto failed = importStaticMeshSceneFromGlbBytes({raw.data(), raw.size()}, budget);
        if (failed.geometry.success)
            std::fprintf(stderr, "unexpected static success %u\n", test);
        assert(!failed.geometry.success && !failed.geometry.diagnostics.empty()
            && failed.geometry.mesh.vertices.empty() && failed.geometry.materials.empty()
            && failed.geometry.images.empty() && !failed.decodedBytes && !failed.importPeakBytes);
    }
    auto uncolored = document();
    Fixture::replace(uncolored, ",\"COLOR_0\":3", "");
    auto raw = bytes(uncolored);
    auto reject = limits();
    reject.colorPolicy = StaticMeshVertexColorPolicy::RejectColor0;
    check(importStaticMeshSceneFromGlbBytes({raw.data(), raw.size()}, reject));
}

void cycle()
{
    auto source = bytes(document());
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{1, Fixture::sourceBudget, 1, Fixture::sourceBudget, 256});
    assert(
        backend->registerFile("scene.glb", source.data(), source.size()) == FileIoStatus::Success);
    MountedFileBackend mounts({1, 1, Fixture::sourceBudget, 32, 256});
    const auto mount = mounts.mount("scene", backend);
    core::runtime::CompletionQueue completions({2});
    FileService files(completions, mounts);
    FileReadRequest request;
    request.contractVersion = 1;
    request.pathScope = FilePathScope::MountedResource;
    request.mount = mount;
    request.path = "scene.glb";
    request.maxBytes = Fixture::sourceBudget;

    struct Completion {
        bool done = false;
        FileReadResult result;
    } completion;

    assert(files.read(request,
               {[](void* out, FileReadResult value) {
                    auto& state = *static_cast<Completion*>(out);
                    state.result = std::move(value);
                    state.done = true;
                },
                   &completion})
        == FileIoStatus::Accepted);
    assert(!completion.done);
    completions.dispatch(2);
    assert(completion.done && completion.result.status == FileIoStatus::Success
        && completion.result.bytes == source);
    assert(mounts.unmount(mount));
    backend->shutdown();
    check(importStaticMeshSceneFromGlbBytes(
        {completion.result.bytes.data(), completion.result.bytes.size()}, limits()));
    assert(files.telemetry().submitted == 1 && !completions.pending()
        && !mounts.telemetry().inFlight && !mounts.telemetry().reservedReadBytes);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 1) {
        contracts();
        cycle();
        return 0;
    }
    assert(argc == 2 && std::string_view(argv[1]) == "--smoke");
    for (unsigned i = 0; i < 20; ++i)
        cycle();
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle();
    const auto after = core::defaultCpuAllocator().stats();
    const auto liveBefore = before.allocationCount - before.deallocationCount;
    const auto liveAfter = after.allocationCount - after.deallocationCount;
    std::printf("Static scene 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(liveBefore),
        static_cast<unsigned long long>(liveAfter));
    assert(before.liveBytes == after.liveBytes && liveBefore == liveAfter);
}
