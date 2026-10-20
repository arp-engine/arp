#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_readback_preparation.h"
#include "texture_readback_layout_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuOriginalTextureReadbackPairFixture final {
public:
    using Pair = rhi_webgpu::WebGpuOriginalTextureReadbackPairProjection;
    using Destination = rhi_webgpu::WebGpuOriginalTextureReadbackDestination;
    using Budget = rhi_webgpu::WebGpuOriginalTextureReadbackPairBudget;
    static constexpr std::uint64_t cpuBytes = 65536u;

    static Budget budget() { return {1u, true, 1u, 1u, 8u, 2u, 3u, 4432u, 1696u, 1536u, cpuBytes}; }

    static void verifyContract()
    {
        Budget b;
        assert(std::string_view(Pair::validateBudget(b)) == "RHI-READBACK-PAIR-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Pair::validateBudget(b)) == "RHI-READBACK-PAIR-OPT-IN");
        b.prototypeOptIn = true;
        assert(std::string_view(Pair::validateBudget(b)) == "RHI-READBACK-PAIR-BUDGET");
        b = budget();
        assert(!Pair::validateBudget(b));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto result = Pair::lowering(backend);
            assert(result.diagnostic && result.validatorOnly == (backend == rhi::Backend::Null)
                && result.nativeFactBindingImplemented == (backend == rhi::Backend::WebGPU));
        }
        assert(!Pair::lowering(static_cast<rhi::Backend>(99)).nativeFactBindingImplemented);
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        assert(!Pair::prepareOriginal({}, b, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-PAIR-DESTINATION-OWNER"
            && !quota->liveBytes());
        Pair::Mip output, sentinel;
        sentinel.bufferOffsetBytes = UINT64_MAX;
        output = sentinel;
        assert(std::string_view(Pair::mapMipOriginal({}, {}, 0u, {}, nullptr, output))
                == "RHI-READBACK-PAIR-OWNER"
            && output.matches(sentinel));
        std::puts(
            "BINDLESS_RHI_READBACK_PAIR_CONTRACT_OK four-backend original-pair immutable-mip-facts no-emit-proof");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyNotReady(
        const std::shared_ptr<Destination>& destination, const char* expected)
    {
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        assert(!Pair::prepareOriginal(destination, budget(), quota, diagnostic)
            && std::string_view(diagnostic) == expected && !quota->liveBytes());
    }

    static auto verifyActual(const std::shared_ptr<Destination>& destination)
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto b = budget();
        const char* diagnostic = nullptr;
        std::shared_ptr<const Destination> original = destination;
        const auto factory = [&] {
            return Pair::prepareOriginal(original, b, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(quota, cpuBytes);
            assert(full.charged() && !factory());
        }
        assert(!quota->liveBytes());
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes()
                && !destination->validate());
        };
        --b.maxProjectedOwners;
        reject("RHI-READBACK-PAIR-CAPACITY");
        b = budget();
        --b.maxMipCopies;
        reject("RHI-READBACK-PAIR-CAPACITY");
        b = budget();
        --b.maxRetainedNativeBytes;
        reject("RHI-READBACK-PAIR-CAPACITY");
        b = budget();
        --b.maxRetainedContentBytes;
        reject("RHI-READBACK-PAIR-CAPACITY");
        b = budget();
        --b.maxUnpublishedResultBytes;
        reject("RHI-READBACK-PAIR-CAPACITY");
        b = budget();
        --b.maxCpuMetadataBytes;
        reject("RHI-READBACK-PAIR-QUOTA");
        b = budget();
        --b.maxAllocationRecords;
        reject("RHI-READBACK-PAIR-ALLOCATION-COUNT");
        b = budget();
        original = std::shared_ptr<const Destination>(quota, destination.get());
        reject("RHI-READBACK-PAIR-DESTINATION-OWNER");
        original = destination;
        auto pair = factory();
        assert(pair && !pair->validate() && pair->projectedOwnerCount() == 8u
            && pair->retainedNativeAllocationBytes() == 4432u
            && pair->layout() == destination->source()->layout()
            && pair->destination() == destination);
        verifyAllocationRecords(pair);
        auto forged = std::shared_ptr<const Pair>(quota, pair.get());
        const auto& input = pair->layout()->inputOwner();
        auto observed = pair->layout()->mipCopies()[0];
        Pair::Mip output, sentinel;
        sentinel.bufferOffsetBytes = UINT64_MAX;
        output = sentinel;
        auto device = destination->nativeResource()->ownerDevice;
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(std::string_view(Pair::mapMipOriginal(forged, input, 0u, observed, device, output))
                == "RHI-READBACK-PAIR-OWNER"
            && output.matches(sentinel));
        forged.reset();
        auto forgedInput =
            std::shared_ptr<const rhi::TextureReadbackInputSnapshot>(quota, input.get());
        assert(
            std::string_view(Pair::mapMipOriginal(pair, forgedInput, 0u, observed, device, output))
                == "RHI-READBACK-PAIR-INPUT-OWNER"
            && output.matches(sentinel));
        forgedInput.reset();
        assert(std::string_view(Pair::mapMipOriginal(pair, input, 0u, observed, nullptr, output))
                == "RHI-READBACK-PAIR-DEVICE"
            && output.matches(sentinel));

        for (std::size_t i = 0u; i < pair->layout()->mipCopies().size(); ++i) {
            const auto& mip = pair->layout()->mipCopies()[i];
            output = sentinel;
            for (const auto change : TextureReadbackLayoutFixture::mipMutations()) {
                observed = mip;
                change(observed);
                assert(
                    std::string_view(Pair::mapMipOriginal(pair, input, i, observed, device, output))
                        == "RHI-TEXTURE-READBACK-LAYOUT-MIP-MISMATCH"
                    && output.matches(sentinel));
            }
            assert(
                !Pair::mapMipOriginal(pair, input, i, mip, device, output) && output.matches(mip));
        }
        output = sentinel;
        assert(std::string_view(Pair::mapMipOriginal(pair, input, 2u, observed, device, output))
                == "RHI-TEXTURE-READBACK-LAYOUT-MIP-INDEX"
            && output.matches(sentinel));
        auto& texture = *destination->source()->resource().texture;
        const auto dimension = texture.dimension;
        texture.dimension = WGPUTextureDimension_1D;
        assert(pair->validate());
        texture.dimension = dimension;
        assert(!pair->validate()
            && std::string_view(pair->executionDiagnostic())
                == "RHI-READBACK-PAIR-EXECUTION-UNPROVEN");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        std::printf(
            "BINDLESS_RHI_READBACK_PAIR_NATIVE_OK %llu owners8 native4432 content1696 result1536 mips2 fields21 immutable-layout allocation0 no-emit-proof\n",
            static_cast<unsigned long long>(peak));
        return pair;
    }

    static void verifyAllocationRecords(const std::shared_ptr<const Pair>& pair)
    {
        using Inventory = core::SharedAllocationProjection;
        using Status = Inventory::Status;
        assert(pair->allocationRecordCount() == 3u);
        std::array<Inventory::Record, 4> records;
        for (std::size_t i = 0u; i < 3u; ++i)
            records[i] = pair->allocationRecordAt(i);
        const auto& destination = *pair->destination();
        const auto& source = *destination.source();
        assert(records[0].bytes == 1536u && records[1].bytes == 1360u && records[2].bytes == 1536u
            && core::SharedOwnerProjection::sameOwner(records[1].backing, source.resource().texture)
            && core::SharedOwnerProjection::sameOwner(
                records[1].allocation, source.resource().allocationOwner())
            && core::SharedOwnerProjection::sameOwner(
                records[2].backing, destination.nativeResource())
            && core::SharedOwnerProjection::sameOwner(
                records[2].allocation, destination.nativeResource()->allocationReleaseToken));
        assert(!pair->allocationRecordAt(3u).allocation);
        const auto mapper = [&](std::size_t i) {
            return records[i];
        };
        auto result = Inventory::inspect(3u, 3u, 4432u, mapper);
        assert(result.status == Status::Complete
            && result.bytes == pair->retainedNativeAllocationBytes());
        assert(Inventory::inspect(3u, 2u, 4432u, mapper).status == Status::CountCapacity);
        assert(Inventory::inspect(3u, 3u, 4431u, mapper).status == Status::ByteCapacity);
        // Repeated original records deduplicate; a conflicting backing/charge
        // under the exact same allocation control must fail deterministically.
        records[3] = records[0];
        result = Inventory::inspect(4u, 4u, 4432u, mapper);
        assert(result.status == Status::Complete && result.bytes == 4432u);
        records[3].backing = records[1].backing;
        assert(Inventory::inspect(4u, 4u, 4432u, mapper).status == Status::Alias);
        records[3] = records[0];
        ++records[3].bytes;
        assert(Inventory::inspect(4u, 4u, 4432u, mapper).status == Status::Alias);
        records[3] = records[0];
        records[3].allocation.reset();
        assert(Inventory::inspect(4u, 4u, 4432u, mapper).status == Status::MissingOwner);
        auto& native = *destination.nativeResource();
        auto original = native.allocationReleaseToken;
        native.allocationReleaseToken.reset();
        assert(pair->validate());
        native.allocationReleaseToken = std::move(original);
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!pair->validate());
        for (std::size_t i = 0u; i < 3u; ++i) {
            auto record = pair->allocationRecordAt(i);
            assert(record.bytes && record.allocation && record.backing);
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        std::puts(
            "BINDLESS_RHI_READBACK_ALLOCATION_RECORDS_OK original-staging-target-destination records3 dedup4432 alias-missing-capacity allocation0 no-domain-authority");
    }
#endif
};
} // namespace arp::test
