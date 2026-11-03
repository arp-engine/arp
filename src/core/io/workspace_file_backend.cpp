#include "core/io/workspace_file_backend.h"

#include "core/io/detail/http_transport_status.h"
#include "core/io/file_query_contract.h"
#include "core/io/file_save_contract.h"
#include "core/io/file_save_receipt_contract.h"
#include "core/io/http_file_contract.h"
#include "core/io/workspace_file_wire.h"
#include "network/http/byte_transfer_executor.h"

#include <atomic>

namespace arp::core::io {
namespace {
using Wire = WorkspaceFileWire;
using Json = core::json::JsonDocument;
using Value = core::json::JsonValue;

bool boolean(const Value& value, const char* key)
{
    const auto& item = Wire::member(value, key);
    Wire::require(item.IsBool());
    return item.GetBool();
}

void keys(const Value& value, std::initializer_list<std::string_view> allowed)
{
    Wire::require(core::json::jsonOnlyUniqueKeys(value, allowed));
}

void payload(const Value& node, std::uint64_t limit, core::ByteBuffer& output)
{
    const auto size = Wire::number(node, "bytes");
    const auto text = Wire::text(node, "data");
    Wire::require(
        size <= limit && size <= (SIZE_MAX - 2) / 4 && text.size() == ((size + 2) / 3) * 4);
    Wire::require((size == 0 ? text.empty() : core::Base64::decode(text, output))
        && output.size() == size && core::Base64::encode(output) == text);
}
} // namespace

struct WorkspaceFileBackend::State {
    struct Remote {
        core::pmr::String session;
        FileVersionToken sessionVersion;
        FileBackendCapability capability;
        FileSaveCapability save;
        std::uint64_t scope = 0, lastId = 0, fileBytes = 0, queryBytes = 0, nameBytes = 0;
        std::uint32_t entries = 0;
        std::size_t requestBytes = 0, responseBytes = 0;
        bool writeAuthorized = false;
    };

    explicit State(WorkspaceFileBackendDesc value) : desc(std::move(value)) {}

    WorkspaceFileBackendDesc desc;
    Remote remote;
    core::UniquePtr<network::http::ByteTransferExecutor> executor;
    runtime::CompletionQueue* queue = nullptr;
    std::atomic_bool closed{false};
    bool connected = false, busy = false;
    const char* diagnostic = nullptr;
    std::uint64_t received = 0;
};

struct WorkspaceFileBackend::Pending {
    enum class Kind { Connect, Read, Query, Save, Receipt };

    class Receiver final : public network::http::ByteReadObserver {
    public:
        explicit Receiver(Pending& value) : owner(value) {}

        bool onHeaders(unsigned status, std::string_view block) noexcept override
        {
            HttpFileResponseHeaders parsed;
            const auto validation = HttpFileContract::responseHeaders(status, block, parsed);
            if (validation.diagnostic || parsed.contentType != "application/json"
                || (!parsed.contentEncoding.empty() && parsed.contentEncoding != "identity")) {
                fail(validation.diagnostic ? validation.status : FileIoStatus::Unsupported);
                return false;
            }
            httpStatus = status;
            if (status >= 300 && status < 400) {
                fail(FileIoStatus::Unsupported);
                return false;
            }
            return true;
        }

        bool onBody(const std::uint8_t* data, std::size_t count) noexcept override
        {
            if (failure != FileIoStatus::Success)
                return false;
            if (count > owner.transfer.maxBodyBytes - body.size()) {
                fail(FileIoStatus::BudgetExceeded);
                return false;
            }
            try {
                core::CpuMemoryQuotaScope quota(owner.quota);
                body.append(reinterpret_cast<const char*>(data), count);
                return true;
            } catch (...) {
                fail(FileIoStatus::BudgetExceeded);
                return false;
            }
        }

        void onEnd() noexcept override { ended = failure == FileIoStatus::Success; }

        void onFailure(network::http::ByteReadStatus status, const char*) noexcept override
        {
            fail(detail::httpFileStatus(status));
        }

        void fail(FileIoStatus status) noexcept
        {
            if (failure == FileIoStatus::Success)
                failure = status;
            body.clear();
        }

        Pending& owner;
        core::pmr::String body;
        unsigned httpStatus = 0;
        FileIoStatus failure = FileIoStatus::Success;
        bool ended = false;
    };

    Pending(core::SharedPtr<State> state, Kind type, runtime::CancellationToken cancellation)
        : owner(std::move(state)), kind(type), token(std::move(cancellation)),
          quota(core::makeShared<core::CpuMemoryQuota>(owner->desc.preparationBytes)),
          receiver(*this)
    {}

    core::SharedPtr<State> owner;
    Kind kind;
    runtime::CancellationToken token;
    core::SharedPtr<core::CpuMemoryQuota> quota;
    Receiver receiver;
    network::http::ByteReadRequest transfer;
    FileReadRequest read;
    FileQueryRequest query;
    FileSaveRequest save;
    FileSaveReceiptRequest receipt;
    FileSaveReceipt ticket;
    WorkspaceFileConnectionCompletion connected;
    FileReadCompletion readComplete;
    FileQueryCompletion queryComplete;
    FileSaveCompletion saveComplete;
    FileSaveReceiptCompletion receiptComplete;
    WorkspaceFileConnectionResult connectionResult;
    FileReadResult readResult;
    FileQueryResult queryResult;
    FileSaveResult saveResult;
    FileSaveReceiptResult receiptResult;

