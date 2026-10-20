#pragma once
#include <cstdint>
#include <limits>

namespace arp::core::runtime {
// Allocation-free evidence join. Each independently named participant must
// arrive exactly once; receipt alone never proves a successful operation.
class CompletionEvidenceBatch final {
public:
    static constexpr std::uint32_t maximumParticipants = std::numeric_limits<std::uint64_t>::digits;

    explicit CompletionEvidenceBatch(std::uint32_t participants) noexcept
        : valid_(participants && participants <= maximumParticipants), count_(participants),
          expected_(valid_
                  ? (participants == maximumParticipants ? UINT64_MAX
                                                         : (std::uint64_t{1} << participants) - 1u)
                  : 0u)
    {}

    bool observe(std::uint32_t participant, bool proven) noexcept
    {
        if (!valid_ || participant >= count_) {
            successful_ = false;
            return false;
        }
        const auto bit = std::uint64_t{1} << participant;
        if (received_ & bit) {
            successful_ = false;
            return false;
        }
        received_ |= bit;
        successful_ &= proven;
        return true;
    }

    bool allReceived() const noexcept { return valid_ && received_ == expected_; }

    bool proven() const noexcept { return allReceived() && successful_; }

    bool hasReceived(std::uint32_t participant) const noexcept
    {
        return valid_ && participant < count_
            && (received_ & (std::uint64_t{1} << participant)) != 0u;
    }

private:
    const bool valid_;
    const std::uint32_t count_;
    const std::uint64_t expected_;
    std::uint64_t received_ = 0u;
    bool successful_ = true;
};
} // namespace arp::core::runtime
