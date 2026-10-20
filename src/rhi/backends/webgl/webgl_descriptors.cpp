#include "rhi/backends/webgl/webgl_descriptors_internal.h"

namespace arp::rhi::webgl {
namespace {
class WebGlDescriptorArena final : public DescriptorArena {
public:
    WebGlDescriptorArena(
        WebGlResourceFactoryKey key, std::shared_ptr<WebGlDescriptorArenaState> state)
        : key_(key), state_(std::move(state))
    {}

    ~WebGlDescriptorArena() override
    {
        state_->active = false;
        state_->epoch.advance();
    }

    DescriptorSetAllocation allocateSet(const DescriptorLayoutDesc&) override
    {
        return {false,
            core::pmr::String("RHI-WEBGL1-DESCRIPTOR-RAW-ALLOCATION-UNAVAILABLE",
                &core::defaultCpuMemoryResource())};
    }

    DescriptorTableAllocation allocateTable(const DescriptorTableDesc&) override
    {
        return {false,
            core::pmr::String(
                "RHI-WEBGL1-DESCRIPTOR-TABLE-UNAVAILABLE", &core::defaultCpuMemoryResource())};
    }

    core::UniquePtr<DescriptorTable> allocateDescriptorTable(const DescriptorTableDesc&) override
    {
        return {};
    }

    DescriptorArenaStats stats() const override { return state_->stats; }

    core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept override
    {
        return state_->epoch.ticket();
    }

    void reset() override
    {
        state_->epoch.advance();
        state_->stats.allocatedSets = state_->stats.allocatedDescriptors = 0;
    }

