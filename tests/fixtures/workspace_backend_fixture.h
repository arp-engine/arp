#pragma once

#include "core/io/file_save_contract.h"
#include "core/io/file_service.h"
#include "core/io/mounted_file_backend.h"
#include "core/io/workspace_file_backend.h"
#include "core/text/number_format.h"
#include "tests/fixtures/unified_io_fixture.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace workspace_backend_fixture {
using namespace arp::core;
using namespace arp::core::io;
using unified_io_fixture::Probe;
inline constexpr const char* readToken = "read-01234567890123456789012345678901";
inline constexpr const char* writeToken = "write-0123456789012345678901234567890";
inline constexpr const char* original = "\xef\xbb\xbf# saved\r\n";

inline WorkspaceFileBackendDesc descriptor(std::string_view origin, const char* token = writeToken)
{
    WorkspaceFileBackendDesc d;
    d.credentialOrigin = origin;
    d.endpoint = pmr::String(origin) + "/workspace/v2";
    d.workspaceId = "fixture";
    d.token = token;
    d.maxPathBytes = 512;
    d.maxFileBytes = 65536;
    d.maxRequestBytes = d.maxResponseBytes = 131072;
    d.preparationBytes = 8 * 1024 * 1024;
    d.maxQueryBytes = 8192;
    d.maxDirectoryEntries = 16;
    d.maxDirectoryNameBytes = 1024;
    d.maxHeaderBytes = 4096;
    d.ioChunkBytes = 1024;
    d.timeoutMs = 3000;
    d.cancellationPollMs = 2;
    d.maxDnsAddresses = 0;
    return d;
}

template <class Predicate, class Pump>
void wait(runtime::CompletionQueue& queue, Predicate&& predicate, Pump&& pump)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        pump();
        queue.dispatch(8);
#if defined(__EMSCRIPTEN__)
        if (!predicate())
            emscripten_sleep(1);
#else
        std::this_thread::yield();
#endif
    }
    assert(predicate());
}

class Client final {
public:
    explicit Client(WorkspaceFileBackendDesc d)
        : backend(makeShared<WorkspaceFileBackend>(std::move(d)))
    {
        assert(!backend->diagnostic());
        mount = mounts.mount("remote", backend);
        assert(mount);
    }

    ~Client()
    {
        mounts.shutdown();
        while (queue.pending())
            queue.dispatch(8);
        assert(queue.reserved() == 0 && mounts.telemetry().inFlight == 0
            && backend->telemetry().inFlight == 0
            && backend->telemetry().activeNativeTransfers == 0);
    }

    template <class Pump> void connect(Pump&& pump)
    {
        Probe<WorkspaceFileConnectionResult> result;
        assert(!files.capability(mount).supportsRead);
        assert(backend->connectAsync({decltype(result)::complete, &result}, queue)
            == FileIoStatus::Accepted);
        assert(result.calls == 0);
        wait(
            queue,
            [&] {
                return result.calls == 1;
            },
            pump);
        if (result.result.status != FileIoStatus::Success)
            std::fprintf(stderr,
                "connect failed %u %s\n",
                unsigned(result.result.status),
                result.result.diagnosticId);
        assert(result.result.status == FileIoStatus::Success && backend->connected());
        assert(files.capability(mount).supportsRead && files.capability(mount).supportsEnumeration);
    }

    FileReadRequest read(std::string_view path) const
    {
        FileReadRequest request;
        request.contractVersion = 1;
        request.pathScope = FilePathScope::MountedResource;
        request.mount = mount;
        request.path = path;
        request.maxBytes = 65536;
        return request;
    }

    FileQueryRequest query(std::string_view path, bool list = false) const
    {
        FileQueryRequest request;
        request.pathScope = FilePathScope::MountedResource;
        request.mount = mount;
        request.path = path;
        request.maxResultBytes = 8192;
        if (list) {
            request.operation = FileQueryOperation::Enumerate;
            request.pageSize = 2;
            request.maxScanEntries = 16;
            request.maxScanNameBytes = 1024;
        } else {
            request.versionPolicy = FileVersionPolicy::ContentSha256;
            request.maxVersionBytes = 65536;
        }
        return request;
    }

    FileSaveRequest save(std::string_view path, std::string_view bytes) const
    {
        FileSaveRequest request;
        request.pathScope = FilePathScope::MountedResource;
        request.mount = mount;
        request.path = path;
        request.bytes.assign(bytes.begin(), bytes.end());
        request.maxWriteBytes = 65536;
        return request;
    }

