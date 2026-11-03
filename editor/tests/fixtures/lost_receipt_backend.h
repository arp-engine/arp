#pragma once

#include "core/io/file_backend.h"

namespace arp::editor::testfixtures {
using namespace core::io;

// Response-loss fixture around real in-memory writes; not remote-write proof.
class LostReceiptBackend final : public ManagedFileBackend {
public:
    explicit LostReceiptBackend(core::SharedPtr<ManagedFileBackend> delegate)
        : delegate_(std::move(delegate))
    {}

    FileBackendCapability capability() const noexcept override { return delegate_->capability(); }

    FileReadResult validateRead(const FileReadRequest& r) const override
    {
        return delegate_->validateRead(r);
    }

    FileIoStatus readAsync(const FileReadRequest& r,
        FileReadCompletion c,
        core::runtime::CompletionQueue& q) noexcept override
    {
        return delegate_->readAsync(r, c, q);
    }

    FileIoStatus queryAsync(const FileQueryRequest& r,
        FileQueryCompletion c,
        core::runtime::CompletionQueue& q) noexcept override
    {
        return delegate_->queryAsync(r, c, q);
    }

    FileSaveCapability saveCapability(core::PoolHandle = {}) const noexcept override
    {
        return delegate_->saveCapability();
    }

    FileIoStatus saveAsync(FileSaveRequest r,
        FileSaveCompletion c,
        core::runtime::CompletionQueue& q) noexcept override
    {
        try {
            auto callback = core::makeUnique<Callback>();
            callback->completion = c;
            callback->lose = loseNext_;
            const auto status = delegate_->saveAsync(std::move(r), {complete, callback.get()}, q);
            if (status == FileIoStatus::Accepted) {
                loseNext_ = false;
                callback.release();
            }
            return status;
        } catch (...) {
            return FileIoStatus::BudgetExceeded;
        }
    }

    void shutdown() noexcept override { delegate_->shutdown(); }

private:
    struct Callback {
        FileSaveCompletion completion;
        bool lose = false;
    };

    static void complete(void* context, FileSaveResult result) noexcept
    {
        core::UniquePtr<Callback> callback{static_cast<Callback*>(context),
            core::CpuAllocatedDeleter<Callback>{
                context, &core::destroyCpuAllocatedUnique<Callback>}};
        if (callback->lose) {
            result.fail(FileIoStatus::OutcomeUnknown, "TEST-SCENE-SAVE-RESPONSE-LOST");
            result.commitState = FileCommitState::OutcomeUnknown;
            result.committedVersion = {};
        }
        callback->completion.complete(callback->completion.userData, result);
    }

    core::SharedPtr<ManagedFileBackend> delegate_;
    bool loseNext_ = true;
};

} // namespace arp::editor::testfixtures
