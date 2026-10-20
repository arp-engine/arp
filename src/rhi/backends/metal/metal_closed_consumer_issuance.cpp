#include "rhi/backends/metal/metal_closed_consumer_issuance.h"

#include "rhi/backends/metal/metal_device.h"

namespace arp::rhi_metal {
const char* MetalRhiClosedConsumerIssuance::validateBudget(Budget budget) noexcept
{
    if (budget.contractVersion != 1u || !budget.prototypeOptIn || !budget.maxChildOwners
        || budget.maxNewBytes < minimumNewBytes)
        return "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-BUDGET";
    if (budget.packet.contractVersion != Packet::Contract::version || !budget.packet.prototypeOptIn
        || budget.packet.maxPacketBytes < Packet::Contract::minimumBytes
        || !budget.packet.maxChildOwners
        || budget.producer.contractVersion != Producer::Contract::version
        || !budget.producer.prototypeOptIn
        || budget.producer.maxOwnedBytes < Producer::Contract::minimumOwnedBytes
        || !budget.producer.maxChildOwners
        || budget.consumer.contractVersion != Cold::Contract::version
        || !budget.consumer.prototypeOptIn
        || budget.consumer.maxOwnedBytes < Cold::Contract::minimumOwnedBytes
        || !budget.consumer.maxChildOwners)
        return "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-COMPONENT-BUDGET";
    return nullptr;
}

const char* MetalRhiClosedConsumerIssuance::sumOwnedBytes(
    const Cold& cold, Budget budget, std::uint64_t& bytes) noexcept
{
    bytes = 0u;
    core::AtomicCapacityBudget capacity(budget.maxNewBytes);
    for (const auto charge : {cold.producer()->packet()->packet()->reportedBufferBytes(),
             cold.producer()->ownedBytes(),
             cold.ownedBytes()})
        if (!charge || !capacity.tryAcquire(charge))
            return "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-BYTE-BUDGET";
    bytes = capacity.used();
    return nullptr;
}

const char* MetalRhiClosedConsumerIssuance::validate() const noexcept
{
    return validateStorage(Cold::Contract::StoragePhase::Cold);
}

const char* MetalRhiClosedConsumerIssuance::validateClosedReadbackReady() const noexcept
{
    return validateStorage(Cold::Contract::StoragePhase::ReadbackReady);
}

const char* MetalRhiClosedConsumerIssuance::validateStorage(
    Cold::Contract::StoragePhase phase) const noexcept
{
    if (phase != Cold::Contract::StoragePhase::Cold
        && phase != Cold::Contract::StoragePhase::ReadbackReady)
        return "RHI-CANONICAL-CONSUMER-STORAGE-PHASE";
    if (const auto* id = validateBudget(budget_))
        return id;
    if (!images_ || !images_->sharedOwnerMatches(images_) || !cold_
        || !cold_->sharedOwnerMatches(cold_))
        return "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ORIGINAL-OWNER";
    if (const auto* id = images_->validate())
        return id;
    const auto* coldId = phase == Cold::Contract::StoragePhase::Cold
        ? cold_->validate()
        : cold_->validateClosedReadbackReady();
    if (coldId)
        return coldId;
    if (!core::SharedOwnerProjection::sameOwner(images_->geometry()->pipeline(), cold_->pipeline()))
        return "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ORIGINAL-PIPELINE";
    if (!packet()->prototypeClosedConsumerCold() || packet()->prototypeStorageOnly())
        return "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ORIGINAL-PURPOSE";
    const std::array<std::uint64_t, 3u> current{packet()->generation(),
        cold_->producer()->generation(),
        cold_->faultDomain()->generation()};
    if (current != generations_ || !current[0u] || !current[1u] || !current[2u]
        || current[0u] == current[1u] || current[0u] == current[2u] || current[1u] == current[2u])
        return "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-GENERATIONS";
    for (std::size_t i = 0u; i < images_->images().size(); ++i) {
        const auto& original = images_->images()[i];
        const auto& bound = cold_->images()[i];
        if (!original.values().matches(bound.values())
            || !core::SharedOwnerProjection::sameOwner(
                original.parent().texture, bound.parent().texture)
            || !core::SharedOwnerProjection::sameOwner(
                original.view().textureView, bound.view().textureView)
            || !core::SharedOwnerProjection::sameOwner(
                bound.parent().texture->immutableRevision(), images_->markers()[i])
            || !core::SharedOwnerProjection::sameOwner(
                bound.view().textureView->immutableRevision(), images_->markers()[i]))
            return "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ORIGINAL-IMAGES";
    }
    std::uint64_t bytes = 0u;
    if (const auto* id = sumOwnedBytes(*cold_, budget_, bytes))
        return id;
    if (bytes != ownedBytes_ || owners_.size() > budget_.maxChildOwners
        || !owners_.matches([&](auto&& visit) {
               projectOwners(visit);
           }))
        return "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ORIGINAL-OWNER";
    return nullptr;
}

std::shared_ptr<const MetalRhiClosedConsumerIssuance> MetalRhiClosedConsumerIssuance::prepare(
    std::shared_ptr<const Images> images,
    std::shared_ptr<const Pipeline> pipeline,
    rhi::BindlessIndirectPacketPool& pool,
    Budget budget,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail = [&](const char* id) -> std::shared_ptr<const MetalRhiClosedConsumerIssuance> {
        diagnostic = id;
        return {};
    };
    const auto decision = Packet::Contract::closedConsumerLowering(
        pool.device_.backend(), budget.contractVersion, budget.prototypeOptIn);
    if (!decision.prototypeColdIntent)
        return fail(decision.diagnosticId);
    if (const auto* id = validateBudget(budget))
        return fail(id);
    if (!images || !images->sharedOwnerMatches(images) || !pipeline
        || !pipeline->sharedOwnerMatches(pipeline))
        return fail("RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ORIGINAL-OWNER");
    if (const auto* id = images->validate())
        return fail(id);
    if (const auto* id = pipeline->validate())
        return fail(id);
    const auto& consumerPipeline = images->geometry()->pipeline();
    const auto& source = consumerPipeline->source();
    if (!core::SharedOwnerProjection::sameOwner(source, pipeline->source()))
        return fail("RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ORIGINAL-SOURCE");
    if (!core::SharedOwnerProjection::sameOwner(
            source->index()->registry(), metalRhiIssuingResourceRegistry(pool.device_)))
        return fail("RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ISSUER-MISMATCH");
    try {
        core::CpuMemoryQuotaScope scope(source->quota());
        auto packet = Packet::prepareImpl(pipeline,
            pool,
            budget.packet,
            rhi::BindlessIndirectPacket::Issuance::PrototypeClosedConsumerCold,
            diagnostic);
        if (!packet)
            return {};
        auto producer = Producer::prepare(packet, pool.device_, budget.producer, diagnostic);
        if (!producer)
            return {};
        Cold::ImageIds ids;
        for (std::size_t i = 0u; i < ids.size(); ++i)
            ids[i] = images->images()[i].values().view;
        auto cold = Cold::prepare(
            producer, consumerPipeline, pool.device_, ids, budget.consumer, diagnostic);
        if (!cold)
            return {};
        std::uint64_t bytes = 0u;
        if (const auto* id = sumOwnedBytes(*cold, budget, bytes))
            return fail(id);
        if (budget.maxChildOwners < 2u || images->childOwnerCount() > budget.maxChildOwners - 2u
            || cold->childOwnerCount() > budget.maxChildOwners - 2u - images->childOwnerCount())
            return fail("RHI-METAL-CLOSED-CONSUMER-ISSUANCE-CHILD-OWNER-BUDGET");
        auto root = core::makeShared<MetalRhiClosedConsumerIssuance>(
            ConstructionKey{}, images, cold, budget, bytes);
        if (const auto* id = root->validate())
            return fail(id);
        return root;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-CLOSED-CONSUMER-ISSUANCE-ALLOCATION-FAILED");
    }
}
} // namespace arp::rhi_metal
