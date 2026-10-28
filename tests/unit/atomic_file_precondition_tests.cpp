#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/versioned_yaml_store.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {
using namespace arp::core;
using namespace arp::core::io;
using Condition = FileReplacePrecondition;

pmr::Vector<std::uint8_t> bytes(std::string_view text)
{
    return {text.begin(), text.end()};
}

class FileFixture final {
public:
    FileFixture()
        : root(std::filesystem::temp_directory_path()
              / ("arp-conditional-file-"
                  + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        assert(std::filesystem::create_directory(root));
    }

    ~FileFixture() { std::filesystem::remove_all(root); }

    pmr::String path(std::string_view name = "target.yaml") const
    {
        return FilePath::utf8(root / std::filesystem::u8path(name));
    }

    void write(std::string_view text)
    {
        std::ofstream file(root / "target.yaml", std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        assert(file.good());
    }

    void expect(std::string_view text) const
    {
        const auto result = readBinaryFile(root / "target.yaml", 65536);
        assert(result.success && result.bytes == bytes(text));
    }

    void noTemporaryFiles() const
    {
        for (const auto& entry : std::filesystem::directory_iterator(root))
            assert(entry.path().filename().string().find(".arp-tmp-") == std::string::npos);
    }

    std::filesystem::path root;
};

AtomicFileResult finish(AtomicFileService& service, PoolHandle handle)
{
    assert(handle);
    AtomicFileResult result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (service.take(handle, result))
            return result;
        std::this_thread::yield();
    }
    assert(false && "file completion deadline");
    return result;
}

void expectFailure(const AtomicFileResult& result, std::string_view diagnostic)
{
    assert(!result.success && !result.committed && result.diagnostic == diagnostic);
}

Condition match(std::string_view text)
{
    Condition condition(Condition::Kind::MatchBytes);
    condition.expectedBytes = bytes(text);
    return condition;
}

struct Schema {
    static constexpr std::string_view type() { return "arp.test"; }

    static constexpr std::uint32_t version() { return 1; }

