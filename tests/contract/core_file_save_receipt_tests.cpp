#include "core/io/file_save_receipt_contract.h"
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

// Null contract producer only: queued facts let the test place cancellation,
// mount retirement and destruction precisely before delivery. No native or
// remote save is simulated as a validated backend by this fixture.
class ReceiptFixtureBackend final : public ManagedFileBackend {
    struct Pending {
        FileSaveReceiptCompletion completion;
        FileSaveReceiptResult result;
    };

public:
    FileBackendCapability capability() const noexcept override
    {
        FileBackendCapability value;
        value.completesThroughCompletionQueue = true;
        return value;
    }

    FileReadResult validateRead(const FileReadRequest&) const override
    {
        FileReadResult result;
        result.status = FileIoStatus::Unsupported;
        return result;
    }

    FileIoStatus readAsync(
        const FileReadRequest&, FileReadCompletion, runtime::CompletionQueue&) noexcept override
    {
        return FileIoStatus::Unsupported;
    }

    FileSaveCapability saveCapability(PoolHandle = {}) const noexcept override
    {
        return {false, false, !closed};
    }

    FileIoStatus receiptAsync(const FileSaveReceiptRequest& request,
        FileSaveReceiptCompletion completion,
        runtime::CompletionQueue& queue) noexcept override
    {
        if (closed)
            return FileIoStatus::ServiceClosed;
        try {
            auto reservation = queue.reserve();
            if (!reservation.valid())
                return FileIoStatus::QueueFull;
            auto pending = makeUnique<Pending>();
            pending->completion = completion;
            auto& result = pending->result;
            result.status = FileIoStatus::Success;
            result.receipt = request.receipt;
            if (forged)
                ++result.receipt.scope;
            result.state = request.operation == FileSaveReceiptOperation::Release
                ? FileSaveReceiptState::Released
                : answer;
            if (result.state == FileSaveReceiptState::Terminal) {
                result.outcome.status = FileIoStatus::Success;
                result.outcome.commitState = FileCommitState::Committed;
                result.outcome.receipt = result.receipt;
            }
            const auto posted = queue.postReserved(std::move(reservation),
                {[](void* data) {
                     UniquePtr<Pending> value{static_cast<Pending*>(data),
                         CpuAllocatedDeleter<Pending>{data, &destroyCpuAllocatedUnique<Pending>}};
                     value->completion.complete(
                         value->completion.userData, std::move(value->result));
                 },
                    pending.get()});
            if (!posted.accepted())
                return FileIoStatus::QueueFull;
            pending.release();
            return FileIoStatus::Accepted;
        } catch (...) {
            return FileIoStatus::BudgetExceeded;
        }
    }

    void shutdown() noexcept override { closed = true; }

    FileSaveReceiptState answer = FileSaveReceiptState::Terminal;
    bool forged = false, closed = false;
};

class Host final {
public:
    Host()
    {
        mount = mounts->mount("receipt", backend);
        assert(mount);
    }

    FileSaveReceiptRequest request() const
    {
        FileSaveReceiptRequest value;
        value.pathScope = FilePathScope::MountedResource;
        value.mount = mount;
        value.maxResultBytes = sizeof(FileSaveReceiptResult);
        value.receipt.scope = 1;
        value.receipt.operationId = 7;
        value.receipt.sessionVersion.kind = FileVersionKind::ContentSha256;
        value.receipt.sessionVersion.digest[0] = 1;
        value.receipt.requestVersion.kind = FileVersionKind::ContentSha256;
        value.receipt.requestVersion.digest[0] = 2;
        return value;
    }

    runtime::CompletionQueue queue{{4}};
    SharedPtr<ReceiptFixtureBackend> backend = makeShared<ReceiptFixtureBackend>();
    UniquePtr<MountedFileBackend> mounts = makeUnique<MountedFileBackend>(
        MountedFileBackendDesc{2, 1, 4 * sizeof(FileSaveReceiptResult), 64, 128});
    PoolHandle mount;
};

