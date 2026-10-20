#pragma once
#include "indirect_argument_fixture.h"
#include "rhi/frontend/command_draw_validation.h"

#include <algorithm>

namespace arp::test {
class ZeroBaseIndirectSourceFixture final {
public:
    using Contract = rhi::ZeroBaseIndirectSourceConsumptionContract;
    using Args = rhi::IndirectArgumentConsumptionContract;
    using Order = core::UnsignedElementSpan::ByteOrder;
    using rhiCommand = rhi::DrawIndexedIndirectCommand;

    explicit ZeroBaseIndirectSourceFixture(Order order = Order::LittleEndian) : order_(order)
    {
        put({3u, 2u, 1u, -2, 1u});
        // Full source, including untouched prefix/tail, has explicit uint32 semantics.
        const auto extras =
            test::IndirectArgumentFixture<1u>::encode({0x12345678u, 0xabcdef01u, 0u, 0, 0u}, order);
        std::copy_n(extras.begin(), 8u, source.begin());
        std::copy_n(extras.begin(), 4u, source.begin() + 28u);
    }

    void put(rhiCommand draw)
    {
        const auto bytes = test::IndirectArgumentFixture<1u>::encode(draw, order_);
        std::copy(bytes.begin(), bytes.end(), source.begin() + 8u);
    }

    Args::Source input() const
    {
        return {source.data(),
            source.size(),
            8u,
            1u,
            20u,
            order_,
            Args::Origin::HostAuthoredImmutable,
            1u};
    }

    static Contract::Budget budget()
    {
        return {1u, true, 32u, 32u + Contract::physicalSnapshotBytes};
    }

    rhi::IndexInputConsumptionContract indices() const
    {
        return {index.data(),
            index.size(),
            rhi::IndexFormat::Uint16,
            Order::LittleEndian,
            rhi::IndexInputConsumptionContract::PrimitiveRestart::Disabled};
    }

    Contract make() const
    {
        const auto immutable = indices();
        return {input(), parameters, consumer, budget(), immutable};
    }

    std::array<std::uint8_t, 32u> source{};
    std::array<std::uint8_t, 8u> index{0xff, 0xff, 3u, 0u, 4u, 0u, 5u, 0u};
    rhi::ZeroBaseIndirectProducerParams parameters{2u, 3u, 0u, 1u};
    rhi::BindlessIndirectInstanceConsumerLayout consumer{1u, 7u, 0u, 3u};

private:
    Order order_;
};
} // namespace arp::test
