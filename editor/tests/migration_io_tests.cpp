#include "core/text/number_format.h"
#include "editor/tools/migration_io.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace core::io;

void cycle(const std::filesystem::path& directory, unsigned index)
{
    const auto input = editor::EditorMigrationIo::readSource(directory / u8"原文件.yaml", 64);
    assert(input.status() == FileIoStatus::Success && input.decoded.text == "name: original\r\n"
        && input.source.bytes.size() == input.decoded.text.size() + 3);
    // These owners remain usable after the internal IO host has retired.
    assert(input.source.bytes.front() == 0xef);
#if defined(_WIN32)
    core::pmr::String filename{"output-", &core::defaultCpuMemoryResource()};
    core::appendUnsigned(filename, index);
    filename += ".yaml";
    const auto written =
        editor::EditorMigrationIo::writeNewFile(directory, filename, input.decoded.text, 64);
    assert(written.status == FileIoStatus::Success
        && written.commitState == FileCommitState::Committed);
    assert(editor::EditorMigrationIo::writeNewFile(directory, filename, "must not overwrite", 64)
               .status
        == FileIoStatus::VersionMismatch);
    const auto loaded =
        editor::EditorMigrationIo::readSource(directory / std::filesystem::u8path(filename), 64);
    assert(loaded.status() == FileIoStatus::Success && loaded.decoded.text == input.decoded.text);
#endif
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace workspace;
    workspace.write(u8"原文件.yaml", "\xef\xbb\xbfname: original\r\n");
    cycle(workspace.root, 0);
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 1; i <= 20; ++i)
            cycle(workspace.root, i);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("Editor migration IO core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    } else {
        workspace.write("bad.yaml", "\xff");
        assert(editor::EditorMigrationIo::readSource(workspace.root / "bad.yaml", 64).status()
            == FileIoStatus::IntegrityMismatch);
        workspace.write("bad.yaml", core::StringView("a\0b", 3));
        assert(editor::EditorMigrationIo::readSource(workspace.root / "bad.yaml", 64).status()
            == FileIoStatus::IntegrityMismatch);
        assert(editor::EditorMigrationIo::readSource(workspace.root / u8"原文件.yaml", 1).status()
            == FileIoStatus::BudgetExceeded);
        assert(editor::EditorMigrationIo::readSource(workspace.root / "missing.yaml", 64).status()
            == FileIoStatus::MissingFile);
        {
            auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            core::CpuMemoryQuotaScope scope(quota);
            assert(
                editor::EditorMigrationIo::readSource(workspace.root / u8"原文件.yaml", 64).status()
                == FileIoStatus::BudgetExceeded);
            assert(editor::EditorMigrationIo::writeNewFile(
                       workspace.root, "allocation-failure.yaml", "x", 64)
                       .status
                == FileIoStatus::BudgetExceeded);
        }
        assert(!std::filesystem::exists(workspace.root / "allocation-failure.yaml"));
#if defined(_WIN32)
        assert(editor::EditorMigrationIo::writeNewFile(workspace.root, "../escape.yaml", "x", 64)
                   .status
            != FileIoStatus::Success);
        assert(
            editor::EditorMigrationIo::writeNewFile(workspace.root, "too-big.yaml", "123", 2).status
            == FileIoStatus::BudgetExceeded);
        assert(!std::filesystem::exists(workspace.root / "too-big.yaml"));
#endif
    }
    std::puts("PASS migration source Text1, new-file FileService save, conflict and retirement");
}
