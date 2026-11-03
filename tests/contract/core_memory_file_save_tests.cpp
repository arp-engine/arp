#include "core/io/file_query_contract.h"
#include "core/io/file_save_contract.h"
#include "core/io/file_service.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp::core;
using namespace arp::core::io;
using unified_io_fixture::Probe;

class Host final {
public:
    explicit Host(MemoryFileBackendDesc desc = {8, 128, 4, 4096, 256, true})
        : memory(makeShared<MemoryFileBackend>(desc))
    {
        mount = mounts.mount("memory", memory);
        assert(mount);
        assert(memory->registerDirectory("scripts") == FileIoStatus::Success);
        assert(
            memory->registerFile("scripts/a.arp", reinterpret_cast<const std::uint8_t*>("old"), 3)
            == FileIoStatus::Success);
    }

    FileSaveRequest request(
        std::string_view path, std::string_view text, const char* baseline = nullptr) const
    {
        FileSaveRequest result;
        result.mount = mount;
        result.pathScope = FilePathScope::MountedResource;
        result.path = path;
        result.bytes.assign(text.begin(), text.end());
        result.maxWriteBytes = 128;
        if (baseline) {
            FileSaveRequest previous;
            const std::string_view bytes(baseline);
            previous.bytes.assign(bytes.begin(), bytes.end());
            result.precondition = FileSavePrecondition::ContentSha256;
            result.expectedVersion = FileSaveContract::contentVersion(previous);
            result.maxBaselineBytes = bytes.size();
        }
        return result;
    }

    FileReadRequest read(std::string_view path) const
    {
        FileReadRequest result;
        result.contractVersion = 1;
        result.mount = mount;
        result.pathScope = FilePathScope::MountedResource;
        result.path = path;
        result.maxBytes = 128;
        return result;
    }

    FileSaveResult save(FileSaveRequest request)
    {
        Probe<FileSaveResult> result;
        assert(files.save(std::move(request), {Probe<FileSaveResult>::complete, &result})
            == FileIoStatus::Accepted);
        assert(result.calls == 0);
        queue.dispatch(8);
        assert(result.calls == 1);
        return result.result;
    }

    void expect(std::string_view path, std::string_view text)
    {
        Probe<FileReadResult> result;
        assert(files.read(read(path), {Probe<FileReadResult>::complete, &result})
            == FileIoStatus::Accepted);
        queue.dispatch(8);
        assert(result.calls == 1 && result.result.status == FileIoStatus::Success);
        const auto& bytes = result.result.bytes;
        assert(bytes.size() == text.size());
        assert(text.empty() || std::equal(bytes.begin(), bytes.end(), text.begin()));
    }

    runtime::CompletionQueue queue{runtime::CompletionQueueDesc{8}};
    SharedPtr<MemoryFileBackend> memory;
    MountedFileBackend mounts{MountedFileBackendDesc{1, 4, 4096, 32, 256}};
    FileService files{queue, mounts};
    PoolHandle mount;
};