    Json request(const char* operation)
    {
        Json node;
        node.SetObject();
        auto& a = node.GetAllocator();
        Wire::field(node, "type", "arp.workspace.request", a);
        node.AddMember("contractVersion", Wire::contractVersion, a);
        Wire::field(node, "workspace", owner->desc.workspaceId, a);
        Wire::field(node, "operation", operation, a);
        if (kind != Kind::Connect)
            Wire::field(node, "session", owner->remote.session, a);
        return node;
    }

    void encode(Json& node)
    {
        auto& d = owner->desc;
        transfer.contractVersion = 2;
        transfer.replayProtected = true;
        transfer.operation = network::http::ByteReadRequest::Operation::BearerJsonPost;
        transfer.url = d.endpoint;
        transfer.credentialOrigin = d.credentialOrigin;
        transfer.bearerToken = d.token;
        transfer.maxRequestBytes = kind == Kind::Connect
            ? d.maxRequestBytes
            : std::min(d.maxRequestBytes, owner->remote.requestBytes);
        transfer.maxBodyBytes = kind == Kind::Connect
            ? d.maxResponseBytes
            : std::min(d.maxResponseBytes, owner->remote.responseBytes);
        transfer.cancellation = token;
        transfer.requestBody =
            Wire::encode(node, static_cast<std::size_t>(transfer.maxRequestBytes));
    }

    void envelope(const Value& root, bool discovery = false)
    {
        Wire::require(Wire::text(root, "type") == "arp.workspace.response"
            && Wire::number(root, "contractVersion") == Wire::contractVersion
            && Wire::text(root, "workspace") == owner->desc.workspaceId);
        if (!discovery)
            Wire::require(Wire::text(root, "session") == owner->remote.session
                && Wire::number(root, "receiptScope") == owner->remote.scope);
    }

    void connect(const Value& root)
    {
        keys(root,
            {"type",
                "contractVersion",
                "workspace",
                "session",
                "receiptScope",
                "status",
                "writeAuthorized",
                "supportsRead",
                "supportsMetadata",
                "supportsEnumeration",
                "supportsContentVersion",
                "observedBaselineReplace",
                "strictConditionalReplace",
                "maxFileBytes",
                "maxRequestBytes",
                "maxResponseBytes",
                "maxQueryBytes",
                "maxDirectoryEntries",
                "maxDirectoryNameBytes",
                "maxReceipts",
                "lastRequestId",
                "receiptLifetime",
                "saveSubmissions",
                "confirmedCommits"});
        envelope(root, true);
        Wire::require(Wire::text(root, "status") == "ready"
            && Wire::identifier(Wire::text(root, "session"))
            && Wire::text(root, "receiptLifetime") == "server-session");
        State::Remote candidate;
        candidate.session = Wire::text(root, "session");
        candidate.scope = Wire::number(root, "receiptScope");
        candidate.lastId = Wire::number(root, "lastRequestId");
        Wire::require(Wire::number(root, "saveSubmissions") <= candidate.lastId
            && Wire::number(root, "confirmedCommits") <= Wire::number(root, "saveSubmissions"));
        candidate.fileBytes = Wire::number(root, "maxFileBytes");
        candidate.queryBytes = Wire::number(root, "maxQueryBytes");
        candidate.nameBytes = Wire::number(root, "maxDirectoryNameBytes");
        const auto entries = Wire::number(root, "maxDirectoryEntries");
        Wire::require(candidate.scope && candidate.fileBytes && candidate.queryBytes
            && candidate.nameBytes && entries && entries <= UINT32_MAX
            && Wire::number(root, "maxReceipts") && Wire::number(root, "maxRequestBytes")
            && Wire::number(root, "maxResponseBytes"));
        candidate.entries = static_cast<std::uint32_t>(entries);
        candidate.capability.kind = FileBackendKind::WorkspaceFileBackend;
        candidate.capability.status = FileBackendStatus::PrototypeComplete;
        candidate.capability.completesThroughCompletionQueue = true;
        candidate.capability.supportsCancellation = true;
        candidate.capability.nativeAsync = network::http::ByteTransferExecutor::browserSupported();
        candidate.capability.supportsRead = boolean(root, "supportsRead");
        candidate.capability.supportsMetadata = boolean(root, "supportsMetadata");
        candidate.capability.supportsEnumeration = boolean(root, "supportsEnumeration");
        candidate.capability.supportsContentVersion = boolean(root, "supportsContentVersion");
        const auto authorized = boolean(root, "writeAuthorized");
        candidate.writeAuthorized = authorized;
        const auto requestBytes = Wire::number(root, "maxRequestBytes");
        const auto responseBytes = Wire::number(root, "maxResponseBytes");
        Wire::require(requestBytes <= SIZE_MAX && responseBytes <= SIZE_MAX);
        candidate.requestBytes = static_cast<std::size_t>(requestBytes);
        candidate.responseBytes = static_cast<std::size_t>(responseBytes);
        candidate.save = {boolean(root, "observedBaselineReplace"),
            boolean(root, "strictConditionalReplace"),
            true};
        Wire::require(authorized
            || (!candidate.save.observedBaselineReplace
                && !candidate.save.strictConditionalReplace));
        candidate.sessionVersion =
            Wire::sessionVersion(owner->desc.endpoint, owner->desc.workspaceId, candidate.session);
        owner->remote = std::move(candidate);
        owner->connected = true;
        connectionResult.status = FileIoStatus::Success;
        connectionResult.capability = owner->remote.capability;
        connectionResult.saveCapability = owner->remote.save;
    }

