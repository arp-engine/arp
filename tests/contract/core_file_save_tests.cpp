#include "core/io/file_io.h"
#include "core/io/file_save_contract.h"
#include "core/io/file_service.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstring>

namespace {
using namespace arp::core;
using namespace arp::core::io;
using namespace unified_io_fixture;

FileSaveRequest saveRequest(std::string_view path, std::string_view bytes)
{
    static const int locationOwner = 0;
    FileSaveRequest request;
    request.mount = {&locationOwner, 0, 1};
    request.pathScope = FilePathScope::MountedResource;
    request.path.assign(path.data(), path.size());
    request.bytes.assign(bytes.begin(), bytes.end());
    request.maxWriteBytes = 1024;
    return request;
}

void expectBaseline(FileSaveRequest& request, std::string_view bytes)
{
    auto baseline = saveRequest("baseline", bytes);
    request.precondition = FileSavePrecondition::ContentSha256;
    request.expectedVersion = FileSaveContract::contentVersion(baseline);
    request.maxBaselineBytes = bytes.size();
}

// A transport outcome fixture, never evidence for a real filesystem commit.
class UnknownOutcomeBackend final : public ManagedFileBackend {
public:
    FileBackendCapability capability() const noexcept override
    {
        FileBackendCapability capability;
        capability.completesThroughCompletionQueue = true;
        return capability;
    }

    FileReadResult validateRead(const FileReadRequest&) const override { return {}; }

    FileIoStatus readAsync(
        const FileReadRequest&, FileReadCompletion, runtime::CompletionQueue&) noexcept override
    {
        return FileIoStatus::Unsupported;
    }

    FileSaveCapability saveCapability(PoolHandle) const noexcept override
    {
        return {true, false, false};
    }

    FileIoStatus saveAsync(FileSaveRequest,
        FileSaveCompletion completion,
        runtime::CompletionQueue& queue) noexcept override
    {
        callback_ = completion;
        const auto status = queue.post({deliver, this});
        return status.accepted() ? FileIoStatus::Accepted : FileIoStatus::QueueFull;
    }

    void shutdown() noexcept override {}

private:
    static void deliver(void* context)
    {
        auto& self = *static_cast<UnknownOutcomeBackend*>(context);
        FileSaveResult result;
        result.commitState = FileCommitState::OutcomeUnknown;
        result.fail(FileIoStatus::OutcomeUnknown, "TEST-TRANSPORT-RESPONSE-LOST");
        self.callback_.complete(self.callback_.userData, result);
    }