    core::UniquePtr<DescriptorSet> allocateDescriptorSet(const DescriptorSetDesc& desc) override
    {
        auto& d = *state_->object->domain;
        if (d.preflight(0, 0))
            return {};
        const auto count = desc.layout.bindings.size();
        auto& s = state_->stats;
        if (!state_->active || !state_->epoch.isActive() || s.allocatedSets >= s.maxSets
            || count > s.maxDescriptors - s.allocatedDescriptors) {
            d.lastDiagnostic = "RHI-WEBGL1-DESCRIPTOR-ARENA-CAPACITY";
            return {};
        }
        if (!validateDescriptorLayout(desc.layout).success) {
            d.lastDiagnostic = "RHI-WEBGL1-DESCRIPTOR-LAYOUT";
            return {};
        }
        for (const auto& binding : desc.layout.bindings)
            if ((d.lastDiagnostic = semantics::WebGlDescriptorCreationContract::binding(binding)))
                return {};
        try {
            core::CpuMemoryQuotaScope quota(d.metadataQuota);
            auto object = core::makeShared<WebGlResourcePayload>(
                key_, state_->object->domain, WebGlObjectKind::DescriptorSet);
            auto result =
                core::makeUnique<WebGlDescriptorSet>(key_, state_, object, desc, s.allocatedSets);
            if ((d.lastDiagnostic = d.publish(object)))
                return {};
            ++s.allocatedSets;
            s.allocatedDescriptors += count;
            return result;
        } catch (const std::bad_alloc&) {
            d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION";
            return {};
        }
    }

private:
    WebGlResourceFactoryKey key_;
    std::shared_ptr<WebGlDescriptorArenaState> state_;
};
} // namespace

core::UniquePtr<DescriptorArena> WebGlResources::createDescriptorArena(
    const DescriptorArenaDesc& desc)
{
    auto& d = *domain_;
    if (d.preflight(0, 0)
        || (d.lastDiagnostic =
                semantics::WebGlDescriptorCreationContract::arena(Backend::WebGL, desc)))
        return {};
    try {
        core::CpuMemoryQuotaScope quota(d.metadataQuota);
        auto state = core::makeShared<WebGlDescriptorArenaState>();
        state->object = core::makeShared<WebGlResourcePayload>(
            WebGlResourceFactoryKey{}, domain_, WebGlObjectKind::DescriptorArena);
        state->stats.maxSets = desc.maxSets;
        state->stats.maxDescriptors = desc.maxDescriptors;
        auto result = core::makeUnique<WebGlDescriptorArena>(WebGlResourceFactoryKey{}, state);
        if ((d.lastDiagnostic = d.publish(state->object)))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION";
        return {};
    }
}

const WebGlDescriptorSet::Cell* WebGlDescriptorSet::cell(std::uint32_t binding) const noexcept
{
    for (std::size_t i = 0; i < definition.layout.bindings.size(); ++i)
        if (definition.layout.bindings[i].binding == binding)
            return &cells[i];
    return nullptr;
}

bool WebGlDescriptorSet::hasWrittenBinding(std::uint32_t b) const
{
    const auto* c = cell(b);
    return c && c->write.has_value();
}

ValidationResult WebGlDescriptorSet::validateBinding(std::uint32_t b) const
{
    const auto* c = cell(b);
    if (!isCurrentAllocation() || !c || !c->write || !c->object || !c->object->current())
        return {false, "RHI-WEBGL1-DESCRIPTOR-CONTENT-STALE"};
    return {true, ""};
}

DescriptorSetStats WebGlDescriptorSet::stats() const
{
    DescriptorSetStats s;
    s.bindingCount = cells.size();
    for (const auto& c : cells)
        if (c.write)
            ++s.writtenBindings;
    return s;
}

ValidationResult WebGlDescriptorSet::writeImpl(const DescriptorBindingWrite& w)
{
    if (!isCurrentAllocation())
        return {false, "RHI-WEBGL1-DESCRIPTOR-ALLOCATION-STALE"};
    const auto* target = cell(w.binding);
    if (!target || w.arrayElement || w.physicalLowering.enabled
        || w.resourceState != ResourceState::ShaderRead)
        return {false, "RHI-WEBGL1-DESCRIPTOR-WRITE-SCHEMA"};
    const auto index = static_cast<std::size_t>(target - cells.data());
    const auto& declaration = definition.layout.bindings[index];
    if (w.type != declaration.type)
        return {false, "RHI-WEBGL1-DESCRIPTOR-WRITE-TYPE"};
    auto owner = object->domain->find(w.resource);
    if (!owner)
        return {false, "RHI-WEBGL1-DESCRIPTOR-FOREIGN-RESOURCE"};
    if (w.type == DescriptorType::UniformBuffer) {
        auto p = std::dynamic_pointer_cast<WebGlBufferPayload>(owner);
        if (w.resourceKind != DescriptorResourceKind::Buffer || !p
            || (p->desc.usage & ResourceUsage::UniformBuffer) == ResourceUsage::None)
            return {false, "RHI-WEBGL1-DESCRIPTOR-BUFFER-USAGE"};
    } else if (w.type == DescriptorType::SampledTexture) {
        auto p = std::dynamic_pointer_cast<WebGlTextureViewPayload>(owner);
        if (w.resourceKind != DescriptorResourceKind::TextureView || !p || !p->texture->current()
            || (p->texture->desc.usage & TextureUsage::Sampled) == TextureUsage::None)
            return {false, "RHI-WEBGL1-DESCRIPTOR-TEXTURE-USAGE"};
    } else if (w.type == DescriptorType::Sampler) {
        if (w.resourceKind != DescriptorResourceKind::Sampler
            || !std::dynamic_pointer_cast<WebGlSamplerPayload>(owner))
            return {false, "RHI-WEBGL1-DESCRIPTOR-SAMPLER-USAGE"};
    } else
        return {false, "RHI-WEBGL1-DESCRIPTOR-WRITE-TYPE"};
    // Fixed cell storage, original owner retained, revision commit remains in
    // DescriptorSet::write. Rejecting a candidate changes neither old content.
    cells[index].write = w;
    cells[index].object = std::move(owner);
    return {true, ""};
}
} // namespace arp::rhi::webgl
