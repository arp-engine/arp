#include "core/algorithm/strided_index_range.h"
#include "rhi/backends/metal/buffer_validation.h"
#include "rhi/backends/metal/metal_binding_facts.h"
#include "rhi/backends/metal/metal_native_descriptors.h"

#include <cassert>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error Contract tests require assertions
#endif
int main()
{
    using namespace arp;
    using namespace rhi_metal;
    using rhi::ShaderStage;
    {
        static_assert(
            std::is_base_of_v<core::SharedObjectIdentity, MetalRhiNativePreparedComputeBindings>);
        static_assert(
            std::is_base_of_v<core::SharedObjectIdentity, MetalRhiNativePreparedGraphicsBindings>);
        static_assert(!std::is_copy_constructible_v<MetalRhiNativePreparedComputeBindings>);
        auto prepared = core::makeShared<MetalRhiNativePreparedComputeBindings>();
        auto other = core::makeShared<MetalRhiNativePreparedComputeBindings>();
        const auto alias =
            std::shared_ptr<const MetalRhiNativePreparedComputeBindings>(other, prepared.get());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(prepared->sharedOwnerMatches(prepared));
            assert(!prepared->sharedOwnerMatches(alias));
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        // Empty identity probes validate no GPU semantics.
    }
    std::uint64_t index = 1;
    core::StridedIndexRange exact(UINT32_MAX - 6u, 3u, 3u, UINT32_MAX);
    assert(exact.valid() && exact.size() == 3u && exact.index(2u, index) && index == UINT32_MAX);
    assert(!exact.index(3u, index) && !index);
    assert(!core::StridedIndexRange(UINT32_MAX - 5u, 3u, 3u, UINT32_MAX).valid());
    assert(!core::StridedIndexRange(0, 0, 1).valid() && !core::StridedIndexRange(0, 1, 0).valid());
    assert(!core::StridedIndexRange(UINT64_MAX, 2, 1).valid());
    assert(core::StridedIndexRange(0, UINT64_MAX, 1).valid());
    MetalRhiStageBufferPointeeFacts facts;
    facts.stage = ShaderStage::Vertex;
    facts.version = 1u;
    facts.entries.push_back({2u, 4u, 2u, 1u, 0u, 0u, 1u, 16u, 32u, true, false});
    facts.entries.push_back({2u, 5u, 2u, 1u, 1u, 0u, 1u, 16u, 32u, true, false});
    MetalRhiStageBufferPointeeRange captured;
    const auto capture = [&](std::uint64_t offset = 0, std::uint64_t bytes = 32) {
        return MetalRhiStageBufferPointeeRangeValidator::capture(
            facts, ShaderStage::Vertex, 2, 5, 2, 1, offset, bytes, captured);
    };
    const auto expect = [&](const char* id, const char* expected) {
        assert(id && std::strcmp(id, expected) == 0);
    };
    {
        core::pmr::Vector<MetalRhiBufferBindingFact> scalar{&core::defaultCpuMemoryResource()};
        scalar.push_back({5u, 16u, 16u, 0u, 1u});
        const MetalRhiBufferBindingFact* resolved = nullptr;
        const auto validate = [&](std::uint32_t access,
                                  std::uint32_t element,
                                  std::uint64_t minimum,
                                  std::uint64_t bytes = 16u) {
            return MetalRhiScalarBufferAbiValidator::validate(
                scalar, 1u, 5u, bytes, access, element, minimum, resolved, "ExactScalarDomain");
        };
        assert(!validate(0u, 1u, 16u) && resolved && resolved->matches(scalar.front()));
        expect(validate(1u, 1u, 16u), "ExactScalarDomain");
        assert(!resolved);
        expect(validate(0u, 33u, 16u), "ExactScalarDomain");
        assert(!resolved);
        expect(validate(0u, 1u, 4u), "ExactScalarDomain");
        assert(!resolved);
        expect(validate(0u, 1u, 16u, 15u), "RHI-METAL-ORDINARY-OFFSET-RANGE");
        scalar.front() = {5u, 4u, 4u, 1u, 33u};
        assert(!validate(1u, 33u, 4u, 20u));
        assert(!validate(1u, 33u, 4u, 16u));
        scalar.push_back(scalar.front());
        expect(validate(1u, 33u, 4u), "RHI-METAL-ORDINARY-OFFSET-FACTS-DUPLICATE");
        assert(!resolved);
    }
    assert(!capture());
    expect(capture(0, 31), "RHI-METAL-ORDINARY-POINTEE-RANGE");
    expect(capture(1, 64), "RHI-METAL-ORDINARY-POINTEE-ALIGNMENT");
    assert(!capture(16, 48));
    expect(capture(UINT64_MAX - 15u, UINT64_MAX), "RHI-METAL-ORDINARY-POINTEE-RANGE");
    facts.version = 0;
    expect(capture(), "RHI-METAL-ORDINARY-POINTEE-FACTS-MISSING");
    facts.version = 1;
    facts.stage = ShaderStage::Fragment;
    expect(capture(), "RHI-METAL-ORDINARY-POINTEE-STAGE-MISMATCH");
    facts.stage = ShaderStage::Vertex;
    const auto original = facts.entries[1];
    facts.entries[1].requiredBytes = 0;
    expect(capture(), "RHI-METAL-ORDINARY-POINTEE-FACTS-MISSING");
    facts.entries[1] = original;
    facts.entries[1].readOnly = false;
    expect(capture(), "RHI-METAL-ORDINARY-POINTEE-DOMAIN-UNSUPPORTED");
    facts.entries[1] = original;
    facts.entries[1].argumentBuffer = true;
    expect(capture(), "RHI-METAL-ORDINARY-POINTEE-DOMAIN-UNSUPPORTED");
    facts.entries[1] = original;
    facts.entries[1].argumentIndexStride = 2;
    expect(capture(), "RHI-METAL-ORDINARY-POINTEE-MAP-MISMATCH");
    facts.entries[1] = original;
    facts.entries[1].arrayCount = 3;
    expect(capture(), "RHI-METAL-ORDINARY-POINTEE-MAP-MISMATCH");
    facts.entries[1] = original;
    facts.entries[1].element = 0;
    expect(capture(), "RHI-METAL-ORDINARY-POINTEE-MAP-MISMATCH");
    facts.entries[1] = original;
    facts.entries.push_back(original);
    expect(capture(), "RHI-METAL-ORDINARY-POINTEE-FACTS-DUPLICATE");
    facts.entries.pop_back();
    assert(!capture());
    facts.entries[1].elementType = 2;
    expect(MetalRhiStageBufferPointeeRangeValidator::validate(
               facts, ShaderStage::Vertex, 2, 5, 2, 1, 0, 32, captured),
        "RHI-METAL-ORDINARY-POINTEE-CAPTURE-MISMATCH");
    // Native metadata fixtures prove validator semantics only, never GPU ABI.
    assert(MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
        rhi::DescriptorType::ReadOnlyStorageBuffer, 2, false));
    assert(!MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
        rhi::DescriptorType::StorageBuffer, 2, false));
    assert(MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
        rhi::DescriptorType::UniformBuffer, 2, false));
    assert(!MetalRhiOrdinaryGraphicsResourceDomain::allowsDescriptor(
        rhi::DescriptorType::UniformBuffer, 0, false));
    MetalRhiStageBufferBindingFacts direct;
    direct.version = 1u;
    direct.stage = ShaderStage::Fragment;
    direct.entries.push_back({0u, 4u, 4u, 1u, 33u});
    const MetalRhiBufferBindingFact* resolved = nullptr;
    const auto validateAtomic = [&](std::uint64_t bytes = 4u) {
        return MetalRhiAtomicBufferAbiValidator::validate(direct, 0u, bytes, resolved);
    };
    assert(!validateAtomic() && resolved);
    MetalRhiStageBufferRange directCapture;
    assert(!MetalRhiStageBufferRangeValidator::capture(
        direct, ShaderStage::Fragment, 0u, 0u, 4u, directCapture));
    direct.entries[0].access = 0u;
    expect(MetalRhiStageBufferRangeValidator::validate(
               direct, ShaderStage::Fragment, 0u, 0u, 4u, directCapture),
        "RHI-METAL-ORDINARY-OFFSET-CAPTURE-MISMATCH");
    direct.entries[0].access = 1u;
    direct.entries[0].elementType = 1u;
    expect(MetalRhiStageBufferRangeValidator::validate(
               direct, ShaderStage::Fragment, 0u, 0u, 4u, directCapture),
        "RHI-METAL-ORDINARY-OFFSET-CAPTURE-MISMATCH");
    direct.entries[0].elementType = 33u;
    expect(validateAtomic(3u), "RHI-METAL-ORDINARY-OFFSET-RANGE");
    direct.entries[0].access = 0u;
    expect(validateAtomic(), "RHI-METAL-ATOMIC-BUFFER-ABI-DOMAIN");
    assert(!resolved);
    direct.entries[0].access = 1u;
    direct.entries[0].elementType = 1u;
    expect(validateAtomic(), "RHI-METAL-ATOMIC-BUFFER-ABI-DOMAIN");
    direct.entries[0].elementType = 33u;
    direct.entries[0].requiredBytes = 8u;
    expect(validateAtomic(8u), "RHI-METAL-ATOMIC-BUFFER-ABI-DOMAIN");
    direct.entries[0].requiredBytes = 4u;
    direct.stage = ShaderStage::Vertex;
    expect(validateAtomic(), "RHI-METAL-ATOMIC-BUFFER-ABI-STAGE");
    direct.stage = ShaderStage::Fragment;
    direct.version = 0u;
    expect(validateAtomic(), "RHI-METAL-ORDINARY-OFFSET-FACTS-MISSING");
    direct.version = 1u;
    direct.entries.push_back(direct.entries[0]);
    expect(validateAtomic(), "RHI-METAL-ORDINARY-OFFSET-FACTS-DUPLICATE");
    for (auto mode : {MetalRhiNativeStorageMode::Private,
             MetalRhiNativeStorageMode::Shared,
             MetalRhiNativeStorageMode::Managed}) {
        assert(!MetalRhiTrackedBufferContract::validateCreation(4u, mode, 4u));
        expect(MetalRhiTrackedBufferContract::validateCreation(5u, mode, 4u),
            "RHI-METAL-TRACKED-BUFFER-SIZE");
        expect(MetalRhiTrackedBufferContract::validateCreation(0u, mode, 4u),
            "RHI-METAL-TRACKED-BUFFER-SIZE");
        expect(MetalRhiTrackedBufferContract::validateCreation(4u, mode, 0u),
            "RHI-METAL-TRACKED-BUFFER-DEVICE-LIMIT");
    }
    for (auto mode : {MetalRhiNativeStorageMode::None,
             MetalRhiNativeStorageMode::Memoryless,
             static_cast<MetalRhiNativeStorageMode>(255)})
        expect(MetalRhiTrackedBufferContract::validateCreation(4u, mode, 4u),
            "RHI-METAL-TRACKED-BUFFER-STORAGE-POLICY");
    assert(!MetalRhiTrackedBufferContract::validateQuery(true, 2u));
    expect(MetalRhiTrackedBufferContract::validateQuery(false, 2u),
        "RHI-METAL-TRACKED-BUFFER-QUERY-UNAVAILABLE");
    for (auto mode : {0u, 1u, UINT32_MAX})
        expect(MetalRhiTrackedBufferContract::validateQuery(true, mode),
            "RHI-METAL-TRACKED-BUFFER-QUERY-MISMATCH");
}
