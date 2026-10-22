#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace arp::rhi {

// Backend-private lowering describes only the exact allocator reservation.
// It deliberately carries no native handle, mapped address or payload label.
struct RecordedStagingUploadSlice {
    std::uint32_t pageIndex = 0u;
    std::uint64_t offsetBytes = 0u;
    std::uint64_t sizeBytes = 0u;
    std::uint64_t frameSerial = 0u;
};

// Staging allocations belong to command recording until queue acceptance.
// The journal is transferred from encoder to command buffer. Destruction
// before acceptance cancels exact slices in reverse allocation order; queue
// acceptance commits ownership to the backend completion/retirement path.
class RecordedStagingUploadJournal final {
public:
    RecordedStagingUploadJournal() = default;

    ~RecordedStagingUploadJournal() { rollback(); }

    RecordedStagingUploadJournal(const RecordedStagingUploadJournal&) = delete;
    RecordedStagingUploadJournal& operator=(const RecordedStagingUploadJournal&) = delete;

    template <typename Pool> void enlist(Pool& pool, const RecordedStagingUploadSlice& slice)
    {
        if (slice.sizeBytes == 0u)
            return;
        Entry entry;
        entry.pool = &pool;
        entry.pageIndex = slice.pageIndex;
        entry.offsetBytes = slice.offsetBytes;
        entry.sizeBytes = slice.sizeBytes;
        entry.frameSerial = slice.frameSerial;
        entry.cancel = [](void* owner, const Entry& value) noexcept {
            return static_cast<Pool*>(owner)->cancel(RecordedStagingUploadSlice{
                value.pageIndex,
                value.offsetBytes,
                value.sizeBytes,
                value.frameSerial,
            });
        };
        entries_.push_back(entry);
    }

    // A completion-owned journal keeps the allocator ledger alive independently
    // of its pool wrapper. Only an exact-completion backend may use this overload.
    template <typename Pool>
    bool enlist(std::shared_ptr<Pool> pool,
        const RecordedStagingUploadSlice& slice,
        std::shared_ptr<void> resourceOwner = {})
    {
        if (!pool || slice.sizeBytes == 0u)
            return false;
        enlist(*pool, slice);
        auto& entry = entries_.back();
        entry.owner = std::move(pool);
        entry.resourceOwner = std::move(resourceOwner);
        entry.complete = [](void* owner, const Entry& value) noexcept {
            return static_cast<Pool*>(owner)->retire(RecordedStagingUploadSlice{
                value.pageIndex, value.offsetBytes, value.sizeBytes, value.frameSerial});
        };
        return true;
    }

    // Queue acceptance must retain this journal. Invoke only after the issuing
    // command's real completion; unrelated completion cannot consume entries.
    bool retireCompleted() noexcept
    {
        for (const auto& entry : entries_)
            if (!entry.complete)
                return false;
        bool success = true;
        while (!entries_.empty()) {
            auto entry = std::move(entries_.back());
            entries_.pop_back();
            success = entry.complete(entry.pool, entry) && success;
        }
        return success;
    }

    bool rollbackLast() noexcept
    {
        if (entries_.empty())
            return false;
        const auto entry = entries_.back();
        entries_.pop_back();
        return entry.cancel && entry.cancel(entry.pool, entry);
    }

    bool rollback() noexcept
    {
        bool success = true;
        while (!entries_.empty()) {
            success = rollbackLast() && success;
        }
        return success;
    }

    void commit() noexcept { entries_.clear(); }

    bool empty() const noexcept { return entries_.empty(); }

    std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Entry {
        std::shared_ptr<void> owner;
        std::shared_ptr<void> resourceOwner;
        void* pool = nullptr;
        std::uint32_t pageIndex = 0u;
        std::uint64_t offsetBytes = 0u;
        std::uint64_t sizeBytes = 0u;
        std::uint64_t frameSerial = 0u;
        bool (*cancel)(void*, const Entry&) noexcept = nullptr;
        bool (*complete)(void*, const Entry&) noexcept = nullptr;
    };

    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::rhi
