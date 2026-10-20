#pragma once
#include "cpu_quota_factory_fixture.h"
#include "metal_closed_consumer_recording_inputs_fixture.h"
#include "metal_closed_consumer_recording_preparation_fixture.h"
#include "metal_closed_consumer_recording_reservation_fixture.h"
#include "metal_native_identity_fixture.h"
#include "metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_closed_consumer_issuance.h"
#include "rhi/gpu_extensions.h"
#include "zero_base_indirect_source_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class MetalClosedConsumerIssuanceFixture final {
    using Root = rhi_metal::MetalRhiClosedConsumerIssuance;

public:
    explicit MetalClosedConsumerIssuanceFixture(OrdinaryGraphicsFixture& f) : f_(f) {}

    void verify(std::shared_ptr<const Root::Images> images, unsigned recordingStage = 0u)
    {
        using namespace rhi;
        using namespace rhi_metal;
        core::CpuMemoryQuotaScope scope(f_.quota);
        static_assert(
            !std::is_default_constructible_v<Root> && !std::is_copy_constructible_v<Root>);
        const auto& source = images->geometry()->pipeline()->source();
        auto pipeline = Root::Pipeline::prepare(source,
            semantics::nativeBackendCapabilityContract(Backend::Metal),
            f_.budget(),
            {1u, true, 128u},
            f_.diagnostic);
        if (source->package()->artifact().execution == BindlessExecution::Native) {
            assert(!pipeline
                && f_.diagnostic == "RHI-METAL-ZERO-BASE-PIPELINE-SCALAR-PACKAGE-REQUIRED");
            std::puts(
                "FreshClosedConsumerPacket/1: Native-array producer package deterministically refused; no cold packet issuance");
            return;
        }
        assert(pipeline && !pipeline->validate());
        // Explicit fixture capacities, refined to exact actual projections
        // before the measured factory and negative budget probes.
        BindlessIndirectPacketPool pool(*f_.device,
            {2u, Root::Packet::Contract::minimumBytes, 2u * Root::Packet::Contract::minimumBytes});
        Root::Budget budget{1u,
            true,
            Root::minimumNewBytes,
            512u,
            {1u, true, Root::Packet::Contract::minimumBytes, 128u},
            {1u, true, Root::Producer::Contract::minimumOwnedBytes, 128u},
            {1u, true, Root::Cold::Contract::minimumOwnedBytes, 256u}};
        auto selectedImages = images;
        auto selectedPipeline = pipeline;
        const auto prepare = [&] {
            return Root::prepare(selectedImages, selectedPipeline, pool, budget, f_.diagnostic);
        };
        if (recordingStage) {
            auto root = prepare();
            assert(root && !root->validate());
            if (recordingStage >= 3u)
                MetalClosedConsumerRecordingReservationFixture(f_).verify(
                    root, recordingStage - 3u);
            else if (recordingStage == 2u)
                MetalClosedConsumerRecordingPreparationFixture(f_).verify(root);
            else
                MetalClosedConsumerRecordingInputsFixture(f_).verify(root);
            return;
        }
        const auto reject = [&](const char* expected) {
            const auto cpu = f_.quota->liveBytes(), gpu = f_.device->memoryStats().allocatedBytes;
            auto denied = prepare();
            if (denied || f_.diagnostic != expected)
                std::fprintf(
                    stderr, "Issuance expected %s got %s\n", expected, f_.diagnostic.c_str());
            assert(!denied && f_.diagnostic == expected && cpu == f_.quota->liveBytes()
                && gpu == f_.device->memoryStats().allocatedBytes && !pool.livePackets()
                && !pool.chargedBytes());
        };
        budget.prototypeOptIn = false;
        reject("RHI-CLOSED-CONSUMER-PACKET-ISSUANCE-OPT-IN");
        budget.prototypeOptIn = true;
        --budget.maxNewBytes;
        reject("RHI-METAL-CLOSED-CONSUMER-ISSUANCE-BUDGET");
        ++budget.maxNewBytes;
        budget.packet.prototypeOptIn = false;
        reject("RHI-METAL-CLOSED-CONSUMER-ISSUANCE-COMPONENT-BUDGET");
        budget.packet.prototypeOptIn = true;
        selectedImages = std::shared_ptr<const Root::Images>(pipeline, images.get());
        reject("RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ORIGINAL-OWNER");
        selectedImages = images;
        selectedPipeline = std::shared_ptr<const Root::Pipeline>(images, pipeline.get());
        reject("RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ORIGINAL-OWNER");
        selectedPipeline = pipeline;
        auto foreign = createMetalRhiDevice(OrdinaryGraphicsFixture::deviceDesc());
        assert(foreign);
        {
            BindlessIndirectPacketPool foreignPool(*foreign,
                {1u, Root::Packet::Contract::minimumBytes, Root::Packet::Contract::minimumBytes});
            assert(!Root::prepare(images, pipeline, foreignPool, budget, f_.diagnostic)
                && f_.diagnostic == "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ISSUER-MISMATCH");
        }
        foreign.reset();
        {
            using Source = MetalRhiZeroBaseIndirectSource;
            const auto& content = source->content();
            Source::Content::Arguments::Source original{content.sourceBytes().data(),
                content.sourceBytes().size(),
                std::uint64_t{content.parameters().sourceWordOffset} * sizeof(std::uint32_t),
                1u,
                sizeof(DrawIndexedIndirectCommand),
                core::UnsignedElementSpan::ByteOrder::LittleEndian,
                Source::Content::Arguments::Origin::HostAuthoredImmutable,
                1u};
            auto other = Source::prepare(source->package(),
                source->index(),
                *f_.device,
                original,
                content.parameters(),
                content.consumer(),
                ZeroBaseIndirectSourceFixture::budget(),
                {1u,
                    true,
                    content.sourceBytes().size(),
                    sizeof(ZeroBaseIndirectProducerParams),
                    128u},
                f_.diagnostic);
            assert(other);
            selectedPipeline = Root::Pipeline::prepare(other,
                semantics::nativeBackendCapabilityContract(Backend::Metal),
                f_.budget(),
                {1u, true, 128u},
                f_.diagnostic);
            assert(selectedPipeline);
            reject("RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ORIGINAL-SOURCE");
            selectedPipeline = pipeline;
        }
        const auto submissions =
            inspectMetalRhiNativeQueueStats(f_.device->queue()).nativeQueueSubmitCount;
        auto root = prepare();
        if (!root)
            std::fprintf(stderr, "Issuance warm: %s\n", f_.diagnostic.c_str());
        assert(root && !root->validate());
        budget.maxChildOwners = static_cast<std::uint32_t>(root->childOwnerCount());
        budget.packet.maxChildOwners =
            static_cast<std::uint32_t>(root->cold()->producer()->packet()->childOwnerCount());
        budget.producer.maxChildOwners =
            static_cast<std::uint32_t>(root->cold()->producer()->childOwnerCount());
        budget.consumer.maxChildOwners =
            static_cast<std::uint32_t>(root->cold()->childOwnerCount());
        const auto owners = root->childOwnerCount();
        root.reset();
        --budget.maxChildOwners;
        reject("RHI-METAL-CLOSED-CONSUMER-ISSUANCE-CHILD-OWNER-BUDGET");
        ++budget.maxChildOwners;
        --budget.packet.maxChildOwners;
        reject("RHI-METAL-ZERO-BASE-PACKET-CHILD-OWNER-BUDGET");
        ++budget.packet.maxChildOwners;
        --budget.producer.maxChildOwners;
        reject("RHI-METAL-ZERO-BASE-COLD-CHILD-OWNER-BUDGET");
        ++budget.producer.maxChildOwners;
        --budget.consumer.maxChildOwners;
        reject("RHI-CANONICAL-CONSUMER-COLD-RESOURCES-OWNER-BUDGET");
        ++budget.consumer.maxChildOwners;
        {
            auto warm = prepare();
            assert(warm && !warm->validate());
        }
        CpuQuotaFactoryFixture factory(f_.quota);
        const auto peak = factory.measure(prepare);
        factory.verify(peak, prepare);
        assert(f_.diagnostic == "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ALLOCATION-FAILED");
        const auto gpu = f_.device->memoryStats().allocatedBytes,
                   capacity = f_.device->memoryStats().budgetCapacityBytes;
        for (const auto available : {Root::minimumNewBytes, Root::minimumNewBytes - 1u}) {
            auto pressure = f_.device->createBuffer({"IssuanceGpuBudget",
                capacity - gpu - available,
                ResourceUsage::StorageBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(pressure);
            auto candidate = prepare();
            if (available == Root::minimumNewBytes)
                assert(candidate && f_.device->memoryStats().allocatedBytes == capacity);
            else
                assert(!candidate
                    && f_.diagnostic == "RHI-METAL-CONSUMER-COLD-BUFFER-CREATION-FAILED");
            candidate.reset();
            pressure.reset();
            assert(f_.device->memoryStats().allocatedBytes == gpu);
        }
        root = prepare();
        assert(root && !root->validate() && root->childOwnerCount() == owners
            && root->ownedBytes() == Root::minimumNewBytes);
        const auto& packet = root->packet();
        assert(packet->prototypeClosedConsumerCold() && !packet->prototypeStorageOnly()
            && !packet->storageDiagnostic());
        assert(!packet->consumerPlan().success && !packet->consumerPlan().executable
            && !packet->consumerPlan().identity);
        assert(packet->validate().diagnostics
            == "RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-CLOSED-CONSUMER-COLD");
        const auto extension = static_cast<const BindlessCrossExtension*>(
            f_.device->queryInterface(kExtensionBindlessCross));
        assert(extension);
        assert(extension->capabilities().indirectPacketStorageContractVersion == 0u);
        auto sibling = prepare();
        assert(sibling && !sibling->validate() && pool.livePackets() == 2u
            && pool.chargedBytes() == 104u);
        for (const auto a : root->generations())
            for (const auto b : sibling->generations())
                assert(a != b);
        assert(!prepare() && f_.diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-POOL-FULL"
            && pool.livePackets() == 2u);
        sibling.reset();
        // The same reused storage algorithm still creates only the permanently
        // excluded old purpose when entered through its original public root.
        auto old = Root::Packet::prepare(pipeline, pool, budget.packet, f_.diagnostic);
        assert(old && !old->validate());
        assert(old->packet()->prototypeStorageOnly()
            && !old->packet()->prototypeClosedConsumerCold()
            && old->packet()->generation() != packet->generation()
            && BindlessIndirectPacketStorageContract::closedConsumerStructuralDiagnostic(
                old->packet()->issuance()));
        old.reset();
        {
            BindlessIndirectPacketPool resident(*f_.device,
                {2u, Root::Packet::Contract::minimumBytes, Root::Packet::Contract::minimumBytes});
            auto held = Root::prepare(images, pipeline, resident, budget, f_.diagnostic);
            assert(held);
            assert(!Root::prepare(images, pipeline, resident, budget, f_.diagnostic)
                && f_.diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-RESIDENT-BUDGET"
                && resident.livePackets() == 1u && resident.chargedBytes() == 52u);
        }
        for (std::size_t i = 0u; i < root->cold()->producer()->packet()->resources().size(); ++i) {
            const auto& resource = root->cold()->producer()->packet()->resources()[i];
            std::array<std::uint8_t, 52u> bytes{};
            const auto size =
                root->cold()->producer()->packet()->buffers()[i].get()->desc().sizeBytes;
            assert(readMetalRhiNativeBuffer(
                resource.buffer, bytes.data(), size, 0u, size, f_.diagnostic));
            if (i < 2u)
                for (std::size_t j = 0u; j < size; ++j)
                    assert(!bytes[j]);
            else {
                BindlessIndirectPacketStorageContract::ParameterBytes expected;
                assert(!BindlessIndirectPacketStorageContract::encodeParameters(
                    source->content().parameters(), expected));
                assert(std::equal(expected.begin(), expected.end(), bytes.begin()));
            }
            assert(root->cold()->producer()->packet()->buffers()[i].get()->hostWritesSealed()
                && root->cold()
                    ->producer()
                    ->packet()
                    ->buffers()[i]
                    .get()
                    ->gpuTransferWritesSealed());
            auto& buffer = *root->cold()->producer()->packet()->buffers()[i];
            assert(f_.device->writeBuffer(buffer, bytes.data(), size).diagnostics
                == "RHI-BUFFER-HOST-WRITES-SEALED");
            auto encoder = f_.device->createCommandEncoder({});
            assert(encoder);
            auto pass = encoder->beginTransferPass({});
            assert(pass);
            assert(
                pass->copyBufferToBuffer({source->sourceResource().id(), buffer.id(), 0u, 0u, 4u})
                    .diagnostics
                == "RHI-BUFFER-GPU-WRITES-SEALED");
            assert(pass->end().success);
            pass.reset();
            encoder.reset();
            MetalNativeIdentityMutationProbe::verify(resource.buffer->resource,
                root->cold()->producer()->resources()[1u].buffer->resource,
                [&] {
                    assert(root->validate());
                });
            const auto id = root->cold()->producer()->packet()->buffers()[i].get()->id();
            f_.registry->unregisterResource(id);
            assert(root->validate());
            f_.registry->registerBuffer(*root->cold()->producer()->packet()->buffers()[i].get());
            assert(!root->validate());
        }
        Root::Cold::Contract::Bytes metadata;
        assert(readMetalRhiNativeBuffer(root->cold()->metadata().resource.buffer,
                   metadata.data(),
                   metadata.size(),
                   0u,
                   metadata.size(),
                   f_.diagnostic)
            && metadata == root->cold()->facts().bytes);
        for (std::size_t i = 0u; i < images->images().size(); ++i)
            assert(root->cold()->facts().images[i].matches(images->images()[i].values()));
        {
            auto producerLease = BindlessFaultSubmissionLease::acquire(
                root->cold()->producer()->faultDomain(), f_.diagnostic);
            auto consumerLease =
                BindlessFaultSubmissionLease::acquire(root->cold()->faultDomain(), f_.diagnostic);
            assert(producerLease.valid() && consumerLease.valid()
                && !BindlessFaultSubmissionLease::acquire(
                    root->cold()->producer()->faultDomain(), f_.diagnostic)
                    .valid());
        }
        auto& generations = const_cast<std::array<std::uint64_t, 3u>&>(root->generations());
        ++generations[0u];
        assert(root->validate());
        --generations[0u];
        auto& ownedImages = const_cast<std::shared_ptr<const Root::Images>&>(root->images());
        ownedImages = std::shared_ptr<const Root::Images>(pipeline, images.get());
        assert(root->validate());
        ownedImages = images;
        auto& ownedCold = const_cast<std::shared_ptr<const Root::Cold>&>(root->cold());
        auto saved = ownedCold;
        ownedCold = std::shared_ptr<const Root::Cold>(images, saved.get());
        assert(root->validate());
        ownedCold = saved;
        auto& facts = const_cast<Root::Cold::Contract::Facts&>(root->cold()->facts());
        ++facts.words[2u];
        assert(root->validate());
        --facts.words[2u];
        {
            core::CpuMemoryQuotaLease pressure(f_.quota, f_.cpuBytes - f_.quota->liveBytes());
            assert(pressure.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (unsigned i = 0u; i < 100u; ++i) {
                assert(!root->validate());
                ++generations[1u];
                assert(root->validate());
                --generations[1u];
            }
            std::pmr::set_default_resource(previous);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
        assert(inspectMetalRhiNativeQueueStats(f_.device->queue()).nativeQueueSubmitCount
                == submissions
            && !inspectSharedMetalRhiCommandCapacity().charged);
        std::weak_ptr<void> token = root->cold()
                                        ->producer()
                                        ->packet()
                                        ->resources()[0u]
                                        .buffer->resource.allocationReleaseToken;
        saved.reset();
        root.reset();
        assert(token.expired() && !pool.livePackets() && !pool.chargedBytes()
            && f_.device->memoryStats().allocatedBytes == gpu);
        std::printf(
            "FreshClosedConsumerPacket/1: original immutable images/geometry/pipelines, new private packet+independent fault generations, bytes%llu owners%zu coldPeak%llu; exact initialized/metadata bytes, budgets/pool/resident/partial rollback/original mutations and100 allocation0 PASS; no submit/public plan/draw\n",
            static_cast<unsigned long long>(Root::minimumNewBytes),
            owners,
            static_cast<unsigned long long>(peak));
        // Issuer retirement rejects the retained original native graph.
        root = prepare();
        assert(root && !root->validate());
        token = root->cold()
                    ->producer()
                    ->packet()
                    ->resources()[0u]
                    .buffer->resource.allocationReleaseToken;
        f_.device.reset();
        assert(root->validate() == std::string_view("RHI-METAL-EXCLUSIVE-IMAGES-ISSUER-RETIRED")
            && !token.expired());
        root.reset();
        assert(token.expired() && !pool.livePackets() && !pool.chargedBytes());
    }

private:
    OrdinaryGraphicsFixture& f_;
};
} // namespace arp::test