    FileSaveCompletion callback_;
};

void contracts()
{
    auto request = saveRequest("nested/script.arp", "hello");
    assert(FileSaveContract::validate(request).status == FileIoStatus::Success);
    {
        const auto quota = makeShared<CpuMemoryQuota>(1);
        bool failed = false;
        {
            CpuMemoryQuotaScope failure(quota);
            try {
                (void)FileSaveContract::contentVersion(request);
            } catch (const std::bad_alloc&) {
                failed = true;
            }
        }
        assert(failed && quota->liveBytes() == 0);
    }
    request.contractVersion = 2;
    assert(FileSaveContract::validate(request).status == FileIoStatus::InvalidRequest);
    request.contractVersion = 1;
    request.path = "../escape";
    assert(FileSaveContract::validate(request).status == FileIoStatus::InvalidRequest);
    request.path = "http://localhost/escape";
    assert(FileSaveContract::validate(request).status == FileIoStatus::InvalidRequest);
    request.path.assign("bad\0path", 8);
    assert(FileSaveContract::validate(request).status == FileIoStatus::InvalidRequest);
    request.path = "script.arp";
    request.maxWriteBytes = UINT64_MAX;
    assert(FileSaveContract::validate(request).status == FileIoStatus::InvalidRequest);
    request.maxWriteBytes = 1;
    assert(FileSaveContract::validate(request).status == FileIoStatus::BudgetExceeded);
    request.maxWriteBytes = 1024;
    request.bytes.reserve(2048);
    assert(FileSaveContract::validate(request).status == FileIoStatus::BudgetExceeded);
    request = saveRequest("script.arp", "");
    expectBaseline(request, "");
    assert(FileSaveContract::validate(request).status == FileIoStatus::Success);
    request.expectedVersion.kind = FileVersionKind::MetadataObservation;
    assert(FileSaveContract::validate(request).status == FileIoStatus::Unsupported);
    request.precondition = static_cast<FileSavePrecondition>(99);
    assert(FileSaveContract::validate(request).status == FileIoStatus::InvalidRequest);

    runtime::CompletionQueue queue({4});
    auto readonly = makeShared<MemoryFileBackend>(MemoryFileBackendDesc{4, 4096, 4, 4096, 256});
    auto outcomes = makeShared<UnknownOutcomeBackend>();
    MountedFileBackend mounts({2, 4, 4096, 32, 256});
    const auto readMount = mounts.mount("readonly", readonly);
    auto writeMount = mounts.mount("transport", outcomes);
    FileService service(queue, mounts);
    assert(!service.saveCapability(readMount).observedBaselineReplace);
    assert(service.saveCapability(writeMount).observedBaselineReplace);
    assert(!service.saveCapability(writeMount).strictConditionalReplace);
    Probe<FileSaveResult> probe;
    request = saveRequest("script.arp", "hello");
    request.mount = readMount;
    assert(service.save(std::move(request), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Unsupported);
    assert(probe.calls == 0 && queue.pending() == 0);
    request = saveRequest("script.arp", "hello");
    request.mount = writeMount;
    assert(service.save(std::move(request), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Accepted);
    assert(mounts.unmount(writeMount));
    const auto replacement = mounts.mount("transport", outcomes);
    assert(replacement && !(replacement == writeMount));
    queue.dispatch(4);
    assert(probe.calls == 1 && probe.result.status == FileIoStatus::OutcomeUnknown);
    assert(probe.result.commitState == FileCommitState::OutcomeUnknown);
    assert(probe.result.deliveryStatus == FileIoStatus::StaleLocation);
    request = saveRequest("script.arp", "hello");
    request.mount = writeMount;
    assert(service.save(std::move(request), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::StaleLocation);
    assert(mounts.telemetry().inFlight == 0 && mounts.telemetry().reservedReadBytes == 0);
    mounts.shutdown();
    service.shutdown();
}

#if defined(_WIN32)
void expectDisk(const Workspace& workspace, std::string_view path, std::string_view text)
{
    const auto read = readBinaryFile(workspace.root / std::filesystem::u8path(path), 4096);
    assert(read.success && read.bytes.size() == text.size());
    assert(text.empty() || std::memcmp(read.bytes.data(), text.data(), text.size()) == 0);
}

void noTemporary(const Workspace& workspace)
{
    for (const auto& entry : std::filesystem::recursive_directory_iterator(workspace.root))
        assert(entry.path().filename().u8string().find(".arp-tmp-") == std::string::npos);
}

void nativeCycle(Workspace& workspace)
{
    auto desc = workspace.descriptor();
    desc.allowObservedBaselineReplace = true;
    auto local = makeShared<NativeDirectoryFileBackend>(desc);
    MountedFileBackend mounts({1, 4, desc.maxBytesInFlight, 32, desc.maxPathBytes});
    auto mount = mounts.mount("workspace", local);
    runtime::CompletionQueue queue({8});
    FileService service(queue, mounts);
    assert(service.saveCapability(mount).observedBaselineReplace);
    Probe<FileSaveResult> probe;
    const auto submit = [&](FileSaveRequest request) {
        request.mount = mount;
        const auto calls = probe.calls;
        const auto status =
            service.save(std::move(request), {Probe<FileSaveResult>::complete, &probe});
        if (status != FileIoStatus::Accepted)
            std::fprintf(stderr, "save admission status=%u\n", unsigned(status));
        assert(status == FileIoStatus::Accepted);
        wait(queue, [&] {
            return probe.calls == calls + 1;
        });
        assert(probe.thread == std::this_thread::get_id());
    };
    workspace.write(u8"nested/保存.arp", "old\r\n");
    auto request = saveRequest(u8"nested/保存.arp", "new\r\n");
    expectBaseline(request, "old\r\n");
    submit(request);
    if (probe.result.status != FileIoStatus::Success)
        std::fprintf(stderr,
            "save failed status=%u diag=%s\n",
            unsigned(probe.result.status),
            probe.result.diagnosticId ? probe.result.diagnosticId : "none");
    assert(probe.result.status == FileIoStatus::Success);
    assert(probe.result.commitState == FileCommitState::Committed);
    assert(probe.result.committedVersion == FileSaveContract::contentVersion(request));
    expectDisk(workspace, u8"nested/保存.arp", "new\r\n");
    submit(request);
    assert(probe.result.status == FileIoStatus::VersionMismatch);
    assert(probe.result.commitState == FileCommitState::NotCommitted);
    expectDisk(workspace, u8"nested/保存.arp", "new\r\n");
    request = saveRequest(u8"nested/保存.arp", "");
    submit(request);
    assert(probe.result.status == FileIoStatus::VersionMismatch);
    expectBaseline(request, "new\r\n");
    submit(request);
    assert(probe.result.commitState == FileCommitState::Committed);
    expectDisk(workspace, u8"nested/保存.arp", "");
    request = saveRequest("does-not-exist.arp", "created");
    expectBaseline(request, "");
    submit(request);
    assert(probe.result.status == FileIoStatus::VersionMismatch);
    assert(!std::filesystem::exists(workspace.root / "does-not-exist.arp"));

    // An external read-only handle denies rename after baseline observation.
    workspace.write("locked.arp", "locked");
    const auto locked = CreateFileW((workspace.root / "locked.arp").c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    request = saveRequest("locked.arp", "replacement");
    expectBaseline(request, "locked");
    submit(request);
    assert(probe.result.status == FileIoStatus::PermissionDenied);
    assert(probe.result.commitState == FileCommitState::NotCommitted);
    assert(CloseHandle(locked));
    expectDisk(workspace, "locked.arp", "locked");
    noTemporary(workspace);

    // Baseline scan budget and allocation rejection preserve the destination.
    request.maxBaselineBytes = 1;
    submit(request);
    assert(probe.result.status == FileIoStatus::BudgetExceeded);
    request = saveRequest("locked.arp", "replacement");
    expectBaseline(request, "locked");
    request.mount = mount;
    auto quota = makeShared<CpuMemoryQuota>(1);
    {
        CpuMemoryQuotaScope failure(quota);
        assert(service.save(std::move(request), {Probe<FileSaveResult>::complete, &probe})
            == FileIoStatus::BudgetExceeded);
    }
    expectDisk(workspace, "locked.arp", "locked");
    assert(quota->liveBytes() == 0);

    // Hold completion dispatch until the worker has committed. Cancellation
    // and a new mount generation must preserve the actual committed receipt.
    runtime::CancellationSource cancel;
    request = saveRequest("late.arp", "committed-before-cancel");
    std::filesystem::remove(workspace.root / "late.arp");
    request.cancellation = cancel.token();
    request.mount = mount;
    const auto calls = probe.calls;
    assert(service.save(std::move(request), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Accepted);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!queue.pending() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    assert(queue.pending() == 1 && probe.calls == calls);
    cancel.cancel();
    assert(mounts.unmount(mount));
    mount = mounts.mount("workspace", local);
    queue.dispatch(8);
    assert(probe.calls == calls + 1);
    assert(probe.result.status == FileIoStatus::Success);
    assert(probe.result.deliveryStatus == FileIoStatus::Cancelled);
    assert(probe.result.commitState == FileCommitState::Committed);
    expectDisk(workspace, "late.arp", "committed-before-cancel");

    // Cancellation at admission cannot write; stale handles cannot retarget.
    request = saveRequest("cancelled.arp", "never");
    request.cancellation = cancel.token();
    request.mount = mount;
    assert(service.save(std::move(request), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Cancelled);
    assert(!std::filesystem::exists(workspace.root / "cancelled.arp"));
    assert(probe.calls == calls + 1);

    request = saveRequest("late-stale.arp", "original-mount-only");
    std::filesystem::remove(workspace.root / "late-stale.arp");
    request.mount = mount;
    assert(service.save(std::move(request), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Accepted);
    const auto staleDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!queue.pending() && std::chrono::steady_clock::now() < staleDeadline)
        std::this_thread::yield();
    assert(queue.pending() == 1);
    assert(mounts.unmount(mount));
    auto readonly = makeShared<MemoryFileBackend>(MemoryFileBackendDesc{4, 4096, 4, 4096, 256});
    mount = mounts.mount("workspace", readonly);
    {
        // Receipt delivery/release is allocation-free after a real commit.
        CpuMemoryQuotaScope failure(quota);
        queue.dispatch(8);
    }
    assert(probe.result.status == FileIoStatus::Success);
    assert(probe.result.deliveryStatus == FileIoStatus::StaleLocation);
    assert(probe.result.commitState == FileCommitState::Committed);
    assert(readonly->telemetry().files == 0);
    expectDisk(workspace, "late-stale.arp", "original-mount-only");
    assert(mounts.unmount(mount));
    mount = mounts.mount("workspace", local);
    request = saveRequest("late-shutdown.arp", "committed-before-close");
    std::filesystem::remove(workspace.root / "late-shutdown.arp");
    request.mount = mount;
    assert(service.save(std::move(request), {Probe<FileSaveResult>::complete, &probe})
        == FileIoStatus::Accepted);
    const auto closeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!queue.pending() && std::chrono::steady_clock::now() < closeDeadline)
        std::this_thread::yield();
    assert(queue.pending() == 1);
    mounts.shutdown();
    queue.dispatch(8);
    assert(probe.result.status == FileIoStatus::Success);
    assert(probe.result.deliveryStatus == FileIoStatus::ServiceClosed);
    assert(probe.result.commitState == FileCommitState::Committed);
    expectDisk(workspace, "late-shutdown.arp", "committed-before-close");
    assert(local->telemetry().inFlight == 0 && local->telemetry().reservedBytes == 0);
    assert(local->telemetry().openNativeObjects == 0);
    assert(mounts.telemetry().inFlight == 0 && mounts.telemetry().reservedReadBytes == 0);
    assert(queue.pending() == 0 && queue.reserved() == 0);
    noTemporary(workspace);
}

void nativePolicy()
{
    Workspace workspace;
    assert(workspace.createJunction());
    auto desc = workspace.descriptor();
    NativeDirectoryFileBackend readonly(desc);
    assert(!readonly.saveCapability().observedBaselineReplace);
    runtime::CompletionQueue queue({4});
    Probe<FileSaveResult> probe;
    auto request = saveRequest("a.bin", "denied");
    assert(readonly.saveAsync(request, {Probe<FileSaveResult>::complete, &probe}, queue)
        == FileIoStatus::Unsupported);
    readonly.shutdown();
    desc.allowObservedBaselineReplace = true;
    NativeDirectoryFileBackend writable(desc);
    {
        std::array<runtime::CompletionReservation, 4> reservations;
        for (auto& reservation : reservations) {
            reservation = queue.reserve();
            assert(reservation.valid());
        }
        request = saveRequest("queue-denied.arp", "denied");
        assert(writable.saveAsync(request, {Probe<FileSaveResult>::complete, &probe}, queue)
            == FileIoStatus::QueueFull);
        assert(!std::filesystem::exists(workspace.root / "queue-denied.arp"));
        assert(probe.calls == 0 && writable.telemetry().inFlight == 0);
    }
    request = saveRequest("link/secret.bin", "denied");
    expectBaseline(request, "outside");
    assert(writable.saveAsync(request, {Probe<FileSaveResult>::complete, &probe}, queue)
        == FileIoStatus::Accepted);
    wait(queue, [&] {
        return probe.calls == 1;
    });
    assert(probe.result.status == FileIoStatus::PermissionDenied);
    assert(probe.result.commitState == FileCommitState::NotCommitted);
    expectDisk(workspace, "link/secret.bin", "outside");
    writable.shutdown();
    assert(writable.telemetry().openNativeObjects == 0);
}
#endif
} // namespace

int main(int argc, char** argv)
{
    contracts();
#if defined(_WIN32)
    nativePolicy();
    Workspace workspace;
    const unsigned cycles = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20 : 1;
    nativeCycle(workspace);
    const auto before = defaultCpuAllocator().stats();
    for (unsigned i = 0; i < cycles; ++i)
        nativeCycle(workspace);
    const auto after = defaultCpuAllocator().stats();
    std::printf("rooted save %u cycles: core bytes %llu -> %llu, live %llu -> %llu\n",
        cycles,
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
#else
    (void)argc;
    (void)argv;
    std::puts("save/routing contracts passed; native-directory writes Unsupported on this host");
#endif
}
