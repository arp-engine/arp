#pragma once

#include "core/generation_pool.h"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace arp::core {

// CPU metadata reservations only. GPU owners and completion authority belong
// to the original runtime adapter, never to this registry or its validator.
template <class Metadata> class SingleUseReservationPool final {
    static_assert(std::is_trivially_copyable_v<Metadata>
        && std::is_nothrow_copy_constructible_v<Metadata>
        && std::is_trivially_destructible_v<Metadata>);

public:
    enum class Stage { Empty, Reserved, Consumed, Quarantined };

private:
    struct Entry {
        Metadata metadata;
        Stage stage = Stage::Reserved;
    };

    struct State {
        explicit State(std::uint32_t capacity) : entries(capacity) {}

        GenerationPool<Entry> entries;
        bool closed = false;
    };

public:
    class Reservation final {
    public:
        Reservation() noexcept = default;

        ~Reservation() { reset(); }

        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;

        Reservation(Reservation&& other) noexcept
            : state_(std::move(other.state_)), ticket_(std::exchange(other.ticket_, {}))
        {}

        Reservation& operator=(Reservation&& other) noexcept
        {
            if (this != &other) {
                reset();
                state_ = std::move(other.state_);
                ticket_ = std::exchange(other.ticket_, {});
            }
            return *this;
        }

        Stage stage() const noexcept
        {
            const auto* entry = state_ ? state_->entries.get(ticket_) : nullptr;
            return entry ? entry->stage : Stage::Empty;
        }

        const Metadata* metadata() const noexcept
        {
            const auto* entry = state_ ? state_->entries.get(ticket_) : nullptr;
            return entry ? &entry->metadata : nullptr;
        }

        explicit operator bool() const noexcept { return stage() != Stage::Empty; }

        // Reserved destruction cancels. An abandoned consumed record is kept
        // and closes admission; its slot cannot silently become reusable.
        void reset() noexcept
        {
            if (state_) {
                if (auto* entry = state_->entries.get(ticket_)) {
                    if (entry->stage == Stage::Reserved)
                        state_->entries.erase(ticket_);
                    else {
                        entry->stage = Stage::Quarantined;
                        state_->closed = true;
                    }
                }
                ticket_ = {};
                state_.reset();
            }
        }

    private:
        Reservation(std::shared_ptr<State> state, PoolHandle ticket) noexcept
            : state_(std::move(state)), ticket_(ticket)
        {}

        std::shared_ptr<State> state_;
        PoolHandle ticket_;
        friend class SingleUseReservationPool;
    };

    SingleUseReservationPool(const SingleUseReservationPool&) = delete;
    SingleUseReservationPool& operator=(const SingleUseReservationPool&) = delete;
    SingleUseReservationPool(SingleUseReservationPool&&) noexcept = default;
    SingleUseReservationPool& operator=(SingleUseReservationPool&&) noexcept = default;

    static std::optional<SingleUseReservationPool> prepareCold(std::uint32_t capacity,
        const std::shared_ptr<CpuMemoryQuota>& quota,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        if (!capacity) {
            diagnostic = "CORE-RESERVATION-CAPACITY";
            return {};
        }
        if (!quota || !quota->limitBytes()) {
            diagnostic = "CORE-RESERVATION-QUOTA";
            return {};
        }
        try {
            CpuMemoryQuotaScope scope(quota);
            return SingleUseReservationPool(makeShared<State>(capacity));
        } catch (const std::bad_alloc&) {
            diagnostic = "CORE-RESERVATION-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "CORE-RESERVATION-CAPACITY";
            return {};
        }
    }

    Reservation reserve(const Metadata& metadata, const char*& diagnostic) noexcept
    {
        diagnostic = nullptr;
        if (!state_) {
            diagnostic = "CORE-RESERVATION-SOURCE";
            return {};
        }
        if (state_->closed) {
            diagnostic = "CORE-RESERVATION-CLOSED";
            return {};
        }
        const auto ticket = state_->entries.emplace(Entry{metadata, Stage::Reserved});
        if (!ticket) {
            diagnostic = state_->entries.diagnostic();
            return {};
        }
        return Reservation(state_, ticket);
    }

    template <class Validate>
    const char* consume(Reservation& reservation, Validate&& validate) noexcept
    {
        static_assert(std::is_nothrow_invocable_r_v<bool, Validate, const Metadata&>);
        if (const auto* diagnostic = validateToken(reservation))
            return diagnostic;
        auto* entry = state_->entries.get(reservation.ticket_);
        if (entry->stage != Stage::Reserved)
            return "CORE-RESERVATION-STAGE";
        if (state_->closed)
            return "CORE-RESERVATION-CLOSED";
        if (!validate(std::as_const(entry->metadata)))
            return "CORE-RESERVATION-METADATA";
        entry->stage = Stage::Consumed;
        return nullptr;
    }

    const char* cancel(Reservation& reservation) noexcept
    {
        if (const auto* diagnostic = validateToken(reservation))
            return diagnostic;
        if (state_->entries.get(reservation.ticket_)->stage != Stage::Reserved)
            return "CORE-RESERVATION-STAGE";
        erase(reservation);
        return nullptr;
    }

    // Explicit CPU metadata removal, not native completion proof. A runtime
    // adapter must authorize its GPU owner retirement independently before
    // using this operation. No fences, submit results or completed flags here.
    const char* eraseConsumedMetadata(Reservation& reservation) noexcept
    {
        if (const auto* diagnostic = validateToken(reservation))
            return diagnostic;
        if (state_->entries.get(reservation.ticket_)->stage == Stage::Reserved)
            return "CORE-RESERVATION-STAGE";
        erase(reservation);
        return nullptr;
    }

    void close() noexcept
    {
        if (!state_)
            return;
        state_->closed = true;
        state_->entries.forEach([&](PoolHandle ticket, Entry& entry) noexcept {
            if (entry.stage == Stage::Reserved)
                state_->entries.erase(ticket);
            else
                entry.stage = Stage::Quarantined;
        });
    }

    bool closed() const noexcept { return !state_ || state_->closed; }

    std::uint32_t liveCount() const noexcept { return state_ ? state_->entries.liveCount() : 0u; }

    std::uint32_t retainedCount() const noexcept
    {
        std::uint32_t count = 0u;
        if (state_)
            std::as_const(state_->entries).forEach([&](PoolHandle, const Entry& entry) noexcept {
                if (entry.stage != Stage::Reserved)
                    ++count;
            });
        return count;
    }

private:
    explicit SingleUseReservationPool(std::shared_ptr<State> state) noexcept
        : state_(std::move(state))
    {}

    const char* validateToken(const Reservation& reservation) const noexcept
    {
        if (!state_)
            return "CORE-RESERVATION-SOURCE";
        if (!reservation.state_)
            return "CORE-RESERVATION-EMPTY";
        if (state_ != reservation.state_)
            return "CORE-RESERVATION-FOREIGN";
        if (!state_->entries.get(reservation.ticket_))
            return "CORE-RESERVATION-STALE";
        return nullptr;
    }

    void erase(Reservation& reservation) noexcept
    {
        state_->entries.erase(reservation.ticket_);
        reservation.ticket_ = {};
        reservation.state_.reset();
    }

    std::shared_ptr<State> state_;
};

} // namespace arp::core
