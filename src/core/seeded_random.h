#pragma once
#include "reflection/reflection_macros.h"

#include <cstdint>

namespace arp::core {
// PCG-XSH-RR 64/32, fixed stream; zero is a valid explicit seed.
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class SeededRandom final {
public:
    explicit SeededRandom(std::uint64_t seed)
    {
        next();
        state_ += seed;
        next();
    }

    std::uint32_t next()
    {
        const auto old = state_;
        state_ = old * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto value = std::uint32_t(((old >> 18u) ^ old) >> 27u);
        const auto rotation = std::uint32_t(old >> 59u);
        return (value >> rotation) | (value << ((-rotation) & 31u));
    }
    AFUNC()

    double unit() { return double(next()) / 4294967296.0; }

    bool bounded(std::uint32_t bound, std::uint32_t& value)
    {
        if (!bound)
            return false;
        const auto threshold = std::uint32_t(-bound) % bound;
        for (;;) {
            const auto r = next();
            if (r >= threshold) {
                value = r % bound;
                return true;
            }
        }
    }

private:
    std::uint64_t state_ = 0;
};
} // namespace arp::core