    FileSaveReceiptRequest receipt(FileSaveReceipt ticket, bool release = false) const
    {
        FileSaveReceiptRequest request;
        request.pathScope = FilePathScope::MountedResource;
        request.mount = mount;
        request.receipt = ticket;
        request.maxResultBytes = sizeof(FileSaveReceiptResult);
        request.operation =
            release ? FileSaveReceiptOperation::Release : FileSaveReceiptOperation::Query;
        return request;
    }

    runtime::CompletionQueue queue{{8}};
    SharedPtr<WorkspaceFileBackend> backend;
    MountedFileBackend mounts{{1, 1, 262144, 64, 512}};
    FileService files{queue, mounts};
    PoolHandle mount;
};

template <class Pump>
void exercise(std::string_view origin, std::string_view folder, Pump&& pump, bool corruptWire)
{
    Client client(descriptor(origin));
    client.connect(pump);
    const auto path = [&](std::string_view name) {
        return pmr::String(folder) + '/' + pmr::String(name);
    };
    Probe<FileReadResult> read;
    const auto readFile = [&](FileReadRequest request) {
        const auto next = read.calls + 1;
        const auto admitted = client.files.read(request, {decltype(read)::complete, &read});
        if (admitted != FileIoStatus::Accepted) {
            const auto validation = client.backend->validateRead(request);
            std::fprintf(stderr,
                "read admission %u path=%s offset=%llu size=%llu version=%u validation=%u %s busy=%u\n",
                unsigned(admitted),
                request.path.c_str(),
                (unsigned long long)request.range.offset,
                (unsigned long long)request.range.size,
                unsigned(request.expectedVersion.kind),
                unsigned(validation.status),
                validation.diagnosticId ? validation.diagnosticId : "none",
                client.backend->telemetry().inFlight);
        }
        assert(admitted == FileIoStatus::Accepted);
        wait(
            client.queue,
            [&] {
                return read.calls == next;
            },
            pump);
    };
    Probe<FileQueryResult> query;
    const auto queryFile = [&](FileQueryRequest request) {
        const auto next = query.calls + 1;
        assert(client.files.query(request, {decltype(query)::complete, &query})
            == FileIoStatus::Accepted);
        wait(
            client.queue,
            [&] {
                return query.calls == next;
            },
            pump);
    };
    Probe<FileSaveResult> save;
    const auto saveFile = [&](FileSaveRequest request) {
        const auto next = save.calls + 1;
        assert(client.files.save(std::move(request), {decltype(save)::complete, &save})
            == FileIoStatus::Accepted);
        wait(
            client.queue,
            [&] {
                return save.calls == next;
            },
            pump);
        if (save.result.commitState == FileCommitState::OutcomeUnknown && !corruptWire)
            std::fprintf(stderr,
                "save unexpected unknown %u %s\n",
                unsigned(save.result.status),
                save.result.diagnosticId);
    };
    Probe<FileSaveReceiptResult> receipt;
    const auto observe = [&](FileSaveReceipt ticket, bool release = false) {
        const auto next = receipt.calls + 1;
        assert(client.files.receipt(
                   client.receipt(ticket, release), {decltype(receipt)::complete, &receipt})
            == FileIoStatus::Accepted);
        wait(
            client.queue,
            [&] {
                return receipt.calls == next;
            },
            pump);
    };
    readFile(client.read(path("rules.arp")));
    assert(read.result.status == FileIoStatus::Success
        && std::string_view(
               reinterpret_cast<const char*>(read.result.bytes.data()), read.result.bytes.size())
            == original);
    queryFile(client.query(path("rules.arp")));
    assert(query.result.status == FileIoStatus::Success
        && query.result.metadata.version.kind == FileVersionKind::ContentSha256);
    const auto originalVersion = query.result.metadata.version;
    auto ranged = client.read(path("rules.arp"));
    ranged.range = FileReadRange::exactRange(3, 2);
    readFile(ranged);
    assert(read.result.status == FileIoStatus::Success && read.result.bytes.size() == 2
        && read.result.bytes[0] == '#');
    auto pinned = client.read(path("rules.arp"));
    pinned.expectedVersion = originalVersion;
    pinned.maxVersionBytes = 65536;
    readFile(pinned);
    assert(read.result.status == FileIoStatus::Success && read.result.version == originalVersion);
    pinned.expectedVersion.digest[0] ^= 1;
    readFile(pinned);
    assert(read.result.status == FileIoStatus::VersionMismatch && read.result.bytes.empty());
    queryFile(client.query(path("rules.arp")));
    assert(query.result.status == FileIoStatus::Success
        && query.result.metadata.version == originalVersion);
    auto listing = client.query(folder, true);
    queryFile(listing);
    assert(query.result.status == FileIoStatus::Success && query.result.totalEntries == 4
        && query.result.entries.size() == 2);
    const auto cursor = query.result.next;
    listing.cursor = cursor;
    queryFile(listing);
    assert(query.result.status == FileIoStatus::Success && query.result.next.offset == 0
        && query.result.entries.size() == 2);

    saveFile(client.save(path("new.arp"), "# new\n"));
    assert(save.result.commitState == FileCommitState::Committed && save.result.receipt.valid());
    const auto ticket = save.result.receipt;
    observe(ticket);
    assert(receipt.result.state == FileSaveReceiptState::Terminal
        && receipt.result.outcome.commitState == FileCommitState::Committed);
    auto forged = ticket;
    forged.requestVersion.digest[0] ^= 1;
    observe(forged, true);
    assert(receipt.result.status != FileIoStatus::Success
        && receipt.result.outcome.commitState == FileCommitState::OutcomeUnknown);
    observe(ticket);
    assert(receipt.result.outcome.commitState == FileCommitState::Committed);
    observe(ticket, true);
    assert(receipt.result.state == FileSaveReceiptState::Released);
    observe(ticket);
    assert(receipt.result.state == FileSaveReceiptState::Unavailable
        && receipt.result.outcome.commitState == FileCommitState::OutcomeUnknown);

    saveFile(client.save(path("new.arp"), "# conflict\n"));
    assert(save.result.commitState == FileCommitState::NotCommitted
        && save.result.status != FileIoStatus::Success);
    observe(save.result.receipt, true);
    assert(receipt.result.state == FileSaveReceiptState::Released);
    auto update = client.save(path("new.arp"), "# updated\n");
    update.precondition = FileSavePrecondition::ContentSha256;
    FileSaveRequest baseline;
    baseline.bytes.assign({'#', ' ', 'n', 'e', 'w', '\n'});
    update.expectedVersion = FileSaveContract::contentVersion(baseline);
    update.maxBaselineBytes = 65536;
    saveFile(std::move(update));
    assert(save.result.commitState == FileCommitState::Committed);
    observe(save.result.receipt, true);
    assert(receipt.result.state == FileSaveReceiptState::Released);
    queryFile(listing);
    assert(query.result.status == FileIoStatus::VersionMismatch);
    auto bounded = client.query(folder, true);
    bounded.pageSize = bounded.maxScanEntries = 1;
    queryFile(bounded);
    assert(query.result.status == FileIoStatus::BudgetExceeded && query.result.entries.empty());

    if (corruptWire) {
        saveFile(client.save(path("lost.arp"), "# lost response\n"));
        assert(save.result.commitState == FileCommitState::OutcomeUnknown
            && save.result.receipt.valid());
        observe(save.result.receipt);
        assert(receipt.result.status == FileIoStatus::Success
            && receipt.result.outcome.commitState == FileCommitState::Committed);
        observe(save.result.receipt, true);
        assert(receipt.result.state == FileSaveReceiptState::Released);
        saveFile(client.save(path("bad.arp"), "# bad wire\n"));
        assert(save.result.commitState == FileCommitState::Committed);
        readFile(client.read(path("bad.arp")));
        assert(read.result.status == FileIoStatus::IntegrityMismatch && read.result.bytes.empty());
        observe(save.result.receipt);
        assert(receipt.result.status == FileIoStatus::IntegrityMismatch
            && receipt.result.outcome.commitState == FileCommitState::OutcomeUnknown);
        observe(save.result.receipt, true);
        assert(receipt.result.state == FileSaveReceiptState::Released);
    }
    Client readonly(descriptor(origin, readToken));
    readonly.connect(pump);
    assert(!readonly.files.saveCapability(readonly.mount).observedBaselineReplace);
    Probe<FileSaveResult> denied;
    assert(readonly.files.save(
               readonly.save(path("denied.arp"), "denied"), {decltype(denied)::complete, &denied})
        == FileIoStatus::Unsupported);
    assert(denied.calls == 0);
    {
        runtime::CancellationSource cancellation;
        cancellation.cancel();
        auto cancelled = client.save(path("cancelled.arp"), "cancelled");
        cancelled.cancellation = cancellation.token();
        assert(client.files.save(std::move(cancelled), {decltype(denied)::complete, &denied})
            == FileIoStatus::Cancelled);
        assert(denied.calls == 0);
    }
    assert(client.backend->telemetry().inFlight == 0
        && client.backend->telemetry().activeNativeTransfers == 0);
}
} // namespace workspace_backend_fixture
