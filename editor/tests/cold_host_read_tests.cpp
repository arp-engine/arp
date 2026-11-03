#include "editor/io/editor_cold_host_read.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace core::io;

FileReadRequest requestFor(const std::filesystem::path& path)
{
    FileReadRequest request;
    request.contractVersion = 1;
    request.pathScope = FilePathScope::HostFilesystem;
    request.path = core::FilePath::utf8(path);
    request.maxBytes = 64;
    return request;
}

void cycle(const std::filesystem::path& root)
{
    constexpr char expected[] = "\xef\xbb\xbf# source\r\na\0b\xff";
    const auto bytes = editor::EditorColdHostRead::read(requestFor(root / u8"来源.arp"));
    assert(bytes.status == FileIoStatus::Success
        && bytes.backendKind == FileBackendKind::ThreadPoolFallbackFileBackend);
    assert(bytes.bytes.size() == sizeof(expected) - 1
        && !std::memcmp(bytes.bytes.data(), expected, bytes.bytes.size()));
    const auto text = editor::EditorColdHostRead::readTextBytes(root / u8"来源.arp", 64);
    assert(text.success && text.text == core::StringView(expected, sizeof(expected) - 1));
    const auto binary = editor::EditorColdHostRead::readBinaryBytes(root / u8"来源.arp", 64);
    assert(binary.success && binary.bytes == bytes.bytes);
    const auto empty = editor::EditorColdHostRead::readBinaryBytes(root / "empty.bin", 0);
    assert(empty.success && empty.bytes.empty());
}

void contracts(const std::filesystem::path& root)
{
    using Reader = editor::EditorColdHostRead;
    const auto exhausted = Reader::readBinaryBytes(root / "b.bin", 0);
    assert(!exhausted.success && exhausted.bytes.empty()
        && exhausted.diagnostics == "EDITOR-COLD-READ-LOGICAL-BYTE-BUDGET");
    assert(Reader::readTextBytes(root / "empty.bin", 0).success);
    auto request = requestFor(root / u8"来源.arp");
    request.range = FileReadRange::exactRange(3, 8);
    const auto range = Reader::read(request);
    assert(range.status == FileIoStatus::Success && range.bytes.size() == 8
        && !std::memcmp(range.bytes.data(), "# source", 8));
    request.range = FileReadRange::exactRange(65, 1);
    assert(Reader::read(request).status == FileIoStatus::InvalidRange);
    request = requestFor(root / u8"来源.arp");
    request.maxBytes = 1;
    assert(Reader::read(request).status == FileIoStatus::BudgetExceeded);
    request.maxBytes = 0;
    assert(Reader::read(request).status == FileIoStatus::InvalidRequest);
    request.maxBytes = UINT64_MAX;
    assert(Reader::read(request).status == FileIoStatus::InvalidRequest);
    request = requestFor(root / "missing.arp");
    assert(Reader::read(request).status == FileIoStatus::MissingFile);
    core::runtime::CancellationSource cancelled;
    cancelled.cancel();
    request.cancellation = cancelled.token();
    assert(Reader::read(request).status == FileIoStatus::Cancelled);
    request = requestFor(root / u8"来源.arp");
    request.pathScope = FilePathScope::MountedResource;
    assert(Reader::read(request).status == FileIoStatus::InvalidRequest);
    request = requestFor(root / u8"来源.arp");
    request.mount.owner = &request;
    request.mount.index = 0;
    request.mount.generation = 1;
    assert(Reader::read(request).status == FileIoStatus::InvalidRequest);
    request = requestFor(root / u8"来源.arp");
    request.expectedVersion.kind = FileVersionKind::ContentSha256;
    request.maxVersionBytes = 64;
    assert(Reader::read(request).status == FileIoStatus::Unsupported);
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        request = {};
        // Pure profile rejection needs no allocation.
        assert(Reader::read(request).status == FileIoStatus::InvalidRequest);
    }
    request = requestFor(root / u8"来源.arp");
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        assert(Reader::read(request).status == FileIoStatus::BudgetExceeded);
    }
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace workspace;
    constexpr char source[] = "\xef\xbb\xbf# source\r\na\0b\xff";
    workspace.write(u8"来源.arp", core::StringView(source, sizeof(source) - 1));
    workspace.write("empty.bin", "");
    cycle(workspace.root);
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            cycle(workspace.root);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("Editor cold host ordinary20 core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    } else
        contracts(workspace.root);
    std::puts("PASS FileService cold host bytes, finite range/cancel/admission and retired owners");
}
