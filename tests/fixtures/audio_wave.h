#pragma once

#include "core/container/array_view.h"
#include "core/memory/cpu_allocator.h"

namespace audio_fixture {

// Explicit little-endian RIFF/WAVE PCM16 fixture; values are interleaved.
class Wave final {
public:
    static arp::core::pmr::Vector<std::uint8_t> pcm16(std::uint16_t channels,
        std::uint32_t rate,
        arp::core::ArrayView<const std::int16_t> samples)
    {
        arp::core::pmr::Vector<std::uint8_t> bytes;
        const auto text = [&](std::string_view value) {
            bytes.insert(bytes.end(), value.begin(), value.end());
        };
        const auto number = [&](std::uint32_t value, unsigned count) {
            for (unsigned i = 0; i < count; ++i)
                bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
        };
        text("RIFF");
        number(36 + static_cast<std::uint32_t>(samples.size() * 2), 4);
        text("WAVEfmt ");
        number(16, 4);
        number(1, 2);
        number(channels, 2);
        number(rate, 4);
        number(rate * channels * 2, 4);
        number(channels * 2, 2);
        number(16, 2);
        text("data");
        number(static_cast<std::uint32_t>(samples.size() * 2), 4);
        for (auto value : samples)
            number(static_cast<std::uint16_t>(value), 2);
        return bytes;
    }
};
} // namespace audio_fixture
