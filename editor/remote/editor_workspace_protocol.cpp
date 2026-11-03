#include "editor/remote/editor_workspace_protocol.h"

#include "core/encoding/base64.h"
#include "core/encoding/json_document.h"
#include "core/io/file_query_contract.h"
#include "core/io/workspace_file_wire.h"
#include "editor/remote/editor_http_access.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <stdexcept>

namespace arp::editor {
namespace {
using namespace core::io;
using core::json::JsonDocument;
using core::json::JsonValue;
using HttpResponse = network::http::Response;
using HttpRequest = network::http::Request;

using Wire = core::io::WorkspaceFileWire;
using Invalid = core::io::WorkspaceFileWireError;

bool identifier(std::string_view value)
{
    if (value.empty() || value.size() > 64)
        return false;
    for (const auto c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
                || c == '_'))
            return false;
    return true;
}

bool token(std::string_view value)
{
    return value.size() >= 32 && value.size() <= 256;
}

HttpResponse error(unsigned status, const char* diagnostic)
{
    HttpResponse response;
    response.set_status(network::http::StatusCode(status));
    response.set_content_type(network::http::ContentType::Json);
    response.add_header("Cache-Control", "no-store");
    response.set_body_text(
        core::pmr::String(
            "{\"type\":\"arp.workspace.response\",\"contractVersion\":2,\"diagnostic\":\"")
        + diagnostic + "\"}");
    return response;
}

void deliver(EditorWorkspaceProtocol::Completion& sink, HttpResponse response)
{
    auto callback = std::move(sink);
    callback(std::move(response));
}

unsigned errorStatus(std::string_view diagnostic)
{
    if (diagnostic == "WORKSPACE-AUTH-DENIED")
        return 401;
    if (diagnostic == "WORKSPACE-HOST-DENIED" || diagnostic == "WORKSPACE-ORIGIN-DENIED"
        || diagnostic == "WORKSPACE-WRITE-DENIED")
        return 403;
    if (diagnostic == "WORKSPACE-BUSY" || diagnostic == "WORKSPACE-CLOSED")
        return 503;
    if (diagnostic == "WORKSPACE-SESSION-MISMATCH" || diagnostic == "WORKSPACE-REQUEST-ID-CONFLICT"
        || diagnostic == "WORKSPACE-REQUEST-ID-RETIRED")
        return 409;
    if (diagnostic == "WORKSPACE-ID-MISMATCH")
        return 404;
    if (diagnostic == "WORKSPACE-RECEIPT-NOT-RETAINED")
        return 410;
    return 400;
}
} // namespace

struct EditorWorkspaceProtocol::State {
    struct Receipt {
        std::uint64_t id = 0;
        FileVersionToken requestVersion;
        FileSaveResult result;
        bool submitted = false, terminal = false;
    };

    struct Pending {
        core::runtime::CancellationSource cancellation;
        Completion completion;
        HttpResponse fallback;
        core::PoolHandle receipt;
        bool cors = false;

        Pending(
            Completion&& sink, HttpResponse&& errorResponse, core::PoolHandle record, bool origin)
            : completion(std::move(sink)), fallback(std::move(errorResponse)), receipt(record),
              cors(origin)
        {}
    };

    core::io::FileService* files;
    EditorWorkspaceProtocolDesc desc;
    core::GenerationPool<Receipt> receipts;
    core::UniquePtr<Pending> pending;
    std::uint64_t lastId = 0;
    std::uint64_t saveSubmissions = 0, confirmedCommits = 0;
    std::uint64_t receiptScope = FileQueryContract::acquireBackendScope();
    bool closed = false;
    const char* diagnostic = nullptr;

    State(FileService& service, EditorWorkspaceProtocolDesc config)
        : files(&service), desc(std::move(config)), receipts(desc.maxReceipts)
    {}

    JsonDocument response(const char* status)
    {
        JsonDocument node;
        node.SetObject();
        auto& a = node.GetAllocator();
        Wire::field(node, "type", "arp.workspace.response", a);
        node.AddMember("contractVersion", Wire::contractVersion, a);
        Wire::field(node, "workspace", desc.workspaceId, a);
        Wire::field(node, "session", desc.sessionId, a);
        node.AddMember("receiptScope", receiptScope, a);
        Wire::field(node, "status", status, a);
        return node;
    }

