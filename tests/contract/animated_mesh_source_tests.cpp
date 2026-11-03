#include "animation/skinned_mesh_player.h"
#include "assets/animated_mesh_source_import.h"
#include "assets/glb_importer_common.h"
#include "core/encoding/base64.h"
#include "core/io/file_io.h"
#include "core/io/file_service.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "tests/fixtures/animated_mesh_source_fixture.h"
#include "tests/fixtures/ui_atlas_pixels.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace {
using namespace arp;
using Fixture = tests::AnimatedMeshSourceFixture;
using Result = assets::AnimatedMeshSourceImportResult;

void near(float value, float expected)
{
    assert(std::isfinite(value) && std::abs(value - expected) < .0001f);
}

void check(const Result& result)
{
    if (!result.success)
        std::fprintf(stderr,
            "import: %s / %s\n",
            result.diagnosticId ? result.diagnosticId : "none",
            result.diagnostics.c_str());
    assert(result.success && !result.diagnosticId);
    const auto& asset = result.asset;
    assert(asset.mesh.vertices.size() == 9 && asset.mesh.indices.size() == 9
        && asset.mesh.submeshes.size() == 3);
    assert(asset.skinnedVertices.size() == 9 && asset.skeleton.jointCount() == 7
        && asset.clips.size() == 1);
    assert(asset.mesh.indices[0] == 1 && asset.mesh.indices[1] == 0);
    assert(asset.materials.size() == 1 && asset.materials[0].baseColorFactor[1] == .4f);
    assert(asset.skinnedVertices[0].joints[0] != asset.skinnedVertices[3].joints[0]);
    animation::SkinnedMeshPlayer player(asset);
    assert(player.valid() && player.update("move", .5f));
    const auto& palette = player.skinMatrices();
    auto point =
        animation::transformPoint(palette[asset.skinnedVertices[0].joints[0]], {-1.25f, 0, 0});
    near(point.x, -26.5f);
    near(point.y, 49.5f);
    near(point.z, 73.75f);
    point = animation::transformPoint(palette[asset.skinnedVertices[3].joints[0]], {-1.25f, 0, 0});
    near(point.x, -15.25f);
    near(point.y, 27);
    near(point.z, 40);
    point = animation::transformPoint(palette[asset.skinnedVertices[6].joints[0]], {-1.25f, 0, 0});
    near(point.x, -6.25f);
    near(point.y, 6);
    near(point.z, 7);
}

Result load(const core::ByteBuffer& bytes,
    assets::AnimatedMeshSourceImportLimits limits = Fixture::limits())
{
    return assets::importAnimatedMeshFromGlbBytes({bytes.data(), bytes.size()}, limits);
}

