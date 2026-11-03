#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/io/file_service.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/io/native_directory_file_backend.h"
#include "examples/build_a_queen/character_source_task.h"
#include "examples/build_a_queen/character_sources.h"
#include "tests/fixtures/queen_content_probe.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {
using namespace arp;
using namespace core::io;
using Sources = build_a_queen::CharacterSources;
using Y = core::YamlContract;

struct Corpus {
    struct File {
        core::pmr::String name{&core::defaultCpuMemoryResource()};
        core::ByteBuffer bytes{&core::defaultCpuMemoryResource()};
    };

    std::array<File, 3> files;

    Corpus()
    {
        const std::array<const char*, 3> names{
            "character_manifest.yaml", "Casual.glb", "Formal.glb"};
        for (std::size_t i = 0; i < names.size(); ++i) {
            files[i].name = names[i];
            auto input =
                core::readBinaryFile(std::filesystem::path(ARP_QUEEN_CHARACTER_FIXTURE) / names[i],
                    i ? Sources::maxCharacterBytes : Sources::maxManifestBytes);
            assert(input.success);
            files[i].bytes = std::move(input.bytes);
        }
    }

    core::pmr::String manifest() const
    {
        return {reinterpret_cast<const char*>(files[0].bytes.data()),
            files[0].bytes.size(),
            &core::defaultCpuMemoryResource()};
    }
};

FileReadResult source(std::string_view bytes)
{
    FileReadResult result;
    result.bytes.assign(bytes.begin(), bytes.end());
    result.version.kind = FileVersionKind::ContentSha256;
    const auto digest = core::Sha256::digest(bytes);
    std::copy(digest.begin(), digest.end(), result.version.digest.begin());
    return result;
}

