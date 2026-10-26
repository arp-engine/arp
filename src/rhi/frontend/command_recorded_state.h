#pragma once

#include "core/memory/cpu_allocator.h"
#include "core/ownership/mutation_journal.h"
#include "rhi/frontend/transfer_recorded_staging_upload.h"
#include "rhi/gpu_commands.h"

namespace arp::rhi {

// Backend implementation mixin. Metadata ownership is separate from native
// resource retention and transfers once from encoder to finished command buffer.
class RecordedStateCommandBuffer : public CommandBuffer {
public:
    explicit RecordedStateCommandBuffer(core::UniquePtr<core::MutationJournal> states,
        core::UniquePtr<RecordedStagingUploadJournal> stagingUploads = {})
        : states_(std::move(states)), stagingUploads_(std::move(stagingUploads))
    {}

    explicit RecordedStateCommandBuffer(std::shared_ptr<core::MutationJournal> states)
        : sharedStates_(std::move(states))
    {}

    bool acceptRecordedOwnership() const noexcept
    {
        if (states_)
            states_->commit();
        else if (sharedStates_)
            return acceptBackendOwnedRecordedOwnership();
        else
            return false;
        if (stagingUploads_)
            stagingUploads_->commit();
        return true;
    }

protected:
    // Native-owned aliases override this hook: queue acceptance must not
    // implicitly commit a native owner's pending completion/content claims.
    virtual bool acceptBackendOwnedRecordedOwnership() const noexcept
    {
        sharedStates_->commit();
        return true;
    }

private:
    core::UniquePtr<core::MutationJournal> states_;
    std::shared_ptr<core::MutationJournal> sharedStates_;
    core::UniquePtr<RecordedStagingUploadJournal> stagingUploads_;
};

inline bool acceptRecordedCommandOwnership(const CommandBuffer& commands) noexcept
{
    const auto* recorded = dynamic_cast<const RecordedStateCommandBuffer*>(&commands);
    if (!recorded)
        return false;
    return recorded->acceptRecordedOwnership();
}

} // namespace arp::rhi