void contracts()
{
    const auto bytes = Fixture::bytes();
    // Both public chunk entrypoints preserve the last good candidate. The
    // legacy profile retains its trailing-data compatibility; the explicit
    // embedded profile rejects it and never wraps a uint32 length addition.
    assets::detail::GlbChunks chunks;
    core::pmr::String containerDiagnostic;
    const auto container = [](const core::ByteBuffer& source) {
        return core::pmr::String(reinterpret_cast<const char*>(source.data()), source.size());
    };
    assert(assets::detail::readGlbChunks(container(bytes), chunks, containerDiagnostic));
    const auto retainedJson = chunks.json, retainedBin = chunks.bin;
    auto trailing = bytes;
    trailing.push_back(0);
    Fixture::word(trailing, 8, static_cast<std::uint32_t>(trailing.size()));
    assert(assets::detail::readGlbChunks(container(trailing), chunks, containerDiagnostic));
    assert(chunks.json == retainedJson && chunks.bin == retainedBin);
    assert(!assets::detail::readGlbChunks(container(trailing),
        chunks,
        containerDiagnostic,
        assets::detail::GlbChunkPolicy::EmbeddedSourceV1));
    for (auto offset : {std::size_t(12), bytes.size() - 308 - 8}) {
        auto overflow = bytes;
        Fixture::word(overflow, offset, UINT32_MAX);
        assert(!assets::detail::readGlbChunks(container(overflow), chunks, containerDiagnostic));
        assert(chunks.json == retainedJson && chunks.bin == retainedBin);
    }
    assert(chunks.json == retainedJson && chunks.bin == retainedBin);
    auto result = load(bytes);
    check(result);
    check(load(Fixture::bytes(Fixture::replace(Fixture::document(),
        "{\"mesh\":1,\"translation\":[5,6,7]}",
        "{\"mesh\":0,\"translation\":[5,6,7]}"))));
    assert(result.authoredTangentPrimitives == 3 && !result.unusedTangentPrimitives);
    auto reject = [&](const core::ByteBuffer& raw,
                      const char* expected = nullptr,
                      assets::AnimatedMeshSourceImportLimits limits = Fixture::limits()) {
        auto bad = load(raw, limits);
        assert(!bad.success && bad.diagnosticId && bad.asset.mesh.vertices.empty());
        if (expected) {
            if (core::StringView(bad.diagnosticId) != expected)
                std::fprintf(stderr, "expected %s, got %s\n", expected, bad.diagnosticId);
            assert(core::StringView(bad.diagnosticId) == expected);
        }
        check(result); // An invalid candidate never mutates the retained good asset.
    };
    for (float weight : {0.f, -1.f, 2.f, float(NAN), float(INFINITY)}) {
        auto payload = Fixture::binary();
        Fixture::real(payload, 164, weight);
        reject(Fixture::bytes(Fixture::document(), payload));
    }
    auto payload = Fixture::binary();
    payload[152] = 1;
    reject(Fixture::bytes(Fixture::document(), payload), "ASSET-ANIMATED-SOURCE-JOINT");
    payload = Fixture::binary();
    payload[144] = 3;
    reject(Fixture::bytes(Fixture::document(), payload), "ASSET-ANIMATED-SOURCE-INDICES");
    payload = Fixture::binary();
    Fixture::real(payload, 0, NAN);
    reject(Fixture::bytes(Fixture::document(), payload), "ASSET-ANIMATED-SOURCE-POSITION");
    payload = Fixture::binary();
    Fixture::real(payload, 44, 0);
    reject(Fixture::bytes(Fixture::document(), payload), "ASSET-ANIMATED-SOURCE-FRAME");
    payload = Fixture::binary();
    Fixture::real(payload, 108, 0);
    reject(Fixture::bytes(Fixture::document(), payload), "ASSET-ANIMATED-SOURCE-FRAME");
    payload = Fixture::binary();
    Fixture::real(payload, 72, INFINITY);
    reject(Fixture::bytes(Fixture::document(), payload), "ASSET-ANIMATED-SOURCE-UV");
    payload = Fixture::binary();
    Fixture::real(payload, 256, 1);
    reject(Fixture::bytes(Fixture::document(), payload), "ASSET-GLB-SKELETON-INVERSE-BIND");
    auto malformed = bytes;
    Fixture::word(malformed, 12, UINT32_MAX);
    reject(malformed, "ASSET-ANIMATED-SOURCE-CONTAINER");
    malformed = bytes;
    Fixture::word(malformed, malformed.size() - 308 - 8, UINT32_MAX);
    reject(malformed, "ASSET-ANIMATED-SOURCE-CONTAINER");
    malformed = bytes;
    malformed.push_back(0);
    Fixture::word(malformed, 8, static_cast<std::uint32_t>(malformed.size()));
    reject(malformed, "ASSET-ANIMATED-SOURCE-CONTAINER");
    reject(Fixture::bytes(Fixture::replace(Fixture::document(),
               "\"samplers\":[{\"input\":7,\"output\":8}]",
               "\"samplers\":[{\"input\":7,\"output\":8,\"interpolation\":\"STEP\"}]")),
        "ASSET-GLB-ANIMATION-INTERPOLATION-UNSUPPORTED");
    reject(Fixture::bytes(Fixture::replace(Fixture::document(), "0.2,0.4,0.6,1", "-0.2,0.4,0.6,1")),
        "ASSET-ANIMATED-SOURCE-MATERIAL-VALUE");
    auto limits = Fixture::limits();
    limits.geometry.maxVertices = 8;
    reject(bytes, "ASSET-ANIMATED-SOURCE-GEOMETRY-BUDGET", limits);
    limits = Fixture::limits();
    limits.geometry.maxIndices = 8;
    reject(bytes, "ASSET-ANIMATED-SOURCE-GEOMETRY-BUDGET", limits);
    limits = Fixture::limits();
    limits.geometry.maxPrimitives = 2;
    reject(bytes, "ASSET-ANIMATED-SOURCE-GEOMETRY-BUDGET", limits);
    limits = Fixture::limits();
    limits.maxDecodedBytes = result.decodedBytes;
    check(load(bytes, limits));
    --limits.maxDecodedBytes;
    reject(bytes, nullptr, limits);
    limits = Fixture::limits();
    limits.maxImportBytes = 1;
    reject(bytes, "ASSET-ANIMATED-SOURCE-IMPORT-BUDGET", limits);
    limits = Fixture::limits();
    limits.selectedScene = 1;
    reject(bytes, "ASSET-GLB-NODE-SCENE", limits);
    reject(bytes, "ASSET-ANIMATED-SOURCE-LIMITS", {});
    auto tangentless = Fixture::replace(
        Fixture::replace(Fixture::document(), ",\"TANGENT\":3", ""), ",\"TANGENT\":3", "");
    auto unused = load(Fixture::bytes(tangentless));
    check(unused);
    assert(unused.unusedTangentPrimitives == 3 && !unused.authoredTangentPrimitives);
    limits = Fixture::limits();
    limits.geometry.tangentPolicy = assets::AnimatedMeshTangentPolicy::RequireAuthored;
    reject(Fixture::bytes(tangentless), "ASSET-ANIMATED-SOURCE-TANGENT-REQUIRED", limits);
    auto color = Fixture::replace(Fixture::document(),
        "\"TANGENT\":3,\"JOINTS_0\":5",
        "\"TANGENT\":3,\"COLOR_0\":3,\"JOINTS_0\":5");
    reject(Fixture::bytes(color), "ASSET-NAMED-VARIANT-COLOR-POLICY");
    limits = Fixture::limits();
    limits.geometry.colorPolicy = assets::AnimatedMeshColorPolicy::DiscardValidatedRgba;
    check(load(Fixture::bytes(color), limits));
    payload = Fixture::binary();
    Fixture::real(payload, 108, -1);
    reject(Fixture::bytes(color, payload), "ASSET-ANIMATED-SOURCE-COLOR", limits);
    auto normalized = Fixture::replace(Fixture::document(),
        "\"bufferView\":6,\"componentType\":5126",
        "\"bufferView\":6,\"componentType\":5121,\"normalized\":true");
    payload = Fixture::binary();
    std::fill(payload.begin() + 164, payload.begin() + 176, 0);
    payload[164] = payload[168] = payload[172] = 255;
    check(load(Fixture::bytes(normalized, payload)));
    const core::ByteBuffer png(
        std::begin(tests::uiAtlasPngBytes), std::end(tests::uiAtlasPngBytes));
    const auto encoded = core::Base64::encode(png);
    auto textured = Fixture::document();
    const core::pmr::String image =
        core::pmr::String(",\"images\":[{\"uri\":\"data:image/png;base64,") + encoded
        + "\"}],\"textures\":[{\"source\":0}]";
    textured.insert(textured.size() - 1, image);
    textured = Fixture::replace(textured,
        "\"baseColorFactor\":[0.2,0.4,0.6,1]",
        "\"baseColorFactor\":[0.2,0.4,0.6,1],\"baseColorTexture\":{\"index\":0}");
    const auto withImage = load(Fixture::bytes(textured));
    check(withImage);
    assert(withImage.asset.images.size() == 1 && withImage.asset.images[0].encodedBytes == png);
    assert(
        withImage.asset.textures.size() == 1 && withImage.asset.materials[0].baseColorTexture == 0);
    limits = Fixture::limits();
    limits.materials.maxEncodedImageBytes = 1;
    reject(Fixture::bytes(textured), "ASSET-ANIMATED-SOURCE-IMAGE-BUDGET", limits);
    auto normalMapped = Fixture::replace(
        textured, "\"name\":\"surface\",", "\"name\":\"surface\",\"normalTexture\":{\"index\":0},");
    normalMapped = Fixture::replace(
        Fixture::replace(normalMapped, ",\"TANGENT\":3", ""), ",\"TANGENT\":3", "");
    reject(Fixture::bytes(normalMapped), "ASSET-ANIMATED-SOURCE-TANGENT-REQUIRED");
    auto tiny = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(tiny);
        const auto bad = load(bytes);
        assert(!bad.success
            && core::StringView(bad.diagnosticId) == "ASSET-ANIMATED-SOURCE-IMPORT-BUDGET");
    }
    assert(!tiny->liveBytes());
    auto caller = core::makeShared<core::CpuMemoryQuota>(Fixture::limits().maxImportBytes);
    {
        core::CpuMemoryQuotaScope scope(caller);
        const auto good = load(bytes);
        check(good);
        assert(core::inspectCpuAllocationCharge(good.asset.mesh.vertices.data()).quota
            == caller.get());
    }
    assert(!caller->liveBytes());
    for (auto cap : {std::uint64_t(0), Fixture::limits().maxImportBytes + 1}) {
        auto incompatible = core::makeShared<core::CpuMemoryQuota>(cap);
        {
            core::CpuMemoryQuotaScope scope(incompatible);
            const auto bad = load(bytes);
            assert(!bad.success
                && core::StringView(bad.diagnosticId) == "ASSET-ANIMATED-SOURCE-QUOTA-LIMIT");
        }
        assert(!incompatible->liveBytes());
    }
}

