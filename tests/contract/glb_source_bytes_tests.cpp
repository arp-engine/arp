#include "assets/glb_static_mesh_importer.h"
#include "core/io/file_service.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "tests/fixtures/static_glb_geometry.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using namespace core::io;
constexpr assets::StaticMeshSourceImportLimits limits{65536, 3, 3, 1, true, 16, 512};

void check(const assets::StaticMeshSourceImportResult& result, std::string_view raw)
{
    assert(result.geometry.success && result.identityVersion == 1 && result.identities.size() == 1);
    const auto& mesh = result.geometry.mesh;
    assert(mesh.vertices.size() == 3 && mesh.indices.size() == 3);
    assert(mesh.vertices[0].position[0] == 1.25f && mesh.vertices[2].position[2] == -.75f
        && mesh.vertices[2].uv[1] == 1 && mesh.vertices[1].tangent[3] == 1);
    const auto expected = core::Sha256::digest(raw);
    assert(std::equal(expected.begin(), expected.end(), result.sourceContentDigest.begin()));
    const auto& identity = result.identities[0];
    assert(identity.node == 0 && identity.mesh == 0 && identity.primitive == 0
        && identity.sourceIndices.indexCount == 3 && identity.worldAffine[0] == 1
        && identity.worldAffine[5] == 1 && identity.worldAffine[10] == 1
        && identity.worldAffine[15] == 1);
}

void cycle()
{
    const auto bytes = tests::StaticGlbGeometryFixture::bytes();
    assert(std::find(bytes.begin(), bytes.end(), 0) != bytes.end());
    assert(std::any_of(bytes.begin(), bytes.end(), [](auto value) {
        return value >= 128;
    }));
    const std::string_view raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{1, limits.maxSourceBytes, 1, limits.maxSourceBytes, 128});
    assert(backend->registerFile("model.glb", bytes.data(), bytes.size()) == FileIoStatus::Success);
    MountedFileBackend mounts({1, 1, limits.maxSourceBytes, 32, 128});
    const auto mount = mounts.mount("glb", backend);
    core::runtime::CompletionQueue queue({2});
    FileService files(queue, mounts);
    FileReadRequest request;
    request.contractVersion = 1;
    request.pathScope = FilePathScope::MountedResource;
    request.path = "model.glb";
    request.mount = mount;
    request.maxBytes = limits.maxSourceBytes;

    struct Completion {
        bool done = false;
        FileReadResult result;
    } completion;

    assert(files.read(request,
               {[](void* pointer, FileReadResult result) {
                    auto& state = *static_cast<Completion*>(pointer);
                    state.result = std::move(result);
                    state.done = true;
                },
                   &completion})
        == FileIoStatus::Accepted);
    assert(!completion.done);
    queue.dispatch(2);
    assert(completion.done && completion.result.status == FileIoStatus::Success
        && completion.result.bytes == bytes
        && completion.result.version.kind == FileVersionKind::ContentSha256);
    // The parser can consume retained bytes after all location authority is gone.
    assert(mounts.unmount(mount));
    backend->shutdown();
    const auto reads = files.telemetry().submitted;
    const auto result = assets::importStaticMeshWithSourceIdentityFromGlbBytes(
        {completion.result.bytes.data(), completion.result.bytes.size()}, limits);
    core::pmr::Vector<std::uint8_t>{}.swap(completion.result.bytes);
    check(result, raw);
    assert(files.telemetry().submitted == reads && reads == 1);
    for (unsigned test = 0; test < 10; ++test) {
        auto changed = bytes;
        auto budget = limits;
        if (test == 0)
            budget.maxSourceBytes = 0;
        if (test == 1)
            budget.maxSourceBytes = UINT64_MAX;
        if (test == 2)
            budget.maxSourceBytes = changed.size() - 1;
        if (test == 3)
            changed.clear();
        if (test == 4)
            changed.pop_back();
        if (test == 5)
            changed[0] = 0xef;
        if (test == 6)
            changed[12] = 0xff;
        if (test == 7)
            budget.maxVertices = 2;
        if (test == 8)
            budget.maxJsonValues = 2;
        if (test == 9)
            changed[20] = 0xff;
        const auto failed = assets::importStaticMeshWithSourceIdentityFromGlbBytes(
            {changed.data(), changed.size()}, budget);
        assert(!failed.geometry.success && failed.identityVersion == 0 && failed.identities.empty()
            && !failed.geometry.diagnostics.empty());
    }
    assert(!assets::importStaticMeshWithSourceIdentityFromGlbBytes({nullptr, 1}, limits)
            .geometry.success);
    files.shutdown();
    mounts.shutdown();
    queue.shutdown();
    assert(!queue.pending() && !queue.reserved() && !mounts.telemetry().inFlight
        && !mounts.telemetry().reservedReadBytes);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--smoke"));
    if (argc == 1) {
        cycle();
        std::puts("PASS bounded GLB raw-byte parser, source digest and no location fallback");
        return 0;
    }
    for (unsigned i = 0; i < 20; ++i)
        cycle();
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle();
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("GLB bytes 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
