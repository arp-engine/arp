#include "core/diagnostics/diagnostic_error.h"
#include "core/encoding/yaml_contract.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "tools/io/file_inventory.h"
#include "tools/survivors/package_file_inventory.h"

#include <limits>

namespace {
using namespace arp;
using Yaml = core::YamlContract;
// Run the same bounded IO contract through the shared owner and legacy adapter.
#if defined(ARP_TEST_SURVIVORS_INVENTORY)
using Inventory = tools::survivors::PackageFileInventory;
using InventoryLimits = tools::survivors::PackageInventoryLimits;
constexpr auto requestType = "arp.survivors-package-inventory-request";
constexpr auto responseType = "arp.survivors-package-inventory";
#define INVENTORY_DIAGNOSTIC "SURVIVORS-PACKAGE-INVENTORY-"
#else
using Inventory = tools::io::ToolFileInventory;
using InventoryLimits = tools::io::ToolFileInventoryLimits;
constexpr auto requestType = "arp.tool-file-inventory-request";
constexpr auto responseType = "arp.tool-file-inventory";
#define INVENTORY_DIAGNOSTIC "TOOL-FILE-INVENTORY-"
#endif
constexpr InventoryLimits limits{16384, 16384};

Yaml::Node document(const unified_io_fixture::Workspace& files)
{
    auto root = Yaml::Node::mapping();
    root["type"] = requestType;
    root["contractVersion"] = 1;
    root["maxFiles"] = 3;
    root["maxFileBytes"] = 64;
    root["maxTotalBytes"] = 192;
    root["maxPathBytes"] = 1024;
    root["files"] = Yaml::Node::sequence();
    for (const auto* name : {"a.bin", "empty.bin", "nested/路径.bin"}) {
        auto entry = Yaml::Node::mapping();
        entry["name"] = name;
        entry["path"] = core::FilePath::utf8(files.root / std::filesystem::u8path(name));
        entry["bytes"] = std::string_view(name) == "a.bin" ? 6
            : std::string_view(name) == "empty.bin"        ? 0
                                                           : 7;
        root["files"].get_value_ref<Yaml::Node::sequence_type&>().push_back(std::move(entry));
    }
    return root;
}

bool rejected(
    const Yaml::Node& root, std::string_view expected = {}, InventoryLimits budget = limits)
{
    try {
        (void)Inventory::prepare(Yaml::serialize(root), budget);
    } catch (const core::DiagnosticError& error) {
        return expected.empty() || expected == error.what();
    } catch (const core::YamlContractError& error) {
        return expected.empty() || expected == error.diagnostic;
    }
    return false;
}

void cycle(const unified_io_fixture::Workspace& files)
{
#if !defined(ARP_TEST_SURVIVORS_INVENTORY)
    bool invalidProtocol = false;
    try {
        (void)Inventory::prepare({}, limits, {nullptr, "response", "diagnostic"});
    } catch (const core::DiagnosticError& error) {
        invalidProtocol = std::string_view(error.what()) == "TOOL-FILE-INVENTORY-PROTOCOL";
    }
    assert(invalidProtocol);
#endif
    auto request = document(files);
    const auto yaml = Yaml::serialize(request);
    const auto result = Yaml::parseSingleDocument(Inventory::prepare(yaml, limits));
    assert(Yaml::text(result, "type") == responseType);
    const auto& entries = Yaml::sequence(result, "files");
    assert(entries.size() == 3 && Yaml::text(entries[0], "path") == "a.bin"
        && Yaml::text(entries[1], "path") == "empty.bin"
        && Yaml::text(entries[2], "path") == "nested/路径.bin");
    assert(Yaml::text(entries[0], "sha256")
        == "bef57ec7f53a6d40beb640a780a639c83bc29ac8a9816f1fc6c5c6dcd93c4721");
    assert(Yaml::text(entries[1], "sha256")
        == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    // Version 2 is an explicit payload contract; version 1 never adds data.
    auto payload = request;
    payload["contractVersion"] = 2;
    payload["payloadEncoding"] = "base64";
    payload["maxPayloadBytes"] = 192;
    const auto payloadResult =
        Yaml::parseSingleDocument(Inventory::prepare(Yaml::serialize(payload), limits));
    assert(Yaml::uint32(payloadResult, "contractVersion") == 2
        && Yaml::text(payloadResult, "payloadEncoding") == "base64");
    const auto& payloadEntries = Yaml::sequence(payloadResult, "files");
    assert(Yaml::text(payloadEntries[0], "data") == "YWJjZGVm");
    assert(Yaml::text(payloadEntries[1], "data").empty());
    assert(!entries[0].contains("data"));
    payload["maxPayloadBytes"] = 0;
    assert(rejected(payload, INVENTORY_DIAGNOSTIC "PAYLOAD-BUDGET"));
    payload["maxPayloadBytes"] = 192;
    payload["payloadEncoding"] = "hex";
    assert(rejected(payload, INVENTORY_DIAGNOSTIC "PAYLOAD-ENCODING"));
    payload["contractVersion"] = 1;
    assert(rejected(payload, "YAML-CONTRACT-FIELD-INVALID"));
    assert(rejected(request, INVENTORY_DIAGNOSTIC "CONTROL-BUDGET", {0, 16384}));
    assert(rejected(request,
        INVENTORY_DIAGNOSTIC "CONTROL-BUDGET",
        {std::numeric_limits<std::size_t>::max(), 16384}));
    assert(rejected(request, INVENTORY_DIAGNOSTIC "CONTROL-BUDGET", {1, 16384}));
    assert(rejected(request, INVENTORY_DIAGNOSTIC "RESPONSE-BUDGET", {16384, 1}));
    const auto reset = [&] {
        request = document(files);
    };
    request["type"] = "unselected.file-inventory-protocol";
    assert(rejected(request, INVENTORY_DIAGNOSTIC "CONTRACT"));
    reset();
    request["contractVersion"] = 9;
    assert(rejected(request, INVENTORY_DIAGNOSTIC "CONTRACT"));
    reset();
    request["extra"] = true;
    assert(rejected(request, "YAML-CONTRACT-FIELD-INVALID"));
    reset();
    request["maxFiles"] = 0;
    assert(rejected(request, INVENTORY_DIAGNOSTIC "BUDGET"));
    reset();
    request["maxFileBytes"] = 0;
    assert(rejected(request, INVENTORY_DIAGNOSTIC "BUDGET"));
    reset();
    request["maxTotalBytes"] = 1;
    assert(rejected(request, INVENTORY_DIAGNOSTIC "BYTE-BUDGET"));
    reset();
    request["maxPathBytes"] = 1;
    assert(rejected(request, INVENTORY_DIAGNOSTIC "PATH"));
    reset();
    request["files"][1]["name"] = "a.bin";
    assert(rejected(request, INVENTORY_DIAGNOSTIC "DUPLICATE"));
    reset();
    request["files"][0]["name"] = "../escape.bin";
    assert(rejected(request, INVENTORY_DIAGNOSTIC "PATH"));
    reset();
    request["files"][0]["path"] = "a.bin";
    assert(rejected(request, INVENTORY_DIAGNOSTIC "PATH"));
    reset();
    request["files"][0]["bytes"] = 5;
    assert(rejected(request, "IO-READ-BYTE-BUDGET"));
    reset();
    request["files"][0]["bytes"] = 7;
    assert(rejected(request, INVENTORY_DIAGNOSTIC "SIZE-CHANGED"));
    reset();
    request["files"][0]["path"] = core::FilePath::utf8(files.root / "missing.bin");
    assert(rejected(request));
    reset();
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    bool exhausted = false;
    try {
        core::CpuMemoryQuotaScope scope(quota);
        (void)Inventory::prepare(yaml, limits);
    } catch (const std::bad_alloc&) {
        exhausted = true;
    }
    assert(exhausted && quota->liveBytes() == 0);
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace files;
    files.write("empty.bin", {});
    cycle(files);
    if (argc > 1 && std::string_view(argv[1]) == "--smoke") {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i) {
            cycle(files);
            const auto after = core::defaultCpuAllocator().stats();
            assert(after.liveBytes == before.liveBytes);
            assert(after.allocationCount - after.deallocationCount
                == before.allocationCount - before.deallocationCount);
        }
        std::puts("ordinary_cycles=20 core_live_bytes_delta=0 core_live_count_delta=0");
    }
}