FileReadResult source(const core::ByteBuffer& bytes)
{
    return source({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

FileReadRequest request(core::PoolHandle mount, std::string_view path, std::size_t cap)
{
    FileReadRequest result;
    result.contractVersion = 1;
    result.pathScope = FilePathScope::MountedResource;
    result.mount = mount;
    result.path = path;
    result.maxBytes = cap;
    return result;
}

void contracts(const Corpus& corpus)
{
    // Binding validation only. Actual backend/mount evidence is exercised by acquired().
    core::GenerationPool<int> identities(2);
    const auto mount = identities.emplace(1), other = identities.emplace(2);
    const auto manifest = request(mount, Sources::manifestFile, Sources::maxManifestBytes);
    Sources empty;
    queen_test::ContentProbe content;
    assert(!content.createContent(empty).success);
    assert(!empty.addCharacter(
        request(mount, "Casual.glb", Sources::maxCharacterBytes), source(corpus.files[1].bytes)));
    const auto invalidManifest = [&](auto edit) {
        auto node = Y::parseSingleDocument(corpus.manifest());
        edit(node);
        Sources rejected;
        assert(!rejected.setManifest(manifest, source(Y::serialize(node))));
        assert(!rejected.manifestSource() && !rejected.ready() && rejected.diagnostic());
    };
    invalidManifest([](auto& n) {
        n["contractVersion"] = 9;
    });
    invalidManifest([](auto& n) {
        n["unknown"] = true;
    });
    invalidManifest([](auto& n) {
        n["characters"][0]["file"] = "../Casual.glb";
    });
    invalidManifest([](auto& n) {
        n["characters"][0]["file"] = "Formal.glb";
    });
    invalidManifest([](auto& n) {
        n["characters"][1]["id"] = "player";
    });
    invalidManifest([](auto& n) {
        n["characters"][0]["file"] = Y::Node::string_type("a\0.glb", 6);
    });
    for (const auto text :
        {std::string_view("bad: ["), std::string_view("\xff", 1), std::string_view("a\0b", 3)}) {
        Sources rejected;
        assert(!rejected.setManifest(manifest, source(text)) && !rejected.ready());
    }
    for (unsigned i = 0; i < 9; ++i) {
        auto binding = manifest;
        auto bytes = source(corpus.files[0].bytes);
        switch (i) {
        case 0:
            binding.pathScope = FilePathScope::HostFilesystem;
            break;
        case 1:
            binding.mount = {};
            break;
        case 2:
            binding.maxBytes = 1;
            break;
        case 3:
            binding.path = "else.yaml";
            break;
        case 4:
            bytes.status = FileIoStatus::MissingFile;
            break;
        case 5:
            bytes.version.digest[0] ^= 1;
            break;
        case 6:
            binding.contractVersion = 0;
            break;
        case 7:
            binding.range = FileReadRange::exactRange(0, 1);
            break;
        case 8:
            binding.expectedVersion = bytes.version;
            binding.expectedVersion.digest[0] ^= 1;
            binding.maxVersionBytes = binding.maxBytes;
            break;
        }
        Sources rejected;
        assert(!rejected.setManifest(binding, std::move(bytes)) && !rejected.ready());
    }
    Sources bound;
    Sources bom;
    core::pmr::String bomText("\xef\xbb\xbf", &core::defaultCpuMemoryResource());
    bomText += corpus.manifest();
    assert(bom.setManifest(manifest, source(bomText)));
    assert(bom.manifestText() == corpus.manifest() && bom.manifestSource()->bytes[0] == 0xef);
    Sources nested;
    assert(nested.setManifest(
        request(mount, "nested/character_manifest.yaml", Sources::maxManifestBytes),
        source(corpus.files[0].bytes)));
    assert(nested.characterPath(0) == "nested/Casual.glb");
    assert(!nested.addCharacter(
        request(mount, "Casual.glb", Sources::maxCharacterBytes), source(corpus.files[1].bytes)));
    assert(bound.setManifest(manifest, source(corpus.files[0].bytes)));
    assert(!bound.setManifest(manifest, source(corpus.files[0].bytes)));
    assert(bound.manifestText() == corpus.manifest());
    const auto file = request(mount, "Casual.glb", Sources::maxCharacterBytes);
    auto foreign = file;
    foreign.mount = other;
    assert(!bound.addCharacter(foreign, source(corpus.files[1].bytes)));
    foreign = file;
    foreign.path = "else/Casual.glb";
    assert(!bound.addCharacter(foreign, source(corpus.files[1].bytes)));
    assert(bound.addCharacter(file, source(corpus.files[1].bytes)));
    const auto version = bound.characterSource(0)->version;
    assert(!bound.addCharacter(file, source(corpus.files[1].bytes)));
    assert(bound.characterSource(0)->version == version && !bound.ready());
    assert(!content.createContent(bound).success);
    assert(bound.addCharacter(request(mount, "Formal.glb", Sources::maxCharacterBytes),
               source(corpus.files[2].bytes))
        && bound.ready());
    assert(bound.characterBytes("not-declared.glb").empty());
    assert(bound.characterPath(2).empty());
    assert(bound.characterSource(2) == nullptr);
    Sources corrupt;
    assert(corrupt.setManifest(manifest, source(corpus.files[0].bytes)));
    assert(corrupt.addCharacter(file, source("corrupt GLB")));
    assert(corrupt.addCharacter(
        request(mount, "Formal.glb", Sources::maxCharacterBytes), source(corpus.files[2].bytes)));
    queen_test::ContentProbe rejected;
    assert(!rejected.createContent(corrupt).success);
}

struct Expected {
    std::uint64_t content = 0, frame = 0, geometry = 0;
};

void taskContracts(const Corpus& corpus)
{
    using Task = build_a_queen::CharacterSourceTask;
    using Phase = build_a_queen::CharacterSourcePhase;
    for (unsigned fault = 0; fault < 12; ++fault) {
        auto backend = core::makeShared<MemoryFileBackend>(MemoryFileBackendDesc{3,
            2 * Sources::maxCharacterBytes + Sources::maxManifestBytes,
            1,
            Sources::maxCharacterBytes,
            512});
        for (std::size_t i = 0; i < corpus.files.size(); ++i) {
            if (fault == 8 && i == 1)
                continue;
            const auto& file = corpus.files[i];
            assert(backend->registerFile(file.name,
                       fault == 9 && i == 0 ? reinterpret_cast<const std::uint8_t*>("bad: [")
                                            : file.bytes.data(),
                       fault == 9 && i == 0 ? 6 : file.bytes.size())
                == FileIoStatus::Success);
        }
        MountedFileBackend mounts({1, 1, Sources::maxCharacterBytes, 32, 512});
        auto mount = mounts.mount("task-contract", backend);
        core::runtime::CompletionQueue queue({2});
        FileService files(queue, mounts);
        core::runtime::CancellationSource external;
        auto binding = request(mount, Sources::manifestFile, Sources::maxManifestBytes);
        binding.cancellation = external.token();
        if (fault == 10)
            binding.maxBytes = Sources::maxManifestBytes + 1;
        auto task = core::makeUnique<Task>(files, std::move(binding));
        assert(task->phase() == Phase::Pending && !files.telemetry().submitted);
        assert(!task->takeSources());
        if (fault == 0)
            task->cancel();
        if (fault == 3)
            external.cancel();
        if (fault == 11)
            files.shutdown();
        task->advance();
        if (fault == 1)
            task->cancel();
        if (fault == 2)
            task.reset(); // A queued callback retains state, not the task object.
        if (fault == 5)
            assert(mounts.unmount(mount));
        assert(queue.dispatch(2).success());
        if (fault == 4)
            external.cancel();
        if (fault == 6)
            assert(mounts.unmount(mount));
        if (fault == 7) {
            task->advance(); // Manifest consumed; no GLB submitted yet.
            assert(files.telemetry().submitted == 1);
            assert(mounts.unmount(mount));
            const auto replacement = mounts.mount("task-contract", backend);
            assert(replacement && !(replacement == mount));
        }
        for (unsigned step = 0; task && task->phase() == Phase::Pending && step < 10; ++step) {
            task->advance();
            assert(queue.dispatch(2).success());
        }
        if (task) {
            assert(task->phase() == ((fault <= 4) ? Phase::Cancelled : Phase::Failed));
            if (task->phase() == Phase::Cancelled)
                assert(task->ioStatus() == FileIoStatus::Cancelled);
            assert(task->diagnostic() && !task->takeSources());
        }
        if (fault == 0 || fault == 3 || fault == 10 || fault == 11)
            assert(!files.telemetry().submitted);
        if (fault == 7)
            assert(files.telemetry().submitted == 1);
        task.reset();
        files.shutdown();
        mounts.shutdown();
        assert(queue.dispatch(2).success());
        assert(queue.pending() == 0 && queue.reserved() == 0);
        assert(mounts.telemetry().inFlight == 0 && backend->telemetry().inFlight == 0);
    }
}

void acquired(const Corpus& corpus,
    Expected expected,
    const char* http = nullptr,
    bool memory = false,
    bool renamed = false)
{
    core::UniquePtr<Sources> prepared;
    {
        core::SharedPtr<ManagedFileBackend> backend;
        if (http) {
            HttpFileBackendDesc desc;
            desc.baseUrl = http;
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = 4 * 1024 * 1024;
            desc.maxPathBytes = 512;
            desc.maxHeaderBytes = 8192;
            desc.ioChunkBytes = 16384;
            desc.timeoutMs = 5000;
            desc.cancellationPollMs = 10;
            backend = core::makeShared<HttpFileBackend>(desc);
        } else if (memory) {
            auto files = core::makeShared<MemoryFileBackend>(MemoryFileBackendDesc{3,
                2 * Sources::maxCharacterBytes + Sources::maxManifestBytes,
                1,
                Sources::maxCharacterBytes,
                512});
            for (const auto& file : corpus.files)
                assert(files->registerFile(file.name, file.bytes.data(), file.bytes.size())
                    == FileIoStatus::Success);
            backend = std::move(files);
        } else {
            NativeDirectoryFileBackendDesc desc;
            desc.rootPath = ARP_QUEEN_CHARACTER_FIXTURE;
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = 4 * 1024 * 1024;
            desc.maxPathBytes = 512;
            desc.ioChunkBytes = 16384;
            desc.directoryBufferBytes = 4096;
            desc.maxScanEntries = 16;
            desc.maxScanNameBytes = 4096;
            backend = core::makeShared<NativeDirectoryFileBackend>(desc);
        }
        MountedFileBackend mounts({1, 1, Sources::maxCharacterBytes, 32, 512});
        const auto mount = mounts.mount("queen-inputs", backend);
        assert(mount);
        core::runtime::CompletionQueue queue({2});
        FileService files(queue, mounts);
        const auto binding = request(mount, Sources::manifestFile, Sources::maxManifestBytes);
        build_a_queen::CharacterSourceTask task(files, binding);
        using Phase = build_a_queen::CharacterSourcePhase;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        assert(!files.telemetry().submitted);
        while (task.phase() == Phase::Pending && std::chrono::steady_clock::now() < deadline) {
            assert(queue.dispatch(2).success());
            task.advance();
            std::this_thread::yield();
        }
        assert(task.phase() == Phase::Ready);
        prepared = task.takeSources();
        assert(task.phase() == Phase::Consumed && !task.takeSources());
        task.advance();
        assert(files.telemetry().submitted == 3);
        assert(prepared->manifestSource()->bytes == corpus.files[0].bytes);
        for (std::size_t i = 0; i < Sources::characterCount; ++i)
            assert(prepared->characterSource(i)->bytes == corpus.files[i + 1].bytes);
        assert(prepared->ready() && mounts.unmount(mount));
        backend->shutdown();
        files.shutdown();
        assert(queue.pending() == 0 && queue.reserved() == 0 && mounts.telemetry().inFlight == 0);
        if (auto* native = dynamic_cast<NativeDirectoryFileBackend*>(backend.get()))
            assert(
                native->telemetry().openNativeObjects == 0 && !native->telemetry().reservedBytes);
        if (auto* remote = dynamic_cast<HttpFileBackend*>(backend.get()))
            assert(!remote->telemetry().inFlight && !remote->telemetry().activeNativeTransfers
                && !remote->telemetry().reservedBytes);
    }
    queen_test::ContentProbe content;
    assert(content.createContent(*prepared).success);
    prepared.reset();
    content.verify();
    assert(renamed || content.staticFingerprint() == expected.content);
    assert(content.staticFingerprint(false) == expected.geometry);
    assert(content.frameFingerprint(.23f) == expected.frame);
}
} // namespace

int run(int argc, char** argv)
{
    Corpus corpus;
    Expected expected;
    {
        queen_test::ContentProbe reference;
        assert(reference.createContent(core::pmr::String(ARP_QUEEN_CHARACTER_FIXTURE)).success);
        expected = {reference.staticFingerprint(),
            reference.frameFingerprint(.23f),
            reference.staticFingerprint(false)};
        assert(reference.vertexCapacity() * sizeof(build_a_queen::SceneContent::Vertex)
            <= 32 * 1024 * 1024);
    }
    const char* http = argc == 3 && std::string_view(argv[1]) == "--http" ? argv[2] : nullptr;
    contracts(corpus);
    taskContracts(corpus);
    {
        Corpus renamed;
        auto manifest = Y::parseSingleDocument(renamed.manifest());
        manifest["characters"][0]["file"] = "player-model.glb";
        manifest["characters"][1]["file"] = "rival-model.glb";
        const auto text = Y::serialize(manifest);
        renamed.files[0].bytes.assign(text.begin(), text.end());
        renamed.files[1].name = "player-model.glb";
        renamed.files[2].name = "rival-model.glb";
        acquired(renamed, expected, nullptr, true, true);
        std::puts(
            "PASS exact manifest-declared alternate filenames, static geometry and gameplay frame");
    }
#ifdef __EMSCRIPTEN__
    const bool memory = true;
#else
    const bool memory = false;
#endif
    acquired(corpus, expected, http, memory);
    const auto before = core::defaultCpuAllocator().stats();
    const auto cycles = argc > 1 ? 20 : 1;
    for (int i = 0; i < cycles; ++i) {
        taskContracts(corpus);
        acquired(corpus, expected, http, memory);
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Queen prepared %s %d ordinary cycles; content=%016llx frame=%016llx; "
                "core bytes %llu -> %llu live %llu -> %llu\n",
        http         ? "HTTP"
            : memory ? "memory"
                     : "native",
        cycles,
        (unsigned long long)expected.content,
        (unsigned long long)expected.frame,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    return 0;
}

int main(int argc, char** argv)
{
    const auto before = core::defaultCpuAllocator().stats();
    const auto status = run(argc, argv);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf(
        "Queen corpus/source/content owners retired: core bytes %llu -> %llu live %llu -> %llu\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    return status;
}
