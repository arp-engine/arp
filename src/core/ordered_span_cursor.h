#pragma once

#include "core/monotonic_id_sequence.h"

#include <cstddef>

namespace arp::core {

// Serialized consumption of contiguous spans. Validation is atomic; a ticket
// belongs to one exact cursor/position and cannot commit twice or after abort.
// Validators inspect immutable input only. No allocation or reset/replay path.
class OrderedSpanCursor final {
public:
    struct Span {
        std::size_t first = 0u, count = 0u;
    };

    class Ticket final {
        friend class OrderedSpanCursor;

        Ticket(
            const OrderedSpanCursor* issuer, std::uint64_t identity, std::size_t record, Span span)
            : issuer_(issuer), identity_(identity), record_(record), span_(span)
        {}

    public:
        Ticket() = default;

        explicit operator bool() const noexcept { return issuer_ != nullptr; }

    private:
        const OrderedSpanCursor* issuer_ = nullptr;
        std::uint64_t identity_ = 0u;
        std::size_t record_ = 0u;
        Span span_;
    };

    OrderedSpanCursor(std::size_t records, std::size_t elements) noexcept
        : identity_(identities_.tryAcquire()), records_(records), elements_(elements)
    {}

    OrderedSpanCursor(const OrderedSpanCursor&) = delete;
    OrderedSpanCursor& operator=(const OrderedSpanCursor&) = delete;

    bool matchesNext(Span span) const noexcept
    {
        return identity_ && !aborted_ && nextRecord_ < records_ && span.first == nextElement_
            && span.count <= elements_ - nextElement_;
    }

    template <class Validator> Ticket preflight(Span span, Validator&& validate) const
    {
        if (!matchesNext(span))
            return {};
        for (std::size_t offset = 0u; offset < span.count; ++offset)
            if (!validate(span.first + offset))
                return {};
        return Ticket(this, identity_, nextRecord_, span);
    }

    bool commit(const Ticket& ticket) noexcept
    {
        if (ticket.issuer_ != this || ticket.identity_ != identity_ || ticket.record_ != nextRecord_
            || !matchesNext(ticket.span_))
            return false;
        nextElement_ += ticket.span_.count;
        ++nextRecord_;
        return true;
    }

    void abort() noexcept { aborted_ = true; }

    bool complete() const noexcept
    {
        return identity_ && !aborted_ && nextRecord_ == records_ && nextElement_ == elements_;
    }

    std::size_t nextRecord() const noexcept { return nextRecord_; }

    std::size_t nextElement() const noexcept { return nextElement_; }

private:
    inline static MonotonicIdSequence identities_;
    const std::uint64_t identity_;
    const std::size_t records_, elements_;
    std::size_t nextRecord_ = 0u, nextElement_ = 0u;
    bool aborted_ = false;
};
} // namespace arp::core
