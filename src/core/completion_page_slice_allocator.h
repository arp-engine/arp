#pragma once

#include "core/monotonic_id_sequence.h"
#include "core/page_slice_allocator.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

namespace arp::core {

// An exact reservation is resolved once by its original completion owner.
// Resolving may run on a callback thread; allocator mutation stays on the
// serialized allocating thread. Retaining this token never retains a facade.
class CompletionPageSliceReservation final {
public:
    enum class State : std::uint8_t { Pending, Cancelled, Completed };

    const FrameRetiredPageSliceAllocation& allocation() const noexcept { return allocation_; }

    State state() const noexcept { return state_.load(std::memory_order_acquire); }

    bool cancel() noexcept { return resolve(State::Cancelled); }

    bool complete() noexcept { return resolve(State::Completed); }

private:
    friend class CompletionPageSliceAllocator;

    bool resolve(State resolved) noexcept
    {
        auto expected = State::Pending;
        return state_.compare_exchange_strong(
            expected, resolved, std::memory_order_release, std::memory_order_relaxed);
    }

    FrameRetiredPageSliceAllocation allocation_;
    std::atomic<State> state_{State::Pending};
};

class CompletionPageSliceAllocator final {
public:
    explicit CompletionPageSliceAllocator(FrameRetiredPageSliceAllocatorDesc desc,
        std::pmr::memory_resource* resource = &defaultCpuMemoryResource())
        : slices_(desc), reservations_(resource)
    {}

    CompletionPageSliceAllocator(const CompletionPageSliceAllocator&) = delete;
    CompletionPageSliceAllocator& operator=(const CompletionPageSliceAllocator&) = delete;

    template <typename PageFactory>
    FrameRetiredPageSliceAllocation allocate(
        FrameRetiredPageSliceRequest request, PageFactory&& factory)
    {
        collect();
        // Generation is non-recycling across allocator instances. An external
        // frame/recording serial cannot make an old reservation current again.
        const auto generation = generations_.tryAcquire();
        if (!generation)
            return {false,
                "exact slice generation exhausted",
                0u,
                0u,
                0u,
                0u,
                "ARP-RHI-STAGING-GENERATION-EXHAUSTED"};
        auto reservation = makeShared<CompletionPageSliceReservation>();
        reservations_.push_back(reservation);
        request.frameSerial = generation;
        try {
            reservation->allocation_ =
                slices_.allocate(request, std::forward<PageFactory>(factory));
        } catch (...) {
            reservations_.pop_back();
            throw;
        }
        if (!reservation->allocation_.success)
            reservations_.pop_back();
        return reservation->allocation_;
    }

    std::shared_ptr<CompletionPageSliceReservation> find(
        const FrameRetiredPageSliceAllocation& allocation) const noexcept
    {
        if (!allocation.success)
            return {};
        for (const auto& reservation : reservations_) {
            const auto& a = reservation->allocation();
            if (a.pageIndex == allocation.pageIndex && a.offsetBytes == allocation.offsetBytes
                && a.sizeBytes == allocation.sizeBytes && a.frameSerial == allocation.frameSerial
                && reservation->state() == CompletionPageSliceReservation::State::Pending)
                return reservation;
        }
        return {};
    }

    bool cancel(const FrameRetiredPageSliceAllocation& allocation) noexcept
    {
        const auto reservation = find(allocation);
        if (!reservation || !reservation->cancel())
            return false;
        collect();
        return true;
    }

    void collect() noexcept
    {
        auto end = std::remove_if(
            reservations_.begin(), reservations_.end(), [this](const auto& reservation) {
                switch (reservation->state()) {
                case CompletionPageSliceReservation::State::Pending:
                    return false;
                case CompletionPageSliceReservation::State::Cancelled:
                    return slices_.cancelAllocation(reservation->allocation());
                case CompletionPageSliceReservation::State::Completed:
                    return slices_.retireAllocation(reservation->allocation());
                }
                return false;
            });
        reservations_.erase(end, reservations_.end());
    }

    FrameRetiredPageSliceAllocatorStats stats() noexcept
    {
        collect();
        return slices_.stats();
    }

private:
    FrameRetiredPageSliceAllocator slices_;
    pmr::Vector<std::shared_ptr<CompletionPageSliceReservation>> reservations_;
    inline static MonotonicIdSequence generations_;
};

} // namespace arp::core
