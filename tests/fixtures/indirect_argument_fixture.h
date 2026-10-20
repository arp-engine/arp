#pragma once
#include "rhi/frontend/command_draw_validation.h"

#include <array>
#include <cassert>

namespace arp::test {
// Explicit schema authoring for contract/native tests, independent of host
// alignment/endianness. Production preparation uses UnsignedElementSpan.
template <std::size_t Count> class IndirectArgumentFixture final {
public:
    using Contract = rhi::IndirectArgumentConsumptionContract;
    using Order = core::UnsignedElementSpan::ByteOrder;

    explicit IndirectArgumentFixture(
        const std::array<rhi::DrawIndexedIndirectCommand, Count>& draws,
        Order order = Order::LittleEndian)
        : order_(order)
    {
        for (std::size_t i = 0u; i < Count; ++i)
            set(i, draws[i]);
    }

    static auto encode(rhi::DrawIndexedIndirectCommand draw, Order order)
    {
        assert(order == Order::LittleEndian || order == Order::BigEndian);
        const std::array<std::uint32_t, 5> words{draw.indexCount,
            draw.instanceCount,
            draw.firstIndex,
            static_cast<std::uint32_t>(draw.vertexOffset),
            draw.firstInstance};
        std::array<std::uint8_t, Contract::recordBytes> bytes{};
        for (std::size_t i = 0u; i < words.size(); ++i)
            for (std::size_t j = 0u; j < sizeof(std::uint32_t); ++j)
                bytes[i * sizeof(std::uint32_t) + j] = static_cast<std::uint8_t>(words[i]
                    >> ((order == Order::LittleEndian ? j : sizeof(std::uint32_t) - 1u - j) * 8u));
        return bytes;
    }

    void set(std::size_t ordinal, rhi::DrawIndexedIndirectCommand draw)
    {
        assert(ordinal < Count);
        const auto record = encode(draw, order_);
        std::copy(record.begin(), record.end(), bytes.begin() + ordinal * Contract::recordBytes);
    }

    typename Contract::Source source() const
    {
        return {bytes.data(),
            bytes.size(),
            0u,
            Count,
            Contract::recordBytes,
            order_,
            Contract::Origin::HostAuthoredImmutable,
            1u};
    }

    typename Contract::Budget budget() const
    {
        return {Count,
            bytes.size(),
            Count * (Contract::recordBytes + sizeof(typename Contract::DrawObservation))};
    }

    std::array<std::uint8_t, Count * Contract::recordBytes> bytes{};

private:
    Order order_;
};
} // namespace arp::test
