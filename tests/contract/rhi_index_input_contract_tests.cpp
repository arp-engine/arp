#include "rhi/frontend/command_draw_validation.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error Index contract tests require assertions
#endif

namespace {
using namespace arp;
using Span = core::UnsignedElementSpan;
using Contract = rhi::IndexInputConsumptionContract;

Contract temporarySource()
{
    const std::array<std::uint8_t, 6> source{2, 0, 6, 0, 4, 0};
    return {source.data(),
        source.size(),
        rhi::IndexFormat::Uint16,
        Span::ByteOrder::LittleEndian,
        Contract::PrimitiveRestart::Disabled};
}

void expect(const char* actual, const char* expected)
{
    assert(actual && std::strcmp(actual, expected) == 0);
}

void typedSpans()
{
    // Unaligned source bytes and declared big endian are portable inputs.
    const std::array<std::uint8_t, 7> source{0xff, 0, 2, 0, 6, 0, 4};
    Span input(source.data() + 1, 6u, Span::Width::Uint16, Span::ByteOrder::BigEndian);
    Span::Extrema out;
    assert(!input.diagnostic() && input.size() == 3u);
    assert(!input.extrema(0u, 3u, out) && out.minimum == 2u && out.maximum == 6u);
    assert(!input.extrema(1u, 2u, out) && out.minimum == 4u && out.maximum == 6u);
    expect(input.extrema(1u, 3u, out), "CORE-UNSIGNED-SPAN-RANGE-INVALID");
    assert(!out.minimum && !out.maximum);
    expect(input.extrema(UINT64_MAX, 2u, out), "CORE-UNSIGNED-SPAN-RANGE-INVALID");
    expect(input.extrema(0u, 0u, out), "CORE-UNSIGNED-SPAN-RANGE-INVALID");
    core::ByteBuffer normalized{&core::defaultCpuMemoryResource()};
    assert(!input.copyLittleEndian(normalized));
    const std::array<std::uint8_t, 6> expected{2, 0, 6, 0, 4, 0};
    assert(normalized.size() == expected.size()
        && std::equal(normalized.begin(), normalized.end(), expected.begin()));
    Span roundtrip(
        normalized.data(), normalized.size(), Span::Width::Uint16, Span::ByteOrder::LittleEndian);
    assert(!roundtrip.extrema(0u, 3u, out) && out.minimum == 2u && out.maximum == 6u);
    std::uint32_t element = 0u;
    assert(roundtrip.read(1u, element) && element == 6u);
    assert(!roundtrip.read(3u, element) && !element);
    assert(!roundtrip.read(UINT64_MAX, element) && !element);
    Span invalid(source.data(), source.size(), Span::Width::Uint16, Span::ByteOrder::BigEndian);
    expect(invalid.diagnostic(), "CORE-UNSIGNED-SPAN-BYTES-INVALID");
    const auto saved = normalized;
    expect(invalid.copyLittleEndian(normalized), "CORE-UNSIGNED-SPAN-BYTES-INVALID");
    assert(saved == normalized);
    Span missing(nullptr, 6u, Span::Width::Uint16, Span::ByteOrder::BigEndian);
    expect(missing.diagnostic(), "CORE-UNSIGNED-SPAN-BYTES-INVALID");
    Span empty(source.data(), 0u, Span::Width::Uint16, Span::ByteOrder::BigEndian);
    expect(empty.diagnostic(), "CORE-UNSIGNED-SPAN-BYTES-INVALID");
    Span width(source.data(), 6u, static_cast<Span::Width>(255), Span::ByteOrder::LittleEndian);
    expect(width.diagnostic(), "CORE-UNSIGNED-SPAN-WIDTH-UNKNOWN");
    assert(!width.elementBytes());
    Span endian(source.data(), 6u, Span::Width::Uint16, static_cast<Span::ByteOrder>(255));
    expect(endian.diagnostic(), "CORE-UNSIGNED-SPAN-BYTE-ORDER-UNKNOWN");
    const std::array<std::uint8_t, 8> wide{0, 0, 0, 1, 0xff, 0xff, 0xff, 0xff};
    Span u32(wide.data(), wide.size(), Span::Width::Uint32, Span::ByteOrder::BigEndian);
    assert(!u32.extrema(0u, 2u, out) && out.minimum == 1u && out.maximum == UINT32_MAX);
}

void snapshots()
{
    auto expiredSource = temporarySource();
    Contract::DrawRange observed;
    assert(!expiredSource.analyzeDraw(0u, 3u, 0, observed) && observed.firstVertex == 2u
        && observed.vertexCount == 5u);
    auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
    std::uint64_t charge = 0;
    {
        core::CpuMemoryQuotaScope scope(quota);
        std::array<std::uint8_t, 10> source{0xff, 0xff, 0, 2, 0, 6, 0, 4, 0xff, 0xff};
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        Contract input(source.data(),
            source.size(),
            rhi::IndexFormat::Uint16,
            Span::ByteOrder::BigEndian,
            Contract::PrimitiveRestart::Disabled);
        std::pmr::set_default_resource(previous);
        assert(!input.diagnostic() && input.elementCount() == 5u);
        charge = quota->liveBytes();
        assert(
            charge && core::inspectCpuAllocationCharge(input.bytes().data()).quota == quota.get());
        source.fill(0u);
        Contract::DrawRange range;
        assert(!input.analyzeDraw(1u, 3u, -2, range));
        assert(range.firstIndex == 1u && range.indexCount == 3u && range.firstVertex == 0u
            && range.vertexCount == 5u && range.baseVertex == -2);
        core::pmr::Vector<rhi::VertexBufferLayoutDesc> layouts{&core::defaultCpuMemoryResource()};
        layouts.push_back(
            {3u, 8u, rhi::VertexInputRate::Vertex, {{0u, 0u, rhi::VertexFormat::Float32x2}}});
        rhi::VertexInputConsumptionContract vertex(layouts);
        assert(!vertex.validateRange(3u, 40u, 0u, range.vertexCount, range.firstVertex, 1u, 0u));
        expect(vertex.validateRange(3u, 39u, 0u, range.vertexCount, range.firstVertex, 1u, 0u),
            "RHI-VERTEX-INPUT-FETCH-RANGE");
        assert(!input.analyzeDraw(2u, 1u, 7, range) && range.firstVertex == 13u
            && range.vertexCount == 1u);
        expect(input.analyzeDraw(1u, 3u, -3, range), "RHI-INDEX-INPUT-BASE-VERTEX-NEGATIVE");
        assert(!range.indexCount && !range.vertexCount);
        expect(input.analyzeDraw(4u, 2u, 0, range), "CORE-UNSIGNED-SPAN-RANGE-INVALID");
        expect(input.analyzeDraw(UINT64_MAX, UINT64_MAX, 0, range),
            "CORE-UNSIGNED-SPAN-RANGE-INVALID");
        // Restart is disabled: its usual sentinel remains an ordinary index.
        assert(!input.analyzeDraw(0u, 5u, 0, range) && range.firstVertex == 2u
            && range.vertexCount == 65534u);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0; i < 1000u; ++i)
            assert(!input.analyzeDraw(1u, 3u, -2, range) && range.vertexCount == 5u);
        std::pmr::set_default_resource(previous);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    assert(!quota->liveBytes());
    const std::array<std::uint8_t, 10> source{0xff, 0xff, 0, 2, 0, 6, 0, 4, 0xff, 0xff};
    auto smaller = core::makeShared<core::CpuMemoryQuota>(charge - 1u);
    bool rejected = false;
    try {
        core::CpuMemoryQuotaScope scope(smaller);
        Contract input(source.data(),
            source.size(),
            rhi::IndexFormat::Uint16,
            Span::ByteOrder::BigEndian,
            Contract::PrimitiveRestart::Disabled);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    assert(rejected && !smaller->liveBytes());
    Contract unknown(source.data(),
        source.size(),
        static_cast<rhi::IndexFormat>(255),
        Span::ByteOrder::BigEndian,
        Contract::PrimitiveRestart::Disabled);
    expect(unknown.diagnostic(), "RHI-INDEX-INPUT-FORMAT-UNKNOWN");
    assert(unknown.bytes().empty());
    for (auto restart :
        {Contract::PrimitiveRestart::Enabled, static_cast<Contract::PrimitiveRestart>(255)}) {
        Contract input(source.data(),
            source.size(),
            rhi::IndexFormat::Uint16,
            Span::ByteOrder::BigEndian,
            restart);
        expect(input.diagnostic(), "RHI-INDEX-INPUT-RESTART-UNSUPPORTED");
        assert(input.bytes().empty());
    }
    const std::array<std::uint8_t, 8> wide{0, 0, 0, 1, 0xff, 0xff, 0xff, 0xff};
    Contract input(wide.data(),
        wide.size(),
        rhi::IndexFormat::Uint32,
        Span::ByteOrder::BigEndian,
        Contract::PrimitiveRestart::Disabled);
    Contract::DrawRange range;
    assert(!input.analyzeDraw(1u, 1u, INT32_MAX, range)
        && range.firstVertex == std::uint64_t{UINT32_MAX} + INT32_MAX && range.vertexCount == 1u);
    assert(!input.analyzeDraw(1u, 1u, INT32_MIN, range) && range.firstVertex == INT32_MAX);
    assert(!input.analyzeDraw(0u, 2u, INT32_MAX, range) && range.vertexCount == UINT32_MAX);
    expect(input.analyzeDraw(0u, 2u, INT32_MIN, range), "RHI-INDEX-INPUT-BASE-VERTEX-NEGATIVE");
    auto moved = std::move(input);
    assert(!moved.analyzeDraw(1u, 1u, 0, range) && range.firstVertex == UINT32_MAX);
}

void borrowedInitializedBytes()
{
    const std::array<std::uint8_t, 16u> source{3, 0, 1, 0, 4, 0, 2, 0, 0, 0, 7, 0, 6, 0, 5, 0};
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    Contract::DrawRange out;
    assert(!Contract::analyzeLittleEndianBytes(
               source.data(), source.size(), rhi::IndexFormat::Uint16, 2u, 0u, 3u, -1, 3u, out)
        && out.firstVertex == 0u && out.vertexCount == 4u && out.firstIndex == 0u
        && out.indexCount == 3u);
    assert(!Contract::analyzeLittleEndianBytes(
               source.data(), source.size(), rhi::IndexFormat::Uint16, 14u, 0u, 1u, -5, 1u, out)
        && out.firstVertex == 0u && out.vertexCount == 1u);
    expect(Contract::analyzeLittleEndianBytes(
               source.data(), source.size(), rhi::IndexFormat::Uint16, 1u, 0u, 1u, 0, 8u, out),
        "RHI-INDEX-INPUT-BINDING-OFFSET");
    expect(
        Contract::analyzeLittleEndianBytes(
            source.data(), source.size(), rhi::IndexFormat::Uint16, UINT64_MAX, 0u, 1u, 0, 8u, out),
        "RHI-INDEX-INPUT-BINDING-OFFSET");
    expect(Contract::analyzeLittleEndianBytes(
               source.data(), source.size(), rhi::IndexFormat::Uint16, 14u, 0u, 2u, 0, 8u, out),
        "CORE-UNSIGNED-SPAN-RANGE-INVALID");
    expect(Contract::analyzeLittleEndianBytes(
               source.data(), source.size(), rhi::IndexFormat::Uint16, 0u, 0u, 4u, 0, 3u, out),
        "RHI-INDEX-INPUT-ANALYSIS-CAPACITY");
    expect(Contract::analyzeLittleEndianBytes(
               source.data(), source.size(), rhi::IndexFormat::Uint16, 0u, 0u, 1u, -4, 8u, out),
        "RHI-INDEX-INPUT-BASE-VERTEX-NEGATIVE");
    expect(Contract::analyzeLittleEndianBytes(
               nullptr, source.size(), rhi::IndexFormat::Uint16, 0u, 0u, 1u, 0, 8u, out),
        "CORE-UNSIGNED-SPAN-BYTES-INVALID");
    assert(!out.indexCount && !out.vertexCount
        && core::defaultCpuAllocator().stats().allocationCount == allocations);
}
} // namespace

int main()
{
    typedSpans();
    snapshots();
    borrowedInitializedBytes();
    std::puts(
        "IndexInputConsumption: exact owned normalized indices, selected span/signed baseVertex, sentinel/bounds/quota/allocator contracts PASS; no native authority");
}
