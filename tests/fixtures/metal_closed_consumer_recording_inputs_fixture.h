#pragma once
#include "cpu_quota_factory_fixture.h"
#include "metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class MetalClosedConsumerRecordingInputsFixture final {
    using Builder = rhi_metal::MetalRhiClosedConsumerRecordingInputs;
    using Contract = Builder::Contract;
    using Buffers = Builder::Producer::Buffers;

public:
    explicit MetalClosedConsumerRecordingInputsFixture(OrdinaryGraphicsFixture& f) : f_(f) {}

    void verify(const std::shared_ptr<const Builder::Issuance>& root)
    {
        using namespace rhi;
        using namespace rhi_metal;
        core::CpuMemoryQuotaScope scope(f_.quota);
        const auto before = f_.device->memoryStats().allocatedBytes;
        const auto prepare = [&]() -> std::optional<Buffers> {
            try {
                Buffers buffers;
                for (std::size_t i = 0u; i < buffers.size(); ++i) {
                    const auto bytes =
                        i ? sizeof(IndirectInstanceBase) : sizeof(DrawIndexedIndirectCommand);
                    buffers[i] =
                        core::shareUnique(f_.device->createBuffer({"SharedAdmissionReadback",
                            bytes,
                            ResourceUsage::CopyDst,
                            MemoryUsage::GpuToCpu,
                            ResourceState::CopyDst}));
                    if (!buffers[i])
                        return std::nullopt;
                    buffers[i]->sealHostWrites();
                }
                return buffers;
            } catch (const std::bad_alloc&) {
                return std::nullopt;
            }
        };
        {
            auto warm = prepare();
            assert(warm);
        }
        CpuQuotaFactoryFixture factory(f_.quota);
        const auto peak = factory.measure(prepare);
        factory.verify(peak, prepare);
        const auto capacity = f_.device->memoryStats().budgetCapacityBytes;
        for (const auto available :
            {Builder::Producer::minimumNewBytes, Builder::Producer::minimumNewBytes - 1u}) {
            auto pressure = f_.device->createBuffer({"SharedAdmissionGpuBudget",
                capacity - before - available,
                ResourceUsage::StorageBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(pressure);
            auto candidate = prepare();
            if (available == Builder::Producer::minimumNewBytes)
                assert(candidate && f_.device->memoryStats().allocatedBytes == capacity);
            else
                assert(!candidate);
            candidate.reset();
            pressure.reset();
            assert(f_.device->memoryStats().allocatedBytes == before);
        }
        auto buffers = prepare();
        assert(buffers);
        auto command = createMetalRhiNativeCommandPayload(
            "StructuralRecordingIdentity", f_.registry, &f_.diagnostic);
        assert(command && command->recordingIdentity());
        const auto recording = command->recordingIdentity();
        Contract::Budget budget{
            1u, true, Contract::commandUnits, Contract::readbackBytes, Contract::referenceSlots};
        Contract::Inputs inputs;
        Contract::Facts facts;
        auto selected = root;
        const auto capture = [&] {
            return Builder::capture(selected, *buffers, budget, recording, inputs, facts);
        };
        const auto* initial = capture();
        if (initial)
            std::fprintf(stderr, "Initial fresh shared native inputs: %s\n", initial);
        assert(!initial && facts.identity.valid() && facts.actualReadbackBytes == 44u);
        assert(inputs.geometry->sharedOwnerMatches(inputs.geometry)
            && inputs.vertex->sharedOwnerMatches(inputs.vertex)
            && core::SharedOwnerProjection::sameOwner(
                inputs.geometry, root->images()->geometry()->sharedContent())
            && core::SharedOwnerProjection::sameOwner(
                inputs.vertex, root->images()->geometry()->pipeline()->sharedVertexInput()));
        CanonicalClosedConsumerRecordingAdmission admission;
        assert(!admission.capture(inputs, budget, recording, admission));
        std::size_t references = 0u;
        admission.projectReferences([&](const std::shared_ptr<const void>& owner) {
            assert(owner);
            ++references;
        });
        assert(references == 26u && !admission.validate(inputs, recording));
        const auto reject = [&](const char* expected) {
            const auto* id = capture();
            if (!id || id != std::string_view(expected))
                std::fprintf(stderr,
                    "Shared native inputs expected %s got %s\n",
                    expected,
                    id ? id : "success");
            assert(id == std::string_view(expected) && !facts.identity.valid()
                && !facts.actualReadbackBytes && !inputs.geometry && !inputs.producer.source);
        };
        --budget.maxCommands;
        reject("RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
        ++budget.maxCommands;
        --budget.maxReadbackBytes;
        reject("RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
        ++budget.maxReadbackBytes;
        --budget.maxReferenceSlots;
        reject("RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
        ++budget.maxReferenceSlots;
        selected = std::shared_ptr<const Builder::Issuance>(root->images(), root.get());
        reject("RHI-METAL-CLOSED-CONSUMER-INPUTS-ORIGINAL-OWNER");
        selected = root;
        const auto savedBuffer = (*buffers)[0u];
        (*buffers)[0u] = std::shared_ptr<GpuBuffer>(root, savedBuffer.get());
        reject("RHI-METAL-CLOSED-CONSUMER-INPUTS-READBACK-OWNER");
        (*buffers)[0u] = savedBuffer;
        auto& geometryOwner = const_cast<std::shared_ptr<const Contract::Geometry>&>(
            root->images()->geometry()->sharedContent());
        const auto savedGeometry = geometryOwner;
        geometryOwner = std::shared_ptr<const Contract::Geometry>(root, savedGeometry.get());
        reject("RHI-METAL-CLOSED-GEOMETRY-ORIGINAL-OWNER");
        geometryOwner = savedGeometry;
        auto& vertexOwner = const_cast<std::shared_ptr<const VertexInputConsumptionContract>&>(
            root->images()->geometry()->pipeline()->sharedVertexInput());
        const auto savedVertex = vertexOwner;
        vertexOwner =
            std::shared_ptr<const VertexInputConsumptionContract>(root, savedVertex.get());
        reject("RHI-METAL-CLOSED-CONSUMER-VERTEX-INPUT-ORIGINAL-OWNER");
        vertexOwner = savedVertex;
        for (std::size_t i = 0u; i < buffers->size(); ++i) {
            auto& buffer = *(*buffers)[i];
            auto& desc = const_cast<BufferDesc&>(buffer.desc());
            const auto memory = desc.memory;
            desc.memory = MemoryUsage::CpuToGpu;
            reject("RHI-CANONICAL-PRODUCER-RECORDING-READBACK");
            desc.memory = memory;
            auto native = metalRhiNativePayloadFor(buffer);
            native->resource.nativeRuntimeBacked = false;
            reject("RHI-METAL-CLOSED-CONSUMER-INPUTS-READBACK-RUNTIME");
            native->resource.nativeRuntimeBacked = true;
            native->resource.nativeRuntimeDiagnosticOnly = true;
            reject("RHI-METAL-CLOSED-CONSUMER-INPUTS-READBACK-RUNTIME");
            native->resource.nativeRuntimeDiagnosticOnly = false;
            const auto storage = native->resource.storageMode;
            native->resource.storageMode = MetalRhiNativeStorageMode::None;
            reject("RHI-METAL-CLOSED-CONSUMER-INPUTS-READBACK-STORAGE");
            native->resource.storageMode = storage;
            const auto device = native->resource.nativeDevice;
            native->resource.nativeDevice =
                std::shared_ptr<void>(native->resource.nativeObject, device.get());
            reject("RHI-METAL-CLOSED-CONSUMER-INPUTS-READBACK-DEVICE");
            native->resource.nativeDevice = device;
            f_.registry->unregisterResource(buffer.id());
            reject("RHI-METAL-CLOSED-CONSUMER-INPUTS-READBACK-NATIVE");
            f_.registry->registerBuffer(buffer);
            assert(!capture());
        }
        assert(!capture() && !admission.validate(inputs, recording));
        auto copied = inputs;
        copied.geometry = std::shared_ptr<const Contract::Geometry>(root, copied.geometry.get());
        assert(admission.validate(copied, recording));
        copied = inputs;
        copied.vertex =
            std::shared_ptr<const VertexInputConsumptionContract>(root, copied.vertex.get());
        assert(admission.validate(copied, recording));
        copied = inputs;
        ++copied.revisions[0u].revision;
        assert(admission.validate(copied, recording));
        copied = inputs;
        assert(admission.validate(inputs, recording + 1u));
        {
            core::CpuMemoryQuotaLease pressure(f_.quota, f_.cpuBytes - f_.quota->liveBytes());
            assert(pressure.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (unsigned i = 0u; i < 100u; ++i) {
                assert(!capture() && !admission.validate(inputs, recording));
            }
            std::pmr::set_default_resource(previous);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
        assert(!command->stats()->nativeBlitCopyEncoded && !command->stats()->nativeDrawEncoded
            && !command->stats()->nativeDispatchEncoded);
        command->abandonUnsubmitted();
        command.reset();
        // Shared structural values remain values after command retirement.
        // Original weak command/native observations and leases are c12d4a2/b.
        assert(!admission.validate(inputs, recording));
        std::printf(
            "FreshClosedConsumerRecordingInputs/1: actual original private cold roots to shared inputs, genuine geometry/layout owners, readback36+fault8=44 refs26 protocol38, readback factory coldPeak%llu; alias/budget/native-registry/recording/revision negatives and100 allocation0 PASS; structural only/no command authority\n",
            static_cast<unsigned long long>(peak));
    }

private:
    OrdinaryGraphicsFixture& f_;
};
} // namespace arp::test