void cycle()
{
    Host host;
    FileService service(host.queue, *host.mounts);
    auto request = host.request();
    Probe<FileSaveReceiptResult> probe;
    const FileSaveReceiptCompletion callback{decltype(probe)::complete, &probe};
    assert(FileSaveReceiptResult{}.outcome.commitState == FileCommitState::OutcomeUnknown);
    assert(!FileSaveResult{}.receipt.valid());
    {
        FileSaveReceiptResult terminal;
        terminal.status = FileIoStatus::Success;
        terminal.state = FileSaveReceiptState::Terminal;
        terminal.receipt = terminal.outcome.receipt = request.receipt;
        terminal.outcome.status = FileIoStatus::Success;
        terminal.outcome.commitState = FileCommitState::Committed;
        assert(!FileSaveReceiptContract::validateResult(request, terminal).diagnostic);
        for (unsigned i = 0; i < 5; ++i) {
            auto invalid = terminal;
            switch (i) {
            case 0:
                ++invalid.outcome.receipt.scope;
                break;
            case 1:
                invalid.state = FileSaveReceiptState::Pending;
                break;
            case 2:
                invalid.state = FileSaveReceiptState::Unavailable;
                break;
            case 3:
                invalid.state = FileSaveReceiptState::Released;
                break;
            case 4:
                invalid.outcome.status = FileIoStatus::Accepted;
                break;
            }
            assert(FileSaveReceiptContract::validateResult(request, invalid).diagnostic);
        }
    }
    for (unsigned i = 0; i < 7; ++i) {
        auto invalid = request;
        switch (i) {
        case 0:
            invalid.contractVersion = 2;
            break;
        case 1:
            invalid.mount = {};
            break;
        case 2:
            invalid.receipt.operationId = 0;
            break;
        case 3:
            invalid.receipt.requestVersion.kind = FileVersionKind::None;
            break;
        case 4:
            invalid.maxResultBytes = UINT64_MAX;
            break;
        case 5:
            invalid.operation = static_cast<FileSaveReceiptOperation>(255);
            break;
        case 6:
            invalid.maxResultBytes = sizeof(FileSaveReceiptResult) - 1;
            break;
        }
        assert(service.receipt(invalid, callback) != FileIoStatus::Accepted && probe.calls == 0);
    }
    assert(service.receipt(request, {}) == FileIoStatus::InvalidRequest);
    {
        runtime::CancellationSource cancellation;
        cancellation.cancel();
        auto cancelled = request;
        cancelled.cancellation = cancellation.token();
        cancelled.operation = FileSaveReceiptOperation::Release;
        assert(service.receipt(cancelled, callback) == FileIoStatus::Cancelled && probe.calls == 0);
        assert(
            host.mounts->receiptAsync(cancelled, callback, host.queue) == FileIoStatus::Cancelled);
    }
    assert(service.receipt(request, callback) == FileIoStatus::Accepted && probe.calls == 0);
    assert(service.receipt(request, callback) == FileIoStatus::QueueFull);
    assert(host.mounts->telemetry().inFlight == 1
        && host.mounts->telemetry().reservedReadBytes == request.maxResultBytes);
    unified_io_fixture::wait(host.queue, [&] {
        return probe.calls == 1;
    });
    assert(probe.result.status == FileIoStatus::Success
        && probe.result.outcome.commitState == FileCommitState::Committed
        && probe.result.outcome.receipt == request.receipt);
    assert(
        host.mounts->telemetry().inFlight == 0 && host.mounts->telemetry().reservedReadBytes == 0);
    host.backend->answer = FileSaveReceiptState::Pending;
    assert(service.receipt(request, callback) == FileIoStatus::Accepted);
    unified_io_fixture::wait(host.queue, [&] {
        return probe.calls == 2;
    });
    assert(probe.result.state == FileSaveReceiptState::Pending
        && probe.result.outcome.commitState == FileCommitState::OutcomeUnknown);
    host.backend->answer = FileSaveReceiptState::Terminal;
    host.backend->forged = true;
    assert(service.receipt(request, callback) == FileIoStatus::Accepted);
    unified_io_fixture::wait(host.queue, [&] {
        return probe.calls == 3;
    });
    assert(probe.result.status == FileIoStatus::IntegrityMismatch
        && probe.result.state == FileSaveReceiptState::Unavailable
        && probe.result.outcome.commitState == FileCommitState::OutcomeUnknown);
    host.backend->forged = false;
    {
        runtime::CancellationSource cancellation;
        request.cancellation = cancellation.token();
        assert(service.receipt(request, callback) == FileIoStatus::Accepted);
        cancellation.cancel();
        unified_io_fixture::wait(host.queue, [&] {
            return probe.calls == 4;
        });
        assert(probe.result.deliveryStatus == FileIoStatus::Cancelled
            && probe.result.outcome.commitState == FileCommitState::Committed);
        request.cancellation = {};
    }
    request.operation = FileSaveReceiptOperation::Release;
    assert(service.receipt(request, callback) == FileIoStatus::Accepted);
    unified_io_fixture::wait(host.queue, [&] {
        return probe.calls == 5;
    });
    assert(probe.result.state == FileSaveReceiptState::Released
        && probe.result.outcome.commitState == FileCommitState::OutcomeUnknown);
    request.operation = FileSaveReceiptOperation::Query;
    {
        auto quota = makeShared<CpuMemoryQuota>(1);
        CpuMemoryQuotaScope scope(quota);
        assert(service.receipt(request, callback) == FileIoStatus::BudgetExceeded);
    }
    assert(host.mounts->telemetry().inFlight == 0 && host.queue.reserved() == 0);
    {
        unsigned calls = 0;
        assert(service.receipt(request,
                   {[](void* value, FileSaveReceiptResult) {
                        ++*static_cast<unsigned*>(value);
                        throw 1;
                    },
                       &calls})
            == FileIoStatus::Accepted);
        try {
            host.queue.dispatch(4);
        } catch (int) {
        }
        assert(calls == 1 && host.queue.pending() == 0 && host.queue.reserved() == 0
            && host.mounts->telemetry().inFlight == 0
            && host.mounts->telemetry().reservedReadBytes == 0);
    }
    assert(service.receipt(request, callback) == FileIoStatus::Accepted);
    assert(host.mounts->unmount(host.mount));
    auto memory = makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, 64, 1, 1024, 64});
    const auto replacement = host.mounts->mount("receipt", memory);
    assert(replacement && !(replacement == host.mount));
    unified_io_fixture::wait(host.queue, [&] {
        return probe.calls == 6;
    });
    assert(probe.result.deliveryStatus == FileIoStatus::StaleLocation
        && probe.result.outcome.commitState == FileCommitState::Committed);
    assert(service.receipt(request, callback) == FileIoStatus::StaleLocation);
    request.mount = replacement;
    assert(service.receipt(request, callback) == FileIoStatus::Unsupported);
    service.shutdown();
    assert(service.receipt(request, callback) == FileIoStatus::ServiceClosed);
}

void destroyBeforeDelivery()
{
    Host host;
    Probe<FileSaveReceiptResult> probe;
    auto request = host.request();
    assert(host.mounts->receiptAsync(request, {decltype(probe)::complete, &probe}, host.queue)
        == FileIoStatus::Accepted);
    host.mounts.reset();
    unified_io_fixture::wait(host.queue, [&] {
        return probe.calls == 1;
    });
    assert(probe.result.deliveryStatus == FileIoStatus::ServiceClosed
        && probe.result.outcome.commitState == FileCommitState::Committed
        && host.queue.reserved() == 0);
}
} // namespace

int main(int argc, char** argv)
{
    const auto loops = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20u : 1u;
    cycle();
    destroyBeforeDelivery();
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < loops; ++i) {
        cycle();
        destroyBeforeDelivery();
    }
    const auto after = arp::core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf(
        "Save receipt contract fixture %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
