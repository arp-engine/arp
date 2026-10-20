#pragma once
#include "rhi/backends/webgl/webgl_resources_internal.h"
#include "rhi/frontend/descriptor_state_copy.h"
#include "rhi/semantics/webgl_descriptor_creation.h"

namespace arp::rhi::webgl {
struct WebGlDescriptorArenaState final {
    std::shared_ptr<WebGlResourcePayload> object;
    core::LifetimeEpoch epoch;
    DescriptorArenaStats stats;
    bool active = true;
};

class WebGlDescriptorSet final : public DescriptorSet {
public:
    struct Cell {
        std::optional<DescriptorBindingWrite> write;
        std::shared_ptr<WebGlResourcePayload> object;
    };

    WebGlDescriptorSet(WebGlResourceFactoryKey,
        std::shared_ptr<WebGlDescriptorArenaState> arena,
        std::shared_ptr<WebGlResourcePayload> object,
        const DescriptorSetDesc& desc,
        std::uint32_t index)
        : arena(std::move(arena)), object(std::move(object)),
          definition(DescriptorStateCopy::copy(desc)), ticket(this->arena->epoch.ticket())
    {
        cells.resize(definition.layout.bindings.size());
        allocation_.success = true;
        allocation_.setIndex = index;
        allocation_.descriptorCapacity = cells.size();
    }

    const DescriptorSetAllocation& allocation() const override { return allocation_; }

    const DescriptorLayoutDesc& layout() const override { return definition.layout; }

    bool isCurrentAllocation() const override
    {
        return arena->active && ticket.isCurrent() && object->current();
    }

    bool hasWrittenBinding(std::uint32_t binding) const override;
    ValidationResult validateBinding(std::uint32_t binding) const override;
    DescriptorSetStats stats() const override;

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override { return ticket; }

    const Cell* cell(std::uint32_t) const noexcept;

    std::shared_ptr<WebGlDescriptorArenaState> arena;
    std::shared_ptr<WebGlResourcePayload> object;
    DescriptorSetDesc definition;
    core::LifetimeEpoch::Ticket ticket;
    core::pmr::Vector<Cell> cells{&core::defaultCpuMemoryResource()};

protected:
    ValidationResult writeImpl(const DescriptorBindingWrite&) override;

    std::optional<DescriptorBindingWrite> successfulBindingWrite(
        std::uint32_t b, std::uint32_t e) const noexcept override
    {
        const auto* c = cell(b);
        return !e && c ? c->write : std::nullopt;
    }

private:
    DescriptorSetAllocation allocation_{
        false, core::pmr::String(&core::defaultCpuMemoryResource())};
};
} // namespace arp::rhi::webgl
