#pragma once

#include "core/io/file_read_contract.h"
#include "core/io/file_save_receipt.h"

namespace arp::core::io {

class FileSaveReceiptContract final {
public:
    static FileReadValidation validate(const FileSaveReceiptRequest& request) noexcept
    {
        if (request.contractVersion != 1)
            return {FileIoStatus::InvalidRequest, "IO-RECEIPT-CONTRACT-VERSION"};
        if (request.pathScope != FilePathScope::MountedResource)
            return {FileIoStatus::InvalidPathScope, "IO-RECEIPT-MOUNTED-SCOPE-REQUIRED"};
        if (!request.mount || !request.receipt.valid())
            return {FileIoStatus::InvalidRequest, "IO-RECEIPT-IDENTITY"};
        if (request.operation != FileSaveReceiptOperation::Query
            && request.operation != FileSaveReceiptOperation::Release)
            return {FileIoStatus::InvalidRequest, "IO-RECEIPT-OPERATION"};
        if (request.maxResultBytes < sizeof(FileSaveReceiptResult)
            || request.maxResultBytes == UINT64_MAX)
            return {FileIoStatus::InvalidRequest, "IO-RECEIPT-RESULT-BUDGET"};
        return {};
    }

    static FileReadValidation validateResult(
        const FileSaveReceiptRequest& request, const FileSaveReceiptResult& result) noexcept
    {
        if (result.state == FileSaveReceiptState::Unavailable) {
            if (result.status == FileIoStatus::Success
                || result.outcome.commitState != FileCommitState::OutcomeUnknown)
                return {FileIoStatus::IntegrityMismatch, "IO-RECEIPT-UNAVAILABLE-OUTCOME"};
            return {};
        }
        if (result.status != FileIoStatus::Success || !(result.receipt == request.receipt))
            return {FileIoStatus::IntegrityMismatch, "IO-RECEIPT-RESULT-IDENTITY"};
        if (request.operation == FileSaveReceiptOperation::Query) {
            if (result.state == FileSaveReceiptState::Pending) {
                if (result.outcome.commitState == FileCommitState::OutcomeUnknown)
                    return {};
            } else if (result.state == FileSaveReceiptState::Terminal
                && result.outcome.receipt == request.receipt
                && result.outcome.status != FileIoStatus::Accepted
                && (result.outcome.commitState == FileCommitState::NotCommitted
                    || result.outcome.commitState == FileCommitState::Committed
                    || result.outcome.commitState == FileCommitState::OutcomeUnknown))
                return {};
        } else if (request.operation == FileSaveReceiptOperation::Release
            && result.state == FileSaveReceiptState::Released
            && result.outcome.commitState == FileCommitState::OutcomeUnknown)
            return {};
        return {FileIoStatus::IntegrityMismatch, "IO-RECEIPT-RESULT-STATE"};
    }
};
} // namespace arp::core::io