    FileIoStatus remoteStatus(const Value& root)
    {
        const auto status = Wire::parseStatus(Wire::text(root, "ioStatus"));
        Wire::require(status != FileIoStatus::Accepted);
        if (core::json::jsonMember(root, "diagnostic"))
            (void)Wire::text(root, "diagnostic");
        return status;
    }

    void decodeRead(const Value& root)
    {
        keys(root,
            {"type",
                "contractVersion",
                "workspace",
                "session",
                "receiptScope",
                "status",
                "ioStatus",
                "diagnostic",
                "version",
                "bytes",
                "data"});
        envelope(root);
        Wire::require(Wire::text(root, "status") == "terminal");
        readResult.status = remoteStatus(root);
        readResult.version = Wire::parseVersion(Wire::member(root, "version"));
        if (readResult.status != FileIoStatus::Success) {
            Wire::require(!core::json::jsonMember(root, "data")
                && !core::json::jsonMember(root, "bytes")
                && readResult.version.kind == FileVersionKind::None);
            readResult.diagnosticId = "IO-WORKSPACE-REMOTE-READ";
            if (const auto* reason = core::json::jsonMember(root, "diagnostic"))
                readResult.diagnostics = core::json::jsonText(*reason);
            return;
        }
        payload(root, read.maxBytes, readResult.bytes);
        if (read.range.mode == FileReadRangeMode::ExactRange)
            Wire::require(readResult.bytes.size() == read.range.size);
        if (read.expectedVersion.kind != FileVersionKind::None)
            Wire::require(read.expectedVersion == readResult.version);
        if (read.range.mode == FileReadRangeMode::WholeFile
            && readResult.version.kind == FileVersionKind::ContentSha256)
            Wire::require(
                Wire::requestVersion({reinterpret_cast<const char*>(readResult.bytes.data()),
                    readResult.bytes.size()})
                == readResult.version);
    }

    void decodeQuery(const Value& root)
    {
        keys(root,
            {"type",
                "contractVersion",
                "workspace",
                "session",
                "receiptScope",
                "status",
                "ioStatus",
                "diagnostic",
                "metadata",
                "entries",
                "totalEntries",
                "next"});
        envelope(root);
        Wire::require(Wire::text(root, "status") == "terminal");
        queryResult.status = remoteStatus(root);
        if (queryResult.status != FileIoStatus::Success) {
            Wire::require(!core::json::jsonMember(root, "metadata")
                && !core::json::jsonMember(root, "entries") && !core::json::jsonMember(root, "next")
                && !core::json::jsonMember(root, "totalEntries"));
            queryResult.diagnosticId = "IO-WORKSPACE-REMOTE-QUERY";
            return;
        }
        queryResult.metadata = Wire::parseMetadata(Wire::member(root, "metadata"));
        const auto& entries = Wire::member(root, "entries");
        Wire::require(entries.IsArray() && entries.Size() <= query.pageSize);
        queryResult.totalEntries = Wire::number(root, "totalEntries");
        const auto& next = Wire::member(root, "next");
        keys(next, {"offset", "version"});
        queryResult.next = {
            Wire::parseVersion(Wire::member(next, "version")), Wire::number(next, "offset")};
        std::uint64_t names = 0;
        for (const auto& item : entries.GetArray()) {
            FileDirectoryEntry entry;
            const auto name = Wire::text(item, "name");
            Wire::require(FilePath::isSafeRelativePath(name, owner->desc.maxPathBytes)
                && name.find('/') == name.npos && name.find('\\') == name.npos
                && name.find('\0') == name.npos && name.size() <= query.maxScanNameBytes - names);
            names += name.size();
            Wire::require(queryResult.entries.empty() || queryResult.entries.back().name < name);
            entry.name = name;
            entry.metadata = Wire::parseMetadata(item, true);
            queryResult.entries.push_back(std::move(entry));
        }
        if (query.operation == FileQueryOperation::Stat) {
            Wire::require(queryResult.totalEntries == 0 && queryResult.next.offset == 0
                && queryResult.next.snapshot.kind == FileVersionKind::None);
            if (query.versionPolicy == FileVersionPolicy::ContentSha256)
                Wire::require(queryResult.metadata.version.kind == FileVersionKind::ContentSha256);
        } else {
            Wire::require(queryResult.metadata.kind == FileEntryKind::Directory
                && queryResult.metadata.version.kind == FileVersionKind::DirectorySnapshotSha256
                && queryResult.totalEntries <= query.maxScanEntries
                && query.cursor.offset <= queryResult.totalEntries
                && entries.Size()
                    == std::min<std::uint64_t>(
                        query.pageSize, queryResult.totalEntries - query.cursor.offset));
            if (query.cursor.snapshot.kind != FileVersionKind::None)
                Wire::require(query.cursor.snapshot == queryResult.metadata.version);
            const auto after = query.cursor.offset + entries.Size();
            Wire::require(after < queryResult.totalEntries ? queryResult.next.offset == after
                        && queryResult.next.snapshot == queryResult.metadata.version
                                                           : queryResult.next.offset == 0
                        && queryResult.next.snapshot.kind == FileVersionKind::None);
        }
        Wire::require(FileQueryContract::resultBytes(queryResult) <= query.maxResultBytes);
    }