    HttpResponse encode(JsonDocument& node, bool cors, unsigned status = 200)
    {
        rapidjson::GenericStringBuffer<rapidjson::UTF8<>, core::json::CoreJsonAllocator> buffer(
            &core::json::jsonAllocator());
        rapidjson::Writer<decltype(buffer),
            rapidjson::UTF8<>,
            rapidjson::UTF8<>,
            core::json::CoreJsonAllocator>
            writer(buffer, &core::json::jsonAllocator());
        Wire::require(node.Accept(writer) && buffer.GetSize() <= desc.maxResponseBytes,
            "WORKSPACE-RESPONSE-BUDGET");
        HttpResponse result;
        result.set_status(network::http::StatusCode(status));
        result.set_content_type(network::http::ContentType::Json);
        result.add_header("Cache-Control", "no-store");
        if (cors)
            result.add_header("Access-Control-Allow-Origin", desc.allowedOrigin);
        result.set_body_text({buffer.GetString(), buffer.GetSize()});
        return result;
    }

    void resultFields(JsonDocument& node, FileIoStatus status, const char* diagnostic)
    {
        auto& a = node.GetAllocator();
        Wire::field(node, "ioStatus", Wire::statusName(status), a);
        if (diagnostic)
            Wire::field(node, "diagnostic", diagnostic, a);
    }

    HttpResponse receiptResponse(const Receipt& receipt, bool cors)
    {
        auto node = response(receipt.terminal ? "terminal" : "pending");
        auto& a = node.GetAllocator();
        node.AddMember("id", receipt.id, a);
        node.AddMember("requestVersion", Wire::version(receipt.requestVersion, a), a);
        node.AddMember("submitted", receipt.submitted, a);
        if (receipt.terminal) {
            resultFields(node, receipt.result.status, receipt.result.diagnosticId);
            Wire::field(node, "deliveryStatus", Wire::statusName(receipt.result.deliveryStatus), a);
            Wire::field(node,
                "commit",
                receipt.result.commitState == FileCommitState::Committed          ? "committed"
                    : receipt.result.commitState == FileCommitState::NotCommitted ? "not-committed"
                                                                                  : "unknown",
                a);
            node.AddMember("version", Wire::version(receipt.result.committedVersion, a), a);
        }
        return encode(node, cors, receipt.terminal ? 200 : 202);
    }

    core::PoolHandle find(std::uint64_t id)
    {
        core::PoolHandle handle;
        receipts.forEach([&](core::PoolHandle h, const Receipt& r) {
            if (r.id == id)
                handle = h;
        });
        return handle;
    }

    template <class Build> void finish(Build&& build) noexcept
    {
        auto operation = std::move(pending);
        HttpResponse reply;
        try {
            core::CpuMemoryQuotaScope quota(
                core::makeShared<core::CpuMemoryQuota>(desc.preparationBytes));
            reply = build(*operation);
        } catch (...) {
            reply = std::move(operation->fallback);
        }
        try {
            operation->completion(std::move(reply));
        } catch (...) {
        }
    }

    static core::UniquePtr<core::SharedPtr<State>> cookie(void* value)
    {
        using Retained = core::SharedPtr<State>;
        return {static_cast<Retained*>(value),
            core::CpuAllocatedDeleter<Retained>{value, &core::destroyCpuAllocatedUnique<Retained>}};
    }

    void readDone(FileReadResult result) noexcept
    {
        finish([&](const Pending& operation) {
            auto node = response("terminal");
            resultFields(node, result.status, result.diagnosticId);
            auto& a = node.GetAllocator();
            node.AddMember("version", Wire::version(result.version, a), a);
            if (result.status == FileIoStatus::Success) {
                Wire::require(
                    result.bytes.size() <= desc.maxFileBytes, "WORKSPACE-READ-RESULT-BUDGET");
                Wire::field(node, "data", core::Base64::encode(result.bytes), a);
                node.AddMember("bytes", std::uint64_t(result.bytes.size()), a);
            }
            return encode(node, operation.cors);
        });
    }