void contracts()
{
    Host host;
    assert(host.files.saveCapability(host.mount).observedBaselineReplace);
    assert(!host.files.saveCapability(host.mount).strictConditionalReplace);
    assert(!host.files.saveCapability(host.mount).operationReceipts);
    auto request = host.request("scripts/a.arp", "new", "old");
    const auto version = FileSaveContract::contentVersion(request);
    auto result = host.save(request);
    assert(
        result.status == FileIoStatus::Success && result.commitState == FileCommitState::Committed);
    assert(result.committedVersion == version);
    host.expect("scripts/a.arp", "new");
    result = host.save(request);
    assert(result.status == FileIoStatus::VersionMismatch
        && result.commitState == FileCommitState::NotCommitted);
    result = host.save(host.request("scripts/a.arp", "other"));
    assert(result.status == FileIoStatus::VersionMismatch);
    result = host.save(host.request("scripts/absent.arp", "other", ""));
    assert(result.status == FileIoStatus::VersionMismatch);
    result = host.save(host.request("scripts", "other"));
    assert(result.status == FileIoStatus::InvalidRequest);
    result = host.save(host.request("unregistered/new.arp", "other"));
    assert(result.status == FileIoStatus::MissingFile);
    result = host.save(host.request(u8"scripts/脚本.arp", ""));
    assert(result.commitState == FileCommitState::Committed);
    host.expect(u8"scripts/脚本.arp", "");
    request = host.request("scripts/a.arp", "budget", "new");
    request.maxBaselineBytes = 2;
    result = host.save(request);
    assert(result.status == FileIoStatus::BudgetExceeded);
    host.expect("scripts/a.arp", "new");

    // Previously accepted reads own their bytes. A second write sees the first
    // actual commit even while both completion callbacks are still pending.
    Probe<FileReadResult> old;
    Probe<FileSaveResult> first, second;
    assert(host.files.read(host.read("scripts/a.arp"), {Probe<FileReadResult>::complete, &old})
        == FileIoStatus::Accepted);
    assert(host.files.save(host.request("scripts/a.arp", "first", "new"),
               {Probe<FileSaveResult>::complete, &first})
        == FileIoStatus::Accepted);
    assert(host.files.save(host.request("scripts/a.arp", "second", "new"),
               {Probe<FileSaveResult>::complete, &second})
        == FileIoStatus::Accepted);
    host.queue.dispatch(8);
    assert(old.calls == 1 && old.result.bytes == pmr::Vector<std::uint8_t>({'n', 'e', 'w'}));
    assert(first.result.commitState == FileCommitState::Committed);
    assert(second.result.status == FileIoStatus::VersionMismatch);
    host.expect("scripts/a.arp", "first");

    // A version-pinned read must observe the new identity after saving.
    auto read = host.read("scripts/a.arp");
    read.expectedVersion = version;
    read.maxVersionBytes = 128;
    Probe<FileReadResult> stale;
    assert(
        host.files.read(read, {Probe<FileReadResult>::complete, &stale}) == FileIoStatus::Accepted);
    host.queue.dispatch(8);
    assert(stale.result.status == FileIoStatus::VersionMismatch);
    auto list = unified_io_fixture::query(FileQueryOperation::Enumerate, "scripts");
    list.mount = host.mount;
    list.pageSize = 1;
    list.maxScanEntries = 8;
    list.maxScanNameBytes = 256;
    list.maxResultBytes = 256;
    Probe<FileQueryResult> page;
    assert(host.files.query(list, {Probe<FileQueryResult>::complete, &page})
        == FileIoStatus::Accepted);
    host.queue.dispatch(8);
    assert(page.result.status == FileIoStatus::Success && page.result.next.offset == 1);
    list.cursor = page.result.next;
    assert(host.save(host.request("scripts/added.arp", "new")).commitState
        == FileCommitState::Committed);
    assert(host.files.query(list, {Probe<FileQueryResult>::complete, &page})
        == FileIoStatus::Accepted);
    host.queue.dispatch(8);
    assert(page.result.status == FileIoStatus::VersionMismatch);
    assert(host.memory->telemetry().inFlight == 0 && host.memory->telemetry().queuedBytes == 0);
}

void failedPreparation()
{
    Host host;
    Probe<FileSaveResult> result;
    auto request = host.request("scripts/a.arp", "failed", "old");
    const auto quota = makeShared<CpuMemoryQuota>(1);
    {
        CpuMemoryQuotaScope allocationFailure(quota);
        assert(host.memory->saveAsync(
                   std::move(request), {Probe<FileSaveResult>::complete, &result}, host.queue)
            == FileIoStatus::BudgetExceeded);
    }
    assert(quota->liveBytes() == 0 && result.calls == 0 && host.queue.pending() == 0);
    host.expect("scripts/a.arp", "old");
    request = host.request("scripts/a.arp", "failed", "old");
    {
        std::array<runtime::CompletionReservation, 8> held;
        for (auto& value : held) {
            value = host.queue.reserve();
            assert(value.valid());
        }
        assert(host.memory->saveAsync(
                   std::move(request), {Probe<FileSaveResult>::complete, &result}, host.queue)
            == FileIoStatus::QueueFull);
    }
    host.expect("scripts/a.arp", "old");
    Host readonly({8, 128, 4, 4096, 256});
    assert(readonly.files.save(readonly.request("scripts/a.arp", "other", "old"),
               {Probe<FileSaveResult>::complete, &result})
        == FileIoStatus::Unsupported);
    Host bounded({2, 8, 1, 4096, 256, true});
    assert(bounded.save(bounded.request("new.arp", "x")).status == FileIoStatus::BudgetExceeded);
    auto oversized = bounded.request("scripts/a.arp", "x", "old");
    oversized.bytes.reserve(16);
    assert(bounded.save(std::move(oversized)).status == FileIoStatus::BudgetExceeded);
    bounded.expect("scripts/a.arp", "old");
    Host queued({8, 128, 4, 16, 256, true});
    assert(queued.files.save(queued.request("scripts/a.arp", "x", "old"),
               {Probe<FileSaveResult>::complete, &result})
        == FileIoStatus::BudgetExceeded);

    // Exercise allocation failures at successive preparation sizes. Every
    // refusal preserves the old bytes, not just the earliest allocation gate.
    unsigned refused = 0, accepted = 0;
    for (const auto bytes : {1u, 64u, 128u, 256u, 512u, 1024u, 4096u}) {
        Host limited;
        auto value = limited.request("scripts/long-enough-to-allocate-a-new-path.arp", "new");
        const auto budget = makeShared<CpuMemoryQuota>(bytes);
        FileIoStatus status;
        {
            CpuMemoryQuotaScope fault(budget);
            status = limited.memory->saveAsync(
                std::move(value), {Probe<FileSaveResult>::complete, &result}, limited.queue);
        }
        if (status == FileIoStatus::Accepted) {
            ++accepted;
            limited.queue.dispatch(8);
            assert(result.result.commitState == FileCommitState::Committed);
        } else {
            ++refused;
            assert(status == FileIoStatus::BudgetExceeded && limited.queue.pending() == 0);
        }
        limited.expect("scripts/a.arp", "old");
    }
    assert(refused > 0 && accepted > 0);
}