    void decodeReceipt(const Value& root, bool releasing)
    {
        keys(root,
            {"type",
                "contractVersion",
                "workspace",
                "session",
                "receiptScope",
                "status",
                "id",
                "requestVersion",
                "submitted",
                "ioStatus",
                "diagnostic",
                "deliveryStatus",
                "commit",
                "version"});
        envelope(root);
        Wire::require(Wire::number(root, "id") == ticket.operationId
            && Wire::parseVersion(Wire::member(root, "requestVersion")) == ticket.requestVersion);
        receiptResult.receipt = ticket;
        receiptResult.status = FileIoStatus::Success;
        const auto phase = Wire::text(root, "status");
        if (releasing) {
            keys(root,
                {"type",
                    "contractVersion",
                    "workspace",
                    "session",
                    "receiptScope",
                    "status",
                    "id",
                    "requestVersion"});
            Wire::require(phase == "forgotten" && receiver.httpStatus == 200);
            receiptResult.state = FileSaveReceiptState::Released;
        } else {
            Wire::require(phase == "terminal" || phase == "pending");
            const bool submitted = boolean(root, "submitted");
            if (phase == "pending") {
                keys(root,
                    {"type",
                        "contractVersion",
                        "workspace",
                        "session",
                        "receiptScope",
                        "status",
                        "id",
                        "requestVersion",
                        "submitted"});
                Wire::require(receiver.httpStatus == 202 && submitted);
                receiptResult.state = FileSaveReceiptState::Pending;
            } else {
                Wire::require(receiver.httpStatus == 200);
                receiptResult.state = FileSaveReceiptState::Terminal;
                auto& result = receiptResult.outcome;
                result.receipt = ticket;
                result.status = remoteStatus(root);
                result.deliveryStatus = Wire::parseStatus(Wire::text(root, "deliveryStatus"));
                const auto commit = Wire::text(root, "commit");
                Wire::require(
                    commit == "committed" || commit == "not-committed" || commit == "unknown");
                result.commitState = commit == "committed" ? FileCommitState::Committed
                    : commit == "not-committed"            ? FileCommitState::NotCommitted
                                                           : FileCommitState::OutcomeUnknown;
                result.committedVersion = Wire::parseVersion(Wire::member(root, "version"));
                if (result.commitState == FileCommitState::Committed)
                    Wire::require(result.committedVersion.kind == FileVersionKind::ContentSha256);
                if (core::json::jsonMember(root, "diagnostic"))
                    result.diagnosticId = "IO-WORKSPACE-REMOTE-SAVE";
            }
        }
        FileSaveReceiptRequest expected;
        expected.receipt = ticket;
        expected.operation =
            releasing ? FileSaveReceiptOperation::Release : FileSaveReceiptOperation::Query;
        Wire::require(!FileSaveReceiptContract::validateResult(expected, receiptResult).diagnostic);
        if (kind == Kind::Save) {
            Wire::require(receiptResult.state == FileSaveReceiptState::Terminal);
            saveResult = receiptResult.outcome;
            if (saveResult.commitState == FileCommitState::Committed)
                Wire::require(
                    saveResult.committedVersion == FileSaveContract::contentVersion(save));
        }
    }

    void fail(FileIoStatus status, const char* diagnostic)
    {
        connectionResult.status = status;
        connectionResult.diagnosticId = diagnostic;
        readResult = {};
        readResult.status = status;
        readResult.diagnosticId = diagnostic;
        queryResult.fail(status, diagnostic);
        saveResult = {};
        saveResult.status = FileIoStatus::OutcomeUnknown;
        saveResult.commitState = FileCommitState::OutcomeUnknown;
        saveResult.receipt = ticket;
        saveResult.diagnosticId = diagnostic;
        receiptResult = {};
        receiptResult.status = status;
        receiptResult.diagnosticId = diagnostic;
    }

