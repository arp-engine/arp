#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "core/io/memory_file_backend.h"
#include "core/io/native_directory_file_backend.h"
#include "examples/survivors/gameplay_source_task.h"
#include "tests/fixtures/queen_source_runtime.h"
#include "tests/fixtures/survivors_gameplay_identity.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
using namespace arp;
using namespace core::io;
using Task = survivors::GameplaySourceTask;
using Limits = survivors::GameplayTextLimits;
using Phase = examples::StartupResourcePhase;
enum class Fault {
    None,
    PreserveBytes,
    Missing,
    ConfigType,
    ConfigVersion,
    ManifestType,
    ManifestVersion,
    Escape,
    Duplicate,
    TooMany,
    Profile,
    FileBudget,
    TotalBudget,
    Utf8,
    Nul,
    CancelBefore,
    CancelQueued,
    Destroy,
    External,
    Stale,
    Closed,
    InvalidMount
};

struct File {
    core::pmr::String path;
    core::pmr::Vector<std::uint8_t> bytes;
};

struct Files {
    core::pmr::Vector<File> files;

    explicit Files(const std::filesystem::path& root)
    {
        auto manifest = core::readTextFile(root / "script-package.yaml", Limits::maxMetadataBytes);
        assert(manifest.success);
        core::pmr::String diagnostic;
        core::pmr::Vector<core::pmr::String> paths;
        assert(survivors::Rules::sourcePaths(manifest.text, paths, diagnostic));
        paths.insert(paths.begin(), "script-package.yaml");
        paths.insert(paths.begin(), "game.yaml");
        for (const auto& path : paths) {
            auto input = core::readBinaryFile(root / path.c_str(), Limits::maxSourceBytes);
            assert(input.success);
            files.push_back({path, std::move(input.bytes)});
        }
    }
};

void verify(const survivors::PreparedGameplay& input,
    const std::filesystem::path& root,
    bool preserve = false)
{
    core::pmr::String diagnostic;
    survivors::GameConfig config;
    survivors::Rules rules;
    assert(config.load(core::FilePath::utf8(root), diagnostic));
    assert(rules.load(core::FilePath::utf8(root), config.maxSourceBytes, diagnostic));
    assert(input.config.duration == config.duration
        && input.config.maxSourceBytes == config.maxSourceBytes);
    assert(input.rules.interpreter() && input.rules.configYaml() == rules.configYaml());
    if (preserve) {
        // A BOM inside a declared script is preserved, never silently stripped.
        const auto before =
            core::readTextFile(root / "scripts/foundation.arp", Limits::maxSourceBytes);
        assert(before.success);
        core::pmr::String expected("\xef\xbb\xbf// byte preservation\r\n");
        expected += rules.source();
        assert(input.rules.source() == expected);
    } else {
        assert(input.rules.source() == rules.source());
        assert(core::Sha256::hexDigest(core::StringView(input.rules.source()))
            == tests::SurvivorsGameplayIdentity::sourceSha256);
        assert(core::Sha256::hexDigest(core::StringView(input.rules.configYaml()))
            == tests::SurvivorsGameplayIdentity::configSha256);
    }
}

void run(queen_source_fixture::Runtime fixture, const std::filesystem::path& root, Fault fault)
{
    auto& runtime = *fixture.owner;
    core::runtime::CancellationSource external;
    auto task = core::makeUnique<Task>(runtime.fileService(),
        runtime.completionQueue(),
        fault == Fault::InvalidMount ? core::PoolHandle{} : fixture.mount,
        external.token());
    assert(task->phase() == Phase::Pending && !task->readCount() && !task->preparation());
    if (fault == Fault::CancelBefore)
        task->cancel();
    if (fault == Fault::Closed)
        runtime.fileService().shutdown();
    task->advance();
    if (fault == Fault::CancelQueued)
        task->cancel();
    if (fault == Fault::Destroy)
        task.reset();
    bool unmounted = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (task && task->phase() == Phase::Pending && std::chrono::steady_clock::now() < deadline) {
        assert(runtime.dispatch(1).success());
        if (fault == Fault::External)
            external.cancel();
        if (fault == Fault::Stale && !unmounted) {
            assert(fixture.mounts->unmount(fixture.mount));
            unmounted = true;
        }
        task->advance();
        std::this_thread::yield();
    }
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
    assert(!fixture.mounts->telemetry().inFlight);
    if (!task)
        return;
    if (fault != Fault::None && fault != Fault::PreserveBytes) {
        assert(task->phase() == Phase::Failed || task->phase() == Phase::Cancelled);
        assert(!task->preparation() && !task->diagnostic().empty());
        if (task->phase() == Phase::Cancelled)
            assert(task->diagnostic() == "SURVIVORS-GAMEPLAY-CANCELLED");
        if (fault == Fault::Missing)
            assert(task->fileStatus() == FileIoStatus::MissingFile && task->diagnosticId());
        return;
    }
    if (task->phase() != Phase::Ready)
        std::fprintf(
            stderr, "gameplay: %.*s\n", int(task->diagnostic().size()), task->diagnostic().data());
    assert(task->phase() == Phase::Ready && task->readCount() == 9);
    assert(task->fileStatus() == FileIoStatus::Success);
    const auto input = task->preparation();
    task.reset();
    fixture.owner.reset();
    fixture.mounts.reset();
    verify(*input, root, fault == Fault::PreserveBytes);
}