    void queryDone(FileQueryResult result) noexcept
    {
        finish([&](const Pending& operation) {
            auto node = response("terminal");
            resultFields(node, result.status, result.diagnosticId);
            auto& a = node.GetAllocator();
            if (result.status == FileIoStatus::Success) {
                Wire::require(result.entries.size() <= desc.maxDirectoryEntries
                        && FileQueryContract::resultBytes(result) <= desc.maxQueryBytes,
                    "WORKSPACE-QUERY-RESULT-BUDGET");
                node.AddMember("metadata", Wire::metadata(result.metadata, a), a);
                JsonValue entries(rapidjson::kArrayType);
                for (const auto& entry : result.entries) {
                    auto item = Wire::metadata(entry.metadata, a);
                    Wire::field(item, "name", entry.name, a);
                    entries.PushBack(item, a);
                }
                node.AddMember("entries", entries, a);
                node.AddMember("totalEntries", result.totalEntries, a);
                JsonValue cursor(rapidjson::kObjectType);
                cursor.AddMember("offset", result.next.offset, a);
                cursor.AddMember("version", Wire::version(result.next.snapshot, a), a);
                node.AddMember("next", cursor, a);
            }
            return encode(node, operation.cors);
        });
    }

    void saveDone(FileSaveResult result) noexcept
    {
        if (result.commitState == FileCommitState::Committed)
            ++confirmedCommits;
        auto* receipt = receipts.get(pending->receipt);
        receipt->result = result;
        receipt->terminal = true;
        finish([&](const Pending& operation) {
            return receiptResponse(*receipt, operation.cors);
        });
    }
};

EditorWorkspaceProtocol::EditorWorkspaceProtocol(
    FileService& files, EditorWorkspaceProtocolDesc desc)
{
    const bool receiptBudget = desc.maxReceipts && desc.maxReceipts <= 4096;
    if (!receiptBudget)
        desc.maxReceipts = 0;
    state_ = core::makeShared<State>(files, std::move(desc));
    const auto& d = state_->desc;
    if (!receiptBudget || !state_->receiptScope || state_->receiptScope > INT64_MAX || !d.mount
        || !identifier(d.workspaceId) || !identifier(d.sessionId) || !token(d.readToken)
        || (!d.writeToken.empty() && (!token(d.writeToken) || d.readToken == d.writeToken))
        || d.expectedHost.empty() || d.expectedHost.size() > 128 || d.allowedOrigin.size() > 512
        || network::http::validateHeaderValue(d.expectedHost) != network::http::DiagnosticCode::none
        || network::http::validateHeaderValue(d.allowedOrigin)
            != network::http::DiagnosticCode::none
        || !d.maxFileBytes || d.maxFileBytes > (SIZE_MAX - 2) / 4
        || d.maxRequestBytes < ((d.maxFileBytes + 2) / 3) * 4
        || d.maxResponseBytes < ((d.maxFileBytes + 2) / 3) * 4 || !d.preparationBytes
        || !d.maxQueryBytes || !d.maxDirectoryEntries || !d.maxDirectoryNameBytes) {
        state_->diagnostic = "WORKSPACE-DESCRIPTOR-INVALID";
        state_->closed = true;
    }
}

EditorWorkspaceProtocol::~EditorWorkspaceProtocol()
{
    shutdown();
}

bool EditorWorkspaceProtocol::valid() const noexcept
{
    return !state_->closed;
}

bool EditorWorkspaceProtocol::pending() const noexcept
{
    return bool(state_->pending);
}

const char* EditorWorkspaceProtocol::diagnostic() const noexcept
{
    return state_->diagnostic;
}

void EditorWorkspaceProtocol::shutdown() noexcept
{
    state_->closed = true;
    if (state_->pending)
        state_->pending->cancellation.cancel();
}

