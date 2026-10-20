#include "../fixtures/metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/buffer_validation.h"
#include "rhi/frontend/command_draw_validation.h"
#include "shader/fragment_atomic_write_proof.h"

#import <Metal/Metal.h>
#include <cstdio>

#ifdef NDEBUG
#error Native write query tests require assertions
#endif

namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_metal;
static_assert(MetalRhiAtomicBufferAbiValidator::readWriteAccess == MTLBindingAccessReadWrite);
static_assert(MetalRhiAtomicBufferAbiValidator::uintElementType == MTLDataTypeUInt);
static_assert(MetalRhiTrackedBufferContract::trackedMode == MTLHazardTrackingModeTracked);

class WriteQueryFixture final {
public:
    static DescriptorLayoutDesc layout()
    {
        DescriptorLayoutDesc result;
        result.name = "AtomicWriteQuery";
        result.bindings = {{3u, DescriptorType::StorageBuffer, ShaderStage::Fragment}};
        return result;
    }

    WriteQueryFixture(const char* vs, const char* fs) : pass(vs, fs, false, layout()) {}

    void run()
    {
        auto& f = pass.graphics;
        MetalRhiNativeArgumentBindingPayload binding;
        binding.binding = 3u;
        binding.type = DescriptorType::StorageBuffer;
        binding.stages = ShaderStage::Fragment;
        binding.resourceKind = DescriptorResourceKind::Buffer;
        binding.argumentKind = MetalRhiArgumentKind::Buffer;
        MetalRhiPreparedBindingLocation location;
        assert(f.provider->stages()[1]
                ->resolve(binding, 0u, f.provider->native()->nativeBindingStages, location)
                .success);
        assert(location.active && !location.array);
        const MetalRhiBufferBindingFact* fact = nullptr;
        const auto* diagnosticId = MetalRhiAtomicBufferAbiValidator::validate(
            f.provider->native()->graphicsBufferFacts[1], location.slot, 4u, fact);
        if (diagnosticId)
            std::fprintf(stderr, "Atomic query failure: %s\n", diagnosticId);
        assert(!diagnosticId && fact);
        std::printf(
            "atomic query: stage=Fragment slot=%u access=%u type=%u alignment=%llu span=%llu\n",
            fact->slot,
            fact->access,
            fact->elementType,
            static_cast<unsigned long long>(fact->alignment),
            static_cast<unsigned long long>(fact->requiredBytes));
        auto buffer = f.device->createBuffer({"AtomicQueryOnly",
            8u,
            ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderWrite});
        assert(buffer);
        const auto registered = f.registry->find(buffer->id());
        assert(registered && registered->buffer);
        const auto native =
            reinterpret_cast<id<MTLBuffer>>(registered->buffer->resource.nativeObject.get());
        assert(native);
        assert(registered->buffer->resource.hazardsExplicitlyTracked
            && native.hazardTrackingMode == MTLHazardTrackingModeTracked);
        const MetalRhiNativeResourceObservation observation(registered->buffer->resource);
        assert(observation.matches(registered->buffer->resource));
        registered->buffer->resource.hazardsExplicitlyTracked = false;
        assert(!observation.matches(registered->buffer->resource));
        registered->buffer->resource.hazardsExplicitlyTracked = true;
        std::printf(
            "independent production buffer hazard query=%lu; explicit tracked selection/query\n",
            static_cast<unsigned long>(native.hazardTrackingMode));
        const auto device =
            reinterpret_cast<id<MTLDevice>>(f.provider->native()->nativeDevice.get());
        assert(device);
        // Actual native substrate query only. This fixture owner cannot replace
        // the production factory's missing explicit hazard selection contract.
        const auto tracked = [device newBufferWithLength:8u
                                                 options:MTLResourceStorageModeShared |
            MTLResourceHazardTrackingModeTracked];
        assert(tracked && tracked.hazardTrackingMode == MTLHazardTrackingModeTracked);
        std::printf("explicit native tracked buffer query=%lu; query-only fixture\n",
            static_cast<unsigned long>(tracked.hazardTrackingMode));
        [tracked release];
        for (auto storage : {MetalRhiNativeStorageMode::Private,
                 MetalRhiNativeStorageMode::Shared,
                 MetalRhiNativeStorageMode::Managed}) {
            auto object = createMetalRhiTrackedNativeBufferObject(
                f.provider->native()->nativeDevice, 8u, storage, f.diagnostic);
            assert(object
                && reinterpret_cast<id<MTLBuffer>>(object.get()).hazardTrackingMode
                    == MTLHazardTrackingModeTracked);
        }
        for (auto storage : {MetalRhiNativeStorageMode::None,
                 MetalRhiNativeStorageMode::Memoryless,
                 static_cast<MetalRhiNativeStorageMode>(255)}) {
            assert(!createMetalRhiTrackedNativeBufferObject(
                f.provider->native()->nativeDevice, 8u, storage, f.diagnostic));
            assert(f.diagnostic == "RHI-METAL-TRACKED-BUFFER-STORAGE-POLICY");
        }
        assert(!createMetalRhiTrackedNativeBufferObject(
            {}, 8u, MetalRhiNativeStorageMode::Shared, f.diagnostic));
        assert(f.diagnostic == "RHI-METAL-TRACKED-BUFFER-DEVICE-UNAVAILABLE");
        assert(!createMetalRhiTrackedNativeBufferObject(f.provider->native()->nativeDevice,
            0u,
            MetalRhiNativeStorageMode::Shared,
            f.diagnostic));
        assert(f.diagnostic == "RHI-METAL-TRACKED-BUFFER-SIZE");
        assert(!createMetalRhiTrackedNativeBufferObject(f.provider->native()->nativeDevice,
            UINT64_MAX,
            MetalRhiNativeStorageMode::Shared,
            f.diagnostic));
        assert(f.diagnostic == "RHI-METAL-TRACKED-BUFFER-SIZE");
        assert(f.set
                ->write({3u,
                    DescriptorType::StorageBuffer,
                    buffer->id(),
                    DescriptorResourceKind::Buffer})
                .success);
        auto prepared = f.prepare();
        auto renderPass = pass.prepare();
        auto command = pass.command(128u);
        assert(!command->recordOrdinaryPreparedRenderPassBegin(renderPass, prepared, f.diagnostic));
        assert(f.diagnostic.find("RESOURCE-DOMAIN-UNIMPLEMENTED") != f.diagnostic.npos);
        assert(!command->stats()->nativeRenderEncoderCreated && !command->stats()->nativeDrawEncoded
            && !command->retentionStorage()->resourceOwnerCount());
        assert(!command->recordBufferBarrier(f.diagnostic));
        assert(f.diagnostic.find("OPERATION-UNIMPLEMENTED") != f.diagnostic.npos);
        command->abandonUnsubmitted();
        assert(!semantics::GraphicsStorageWriteLowering::current(Backend::Metal).executable);
        std::puts(
            "Actual Metal atomic ABI/explicit tracked production creation/query PASS; bounded writes/barriers/public execution remain closed");
    }

private:
    test::OrdinaryGraphicsPassFixture pass;
};
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    @autoreleasepool {
        WriteQueryFixture(argv[1], argv[2]).run();
    }
}