void corpus(const std::filesystem::path& path)
{
    auto limits = Fixture::limits();
    limits.maxSourceBytes = 2 * 1024 * 1024;
    limits.maxImportBytes = 64 * 1024 * 1024;
    limits.maxDecodedBytes = 16 * 1024 * 1024;
    limits.maxJsonValues = 262144;
    limits.layout = {3000, 3000, 200000, 1000000, 256 * 1024};
    limits.scene = {128, 128, 2, 16, 4, 256, 16, 65536};
    limits.animation = {32, 3000, 3000, 65536, 65536, 262144, 256 * 1024};
    limits.skeleton = {256, 65536, 4 * 1024 * 1024, .00001f};
    limits.geometry.maxVertices = 65536;
    limits.geometry.maxIndices = 65536;
    limits.geometry.maxPrimitives = 32;
    limits.materials = {32, 32, 32, 1024 * 1024, 4 * 1024 * 1024};
    const auto bytes = core::readBinaryFile(path, limits.maxSourceBytes);
    assert(bytes.success);
    auto result = load(bytes.bytes, limits);
    if (!result.success)
        std::fprintf(stderr, "corpus: %s / %s\n", result.diagnosticId, result.diagnostics.c_str());
    assert(result.success && result.asset.skeleton.jointCount() == 129
        && result.asset.clips.size() == 24);
    const bool formal = path.filename() == "Formal.glb";
    assert(formal || path.filename() == "Casual.glb");
    const auto primitives = formal ? 10u : 9u;
    assert(result.asset.mesh.submeshes.size() == primitives
        && result.unusedTangentPrimitives == primitives);
    assert(result.asset.mesh.vertices.size() == (formal ? 12296u : 12968u));
    assert(result.asset.mesh.indices.size() == (formal ? 18324u : 19272u));
    animation::SkinnedMeshPlayer player(result.asset);
    assert(player.valid());
    for (const auto& clip : result.asset.clips)
        assert(player.update(clip.name, clip.durationSeconds * .25f));
    std::printf(
        "full animated corpus: vertices %zu indices %zu primitives %zu joints %zu clips %zu decoded %llu quota peak %llu\n",
        result.asset.mesh.vertices.size(),
        result.asset.mesh.indices.size(),
        result.asset.mesh.submeshes.size(),
        result.asset.skeleton.jointCount(),
        result.asset.clips.size(),
        (unsigned long long)result.decodedBytes,
        (unsigned long long)result.importQuotaPeakBytes);
}

