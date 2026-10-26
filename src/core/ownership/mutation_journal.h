#pragma once

#include <tuple>
#include <type_traits>
#include <utility>

namespace arp::core {

class MutationJournal;

// Embedded in a stable object. Destruction detaches without touching its value;
// this lets that object die before the journal without a dangling callback.
class MutationRecord {
public:
    MutationRecord() = default;

    ~MutationRecord() { detach(); }

    MutationRecord(const MutationRecord&) = delete;
    MutationRecord& operator=(const MutationRecord&) = delete;

    bool pending() const noexcept { return owner_ != nullptr; }

    bool ownedBy(const MutationJournal& owner) const noexcept { return owner_ == &owner; }

private:
    friend class MutationJournal;
    void detach() noexcept;
    MutationJournal* owner_ = nullptr;
    MutationRecord* previous_ = nullptr;
    MutationRecord* next_ = nullptr;
    void* context_ = nullptr;
    void (*restore_)(void*) noexcept = nullptr;
    void (*invalidate_)(void*) noexcept = nullptr;
};

class MutationJournal {
public:
    MutationJournal() = default;

    ~MutationJournal() { rollback(); }

    MutationJournal(const MutationJournal&) = delete;
    MutationJournal& operator=(const MutationJournal&) = delete;

    bool enlist(MutationRecord& record,
        void* context,
        void (*restore)(void*) noexcept,
        void (*invalidate)(void*) noexcept = nullptr) noexcept
    {
        if (record.pending())
            return record.ownedBy(*this);
        record.owner_ = this;
        record.context_ = context;
        record.restore_ = restore;
        record.invalidate_ = invalidate;
        record.next_ = first_;
        if (first_)
            first_->previous_ = &record;
        first_ = &record;
        return true;
    }

    void commit() noexcept { discard(); }

    bool canInvalidate() const noexcept
    {
        for (auto* record = first_; record; record = record->next_)
            if (!record->invalidate_)
                return false;
        return true;
    }

    // A terminal outcome has no valid saved value to restore. Every participant
    // must explicitly support invalidation; refusal invokes no callbacks.
    bool invalidate() noexcept
    {
        if (!canInvalidate())
            return false;
        while (first_) {
            auto* record = first_;
            record->invalidate_(record->context_);
            record->detach();
        }
        return true;
    }

    // Forget saved values without restoring them. Terminally invalidated state
    // uses this operation to relinquish journal ownership without claiming a
    // successful transaction or reviving uncertain submitted values.
    void discard() noexcept
    {
        while (first_)
            first_->detach();
    }

    // Drop only this participant's saved value. An unrelated journal cannot
    // release it, and no restore/invalidation callback is invoked.
    bool discard(MutationRecord& record) noexcept
    {
        if (!record.ownedBy(*this))
            return false;
        record.detach();
        return true;
    }

    void rollback() noexcept
    {
        while (first_) {
            auto* record = first_;
            record->restore_(record->context_);
            record->detach();
        }
    }

    bool empty() const noexcept { return first_ == nullptr; }

private:
    friend class MutationRecord;
    MutationRecord* first_ = nullptr;
};

inline void MutationRecord::detach() noexcept
{
    if (!owner_)
        return;
    if (previous_)
        previous_->next_ = next_;
    else
        owner_->first_ = next_;
    if (next_)
        next_->previous_ = previous_;
    owner_ = nullptr;
    previous_ = next_ = nullptr;
    context_ = nullptr;
    restore_ = nullptr;
    invalidate_ = nullptr;
}

// current must outlive this guard. Copying its bounded value may allocate while
// preparing; restoring uses noexcept swap and retains storage for the next use.
template <class T> class MutationSnapshot {
public:
    // The optional observer runs after rollback restores current. Its context
    // must outlive this snapshot; destruction/commit never invokes it.
    explicit MutationSnapshot(T& current,
        void* restoreContext = nullptr,
        void (*afterRestore)(void*) noexcept = nullptr,
        void (*onInvalidate)(void*) noexcept = nullptr)
        : current_(current), saved_(copyValue(current, 0)), restoreContext_(restoreContext),
          afterRestore_(afterRestore), onInvalidate_(onInvalidate)
    {}

    MutationSnapshot(const MutationSnapshot&) = delete;
    MutationSnapshot& operator=(const MutationSnapshot&) = delete;

    bool pending() const noexcept { return record_.pending(); }

    bool ownedBy(const MutationJournal& journal) const noexcept { return record_.ownedBy(journal); }

    bool discard(MutationJournal& journal) noexcept { return journal.discard(record_); }

    bool canPrepare(const MutationJournal& journal) const noexcept
    {
        return !record_.pending() || record_.ownedBy(journal);
    }

    bool prepare(MutationJournal& journal)
    {
        if (record_.pending())
            return record_.ownedBy(journal);
        saved_ = current_;
        const auto restore = [](void* context) noexcept {
            auto& snapshot = *static_cast<MutationSnapshot*>(context);
            static_assert(std::is_nothrow_swappable_v<T>);
            using std::swap;
            swap(snapshot.current_, snapshot.saved_);
            if (snapshot.afterRestore_)
                snapshot.afterRestore_(snapshot.restoreContext_);
        };
        void (*invalidate)(void*) noexcept = nullptr;
        if (onInvalidate_)
            invalidate = [](void* context) noexcept {
                auto& snapshot = *static_cast<MutationSnapshot*>(context);
                snapshot.onInvalidate_(snapshot.restoreContext_);
            };
        return journal.enlist(record_, this, restore, invalidate);
    }

private:
    template <class U>
    static auto copyValue(const U& value, int) -> decltype(U(value, value.get_allocator()))
    {
        return U(value, value.get_allocator());
    }

    template <class U> static U copyValue(const U& value, ...) { return value; }

    T& current_;
    T saved_;
    void* restoreContext_;
    void (*afterRestore_)(void*) noexcept;
    void (*onInvalidate_)(void*) noexcept;
    MutationRecord record_;
};

// A cold, externally serialized factory can mutate scalar accounting only
// while building an unpublished candidate. False/empty return and exceptions
// restore every captured value. Reuses the journal; allocates no second graph.
template <class Action, class... Values>
auto withMutationRollback(Action&& action, Values&... values) -> decltype(action())
{
    std::tuple<MutationSnapshot<Values>...> snapshots(values...);
    MutationJournal journal; // dies first, while every snapshot remains alive
    const auto prepared = std::apply(
        [&](auto&... snapshot) {
            return (snapshot.prepare(journal) && ...);
        },
        snapshots);
    if (!prepared)
        return {};
    auto candidate = action();
    if (candidate)
        journal.commit();
    return candidate;
}

} // namespace arp::core
