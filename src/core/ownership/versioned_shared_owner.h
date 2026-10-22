#pragma once
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace arp::core {
struct VersionedOwnedValueContractError {
    const char* diagnostic;
};

// An immutable trivial request carries the active alternative version. Values
// may be owned records or handles; no second tag, heap box or type erasure.
// Both getters preserve references and allocate nothing.
template <class Request, class Ordinary, class Expanded, std::uint32_t ExpandedVersion>
class VersionedOwnedValue final {
    static_assert(std::is_trivially_copyable_v<Request>);
    static_assert(!std::is_same_v<Ordinary, Expanded>);

    union Storage {
        Ordinary ordinary;
        Expanded expanded;

        explicit Storage(Ordinary owner) noexcept(std::is_nothrow_move_constructible_v<Ordinary>)
            : ordinary(std::move(owner))
        {}

        explicit Storage(Expanded owner) noexcept(std::is_nothrow_move_constructible_v<Expanded>)
            : expanded(std::move(owner))
        {}

        ~Storage() {} // VersionedOwnedValue destroys exactly the active member.
    };

    static Request checked(Request request, bool expanded)
    {
        if ((request.contractVersion == ExpandedVersion) != expanded)
            throw VersionedOwnedValueContractError{"CORE-VERSIONED-SHARED-OWNER-TYPE-VERSION"};
        return request;
    }

public:
    VersionedOwnedValue(Request request, Ordinary owner)
        : request_(checked(request, false)), storage_(std::move(owner))
    {}

    VersionedOwnedValue(Request request, Expanded owner)
        : request_(checked(request, true)), storage_(std::move(owner))
    {}

    ~VersionedOwnedValue()
    {
        if (request_.contractVersion == ExpandedVersion)
            storage_.expanded.~Expanded();
        else
            storage_.ordinary.~Ordinary();
    }

    VersionedOwnedValue(const VersionedOwnedValue&) = delete;
    VersionedOwnedValue& operator=(const VersionedOwnedValue&) = delete;

    const Request& request() const noexcept { return request_; }

    const Ordinary& ordinary() const noexcept
    {
        return request_.contractVersion == ExpandedVersion ? missingOrdinary_ : storage_.ordinary;
    }

    const Expanded& expanded() const noexcept
    {
        return request_.contractVersion == ExpandedVersion ? storage_.expanded : missingExpanded_;
    }

    // Project a common result directly from the active owned alternative. This
    // performs one explicit version check and preserves reference return types.
    template <class Visitor>
    decltype(auto) visit(Visitor&& visitor) const
        noexcept(noexcept(std::forward<Visitor>(visitor)(std::declval<const Ordinary&>()))
            && noexcept(std::forward<Visitor>(visitor)(std::declval<const Expanded&>())))
    {
        static_assert(std::is_same_v<decltype(std::forward<Visitor>(visitor)(
                                         std::declval<const Ordinary&>())),
            decltype(std::forward<Visitor>(visitor)(std::declval<const Expanded&>()))>);
        if (request_.contractVersion == ExpandedVersion)
            return std::forward<Visitor>(visitor)(storage_.expanded);
        return std::forward<Visitor>(visitor)(storage_.ordinary);
    }

private:
    const Request request_;
    Storage storage_;
    inline static const Ordinary missingOrdinary_{};
    inline static const Expanded missingExpanded_{};
};

using VersionedSharedOwnerContractError = VersionedOwnedValueContractError;
template <class Request, class Ordinary, class Expanded, std::uint32_t ExpandedVersion>
using VersionedSharedOwner = VersionedOwnedValue<Request,
    std::shared_ptr<const Ordinary>,
    std::shared_ptr<const Expanded>,
    ExpandedVersion>;
} // namespace arp::core
