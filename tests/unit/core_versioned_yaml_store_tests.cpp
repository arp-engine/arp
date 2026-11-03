#include "core/io/file_io.h"
#include "core/io/versioned_yaml_store.h"
#include "tests/fixtures/atomic_file_completion.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
using namespace arp;
using namespace core;
using namespace core::io;

struct Schema {
    static constexpr StringView type() noexcept { return "arp.validator-probe"; }

    static constexpr std::uint32_t version() noexcept { return 1; }

    inline static unsigned calls = 0;

    static bool validate(const YamlContract::Node& root, pmr::String& diagnostic)
    {
        ++calls;
        if (YamlContract::uint32(root, "value") != 7) {
            diagnostic = "PROBE-SCHEMA-VALUE";
            return false;
        }
        return true;
    }
};

using Store = VersionedYamlStore<Schema>;
constexpr std::size_t maxBytes = 4096;

struct Case {
    const char* name;
    StringView text;
    const char* diagnostic;
};

// The two multi-document rows were accepted by the original worker. Every
// other diagnosis matches its frozen independent pre-change matrix.
constexpr std::array<Case, 10> cases{{
    {"valid", "type: arp.validator-probe\ncontractVersion: 1\nvalue: 7\n", ""},
    {"bom", "\xEF\xBB\xBFtype: arp.validator-probe\r\ncontractVersion: 1\r\nvalue: 7\r\n", ""},
    {"wrong-type", "type: other\ncontractVersion: 1\nvalue: 7\n", "ARP-SAVE-UNSUPPORTED-CONTRACT"},
    {"wrong-version",
        "type: arp.validator-probe\ncontractVersion: 2\nvalue: 7\n",
        "ARP-SAVE-UNSUPPORTED-CONTRACT"},
    {"bad-yaml", "type: [\n", "ARP-SAVE-CORRUPT-YAML"},
    {"bad-value",
        "type: arp.validator-probe\ncontractVersion: 1\nvalue: 8\n",
        "PROBE-SCHEMA-VALUE"},
    {"empty", "", "ARP-SAVE-CORRUPT-YAML"},
    {"multi-document",
        "type: arp.validator-probe\ncontractVersion: 1\nvalue: 7\n---\nother: document\n",
        "ARP-SAVE-CORRUPT-YAML"},
    {"multi-empty",
        "type: arp.validator-probe\ncontractVersion: 1\nvalue: 7\n---\n",
        "ARP-SAVE-CORRUPT-YAML"},
    {"duplicate-key",
        "type: arp.validator-probe\ncontractVersion: 1\nvalue: 7\nvalue: 8\n",
        "ARP-SAVE-CORRUPT-YAML"},
}};

void memoryContract(bool print)
{
    for (const auto& item : cases) {
        const pmr::Vector<std::uint8_t> bytes(item.text.begin(), item.text.end());
        pmr::String diagnostic("stale caller diagnosis");
        const auto accepted = Store::validateContents(bytes, maxBytes, diagnostic);
        assert(accepted == !*item.diagnostic && diagnostic == item.diagnostic);
        assert(bytes.size() == item.text.size()
            && (bytes.empty() || !std::memcmp(bytes.data(), item.text.data(), bytes.size())));
        if (print)
            std::printf("RESULT %s %u %s\n", item.name, unsigned(accepted), diagnostic.c_str());
    }
    const pmr::Vector<std::uint8_t> content(cases[0].text.begin(), cases[0].text.end());
    pmr::String diagnostic;
    const auto calls = Schema::calls;
    for (const auto limit :
        {std::size_t(0), std::numeric_limits<std::size_t>::max(), content.size() - 1}) {
        assert(!Store::validateContents(content, limit, diagnostic)
            && diagnostic == "ARP-SAVE-CONTENT-BUDGET" && Schema::calls == calls);
    }
    // Admission is based on borrowed input length, not its unrelated capacity.
    auto exact = content;
    exact.resize(maxBytes, ' ');
    exact.reserve(2 * maxBytes);
    assert(Store::validateContents(exact, maxBytes, diagnostic) && diagnostic.empty());
    exact.push_back(' ');
    assert(!Store::validateContents(exact, maxBytes, diagnostic)
        && diagnostic == "ARP-SAVE-CONTENT-BUDGET");
    assert(Store::validateContents(content, content.size(), diagnostic) && diagnostic.empty());
}

#if !defined(__EMSCRIPTEN__)
// Shared real worker completion and existing bounded temporary workspace. No
// test codec, simulated commit or memory backend stands in for Native saves.
void workerContract()
{
    unified_io_fixture::Workspace files;
    const auto path = FilePath::utf8(files.root / "record.yaml");
    AtomicFileService service(1, maxBytes);
    Store store(service);
    const auto finish = [&](PoolHandle handle) {
        return tests::atomic_file::finish(service, handle);
    };
    const auto expectBytes = [&](StringView expected) {
        const auto read = readBinaryFile(files.root / "record.yaml", maxBytes);
        assert(read.success && read.bytes.size() == expected.size()
            && (read.bytes.empty()
                || !std::memcmp(read.bytes.data(), expected.data(), read.bytes.size())));
    };
    const auto reject = [](const AtomicFileResult& result, const char* diagnostic) {
        assert(!result.success && !result.committed && result.diagnostic == diagnostic);
    };
    for (const auto& item : cases) {
        files.write("record.yaml", item.text);
        const auto loaded = finish(store.load(path));
        assert(loaded.success == !*item.diagnostic && !loaded.committed
            && loaded.diagnostic == item.diagnostic);
        expectBytes(item.text);
        if (*item.diagnostic) {
            reject(finish(store.save(path, pmr::String(cases[0].text))), item.diagnostic);
            expectBytes(item.text);
            files.write("record.yaml", cases[0].text);
            reject(finish(store.save(path, pmr::String(item.text))), item.diagnostic);
            expectBytes(cases[0].text);
        }
    }
    assert(std::filesystem::remove(files.root / "record.yaml"));
    reject(finish(store.save(path, pmr::String(cases[7].text))), cases[7].diagnostic);
    assert(!std::filesystem::exists(files.root / "record.yaml"));
    const auto saved = finish(store.save(path, pmr::String(cases[1].text)));
    assert(saved.success && saved.committed);
    expectBytes(cases[1].text);
    for (const auto& file : std::filesystem::directory_iterator(files.root))
        assert(file.path().filename().string().find(".arp-tmp-") == std::string::npos);
}
#endif

template <class Cycle> void ordinary(Cycle cycle)
{
    cycle();
    const auto before = defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i) {
        cycle();
        const auto after = defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::printf("Versioned YAML ordinary20: core bytes=%llu live=%llu delta=0\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount));
}
} // namespace

int main(int argc, char** argv)
{
    const std::string_view mode = argc == 2 ? argv[1] : "";
    if (mode == "--smoke")
        ordinary([] {
            memoryContract(false);
        });
#if !defined(__EMSCRIPTEN__)
    else if (mode == "--worker") {
        workerContract();
        std::puts(
            "PASS Native worker ten-row parity, old/new rejection, exact BOM/CRLF and absent target preservation");
    } else if (mode == "--worker-smoke")
        ordinary(workerContract);
#endif
    else {
        assert(argc == 1);
        memoryContract(true);
        std::puts("PASS borrowed bounded single-document YAML validation and diagnostic reuse");
    }
}