void deliveryLifetime()
{
    Host host;
    runtime::CancellationSource cancel;
    auto request = host.request("scripts/a.arp", "cancelled", "old");
    request.cancellation = cancel.token();
    cancel.cancel();
    auto result = host.save(std::move(request));
    assert(result.status == FileIoStatus::Cancelled
        && result.commitState == FileCommitState::NotCommitted);
    host.expect("scripts/a.arp", "old");
    runtime::CancellationSource late;
    request = host.request("scripts/a.arp", "committed", "old");
    request.cancellation = late.token();
    Probe<FileSaveResult> receipt;
    assert(host.files.save(std::move(request), {Probe<FileSaveResult>::complete, &receipt})
        == FileIoStatus::Accepted);
    late.cancel();
    host.queue.dispatch(8);
    assert(receipt.result.status == FileIoStatus::Success
        && receipt.result.commitState == FileCommitState::Committed
        && receipt.result.deliveryStatus == FileIoStatus::Cancelled);
    host.expect("scripts/a.arp", "committed");
    request = host.request("scripts/a.arp", "unmounted", "committed");
    assert(host.files.save(std::move(request), {Probe<FileSaveResult>::complete, &receipt})
        == FileIoStatus::Accepted);
    assert(host.mounts.unmount(host.mount));
    const auto fresh = host.mounts.mount("memory", host.memory);
    assert(fresh && !(fresh == host.mount));
    host.queue.dispatch(8);
    assert(receipt.result.commitState == FileCommitState::Committed
        && receipt.result.deliveryStatus == FileIoStatus::StaleLocation);
    host.mount = fresh;
    host.expect("scripts/a.arp", "unmounted");

    // Callback retention is independent of the destroyed backend's file table.
    runtime::CompletionQueue queue({2});
    auto backend = makeUnique<MemoryFileBackend>(MemoryFileBackendDesc{2, 128, 1, 4096, 256, true});
    request = host.request("created.arp", "done");
    assert(
        backend->saveAsync(std::move(request), {Probe<FileSaveResult>::complete, &receipt}, queue)
        == FileIoStatus::Accepted);
    backend.reset();
    const auto before = defaultCpuAllocator().stats().allocationCount;
    queue.dispatch(2);
    assert(defaultCpuAllocator().stats().allocationCount == before);
    assert(receipt.result.commitState == FileCommitState::Committed
        && receipt.result.deliveryStatus == FileIoStatus::ServiceClosed);
}
} // namespace

int main(int argc, char** argv)
{
    const auto cycle = [] {
        contracts();
        failedPreparation();
        deliveryLifetime();
    };
    cycle();
    const unsigned cycles = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20 : 1;
    const auto before = defaultCpuAllocator().stats();
    for (unsigned i = 0; i < cycles; ++i)
        cycle();
    const auto after = defaultCpuAllocator().stats();
    std::printf("memory save %u cycles: core bytes %llu -> %llu, live %llu -> %llu\n",
        cycles,
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
}
