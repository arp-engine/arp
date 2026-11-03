#include "core/io/file_save_contract.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "tools/io/cold_tool_files.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace core::io;

void cycle(const std::filesystem::path& root)
{
    tools::io::ColdToolFiles files(4096, 2);
    const auto mount = files.mountDirectory("source", root);
    assert(mount);
    FileTextReadRequest request;
    request.file.contractVersion = 1;
    request.file.pathScope = FilePathScope::MountedResource;
    request.file.mount = mount;
    request.file.path = "text.yaml";
    request.file.maxBytes = 4096;
    request.text.maxOutputBytes = 4096;
    request.text.bom = core::TextBomPolicy::Strip;
    auto result = files.readText(request);
    assert(result.status() == FileIoStatus::Success && result.decoded.text == "name: original\r\n");
    assert(result.source.bytes.size() == result.decoded.text.size() + 3);
    request.file.path = "missing.yaml";
    assert(files.readText(request).status() == FileIoStatus::MissingFile);
    request.file.maxBytes = 4097;
    assert(files.readText(request).status() == FileIoStatus::InvalidRequest);
    request.file.maxBytes = 4096;
    request.file.path = "../escape";
    assert(files.readText(request).status() != FileIoStatus::Success);
    files.retire();
    // Results own bytes/text after IO retirement; no reopening or borrowed read buffer.
    assert(result.decoded.text == "name: original\r\n" && result.source.bytes[0] == 0xef);
    request.file.path = "text.yaml";
    assert(files.readText(request).status() == FileIoStatus::ServiceClosed);
}

void hostCycle(const std::filesystem::path& root)
{
    tools::io::ColdToolFiles files(4096, 0, tools::io::ColdToolFileScope::NativeHostFilesystem);
    const auto capability = files.service().capability();
    assert(capability.kind == FileBackendKind::ThreadPoolFallbackFileBackend
        && capability.status == FileBackendStatus::PrototypeComplete);
    FileReadRequest request;
    request.contractVersion = 1;
    request.pathScope = FilePathScope::HostFilesystem;
    request.path = core::FilePath::utf8(root / "text.yaml");
    request.maxBytes = 4096;
    auto result = files.read(request);
    assert(result.status == FileIoStatus::Success && result.bytes.front() == 0xef);
    FileTextReadRequest text;
    text.file = request;
    text.text.maxOutputBytes = 4096;
    text.text.bom = core::TextBomPolicy::Strip;
    assert(files.readText(text).decoded.text == "name: original\r\n");
    bool rejected = false;
    try {
        files.mountDirectory("not-a-mount-host", root);
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected && files.save({}).status == FileIoStatus::Unsupported);
    request.maxBytes = 1;
    assert(files.read(request).status == FileIoStatus::BudgetExceeded);
    request.maxBytes = 4096;
    files.retire();
    assert(
        result.bytes.front() == 0xef && files.read(request).status == FileIoStatus::ServiceClosed);
}

void save(const std::filesystem::path& root)
{
#if defined(_WIN32)
    tools::io::ColdToolFiles files(4096, 1);
    const auto mount = files.mountDirectory("output", root, true);
    assert(mount && files.service().saveCapability(mount).observedBaselineReplace);
    FileReadRequest request;
    request.contractVersion = 1;
    request.mount = mount;
    request.pathScope = FilePathScope::MountedResource;
    request.path = "old.yaml";
    request.maxBytes = 4096;
    const auto baseline = files.read(request);
    assert(baseline.status == FileIoStatus::Success);
    FileSaveRequest commit;
    commit.mount = mount;
    commit.pathScope = FilePathScope::MountedResource;
    commit.path = request.path;
    commit.maxWriteBytes = commit.maxBaselineBytes = 4096;
    commit.precondition = FileSavePrecondition::ContentSha256;
    core::Sha256 hash;
    hash.update({reinterpret_cast<const char*>(baseline.bytes.data()), baseline.bytes.size()});
    commit.expectedVersion = FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
    commit.bytes.assign({'n', 'e', 'w'});
    FileSaveRequest repeated;
    repeated.mount = commit.mount;
    repeated.pathScope = commit.pathScope;
    repeated.path = commit.path;
    repeated.maxWriteBytes = commit.maxWriteBytes;
    repeated.maxBaselineBytes = commit.maxBaselineBytes;
    repeated.precondition = commit.precondition;
    repeated.expectedVersion = commit.expectedVersion;
    repeated.bytes.assign(commit.bytes.begin(), commit.bytes.end());
    const auto committed = files.save(std::move(commit));
    assert(committed.status == FileIoStatus::Success
        && committed.commitState == FileCommitState::Committed);
    const auto conflict = files.save(std::move(repeated));
    assert(conflict.status == FileIoStatus::VersionMismatch
        && conflict.commitState == FileCommitState::NotCommitted);
    files.retire();
#endif
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace workspace;
    workspace.write("text.yaml", "\xef\xbb\xbfname: original\r\n");
    workspace.write("old.yaml", "old");
    cycle(workspace.root);
    hostCycle(workspace.root);
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = arp::core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i) {
            cycle(workspace.root);
            hostCycle(workspace.root);
        }
        const auto after = arp::core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("Cold tool core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    } else {
        save(workspace.root);
        bool rejected = false;
        try {
            arp::tools::io::ColdToolFiles invalid(0, 1);
        } catch (const std::exception&) {
            rejected = true;
        }
        assert(rejected);
        assert(!arp::tools::io::ColdToolFiles::validBudget(UINT64_MAX));
    }
    std::puts(
        "PASS cold CLI FileService profiles, bounded text/bytes, baseline save and retirement");
}