    void decode()
    {
        if (!receiver.ended || receiver.failure != FileIoStatus::Success) {
            fail(receiver.failure == FileIoStatus::Success ? FileIoStatus::IntegrityMismatch
                                                           : receiver.failure,
                "IO-WORKSPACE-TRANSFER");
            return;
        }
        Json root;
        root.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(
            receiver.body.data(), receiver.body.size());
        Wire::require(!root.HasParseError() && root.IsObject());
        if (receiver.httpStatus >= 400) {
            keys(root, {"type", "contractVersion", "diagnostic"});
            Wire::require(Wire::text(root, "type") == "arp.workspace.response"
                && Wire::number(root, "contractVersion") == Wire::contractVersion);
            const auto diagnostic = Wire::text(root, "diagnostic");
            const auto status = receiver.httpStatus == 401 || receiver.httpStatus == 403
                ? FileIoStatus::PermissionDenied
                : receiver.httpStatus == 409 || receiver.httpStatus == 410
                ? FileIoStatus::StaleLocation
                : receiver.httpStatus == 429 ? FileIoStatus::BudgetExceeded
                : receiver.httpStatus == 503 ? FileIoStatus::BackendUnavailable
                                             : FileIoStatus::InvalidRequest;
            fail(status, "IO-WORKSPACE-REMOTE-REJECTION");
            if (kind == Kind::Save
                && (diagnostic == "WORKSPACE-AUTH-DENIED" || diagnostic == "WORKSPACE-HOST-DENIED"
                    || diagnostic == "WORKSPACE-ORIGIN-DENIED"
                    || diagnostic == "WORKSPACE-WRITE-DENIED")) {
                saveResult.status = status;
                saveResult.commitState = FileCommitState::NotCommitted;
            }
            if (diagnostic == "WORKSPACE-SESSION-MISMATCH")
                owner->connected = false;
            return;
        }
        Wire::require(receiver.httpStatus == 200 || receiver.httpStatus == 202);
        switch (kind) {
        case Kind::Connect:
            Wire::require(receiver.httpStatus == 200);
            connect(root);
            break;
        case Kind::Read:
            Wire::require(receiver.httpStatus == 200);
            decodeRead(root);
            break;
        case Kind::Query:
            Wire::require(receiver.httpStatus == 200);
            decodeQuery(root);
            break;
        case Kind::Save:
            decodeReceipt(root, false);
            break;
        case Kind::Receipt:
            decodeReceipt(root, receipt.operation == FileSaveReceiptOperation::Release);
            break;
        }
    }
};

WorkspaceFileBackend::WorkspaceFileBackend(WorkspaceFileBackendDesc desc)
    : state_(core::makeShared<State>(std::move(desc)))
{
    auto& s = *state_;
    const auto& d = s.desc;
    try {
        Wire::require(d.contractVersion == 1 && Wire::identifier(d.workspaceId) && d.maxPathBytes
            && d.maxFileBytes && d.maxFileBytes <= INT64_MAX && d.maxRequestBytes
            && d.maxRequestBytes <= INT32_MAX && d.maxResponseBytes
            && d.maxResponseBytes <= INT32_MAX && d.preparationBytes && d.maxQueryBytes
            && d.maxQueryBytes <= SIZE_MAX && d.maxDirectoryEntries && d.maxDirectoryNameBytes
            && d.maxDirectoryNameBytes <= SIZE_MAX && d.endpoint.size() <= d.maxPathBytes
            && d.credentialOrigin.size() <= d.maxPathBytes
            && d.endpoint == d.credentialOrigin + Wire::endpointPath);
        network::http::ByteReadRequest probe;
        probe.url = d.endpoint;
        probe.contractVersion = 2;
        probe.replayProtected = true;
        probe.operation = network::http::ByteReadRequest::Operation::BearerJsonPost;
        probe.credentialOrigin = d.credentialOrigin;
        probe.bearerToken = d.token;
        probe.requestBody = "{}";
        probe.maxRequestBytes = d.maxRequestBytes;
        probe.maxBodyBytes = d.maxResponseBytes;
        const network::http::ByteReadDesc transport{
            d.maxHeaderBytes, d.ioChunkBytes, d.timeoutMs, d.cancellationPollMs, d.maxDnsAddresses};
        Wire::require(network::http::ByteReadTransport::validate(transport, probe)
            == network::http::ByteReadStatus::Success);
        s.executor = core::makeUnique<network::http::ByteTransferExecutor>(
            network::http::ByteTransferExecutorDesc{transport, 1});
    } catch (...) {
        s.closed = true;
        s.diagnostic = "IO-WORKSPACE-DESCRIPTOR";
    }
}

WorkspaceFileBackend::~WorkspaceFileBackend()
{
    shutdown();
}

bool WorkspaceFileBackend::connected() const noexcept
{
    return state_->connected && !state_->closed;
}

const char* WorkspaceFileBackend::diagnostic() const noexcept
{
    return state_->diagnostic;
}

FileBackendCapability WorkspaceFileBackend::capability() const noexcept
{
    if (connected())
        return state_->remote.capability;
    FileBackendCapability result;
    result.kind = FileBackendKind::WorkspaceFileBackend;
    result.status = FileBackendStatus::PrototypeComplete;
    result.completesThroughCompletionQueue = true;
    return result;
}

FileSaveCapability WorkspaceFileBackend::saveCapability(core::PoolHandle) const noexcept
{
    return connected() ? state_->remote.save : FileSaveCapability{};
}

FileIoStatus WorkspaceFileBackend::admit(runtime::CompletionQueue& queue) const noexcept
{
    if (state_->closed)
        return FileIoStatus::ServiceClosed;
    if (!network::http::ByteTransferExecutor::supported())
        return FileIoStatus::Unsupported;
    if (state_->queue && state_->queue != &queue)
        return FileIoStatus::InvalidRequest;
    return state_->busy ? FileIoStatus::QueueFull : FileIoStatus::Success;
}

FileIoStatus WorkspaceFileBackend::submit(
    core::UniquePtr<Pending> pending, runtime::CompletionQueue& queue) noexcept
{
    try {
        const auto status = state_->executor->submit(
            pending->transfer, pending->receiver, deliver, pending.get(), queue, pending->quota);
        if (status != network::http::ByteReadStatus::Accepted)
            return detail::httpFileStatus(status);
        state_->busy = true;
        state_->queue = &queue;
        if (pending->kind == Pending::Kind::Save)
            state_->remote.lastId = pending->ticket.operationId;
        pending.release();
        return FileIoStatus::Accepted;
    } catch (...) {
        return FileIoStatus::BudgetExceeded;
    }
}

FileIoStatus WorkspaceFileBackend::connectAsync(WorkspaceFileConnectionCompletion completion,
    runtime::CompletionQueue& queue,
    runtime::CancellationToken token) noexcept
{
    const auto admission = admit(queue);
    if (admission != FileIoStatus::Success)
        return admission;
    if (!completion.valid() || connected())
        return FileIoStatus::InvalidRequest;
    if (token.cancelled())
        return FileIoStatus::Cancelled;
    try {
        auto p = core::makeUnique<Pending>(state_, Pending::Kind::Connect, token);
        core::CpuMemoryQuotaScope quota(p->quota);
        p->connected = completion;
        auto root = p->request("info");
        p->encode(root);
        return submit(std::move(p), queue);
    } catch (...) {
        return FileIoStatus::BudgetExceeded;
    }
}

FileReadResult WorkspaceFileBackend::validateRead(const FileReadRequest& request) const
{
    FileReadResult result;
    result.backendKind = FileBackendKind::WorkspaceFileBackend;
    const auto validation = FileReadContract::validate(request);
    result.status = validation.status;
    result.diagnosticId = validation.diagnostic;
    if (validation.diagnostic)
        return result;
    if (state_->closed)
        result.status = FileIoStatus::ServiceClosed;
    else if (!connected())
        result.status = FileIoStatus::BackendUnavailable;
    else if (!capability().supportsRead
        || request.partialPolicy != FilePartialReadPolicy::DenyPartial)
        result.status = FileIoStatus::Unsupported;
    else if (request.contractVersion != 1 || request.pathScope != FilePathScope::MountedResource
        || !FilePath::isSafeRelativePath(request.path, state_->desc.maxPathBytes))
        result.status = FileIoStatus::InvalidRequest;
    else if (request.maxBytes > std::min(state_->desc.maxFileBytes, state_->remote.fileBytes)
        || request.maxVersionBytes > std::min(state_->desc.maxFileBytes, state_->remote.fileBytes))
        result.status = FileIoStatus::BudgetExceeded;
    else if (request.range.offset > INT64_MAX || request.range.size > INT64_MAX)
        result.status = FileIoStatus::InvalidRange;
    else if (request.expectedVersion.kind != FileVersionKind::None
        && !capability().supportsContentVersion)
        result.status = FileIoStatus::Unsupported;
    if (result.status != FileIoStatus::Success)
        result.diagnosticId = "IO-WORKSPACE-READ-ADMISSION";
    return result;
}

FileIoStatus WorkspaceFileBackend::readAsync(const FileReadRequest& request,
    FileReadCompletion completion,
    runtime::CompletionQueue& queue) noexcept
{
    const auto admission = admit(queue);
    if (admission != FileIoStatus::Success)
        return admission;
    if (!completion.valid())
        return FileIoStatus::InvalidRequest;
    try {
        const auto validation = validateRead(request);
        if (validation.status != FileIoStatus::Success)
            return validation.status;
        auto p = core::makeUnique<Pending>(state_, Pending::Kind::Read, request.cancellation);
        core::CpuMemoryQuotaScope quota(p->quota);
        p->read = request;
        p->readComplete = completion;
        auto root = p->request("read");
        auto& a = root.GetAllocator();
        Wire::field(root, "path", request.path, a);
        root.AddMember("maxBytes", request.maxBytes, a);
        root.AddMember("offset", request.range.offset, a);
        root.AddMember("size", request.range.size, a);
        root.AddMember("version", Wire::version(request.expectedVersion, a), a);
        root.AddMember("maxVersionBytes", request.maxVersionBytes, a);
        p->encode(root);
        return submit(std::move(p), queue);
    } catch (...) {
        return FileIoStatus::BudgetExceeded;
    }
}

FileIoStatus WorkspaceFileBackend::queryAsync(const FileQueryRequest& request,
    FileQueryCompletion completion,
    runtime::CompletionQueue& queue) noexcept
{
    const auto admission = admit(queue);
    if (admission != FileIoStatus::Success)
        return admission;
    if (!completion.valid())
        return FileIoStatus::InvalidRequest;
    if (!connected())
        return FileIoStatus::BackendUnavailable;
    const auto validation = FileQueryContract::validate(request);
    if (validation.diagnostic)
        return validation.status;
    const auto caps = capability();
    if ((request.operation == FileQueryOperation::Stat && !caps.supportsMetadata)
        || (request.operation == FileQueryOperation::Enumerate && !caps.supportsEnumeration)
        || (request.versionPolicy == FileVersionPolicy::ContentSha256
            && !caps.supportsContentVersion))
        return FileIoStatus::Unsupported;
    const auto& d = state_->desc;
    const auto& remote = state_->remote;
    if (request.path.size() > d.maxPathBytes
        || request.maxResultBytes > std::min(d.maxQueryBytes, remote.queryBytes)
        || request.maxVersionBytes > std::min(d.maxFileBytes, remote.fileBytes)
        || request.maxScanEntries > std::min(d.maxDirectoryEntries, remote.entries)
        || request.maxScanNameBytes > std::min(d.maxDirectoryNameBytes, remote.nameBytes))
        return FileIoStatus::BudgetExceeded;
    if (request.cursor.offset > INT64_MAX)
        return FileIoStatus::InvalidRange;
    try {
        auto p = core::makeUnique<Pending>(state_, Pending::Kind::Query, request.cancellation);
        core::CpuMemoryQuotaScope quota(p->quota);
        p->query = request;
        p->queryComplete = completion;
        const bool list = request.operation == FileQueryOperation::Enumerate;
        auto root = p->request(list ? "list" : "stat");
        auto& a = root.GetAllocator();
        Wire::field(root, "path", request.path, a);
        root.AddMember("maxResultBytes", request.maxResultBytes, a);
        if (list) {
            root.AddMember("pageSize", request.pageSize, a);
            root.AddMember("offset", request.cursor.offset, a);
            root.AddMember("version", Wire::version(request.cursor.snapshot, a), a);
            root.AddMember("maxScanEntries", request.maxScanEntries, a);
            root.AddMember("maxScanNameBytes", request.maxScanNameBytes, a);
        } else {
            root.AddMember(
                "contentVersion", request.versionPolicy == FileVersionPolicy::ContentSha256, a);
            root.AddMember("maxVersionBytes", request.maxVersionBytes, a);
        }
        p->encode(root);
        return submit(std::move(p), queue);
    } catch (...) {
        return FileIoStatus::BudgetExceeded;
    }
}

FileIoStatus WorkspaceFileBackend::saveAsync(FileSaveRequest request,
    FileSaveCompletion completion,
    runtime::CompletionQueue& queue) noexcept
{
    const auto admission = admit(queue);
    if (admission != FileIoStatus::Success)
        return admission;
    if (!completion.valid())
        return FileIoStatus::InvalidRequest;
    if (!connected())
        return FileIoStatus::BackendUnavailable;
    const auto validation = FileSaveContract::validate(request);
    if (validation.diagnostic)
        return validation.status;
    if (!saveCapability().observedBaselineReplace)
        return FileIoStatus::Unsupported;
    if (request.path.size() > state_->desc.maxPathBytes
        || request.maxWriteBytes > std::min(state_->desc.maxFileBytes, state_->remote.fileBytes)
        || request.maxBaselineBytes > std::min(state_->desc.maxFileBytes, state_->remote.fileBytes))
        return FileIoStatus::BudgetExceeded;
    if (state_->remote.lastId >= INT64_MAX)
        return FileIoStatus::BudgetExceeded;
    try {
        auto p = core::makeUnique<Pending>(state_, Pending::Kind::Save, request.cancellation);
        core::CpuMemoryQuotaScope quota(p->quota);
        p->save = std::move(request);
        p->saveComplete = completion;
        auto root = p->request("replace");
        auto& a = root.GetAllocator();
        p->ticket.scope = state_->remote.scope;
        p->ticket.operationId = state_->remote.lastId + 1;
        p->ticket.sessionVersion = state_->remote.sessionVersion;
        root.AddMember("id", p->ticket.operationId, a);
        Wire::field(root, "path", p->save.path, a);
        Wire::field(root,
            "precondition",
            p->save.precondition == FileSavePrecondition::MustBeMissing ? "missing"
                                                                        : "content-sha256",
            a);
        root.AddMember("version", Wire::version(p->save.expectedVersion, a), a);
        root.AddMember("bytes", std::uint64_t(p->save.bytes.size()), a);
        root.AddMember("maxWriteBytes", p->save.maxWriteBytes, a);
        root.AddMember("maxBaselineBytes", p->save.maxBaselineBytes, a);
        Wire::field(root, "data", core::Base64::encode(p->save.bytes), a);
        p->encode(root);
        p->ticket.requestVersion = Wire::requestVersion(p->transfer.requestBody);
        return submit(std::move(p), queue);
    } catch (...) {
        return FileIoStatus::BudgetExceeded;
    }
}

FileIoStatus WorkspaceFileBackend::receiptAsync(const FileSaveReceiptRequest& request,
    FileSaveReceiptCompletion completion,
    runtime::CompletionQueue& queue) noexcept
{
    const auto admission = admit(queue);
    if (admission != FileIoStatus::Success)
        return admission;
    if (!completion.valid())
        return FileIoStatus::InvalidRequest;
    if (!connected())
        return FileIoStatus::BackendUnavailable;
    const auto validation = FileSaveReceiptContract::validate(request);
    if (validation.diagnostic)
        return validation.status;
    if (request.receipt.scope != state_->remote.scope
        || request.receipt.sessionVersion != state_->remote.sessionVersion)
        return FileIoStatus::StaleLocation;
    if (request.receipt.operationId > INT64_MAX)
        return FileIoStatus::InvalidRequest;
    if (request.operation == FileSaveReceiptOperation::Release && !state_->remote.writeAuthorized)
        return FileIoStatus::PermissionDenied;
    try {
        auto p = core::makeUnique<Pending>(state_, Pending::Kind::Receipt, request.cancellation);
        core::CpuMemoryQuotaScope quota(p->quota);
        p->receipt = request;
        p->ticket = request.receipt;
        p->receiptComplete = completion;
        auto root = p->request(
            request.operation == FileSaveReceiptOperation::Release ? "forget" : "receipt");
        auto& a = root.GetAllocator();
        root.AddMember("id", p->ticket.operationId, a);
        root.AddMember("receiptScope", p->ticket.scope, a);
        root.AddMember("requestVersion", Wire::version(p->ticket.requestVersion, a), a);
        p->encode(root);
        return submit(std::move(p), queue);
    } catch (...) {
        return FileIoStatus::BudgetExceeded;
    }
}

void WorkspaceFileBackend::deliver(void* value)
{
    core::UniquePtr<Pending> p{static_cast<Pending*>(value),
        core::CpuAllocatedDeleter<Pending>{value, &core::destroyCpuAllocatedUnique<Pending>}};
    auto owner = p->owner;
    owner->received += p->receiver.body.size();
    try {
        core::CpuMemoryQuotaScope quota(p->quota);
        p->decode();
    } catch (const std::bad_alloc&) {
        p->fail(FileIoStatus::BudgetExceeded, "IO-WORKSPACE-DECODE-BUDGET");
    } catch (...) {
        p->fail(FileIoStatus::IntegrityMismatch, "IO-WORKSPACE-RESPONSE-CONTRACT");
    }
    const auto delivery = p->token.cancelled() ? FileIoStatus::Cancelled
        : owner->closed                        ? FileIoStatus::ServiceClosed
                                               : FileIoStatus::Success;
    owner->busy = false;
    if (p->kind == Pending::Kind::Connect) {
        if (delivery != FileIoStatus::Success) {
            owner->connected = false;
            p->connectionResult = {};
            p->connectionResult.status = delivery;
            p->connectionResult.diagnosticId = "IO-WORKSPACE-CONNECT-TERMINATED";
        }
        const auto callback = p->connected;
        auto result = p->connectionResult;
        p.reset();
        callback.complete(callback.userData, result);
    } else if (p->kind == Pending::Kind::Read) {
        if (delivery != FileIoStatus::Success) {
            p->readResult = {};
            p->readResult.status = delivery;
            p->readResult.diagnosticId = "IO-WORKSPACE-READ-TERMINATED";
        }
        p->readResult.backendKind = FileBackendKind::WorkspaceFileBackend;
        const auto callback = p->readComplete;
        auto result = std::move(p->readResult);
        p.reset();
        callback.complete(callback.userData, std::move(result));
    } else if (p->kind == Pending::Kind::Query) {
        if (delivery != FileIoStatus::Success)
            p->queryResult.fail(delivery, "IO-WORKSPACE-QUERY-TERMINATED");
        p->queryResult.backendKind = FileBackendKind::WorkspaceFileBackend;
        const auto callback = p->queryComplete;
        auto result = std::move(p->queryResult);
        p.reset();
        callback.complete(callback.userData, std::move(result));
    } else if (p->kind == Pending::Kind::Save) {
        if (delivery != FileIoStatus::Success)
            p->saveResult.deliveryStatus = delivery;
        p->saveResult.backendKind = FileBackendKind::WorkspaceFileBackend;
        const auto callback = p->saveComplete;
        auto result = p->saveResult;
        p.reset();
        callback.complete(callback.userData, result);
    } else {
        if (delivery != FileIoStatus::Success)
            p->receiptResult.deliveryStatus = delivery;
        p->receiptResult.outcome.backendKind = FileBackendKind::WorkspaceFileBackend;
        const auto callback = p->receiptComplete;
        auto result = p->receiptResult;
        p.reset();
        callback.complete(callback.userData, result);
    }
}

void WorkspaceFileBackend::shutdown() noexcept
{
    if (state_->closed.exchange(true))
        return;
    state_->connected = false;
    if (state_->executor)
        state_->executor->shutdown();
}

WorkspaceFileBackendTelemetry WorkspaceFileBackend::telemetry() const noexcept
{
    return {state_->busy ? 1u : 0u,
        state_->executor ? state_->executor->telemetry().activeNativeTransfers : 0,
        state_->received};
}
} // namespace arp::core::io