void EditorWorkspaceProtocol::handle(const HttpRequest& request, Completion completion)
{
    auto& s = *state_;
    if (!completion)
        return;
    bool ownsPending = false;
    bool corsAllowed = false;
    try {
        core::CpuMemoryQuotaScope quota(
            core::makeShared<core::CpuMemoryQuota>(s.desc.preparationBytes));
        Wire::require(!s.closed, "WORKSPACE-CLOSED");
        Wire::require(
            request.headers().count("Host") == 1 && request.header("Host") == s.desc.expectedHost,
            "WORKSPACE-HOST-DENIED");
        Wire::require(request.headers().count("Origin") <= 1, "WORKSPACE-ORIGIN-DENIED");
        const bool cors = request.headers().count("Origin") == 1;
        Wire::require(!cors
                || (!s.desc.allowedOrigin.empty()
                    && request.header("Origin") == s.desc.allowedOrigin),
            "WORKSPACE-ORIGIN-DENIED");
        corsAllowed = cors;
        Wire::require(request.target() == Wire::endpointPath, "WORKSPACE-ROUTE");
        if (request.method() == network::http::Method::Options) {
            Wire::require(cors && request.body().empty()
                    && request.header("Access-Control-Request-Method") == "POST",
                "WORKSPACE-PREFLIGHT");
            HttpResponse response;
            response.set_status(network::http::StatusCode(204));
            response.add_header("Access-Control-Allow-Origin", s.desc.allowedOrigin);
            response.add_header("Access-Control-Allow-Methods", "POST");
            response.add_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
            deliver(completion, std::move(response));
            return;
        }
        Wire::require(request.method() == network::http::Method::Post, "WORKSPACE-METHOD");
        Wire::require(request.headers().count("Authorization") == 1, "WORKSPACE-AUTH-DENIED");
        const auto authorization = request.header("Authorization");
        Wire::require(authorization.size() > 7 && authorization.substr(0, 7) == "Bearer "
                && authorization.size() <= 263,
            "WORKSPACE-AUTH-DENIED");
        const auto credential = authorization.substr(7);
        const bool write =
            !s.desc.writeToken.empty() && detail::constantTimeEquals(credential, s.desc.writeToken);
        Wire::require(write || detail::constantTimeEquals(credential, s.desc.readToken),
            "WORKSPACE-AUTH-DENIED");
        Wire::require(request.headers().count("Content-Type") == 1
                && request.header("Content-Type") == "application/json" && !request.body().empty()
                && request.body().size() <= s.desc.maxRequestBytes,
            "WORKSPACE-REQUEST-BUDGET-OR-TYPE");
        JsonDocument root;
        root.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(
            request.body_text().data(), request.body_text().size());
        Wire::require(!root.HasParseError() && root.IsObject());
        Wire::require(Wire::text(root, "type") == "arp.workspace.request"
            && Wire::number(root, "contractVersion") == Wire::contractVersion);
        Wire::require(Wire::text(root, "workspace") == s.desc.workspaceId, "WORKSPACE-ID-MISMATCH");
        const auto op = Wire::text(root, "operation");
        if (op == "info") {
            Wire::require(core::json::jsonOnlyUniqueKeys(
                root, {"type", "contractVersion", "workspace", "operation"}));
            auto node = s.response("ready");
            auto& a = node.GetAllocator();
            node.AddMember("writeAuthorized", write, a);
            const auto location = s.files->capability(s.desc.mount);
            node.AddMember("supportsRead", location.supportsRead, a);
            node.AddMember("supportsMetadata", location.supportsMetadata, a);
            node.AddMember("supportsEnumeration", location.supportsEnumeration, a);
            node.AddMember("supportsContentVersion", location.supportsContentVersion, a);
            const auto capability = s.files->saveCapability(s.desc.mount);
            node.AddMember(
                "observedBaselineReplace", write && capability.observedBaselineReplace, a);
            node.AddMember(
                "strictConditionalReplace", write && capability.strictConditionalReplace, a);
            node.AddMember("maxFileBytes", std::uint64_t(s.desc.maxFileBytes), a);
            node.AddMember("maxRequestBytes", std::uint64_t(s.desc.maxRequestBytes), a);
            node.AddMember("maxResponseBytes", std::uint64_t(s.desc.maxResponseBytes), a);
            node.AddMember("maxQueryBytes", std::uint64_t(s.desc.maxQueryBytes), a);
            node.AddMember("maxDirectoryEntries", s.desc.maxDirectoryEntries, a);
            node.AddMember("maxDirectoryNameBytes", std::uint64_t(s.desc.maxDirectoryNameBytes), a);
            node.AddMember("maxReceipts", s.desc.maxReceipts, a);
            node.AddMember("lastRequestId", s.lastId, a);
            node.AddMember("saveSubmissions", s.saveSubmissions, a);
            node.AddMember("confirmedCommits", s.confirmedCommits, a);
            Wire::field(node, "receiptLifetime", "server-session", a);
            deliver(completion, s.encode(node, cors));
            return;
        }
        Wire::require(
            Wire::text(root, "session") == s.desc.sessionId, "WORKSPACE-SESSION-MISMATCH");
        if (op == "receipt" || op == "forget") {
            Wire::require(core::json::jsonOnlyUniqueKeys(root,
                {"type",
                    "contractVersion",
                    "workspace",
                    "operation",
                    "session",
                    "id",
                    "receiptScope",
                    "requestVersion"}));
            const auto id = Wire::number(root, "id");
            const auto handle = s.find(id);
            auto* receipt = s.receipts.get(handle);
            Wire::require(receipt != nullptr, "WORKSPACE-RECEIPT-NOT-RETAINED");
            Wire::require(Wire::number(root, "receiptScope") == s.receiptScope
                    && Wire::parseVersion(Wire::member(root, "requestVersion"))
                        == receipt->requestVersion,
                "WORKSPACE-REQUEST-ID-CONFLICT");
            if (op == "forget") {
                Wire::require(write && receipt->terminal, "WORKSPACE-RECEIPT-NOT-RELEASABLE");
                auto node = s.response("forgotten");
                node.AddMember("id", id, node.GetAllocator());
                node.AddMember("requestVersion",
                    Wire::version(receipt->requestVersion, node.GetAllocator()),
                    node.GetAllocator());
                auto response = s.encode(node, cors);
                s.receipts.erase(handle);
                deliver(completion, std::move(response));
            } else
                deliver(completion, s.receiptResponse(*receipt, cors));
            return;
        }
        const bool saving = op == "replace";
        FileVersionToken requestVersion;
        std::uint64_t id = 0;
        if (saving) {
            Wire::require(write && s.files->saveCapability(s.desc.mount).observedBaselineReplace,
                "WORKSPACE-WRITE-DENIED");
            id = Wire::number(root, "id");
            requestVersion = Wire::requestVersion(request.body_text());
            if (auto* previous = s.receipts.get(s.find(id))) {
                Wire::require(
                    previous->requestVersion == requestVersion, "WORKSPACE-REQUEST-ID-CONFLICT");
                deliver(completion, s.receiptResponse(*previous, cors));
                return;
            }
            Wire::require(id > s.lastId, "WORKSPACE-REQUEST-ID-RETIRED");
        }
        Wire::require(!s.pending, "WORKSPACE-BUSY");
        auto fallback = error(503, "WORKSPACE-RESPONSE-PREPARATION");
        if (cors)
            fallback.add_header("Access-Control-Allow-Origin", s.desc.allowedOrigin);
        auto retained = core::makeUnique<core::SharedPtr<State>>(state_);
        if (op == "read") {
            Wire::require(core::json::jsonOnlyUniqueKeys(root,
                {"type",
                    "contractVersion",
                    "workspace",
                    "session",
                    "operation",
                    "path",
                    "maxBytes",
                    "offset",
                    "size",
                    "version",
                    "maxVersionBytes"}));
            FileReadRequest input;
            input.contractVersion = 1;
            input.pathScope = FilePathScope::MountedResource;
            input.mount = s.desc.mount;
            input.path = Wire::text(root, "path");
            input.maxBytes = Wire::number(root, "maxBytes");
            Wire::require(input.maxBytes && input.maxBytes <= s.desc.maxFileBytes);
            input.expectedVersion = Wire::parseVersion(Wire::member(root, "version"));
            input.maxVersionBytes = Wire::number(root, "maxVersionBytes");
            Wire::require(input.maxVersionBytes <= s.desc.maxFileBytes);
            const auto offset = Wire::number(root, "offset"), size = Wire::number(root, "size");
            input.range =
                size ? FileReadRange::exactRange(offset, size) : FileReadRange::wholeFile();
            Wire::require(size || offset == 0);
            s.pending = core::makeUnique<State::Pending>(
                std::move(completion), std::move(fallback), core::PoolHandle{}, cors);
            ownsPending = true;
            input.cancellation = s.pending->cancellation.token();
            const auto status = s.files->read(input,
                {[](void* data, FileReadResult result) noexcept {
                     auto owner = State::cookie(data);
                     (*owner)->readDone(std::move(result));
                 },
                    retained.get()});
            if (status == FileIoStatus::Accepted)
                retained.release();
            else {
                FileReadResult result;
                result.status = status;
                result.diagnosticId = "WORKSPACE-READ-NOT-ACCEPTED";
                s.readDone(std::move(result));
            }
            return;
        }
        if (op == "stat" || op == "list") {
            Wire::require(core::json::jsonOnlyUniqueKeys(root,
                {"type",
                    "contractVersion",
                    "workspace",
                    "session",
                    "operation",
                    "path",
                    "contentVersion",
                    "maxVersionBytes",
                    "maxResultBytes",
                    "maxScanEntries",
                    "maxScanNameBytes",
                    "pageSize",
                    "offset",
                    "version"}));
            FileQueryRequest input;
            input.pathScope = FilePathScope::MountedResource;
            input.mount = s.desc.mount;
            input.path = Wire::text(root, "path");
            input.maxResultBytes = Wire::number(root, "maxResultBytes");
            Wire::require(input.maxResultBytes && input.maxResultBytes <= s.desc.maxQueryBytes);
            if (op == "list") {
                input.operation = FileQueryOperation::Enumerate;
                const auto page = Wire::number(root, "pageSize");
                Wire::require(page && page <= s.desc.maxDirectoryEntries);
                input.pageSize = static_cast<std::uint32_t>(page);
                const auto scan = Wire::number(root, "maxScanEntries");
                Wire::require(scan && scan <= s.desc.maxDirectoryEntries && page <= scan);
                input.maxScanEntries = static_cast<std::uint32_t>(scan);
                input.maxScanNameBytes = Wire::number(root, "maxScanNameBytes");
                Wire::require(input.maxScanNameBytes
                    && input.maxScanNameBytes <= s.desc.maxDirectoryNameBytes);
                input.cursor = {Wire::parseVersion(Wire::member(root, "version")),
                    Wire::number(root, "offset")};
                Wire::require(!core::json::jsonMember(root, "contentVersion")
                    && !core::json::jsonMember(root, "maxVersionBytes"));
            } else {
                Wire::require(!core::json::jsonMember(root, "pageSize")
                    && !core::json::jsonMember(root, "offset")
                    && !core::json::jsonMember(root, "maxScanEntries")
                    && !core::json::jsonMember(root, "maxScanNameBytes")
                    && !core::json::jsonMember(root, "version"));
                const auto& hash = Wire::member(root, "contentVersion");
                Wire::require(hash.IsBool());
                input.versionPolicy =
                    hash.GetBool() ? FileVersionPolicy::ContentSha256 : FileVersionPolicy::None;
                input.maxVersionBytes = Wire::number(root, "maxVersionBytes");
                Wire::require(input.maxVersionBytes <= s.desc.maxFileBytes);
            }
            s.pending = core::makeUnique<State::Pending>(
                std::move(completion), std::move(fallback), core::PoolHandle{}, cors);
            ownsPending = true;
            input.cancellation = s.pending->cancellation.token();
            const auto status = s.files->query(input,
                {[](void* data, FileQueryResult result) noexcept {
                     auto owner = State::cookie(data);
                     (*owner)->queryDone(std::move(result));
                 },
                    retained.get()});
            if (status == FileIoStatus::Accepted)
                retained.release();
            else {
                FileQueryResult result;
                result.fail(status, "WORKSPACE-QUERY-NOT-ACCEPTED");
                s.queryDone(std::move(result));
            }
            return;
        }
        Wire::require(saving, "WORKSPACE-OPERATION-UNSUPPORTED");
        Wire::require(core::json::jsonOnlyUniqueKeys(root,
            {"type",
                "contractVersion",
                "workspace",
                "session",
                "operation",
                "path",
                "id",
                "precondition",
                "version",
                "maxWriteBytes",
                "maxBaselineBytes",
                "bytes",
                "data"}));
        FileSaveRequest input;
        input.pathScope = FilePathScope::MountedResource;
        input.mount = s.desc.mount;
        input.path = Wire::text(root, "path");
        input.maxWriteBytes = Wire::number(root, "maxWriteBytes");
        Wire::require(input.maxWriteBytes && input.maxWriteBytes <= s.desc.maxFileBytes);
        input.expectedVersion = Wire::parseVersion(Wire::member(root, "version"));
        const auto condition = Wire::text(root, "precondition");
        Wire::require(condition == "missing" || condition == "content-sha256");
        input.precondition = condition == "missing" ? FileSavePrecondition::MustBeMissing
                                                    : FileSavePrecondition::ContentSha256;
        input.maxBaselineBytes = Wire::number(root, "maxBaselineBytes");
        Wire::require(input.maxBaselineBytes <= s.desc.maxFileBytes);
        const auto bytes = Wire::number(root, "bytes");
        const auto encoded = Wire::text(root, "data");
        Wire::require(bytes <= s.desc.maxFileBytes && encoded.size() == ((bytes + 2) / 3) * 4
                && (bytes == 0 ? encoded.empty() : core::Base64::decode(encoded, input.bytes))
                && input.bytes.size() == bytes && core::Base64::encode(input.bytes) == encoded,
            "WORKSPACE-PAYLOAD-INVALID");
        s.pending = core::makeUnique<State::Pending>(
            std::move(completion), std::move(fallback), core::PoolHandle{}, cors);
        ownsPending = true;
        auto receipt = s.receipts.emplace(State::Receipt{id, requestVersion, {}, false, false});
        if (!receipt) {
            s.finish([&](const State::Pending&) {
                return error(429, "WORKSPACE-RECEIPT-BUDGET");
            });
            return;
        }
        s.pending->receipt = receipt;
        s.lastId = id;
        input.cancellation = s.pending->cancellation.token();
        const auto status = s.files->save(std::move(input),
            {[](void* data, FileSaveResult result) noexcept {
                 auto owner = State::cookie(data);
                 (*owner)->saveDone(result);
             },
                retained.get()});
        if (status == FileIoStatus::Accepted) {
            s.receipts.get(receipt)->submitted = true;
            ++s.saveSubmissions;
            retained.release();
        } else {
            FileSaveResult result;
            result.fail(status, "WORKSPACE-SAVE-NOT-ACCEPTED");
            s.saveDone(result);
        }
    } catch (const Invalid& failure) {
        if (ownsPending && s.pending)
            s.finish([&](const State::Pending&) {
                return error(errorStatus(failure.diagnostic), failure.diagnostic);
            });
        else if (completion) {
            auto reply = error(errorStatus(failure.diagnostic), failure.diagnostic);
            if (corsAllowed)
                reply.add_header("Access-Control-Allow-Origin", s.desc.allowedOrigin);
            deliver(completion, std::move(reply));
        }
    } catch (...) {
        if (ownsPending && s.pending)
            s.finish([&](const State::Pending&) {
                return error(503, "WORKSPACE-PREPARATION");
            });
        else if (completion) {
            auto reply = error(503, "WORKSPACE-PREPARATION");
            if (corsAllowed)
                reply.add_header("Access-Control-Allow-Origin", s.desc.allowedOrigin);
            deliver(completion, std::move(reply));
        }
    }
}
} // namespace arp::editor