void acquired(const char* baseUrl = nullptr, bool corrupt = false)
{
    using namespace core::io;
    core::SharedPtr<ManagedFileBackend> backend;
    auto expected = Fixture::bytes();
    if (corrupt) {
        auto payload = Fixture::binary();
        Fixture::real(payload, 164, 0);
        expected = Fixture::bytes(Fixture::document(), payload);
    }
    if (baseUrl) {
        HttpFileBackendDesc desc;
        desc.baseUrl = baseUrl;
        desc.maxInFlight = 1;
        // One 64 KiB body request plus bounded header/URL/transfer storage.
        desc.maxBytesInFlight = 128 * 1024;
        desc.maxPathBytes = 256;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 1024;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 10;
        backend = core::makeShared<HttpFileBackend>(desc);
    } else {
        auto memory =
            core::makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, 65536, 1, 65536, 256});
        assert(memory->registerFile("model.glb", expected.data(), expected.size())
            == FileIoStatus::Success);
        backend = std::move(memory);
    }
    FileReadResult retained;
    {
        MountedFileBackend mounts({1, 1, 65536, 32, 256});
        const auto mount = mounts.mount("animated-source", backend);
        assert(mount);
        core::runtime::CompletionQueue queue({2});
        FileService files(queue, mounts);
        FileReadRequest request;
        request.contractVersion = 1;
        request.pathScope = FilePathScope::MountedResource;
        request.path = "model.glb";
        request.mount = mount;
        request.maxBytes = 65536;

        struct Completion {
            FileReadResult& retained;
            bool done = false;
        } completion{retained};

        assert(files.read(request,
                   {[](void* raw, FileReadResult value) {
                        auto& c = *static_cast<Completion*>(raw);
                        c.retained = std::move(value);
                        c.done = true;
                    },
                       &completion})
            == FileIoStatus::Accepted);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!completion.done && std::chrono::steady_clock::now() < deadline) {
            assert(queue.dispatch(2).success());
            std::this_thread::yield();
        }
        assert(completion.done && retained.status == FileIoStatus::Success
            && retained.bytes == expected);
        assert(mounts.unmount(mount));
        backend->shutdown();
        files.shutdown();
        assert(queue.pending() == 0 && queue.reserved() == 0 && mounts.telemetry().inFlight == 0);
        if (auto* http = dynamic_cast<HttpFileBackend*>(backend.get()))
            assert(!http->telemetry().inFlight && !http->telemetry().activeNativeTransfers
                && !http->telemetry().reservedBytes);
    }
    backend.reset();
    auto result = load(retained.bytes);
    if (corrupt)
        assert(!result.success && result.asset.mesh.vertices.empty());
    else
        check(result); // Decoder sees retained bytes after the entire source owner is gone.
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && core::StringView(argv[1]) == "--emit") {
        const auto bytes = Fixture::bytes();
        std::ofstream output(std::filesystem::u8path(argv[2]), std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        assert(output.good());
        return 0;
    }
    if (argc == 3 && core::StringView(argv[1]) == "--http") {
        for (int i = 0; i < 20; ++i)
            acquired(argv[2]);
        const auto before = core::defaultCpuAllocator().stats();
        for (int i = 0; i < 20; ++i)
            acquired(argv[2]);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("animated HTTP 20+20: core bytes %llu -> %llu, live %llu -> %llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        return 0;
    }
    if (argc == 3 && core::StringView(argv[1]) == "--corpus") {
        corpus(std::filesystem::u8path(argv[2]));
        return 0;
    }
    contracts();
    acquired();
    acquired(nullptr, true);
    const auto before = core::defaultCpuAllocator().stats();
    const auto cycles = argc > 1 ? 20 : 1;
    for (int i = 0; i < cycles; ++i) {
        contracts();
        acquired();
        acquired(nullptr, true);
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("full animated %d ordinary cycles: core bytes %llu -> %llu, live %llu -> %llu\n",
        cycles,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
