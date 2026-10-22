#pragma once
#include "core/ownership/shared_owner_projection.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/frontend/bindless_indirect_packet.h"

namespace arp::rhi {
// Cold referential/value correspondence only. A trusted backend root must
// independently prove that expectedSource owns content's exact uploaded bytes.
// Null success never proves native bytes, ABI, producer or consumer execution.
class ZeroBaseIndirectProcessorCorrespondence final {
public:
    static constexpr std::uint32_t version = 1u;

    struct Facts {
        std::uint32_t contractVersion = 0u;
        CanonicalZeroBaseIndirectSourceContract::Facts canonical;
        std::uint64_t packetGeneration = 0u, snapshotGeneration = 0u;
    };

    static ValidationResult verify(const BindlessIndirectProcessorIntent& intent,
        const std::shared_ptr<const BindlessShaderPackage>& expectedPackage,
        const std::shared_ptr<const GpuBuffer>& expectedSource,
        const ZeroBaseIndirectSourceConsumptionContract& content,
        Facts& output)
    {
        output = {};
        const auto valid = intent.validate();
        if (!valid.success)
            return valid;
        const auto& desc = intent.desc();
        if (!expectedPackage || !expectedPackage->sharedOwnerMatches(expectedPackage)
            || !desc.producer->sharedOwnerMatches(desc.producer)
            || !core::SharedOwnerProjection::sameOwner(expectedPackage, desc.producer))
            return {false, "RHI-ZERO-BASE-PROCESSOR-PACKAGE-OWNER-MISMATCH"};
        if (!expectedSource || !expectedSource->sharedOwnerMatches(expectedSource)
            || !desc.logicalSource->sharedOwnerMatches(desc.logicalSource)
            || !core::SharedOwnerProjection::sameOwner(expectedSource, desc.logicalSource))
            return {false, "RHI-ZERO-BASE-PROCESSOR-SOURCE-OWNER-MISMATCH"};
        Facts facts;
        if (const auto* id = CanonicalZeroBaseIndirectSourceContract::verify(*expectedPackage,
                content,
                CanonicalZeroBaseIndirectSourceContract::selectedEntry,
                facts.canonical))
            return {false, id};
        if (const auto* id = content.validateSourceStorage(*expectedSource))
            return {false, id};
        if (const auto* id = content.validateParameters(desc.packet->parameters()))
            return {false, id};
        if (const auto* id = content.validateConsumer(
                desc.consumerAuthorization->package().artifact().indirectInstanceConsumer))
            return {false, id};
        facts.contractVersion = version;
        facts.packetGeneration = desc.packet->generation();
        facts.snapshotGeneration = desc.consumerGeneration;
        output = facts;
        return {true, {}};
    }
};
} // namespace arp::rhi
