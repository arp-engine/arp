#pragma once

#include "core/io/file_service.h"

namespace arp::editor {

struct ProjectFileReceiptObservation {
    bool queryAttempted = false;
    bool releaseAttempted = false;
    core::io::FileSaveReceiptResult result;

    bool operator==(const ProjectFileReceiptObservation& other) const noexcept
    {
        return queryAttempted == other.queryAttempted && releaseAttempted == other.releaseAttempted
            && result.status == other.result.status
            && result.deliveryStatus == other.result.deliveryStatus
            && result.state == other.result.state && result.receipt == other.result.receipt
            && result.diagnosticId == other.result.diagnosticId
            && result.outcome.status == other.result.outcome.status
            && result.outcome.deliveryStatus == other.result.outcome.deliveryStatus
            && result.outcome.commitState == other.result.outcome.commitState
            && result.outcome.committedVersion == other.result.outcome.committedVersion
            && result.outcome.receipt == other.result.outcome.receipt
            && result.outcome.diagnosticId == other.result.outcome.diagnosticId;
    }
};

// Shared owner-thread submission/receipt lifetime for Editor source saves.
// Construction prepares only; callbacks retain private state, never documents.
class ProjectFileSaveTask final {
public:
    ProjectFileSaveTask(core::io::FileService&, core::io::FileSaveRequest);
    // A single explicit observation; never submits a save or chains Release.
    ProjectFileSaveTask(core::io::FileService&, core::io::FileSaveReceiptRequest);
    ~ProjectFileSaveTask();
    ProjectFileSaveTask(const ProjectFileSaveTask&) = delete;
    ProjectFileSaveTask& operator=(const ProjectFileSaveTask&) = delete;
    void advance() noexcept;
    void cancel() noexcept;
    void reject(core::io::FileIoStatus, const char*) noexcept;
    bool submitted() const noexcept;
    bool terminal() const noexcept;
    bool cancellationRequested() const noexcept;
    const core::io::FileSaveResult& result() const noexcept;
    const ProjectFileReceiptObservation& receiptObservation() const noexcept;

private:
    struct State;
    static void complete(void*, core::io::FileSaveResult) noexcept;
    core::io::FileService& files_;
    core::SharedPtr<State> state_;
};
} // namespace arp::editor
