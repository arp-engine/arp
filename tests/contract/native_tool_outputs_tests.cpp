#include "core/io/file_io.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "tools/io/native_tool_outputs.h"

#include <cassert>
#include <cstring>

namespace {
using namespace arp;
using namespace tools::io;

void cycle(const std::filesystem::path& root)
{
    const std::array<NativeToolOutputFile, 2> files{
        {{root / "nested/a.cpp", "first\r\nsecond\n"}, {root / "b.bin", {"x\0y", 3}}}};
    auto saved =
        NativeToolOutputWriter{}.save({files, 2, 4096, NativeToolOutputEncoding::ExactBytes});
    assert(saved.success() && saved.committedFiles == 2);
    auto binary = core::readBinaryFile(files[1].path, 4096);
    assert(binary.success && binary.bytes.size() == 3 && binary.bytes[1] == 0);
    auto text = core::readBinaryFile(files[0].path, 4096);
    assert(text.success && text.bytes.size() == files[0].bytes.size());
    assert(std::memcmp(text.bytes.data(), files[0].bytes.data(), text.bytes.size()) == 0);
    saved =
        NativeToolOutputWriter{}.save({files, 2, 4096, NativeToolOutputEncoding::LegacyNativeText});
    assert(saved.success() && saved.committedFiles == 2);
    text = core::readBinaryFile(files[0].path, 4096);
#if defined(_WIN32)
    constexpr core::StringView expected = "first\r\r\nsecond\r\n";
#else
    const auto expected = files[0].bytes;
#endif
    assert(text.success && text.bytes.size() == expected.size());
    assert(std::memcmp(text.bytes.data(), expected.data(), expected.size()) == 0);
}

void failures(unified_io_fixture::Workspace& workspace)
{
    NativeToolOutputWriter writer;
    const std::array<NativeToolOutputFile, 2> files{
        {{workspace.root / "new/a", "original"}, {workspace.root / "new/b", "original"}}};
    assert(writer.save({files, 1, 4096}).failure == NativeToolOutputFailure::Options);
    assert(writer.save({files, 2, 1}).failure == NativeToolOutputFailure::Budget);
    assert(!std::filesystem::exists(workspace.root / "new"));
    const std::array<NativeToolOutputFile, 2> duplicate{
        {{workspace.root / "new/a", "x"}, {workspace.root / "new/../new/a", "y"}}};
    const auto repeated = writer.save({duplicate, 2, 4096});
    assert(repeated.failure == NativeToolOutputFailure::Options && !repeated.committedFiles);
    assert(!std::filesystem::exists(workspace.root / "new"));
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        assert(writer.save({files, 2, 4096}).failure == NativeToolOutputFailure::Allocation);
    }
    assert(!std::filesystem::exists(workspace.root / "new"));
    const auto created = writer.save({files, 2, 4096});
    if (!created.success())
        std::fprintf(stderr,
            "create failed: %s commits=%zu status=%u\n",
            created.diagnosticId,
            created.committedFiles,
            static_cast<unsigned>(created.lastSave.status));
    assert(created.success());
    workspace.write("new/a", core::pmr::String(40, 'a'));
    workspace.write("new/b", core::pmr::String(40, 'b'));
    const auto baseline = writer.save({files, 2, 64});
    assert(baseline.failure == NativeToolOutputFailure::Budget && !baseline.committedFiles);
    const auto before = core::readBinaryFile(files[0].path, 4096);
    assert(before.success && before.bytes.size() == 40 && before.bytes[0] == 'a');
    workspace.write("occupied", "sentinel");
    const std::array<NativeToolOutputFile, 1> occupied{{{workspace.root / "occupied/a", "x"}}};
    assert(writer.save({occupied, 1, 4096}).failure == NativeToolOutputFailure::Directory);
    const auto untouched = core::readBinaryFile(workspace.root / "occupied", 4096);
    assert(untouched.success && untouched.bytes.size() == 8);
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace workspace;
    cycle(workspace.root);
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            cycle(workspace.root);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("Tool outputs core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    } else
        failures(workspace);
    std::puts(
        "PASS bounded candidates/baselines, original native text mapping and individual commits");
}
