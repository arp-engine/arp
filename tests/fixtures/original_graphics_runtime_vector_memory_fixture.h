#pragma once
#include "original_graphics_runtime_memory_fixture.h"

namespace arp::test {
class OriginalGraphicsRuntimeVectorMemoryFixture final {
public:
    template <class Root, class Sources>
    static void inspect(const Root& root,
        const Sources& sources,
        rhi::GpuDevice* device,
        bool fault,
        std::uint64_t stride,
        std::uint64_t capacity,
        std::uint64_t probeBytes,
        bool undersized = false)
    {
        using Source = rhi::OriginalCommandBindlessRootSource;
        const std::array<OriginalGraphicsRuntimeMemoryFixture::Binding, 2u> bindings{
            {{0u, 0u, stride, capacity, rhi::ShaderStage::Vertex},
                {0u, 0u, stride, capacity, rhi::ShaderStage::Fragment}}};
        OriginalGraphicsRuntimeMemoryFixture::inspect(
            root, sources, device, fault, undersized, bindings);
        if (!device)
            return;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{root->originalMetadataQuota()}, [&]() noexcept -> const char* {
                const auto* admission = Source::validateOriginalGraphicsBufferRanges(root);
                assert(undersized ? admission != nullptr : admission == nullptr);
                for (const char* entry :
                    {"vertexMain", fault ? "faultFragment" : "cleanFragment"}) {
                    rhi::ShaderBufferMemberMemory facts;
                    rhi::BindlessResourceAccess access;
                    std::size_t index = SIZE_MAX;
                    assert(!Source::originalOrdinaryBufferMemberMemory(
                        root, entry, 0u, 1u, facts, access, index, 2u));
                    assert(!facts.hasRuntimeArray && facts.sourceOccupiedBytes == probeBytes
                        && facts.targetMinimumBytes == probeBytes && !facts.sourceElementCapacity
                        && access.bufferSize == probeBytes && index < sources->sources().size());
                }
                return nullptr;
            }));
        std::puts("BINDLESS_RUNTIME_VECTOR_ROOT_MEMORY2_OK VS-FS original-owner hot0");
    }
};
} // namespace arp::test