    static bool validate(const YamlContract::Node&, pmr::String&) { return true; }
};

void cycle(FileFixture& files)
{
    std::filesystem::remove(files.root / "target.yaml");
    AtomicFileService service(2, 128);
    auto result = finish(service,
        service.replaceIfUnchanged(
            files.path(), bytes("one\n"), Condition(Condition::Kind::MustBeMissing)));
    assert(result.success && result.committed);
    files.expect("one\n");
    expectFailure(finish(service,
                      service.replaceIfUnchanged(
                          files.path(), bytes("two\n"), Condition(Condition::Kind::MustBeMissing))),
        "ARP-FILE-PRECONDITION-EXISTS");
    files.expect("one\n");
    // Same byte count and timestamp cannot stand in for an exact comparison.
    const auto timestamp = std::filesystem::last_write_time(files.root / "target.yaml");
    files.write("two\n");
    std::filesystem::last_write_time(files.root / "target.yaml", timestamp);
    expectFailure(
        finish(service, service.replaceIfUnchanged(files.path(), bytes("new"), match("one\n"))),
        "ARP-FILE-PRECONDITION-CHANGED");
    files.expect("two\n");
    files.write("one\r\n");
    expectFailure(
        finish(service, service.replaceIfUnchanged(files.path(), bytes("new"), match("one\n"))),
        "ARP-FILE-PRECONDITION-CHANGED");
    files.expect("one\r\n");
    result = finish(service, service.replaceIfUnchanged(files.path(), {}, match("one\r\n")));
    assert(result.success && result.committed);
    files.expect("");
    result = finish(
        service, service.replaceIfUnchanged(files.path(), bytes("empty matched"), match("")));
    assert(result.success && result.committed);
    std::filesystem::remove(files.root / "target.yaml");
    result = finish(service, service.replaceIfUnchanged(files.path(), {}, match("")));
    expectFailure(result, "ARP-FILE-PRECONDITION-MISSING");
    assert(result.missing && !std::filesystem::exists(files.root / "target.yaml"));
    std::filesystem::create_directory(files.root / "target.yaml");
    expectFailure(finish(service, service.replaceIfUnchanged(files.path(), {}, match(""))),
        "ARP-FILE-PRECONDITION-UNREADABLE");
    assert(std::filesystem::is_directory(files.root / "target.yaml"));
    std::filesystem::remove(files.root / "target.yaml");
    const pmr::String large(129, 'x');
    files.write(large);
    expectFailure(
        finish(service, service.replaceIfUnchanged(files.path(), bytes("new"), match("old"))),
        "ARP-FILE-PRECONDITION-UNREADABLE");
    files.expect(large);
    assert(!service.replaceIfUnchanged(files.path(), bytes(large), match("old")));
    assert(std::string_view(service.diagnostic()) == "ARP-FILE-REQUEST-CONFIG");
    assert(!service.replaceIfUnchanged(files.path(), {}, match(large)));
    assert(std::string_view(service.diagnostic()) == "ARP-FILE-PRECONDITION-CONFIG");
    auto excessCapacity = bytes("small");
    excessCapacity.reserve(129);
    assert(!service.replaceIfUnchanged(files.path(), std::move(excessCapacity), match("old")));
    auto excessExpected = match("old");
    excessExpected.expectedBytes.reserve(129);
    assert(!service.replaceIfUnchanged(files.path(), {}, std::move(excessExpected)));
    auto invalid = match("old");
    invalid.kind = Condition::Kind::MustBeMissing;
    assert(!service.replaceIfUnchanged(files.path(), {}, std::move(invalid)));
    invalid = Condition(static_cast<Condition::Kind>(99));
    assert(!service.replaceIfUnchanged(files.path(), {}, std::move(invalid)));
    assert(!service.replaceIfUnchanged({}, {}, match("")));
    files.expect(large);
    // Pool occupancy includes completed requests until the owner consumes them.
    const auto first = service.read(files.path());
    const auto second = service.read(files.path());
    assert(first && second);
    assert(!service.replaceIfUnchanged(files.path(), {}, match("")));
    assert(std::string_view(service.diagnostic()) == "ARP-FILE-REQUEST-CAPACITY");
    finish(service, first);
    finish(service, second);
    AtomicFileResult stale;
    assert(!service.take(first, stale));
    assert(std::string_view(service.diagnostic()) == "ARP-FILE-REQUEST-STALE");
    // The old validator path still checks BOTH old and replacement documents.
    VersionedYamlStore<Schema> store(service);
    files.write("type: arp.test\ncontractVersion: 2\n");
    const pmr::String valid("type: arp.test\ncontractVersion: 1\n");
    expectFailure(
        finish(service, store.save(files.path(), valid)), "ARP-SAVE-UNSUPPORTED-CONTRACT");
    files.expect("type: arp.test\ncontractVersion: 2\n");
    files.write("invalid: [");
    expectFailure(finish(service, store.save(files.path(), valid)), "ARP-SAVE-CORRUPT-YAML");
    files.expect("invalid: [");
    files.write(valid);
    expectFailure(finish(service, store.save(files.path(), "invalid: [")), "ARP-SAVE-CORRUPT-YAML");
    assert(finish(service, store.save(files.path(), valid)).committed);
    assert(finish(service, store.load(files.path())).success);
    files.noTemporaryFiles();
}

void submissionFailure(FileFixture& files)
{
    files.write("old");
    AtomicFileService service(1, 128);
    const auto quota = makeShared<CpuMemoryQuota>(1);
    bool rejected = false;
    for (unsigned round = 0; round < 20 && !rejected; ++round) {
        auto path = files.path();
        auto replacement = bytes("new");
        auto expected = match(round == 0 ? "old" : "new");
        PoolHandle handle;
        {
            CpuMemoryQuotaScope scope(quota);
            handle = service.replaceIfUnchanged(
                std::move(path), std::move(replacement), std::move(expected));
        }
        if (handle)
            assert(finish(service, handle).success);
        else {
            rejected = true;
            assert(std::string_view(service.diagnostic()) == "ARP-FILE-WORK-EXCEPTION");
        }
    }
    assert(rejected && quota->liveBytes() == 0);
    // The failed submission must not consume the only request slot.
    assert(finish(service, service.read(files.path())).success);
}

void drain(FileFixture& files)
{
    files.write("old");
    {
        AtomicFileService service(1, 128);
        assert(service.replaceIfUnchanged(files.path(), bytes("drained"), match("old")));
    }
    files.expect("drained");
    files.noTemporaryFiles();
}
} // namespace

int main(int argc, char** argv)
{
    FileFixture files;
    cycle(files);
    submissionFailure(files);
    drain(files);
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        const auto before = defaultCpuAllocator().stats();
        for (unsigned round = 0; round < 20; ++round) {
            cycle(files);
            drain(files);
            const auto after = defaultCpuAllocator().stats();
            assert(after.liveBytes == before.liveBytes);
            assert(after.allocationCount - after.deallocationCount
                == before.allocationCount - before.deallocationCount);
        }
        std::printf(
            "ordinary_cycles=20 live_bytes_delta=0 live_count_delta=0 core_process_peak_bytes=%llu\n",
            static_cast<unsigned long long>(defaultCpuAllocator().stats().peakBytes));
    }
}
