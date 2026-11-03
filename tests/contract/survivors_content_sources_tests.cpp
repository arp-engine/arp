#include "core/io/file_io.h"
#include "core/io/memory_file_backend.h"
#include "core/io/native_directory_file_backend.h"
#include "examples/survivors/content_source_task.h"
#include "tests/fixtures/queen_source_runtime.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
using namespace arp;
using namespace core::io;
using namespace survivors;
using Phase = examples::StartupResourcePhase;
using Y = core::YamlContract;

struct File {
    core::pmr::String path;
    core::pmr::Vector<std::uint8_t> bytes;
};

struct Corpus {
    GameConfig config;
    core::pmr::Vector<File> files;
    std::filesystem::path root;

    explicit Corpus(const std::filesystem::path& directory) : root(directory)
    {
        core::pmr::String diagnostic;
        assert(config.load(core::FilePath::utf8(root), diagnostic));
        auto add = [&](core::pmr::String path) {
            auto bytes =
                core::readBinaryFile(root / path.c_str(), ContentSourceTask::maxSourceFileBytes);
            assert(bytes.success);
            files.push_back({std::move(path), std::move(bytes.bytes)});
        };
        add(ContentBindings::manifest);
        const auto n = Y::Node::deserialize(core::pmr::String(
            reinterpret_cast<const char*>(files[0].bytes.data()), files[0].bytes.size()));
        add(Y::text(n, "atlasMetadata"));
        add(Y::text(n, "atlasImage"));
        for (const auto* path :
            {ContentBindings::tileSet, ContentBindings::tileMap, ContentBindings::prefab})
            add(path);
        for (const auto& sound : ContentBindings::sounds)
            add(sound.path);
        assert(files.size() == ContentBindings::maxFiles);
    }
};

