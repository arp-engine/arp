#include "editor/cli/cli_source.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace core::io;

void cycle(const std::filesystem::path& root)
{
    const auto result = editor::EditorCliSource::read(root / u8"脚本.arp", 64);
    constexpr char expected[] = "\xef\xbb\xbf# source\r\na\0b\xff";
    assert(result.status == FileIoStatus::Success);
    assert(result.backendKind == FileBackendKind::ThreadPoolFallbackFileBackend);
    assert(result.bytes.size() == sizeof(expected) - 1);
    assert(!std::memcmp(result.bytes.data(), expected, result.bytes.size()));
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace workspace;
    constexpr char source[] = "\xef\xbb\xbf# source\r\na\0b\xff";
    workspace.write(u8"脚本.arp", std::string_view(source, sizeof(source) - 1));
    cycle(workspace.root);
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = arp::core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            cycle(workspace.root);
        const auto after = arp::core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("CLI source ordinary20 core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    } else {
        using arp::editor::EditorCliSource;
        assert(EditorCliSource::read(workspace.root / "missing.arp", 64).status
            == FileIoStatus::MissingFile);
        assert(EditorCliSource::read(workspace.root / u8"脚本.arp", 1).status
            == FileIoStatus::BudgetExceeded);
        assert(EditorCliSource::read({}, 64).status == FileIoStatus::InvalidRequest);
        assert(EditorCliSource::read(workspace.root / "a.bin", 0).status
            == FileIoStatus::InvalidRequest);
        assert(EditorCliSource::read(workspace.root / "a.bin", EditorCliSource::maxSourceBytes + 1)
                   .status
            == FileIoStatus::InvalidRequest);
        workspace.write("empty.arp", "");
        assert(EditorCliSource::read(workspace.root / "empty.arp").status == FileIoStatus::Success);
        auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1);
        arp::core::CpuMemoryQuotaScope scope(quota);
        assert(
            EditorCliSource::read(workspace.root / "a.bin").status == FileIoStatus::BudgetExceeded);
    }
    std::puts("PASS native CLI bootstrap FileService, byte identity and retirement");
}
