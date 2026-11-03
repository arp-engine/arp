#pragma once

#include "core/io/file_save_receipt.h"
#include "core/io/workspace_file_wire.h"

namespace arp::editor {

// Editor command wire values reuse the file-domain version/status codec. No
// credentials, mount handles, host paths or backend pointers cross this ABI.
class EditorFileReceiptWire final {
    using Wire = core::io::WorkspaceFileWire;
    using Value = core::json::JsonValue;
    using Allocator = core::json::CoreJsonAllocator;

public:
    static core::io::FileSaveReceipt parse(const Value& value)
    {
        Wire::require(core::json::jsonOnlyUniqueKeys(value,
            {"contractVersion", "scope", "operationId", "sessionVersion", "requestVersion"}));
        Wire::require(Wire::number(value, "contractVersion") == 1);
        core::io::FileSaveReceipt ticket;
        ticket.scope = Wire::number(value, "scope");
        ticket.operationId = Wire::number(value, "operationId");
        ticket.sessionVersion = Wire::parseVersion(Wire::member(value, "sessionVersion"));
        ticket.requestVersion = Wire::parseVersion(Wire::member(value, "requestVersion"));
        Wire::require(ticket.valid());
        return ticket;
    }

    static Value identity(const core::io::FileSaveReceipt& ticket, Allocator& a)
    {
        if (!ticket.valid())
            return Value(rapidjson::kNullType);
        Value value(rapidjson::kObjectType);
        value.AddMember("contractVersion", 1, a);
        value.AddMember("scope", ticket.scope, a);
        value.AddMember("operationId", ticket.operationId, a);
        value.AddMember("sessionVersion", Wire::version(ticket.sessionVersion, a), a);
        value.AddMember("requestVersion", Wire::version(ticket.requestVersion, a), a);
        return value;
    }

    static Value outcome(const core::io::FileSaveResult& result, Allocator& a)
    {
        Wire::require(result.commitState == core::io::FileCommitState::Committed
            || result.commitState == core::io::FileCommitState::NotCommitted
            || result.commitState == core::io::FileCommitState::OutcomeUnknown);
        Value value(rapidjson::kObjectType);
        Wire::field(value, "ioStatus", Wire::statusName(result.status), a);
        Wire::field(value, "deliveryStatus", Wire::statusName(result.deliveryStatus), a);
        Wire::field(value,
            "commitState",
            result.commitState == core::io::FileCommitState::Committed          ? "committed"
                : result.commitState == core::io::FileCommitState::NotCommitted ? "not_committed"
                                                                                : "outcome_unknown",
            a);
        Wire::field(value, "diagnostic", result.diagnosticId ? result.diagnosticId : "", a);
        value.AddMember("version", Wire::version(result.committedVersion, a), a);
        value.AddMember("receipt", identity(result.receipt, a), a);
        return value;
    }

    static core::pmr::String encodeIdentity(
        const core::io::FileSaveReceipt& ticket, std::size_t budget)
    {
        return Wire::encode(identity(ticket, core::json::jsonAllocator()), budget);
    }

    static core::pmr::String encodeResult(
        const core::io::FileSaveReceiptResult& result, std::size_t budget)
    {
        auto& a = core::json::jsonAllocator();
        Value value(rapidjson::kObjectType);
        value.AddMember("contractVersion", 1, a);
        Wire::field(value, "ioStatus", Wire::statusName(result.status), a);
        Wire::field(value, "deliveryStatus", Wire::statusName(result.deliveryStatus), a);
        const char* state = nullptr;
        switch (result.state) {
        case core::io::FileSaveReceiptState::Unavailable:
            state = "unavailable";
            break;
        case core::io::FileSaveReceiptState::Pending:
            state = "pending";
            break;
        case core::io::FileSaveReceiptState::Terminal:
            state = "terminal";
            break;
        case core::io::FileSaveReceiptState::Released:
            state = "released";
            break;
        }
        Wire::require(state != nullptr);
        Wire::field(value, "state", state, a);
        Wire::field(value, "diagnostic", result.diagnosticId ? result.diagnosticId : "", a);
        value.AddMember("receipt", identity(result.receipt, a), a);
        value.AddMember("outcome", outcome(result.outcome, a), a);
        return Wire::encode(value, budget);
    }
};
} // namespace arp::editor