void verify(PreparedContent& input, const Corpus& corpus)
{
    resource::ResourceManager oldResources;
    Content old(oldResources);
    core::pmr::String diagnostic;
    assert(old.load(core::FilePath::utf8(corpus.root), corpus.config, diagnostic));
    auto& content = input.content();
    assert(content.atlas.ready() && content.map.ready() && content.orbitPrefab.ready());
    assert(content.atlas.id() == old.atlas.id() && content.map.id() == old.map.id());
    assert(content.atlas.get()->image().rgba8 == old.atlas.get()->image().rgba8);
    assert(content.orbitPrefab.get()->yaml() == old.orbitPrefab.get()->yaml());
    const auto manifest = Y::Node::deserialize(core::pmr::String(
        reinterpret_cast<const char*>(corpus.files[0].bytes.data()), corpus.files[0].bytes.size()));
    for (const auto& node : Y::sequence(manifest, "sprites")) {
        const auto name = Y::text(node, "name");
        const auto actual = content.sprite(name), before = old.sprite(name);
        assert(actual.ready() && before.ready() && actual.id() == before.id());
        assert(actual.get()->sliceName() == before.get()->sliceName());
    }
    for (const auto& node : Y::sequence(manifest, "animations")) {
        const auto name = Y::text(node, "name");
        const auto actual = content.animation(name), before = old.animation(name);
        assert(actual.ready() && before.ready() && actual.id() == before.id());
        assert(actual.get()->loop() == before.get()->loop());
        const auto& a = actual.get()->frames();
        const auto& b = before.get()->frames();
        assert(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
            assert(a[i].spriteId == b[i].spriteId && a[i].seconds == b[i].seconds
                && a[i].eventId == b[i].eventId);
    }
    const auto& chunks = content.map.get()->chunks();
    const auto& previous = old.map.get()->chunks();
    assert(chunks.size() == previous.size());
    for (std::size_t c = 0; c < chunks.size(); ++c) {
        assert(chunks[c].x == previous[c].x && chunks[c].y == previous[c].y
            && chunks[c].layers.size() == previous[c].layers.size());
        for (std::size_t l = 0; l < chunks[c].layers.size(); ++l) {
            const auto& a = chunks[c].layers[l];
            const auto& b = previous[c].layers[l];
            assert(a.name == b.name && a.sortingLayer == b.sortingLayer
                && a.sortingOrder == b.sortingOrder && a.cells.size() == b.cells.size());
            for (std::size_t i = 0; i < a.cells.size(); ++i)
                assert(a.cells[i].tileId == b.cells[i].tileId && a.cells[i].x == b.cells[i].x
                    && a.cells[i].y == b.cells[i].y
                    && a.cells[i].quarterTurns == b.cells[i].quarterTurns
                    && a.cells[i].flipX == b.cells[i].flipX
                    && a.cells[i].flipY == b.cells[i].flipY);
        }
    }
    for (std::size_t i = 0; i < content.sounds.size(); ++i) {
        const auto actual = content.sounds[i].get()->view(), before = old.sounds[i].get()->view();
        assert(actual.valid() && actual.frameCount == before.frameCount
            && actual.channels == before.channels);
        assert(
            std::memcmp(
                actual.samples, before.samples, actual.frameCount * actual.channels * sizeof(float))
            == 0);
    }
    assert(input.sourceIdentity().find("sha256-base64:") == 0);
}

enum class Fault {
    None,
    Missing,
    Version,
    Escape,
    Duplicate,
    Png,
    TileSet,
    Map,
    Prefab,
    Wave,
    Utf8,
    Budget,
    InvalidMount,
    Stale,
    Cancel,
    Destroy
};

void run(queen_source_fixture::Runtime fixture, const Corpus& corpus, Fault fault)
{
    auto& runtime = *fixture.owner;
    auto config = corpus.config;
    if (fault == Fault::Budget)
        config.maxTextureBytes = ContentSourceTask::maxTextureFileBytes + 1;
    auto mount = fixture.mount;
    if (fault == Fault::Stale)
        assert(fixture.mounts->unmount(mount));
    if (fault == Fault::InvalidMount)
        mount = {};
    auto task = core::makeUnique<ContentSourceTask>(
        runtime.fileService(), runtime.completionQueue(), mount, config);
    task->advance();
    if (fault == Fault::Cancel)
        task->cancel();
    if (fault == Fault::Destroy)
        task.reset();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (task && task->phase() == Phase::Pending) {
        assert(std::chrono::steady_clock::now() < deadline);
        assert(runtime.dispatch().success());
        task->advance();
    }
    if (fault != Fault::None) {
        assert(!task
            || (task->phase() != Phase::Ready && !task->preparation()
                && !task->diagnostic().empty()));
        task.reset();
        runtime.shutdown();
        assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
        return;
    }
    if (task->phase() != Phase::Ready)
        std::fprintf(
            stderr, "content: %.*s\n", int(task->diagnostic().size()), task->diagnostic().data());
    assert(task->phase() == Phase::Ready && task->readCount() == 10);
    auto prepared = task->preparation();
    task.reset();
    fixture.owner.reset();
    fixture.mounts.reset();
    assert(prepared);
    verify(*prepared, corpus);
    std::printf("content identity: %s payload=%zu\n",
        prepared->sourceIdentity().c_str(),
        prepared->content().processedByteCount());
}

void cycle(const Corpus& corpus, Fault fault)
{
    auto memory = core::makeShared<MemoryFileBackend>(MemoryFileBackendDesc{
        12, 20u * 1024u * 1024u, 1, ContentSourceTask::maxSourceFileBytes, 512});
    for (const auto& file : corpus.files) {
        if (fault == Fault::Missing && file.path == "dungeon.png")
            continue;
        auto bytes = file.bytes;
        if (file.path == ContentBindings::manifest
            && (fault == Fault::Version || fault == Fault::Escape || fault == Fault::Duplicate)) {
            auto node = Y::Node::deserialize(
                core::pmr::String(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            if (fault == Fault::Version)
                node["contractVersion"] = 99;
            if (fault == Fault::Escape)
                node["atlasImage"] = "../outside.png";
            if (fault == Fault::Duplicate)
                node["atlasImage"] = "content.yaml";
            const auto text = Y::serialize(node);
            bytes.assign(text.begin(), text.end());
        }
        if ((fault == Fault::Png && file.path == "dungeon.png")
            || (fault == Fault::Wave && file.path == "hit.wav")
            || (fault == Fault::Utf8 && file.path == ContentBindings::manifest))
            bytes[0] = 0xff;
        if ((fault == Fault::TileSet && file.path == ContentBindings::tileSet)
            || (fault == Fault::Map && file.path == ContentBindings::tileMap)
            || (fault == Fault::Prefab && file.path == ContentBindings::prefab)) {
            const core::pmr::String invalid = "invalid: [";
            bytes.assign(invalid.begin(), invalid.end());
        }
        assert(
            memory->registerFile(file.path, bytes.data(), bytes.size()) == FileIoStatus::Success);
    }
    run(queen_source_fixture::create(memory, ContentSourceTask::maxSourceFileBytes), corpus, fault);
    assert(!memory->telemetry().inFlight);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    const bool http = std::strcmp(argv[1], "--http") == 0;
    Corpus corpus(http ? ARP_SURVIVORS_ASSET_DIR : argv[1]);
    if (http) {
        assert(argc == 3);
        run(queen_source_fixture::create(argv[2], ContentSourceTask::maxSourceFileBytes),
            corpus,
            Fault::None);
    } else if (argc > 2 && std::strcmp(argv[2], "--native") == 0) {
        NativeDirectoryFileBackendDesc desc;
        desc.rootPath = core::FilePath::utf8(corpus.root);
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = ContentSourceTask::maxSourceFileBytes;
        desc.maxPathBytes = 512;
        desc.ioChunkBytes = 16384;
        desc.directoryBufferBytes = 4096;
        desc.maxScanEntries = 16;
        desc.maxScanNameBytes = 4096;
        run(queen_source_fixture::create(core::makeShared<NativeDirectoryFileBackend>(desc),
                ContentSourceTask::maxSourceFileBytes),
            corpus,
            Fault::None);
    } else {
        cycle(corpus, Fault::None);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < (argc > 2 ? 20u : 1u); ++i)
            for (const auto fault : {Fault::None,
                     Fault::Missing,
                     Fault::Version,
                     Fault::Escape,
                     Fault::Duplicate,
                     Fault::Png,
                     Fault::TileSet,
                     Fault::Map,
                     Fault::Prefab,
                     Fault::Wave,
                     Fault::Utf8,
                     Fault::Budget,
                     Fault::InvalidMount,
                     Fault::Stale,
                     Fault::Cancel,
                     Fault::Destroy})
                cycle(corpus, fault);
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("content core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::puts("PASS typed Content10, original resources/PCM, IO retired before consumption");
}