void cycle(const Files& corpus, const std::filesystem::path& root, Fault fault)
{
    auto memory = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{40, Limits::maxInputBytes, 1, Limits::maxSourceBytes, 512});
    assert(memory->registerDirectory("scripts") == FileIoStatus::Success);
    for (const auto& file : corpus.files) {
        if (fault == Fault::Missing && file.path == "scripts/foundation.arp")
            continue;
        auto bytes = file.bytes;
        if (file.path == "game.yaml") {
            auto node = core::YamlContract::Node::deserialize(
                core::pmr::String(bytes.begin(), bytes.end()));
            bool changed = true;
            if (fault == Fault::ConfigType)
                node["type"] = "other-config";
            else if (fault == Fault::ConfigVersion)
                node["contractVersion"] = 99;
            else if (fault == Fault::Profile)
                node["maxSourceBytes"] = Limits::maxSourceBytes + 1;
            else if (fault == Fault::FileBudget)
                node["maxSourceBytes"] = 1;
            else if (fault == Fault::TotalBudget)
                node["maxSourceBytes"] = 12;
            else
                changed = false;
            if (changed) {
                const auto text = core::YamlContract::serialize(node);
                bytes.assign(text.begin(), text.end());
            }
        }
        if (file.path == "script-package.yaml") {
            auto node = core::YamlContract::Node::deserialize(
                core::pmr::String(bytes.begin(), bytes.end()));
            bool changed = true;
            if (fault == Fault::ManifestType)
                node["type"] = "other-script-package";
            else if (fault == Fault::ManifestVersion)
                node["contractVersion"] = 99;
            else if (fault == Fault::Escape)
                node["sources"][0] = "../outside.arp";
            else if (fault == Fault::Duplicate)
                node["sources"][1] = node["sources"][0];
            else if (fault == Fault::TooMany) {
                for (std::size_t i = 0; i < Limits::maxScripts; ++i)
                    node["sources"]
                        .get_value_ref<core::YamlContract::Node::sequence_type&>()
                        .push_back("scripts/extra.arp");
            } else
                changed = false;
            if (changed) {
                const auto text = core::YamlContract::serialize(node);
                bytes.assign(text.begin(), text.end());
            }
        }
        if (file.path == "scripts/foundation.arp") {
            if (fault == Fault::Utf8)
                bytes[0] = 0xff;
            if (fault == Fault::Nul)
                bytes[0] = 0;
            if (fault == Fault::PreserveBytes) {
                const core::pmr::String prefix("\xef\xbb\xbf// byte preservation\r\n");
                bytes.insert(bytes.begin(), prefix.begin(), prefix.end());
            }
        }
        if (fault == Fault::TotalBudget && file.path.find("scripts/") == 0)
            bytes.assign(8, 'a');
        assert(
            memory->registerFile(file.path, bytes.data(), bytes.size()) == FileIoStatus::Success);
    }
    run(queen_source_fixture::create(memory, Limits::maxSourceBytes), root, fault);
    assert(!memory->telemetry().inFlight);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    const bool http = std::strcmp(argv[1], "--http") == 0;
    const bool native = argc > 2 && std::strcmp(argv[2], "--native") == 0;
    const std::filesystem::path root = http ? ARP_SURVIVORS_ASSET_DIR : argv[1];
    if (http) {
        assert(argc == 3);
        run(queen_source_fixture::create(argv[2], Limits::maxSourceBytes), root, Fault::None);
    } else if (native) {
        NativeDirectoryFileBackendDesc desc;
        desc.rootPath = core::FilePath::utf8(root);
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = Limits::maxSourceBytes;
        desc.maxPathBytes = 512;
        desc.ioChunkBytes = 16384;
        desc.directoryBufferBytes = 65536;
        desc.maxScanEntries = 32;
        desc.maxScanNameBytes = 32 * desc.maxPathBytes;
        run(queen_source_fixture::create(
                core::makeShared<NativeDirectoryFileBackend>(desc), Limits::maxSourceBytes),
            root,
            Fault::None);
    } else {
        Files corpus(root);
        cycle(corpus, root, Fault::None);
        {
            survivors::Rules rules;
            core::pmr::String diagnostic;
            core::pmr::Vector<core::pmr::String> parts{"a\r\n", "b"};
            assert(rules.prepare("original-config", parts, 5, diagnostic));
            assert(rules.source() == "a\r\n\nb");
            assert(!rules.prepare("replacement-config", parts, 4, diagnostic));
            assert(diagnostic == "ARP-SURVIVORS-SCRIPT-SOURCE-BUDGET");
            assert(rules.source() == "a\r\n\nb" && rules.configYaml() == "original-config");
        }
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < (argc > 2 ? 20u : 1u); ++i)
            for (const auto fault : {Fault::None,
                     Fault::PreserveBytes,
                     Fault::Missing,
                     Fault::ConfigType,
                     Fault::ConfigVersion,
                     Fault::ManifestType,
                     Fault::ManifestVersion,
                     Fault::Escape,
                     Fault::Duplicate,
                     Fault::TooMany,
                     Fault::Profile,
                     Fault::FileBudget,
                     Fault::TotalBudget,
                     Fault::Utf8,
                     Fault::Nul,
                     Fault::CancelBefore,
                     Fault::CancelQueued,
                     Fault::Destroy,
                     Fault::External,
                     Fault::Stale,
                     Fault::Closed,
                     Fault::InvalidMount})
                cycle(corpus, root, fault);
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Survivors gameplay core=%llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::puts(
        "PASS original gameplay text, nine explicit reads, IO retired before consumption; no GPU claim");
}
