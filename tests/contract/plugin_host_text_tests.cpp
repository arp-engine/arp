#include "scriptvm/plugin/plugin_host_text_reader.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {

using namespace arp;
using namespace scriptvm;

void contract()
{
    unified_io_fixture::Workspace workspace;
    const auto path = workspace.root / "output";
    PluginHostTextBudget budget{1, 16, 32, 8};
    const PluginHostTextReader reader(budget);
    workspace.write("output",
        "\xef\xbb\xbf"
        "valid\r\n");
    auto read = reader.read(path, PluginHostTextKind::Metadata);
    assert(read.success
        && read.text
            == "\xef\xbb\xbf"
               "valid\r\n"
        && !read.truncated);
    workspace.write("output", "1234567890123456");
    assert(reader.read(path, PluginHostTextKind::Metadata).success);
    workspace.write("output", "12345678901234567");
    read = reader.read(path, PluginHostTextKind::Metadata);
    assert(!read.success && read.text.empty() && read.diagnosticId);
    assert(reader.read(path, PluginHostTextKind::Audit).success);
    workspace.write("output", std::string(33, 'a'));
    assert(!reader.read(path, PluginHostTextKind::Audit).success);
    for (const auto text : {core::StringView("\xc0\xaf", 2), core::StringView("a\0b", 3)}) {
        workspace.write("output", text);
        read = reader.read(path, PluginHostTextKind::Metadata);
        assert(!read.success && read.diagnosticId && read.text.empty());
    }
    workspace.write("output", "");
    assert(reader.read(path, PluginHostTextKind::Audit).success);
    assert(reader.diagnostic(path).success && reader.diagnostic(path).text.empty());
    for (unsigned count : {7, 8, 9}) {
        workspace.write("output", std::string(count, 'x'));
        read = reader.diagnostic(path);
        assert(read.success && read.text.size() == std::min(count, 8u));
        assert(read.truncated == (count > 8));
    }
    workspace.write("output", core::StringView("a\0\xff\\\r\n\t\x01z", 9));
    read = reader.diagnostic(path);
    assert(read.success && read.truncated && read.text == "a\\x00\\xff\\\\\r\n\t\\x01");
    assert(!reader.read(path, static_cast<PluginHostTextKind>(99)).success);
    assert(!reader.read(workspace.root / "missing", PluginHostTextKind::Metadata).success);
    assert(!reader.diagnostic(workspace.root / "missing").success);
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        auto invalid = budget;
        switch (scenario) {
        case 0:
            invalid.contractVersion = 2;
            break;
        case 1:
            invalid.maxMetadataBytes = 0;
            break;
        case 2:
            invalid.maxMetadataBytes = UINT64_MAX;
            break;
        case 3:
            invalid.maxAuditBytes = 0;
            break;
        case 4:
            invalid.maxAuditBytes = UINT64_MAX;
            break;
        case 5:
            invalid.maxDiagnosticBytes = 0;
            break;
        default:
            invalid.maxDiagnosticBytes = UINT64_MAX;
            break;
        }
        const PluginHostTextReader rejected(invalid);
        // A config error must win over even a missing path, before any IO.
        read = rejected.read(workspace.root / "missing", PluginHostTextKind::Metadata);
        assert(
            !read.success && core::StringView(read.diagnosticId) == "ARP-PLUGIN-HOST-TEXT-CONFIG");
        assert(!rejected.diagnostic(path).success);
    }
    // A large tool log remains a bounded prefix under a small core quota.
    workspace.write("output", std::string(1024 * 1024, 'e'));
    auto quota = core::makeShared<core::CpuMemoryQuota>(4096);
    {
        core::CpuMemoryQuotaScope scope(quota);
        const auto excerpt = reader.diagnostic(path);
        assert(excerpt.success && excerpt.truncated && excerpt.text == "eeeeeeee");
    }
    assert(quota->liveBytes() == 0);
    budget.maxAuditBytes = 2 * 1024 * 1024;
    {
        core::CpuMemoryQuotaScope scope(quota);
        const auto denied = PluginHostTextReader(budget).read(path, PluginHostTextKind::Audit);
        assert(!denied.success && denied.text.empty() && denied.diagnosticId);
    }
    assert(quota->liveBytes() == 0);
    workspace.write("output", "ok");
    // Failure cannot poison subsequent reads or leave a partially published result.
    assert(reader.read(path, PluginHostTextKind::Metadata).text == "ok");
}

} // namespace

int main(int argc, char** argv)
{
    const bool quick = argc == 2 && arp::core::StringView(argv[1]) == "--quick";
    assert(argc == 1 || quick);
    if (quick) {
        contract();
        std::puts("PluginHostText/1 quick PASS");
        return 0;
    }
    for (unsigned i = 0; i < 20; ++i)
        contract();
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        contract();
    const auto after = arp::core::defaultCpuAllocator().stats();
    std::printf("Plugin host text 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
