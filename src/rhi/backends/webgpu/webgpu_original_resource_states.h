#pragma once
#include "core/shared_identity_projection.h"
#include "rhi/backends/webgpu/webgpu_original_command_inputs.h"

namespace arp::rhi_webgpu {
struct WebGpuOriginalResourceStatesBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxSourceReferences = 0u, maxStateControls = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

// Complete original input/resource graph state ownership and current facts.
// This is not command-by-command state/access legality or native recording proof.
class WebGpuOriginalResourceStates final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOriginalResourceStates;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Control = rhi::common::RecordedWholeResourceStateView;

public:
    enum class Domain { Buffer, UniformWholeTexture };

    struct Source {
        rhi::GpuResourceId resource;
        Domain domain = Domain::Buffer;
        std::shared_ptr<const void> owner;
        Control control;

        core::SharedIdentityProjection::Record identity() const noexcept
        {
            return {resource.value, static_cast<std::uint32_t>(domain), owner, control.identity()};
        }
    };

    struct State {
        Source source;
        Control::Observation observation;
    };

    WebGpuOriginalResourceStates(ConstructionKey,
        std::shared_ptr<const WebGpuOriginalCommandInputs> inputs,
        std::size_t sources,
        const WebGpuOriginalResourceStatesBudget& budget)
        : inputs_(std::move(inputs)), sourceCount_(sources), states_(budget.maxStateControls)
    {}

    static const char* validateBudget(const WebGpuOriginalResourceStatesBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-WEBGPU-ORIGINAL-STATES-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-WEBGPU-ORIGINAL-STATES-OPT-IN";
        if (!b.maxCpuMetadataBytes)
            return "RHI-WEBGPU-ORIGINAL-STATES-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const WebGpuOriginalResourceStates> prepareOriginal(
        std::shared_ptr<const WebGpuOriginalCommandInputs> inputs,
        const WebGpuOriginalResourceStatesBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-WEBGPU-ORIGINAL-STATES-QUOTA";
            return {};
        }
        if ((diagnostic = validateInputs(inputs)))
            return {};
        const auto& graph = *inputs->scalars()->usage()->projection();
        std::size_t count = 0u;
        if (!countSources(graph, budget.maxSourceReferences, count)) {
            diagnostic = "RHI-WEBGPU-ORIGINAL-STATES-REFERENCE-CAPACITY";
            return {};
        }
        const auto inventory = core::SharedIdentityProjection::inspect(
            count, budget.maxSourceReferences, budget.maxStateControls, [&](std::size_t i) {
                return sourceAt(graph, i).identity();
            });
        using Status = core::SharedIdentityProjection::Status;
        if (inventory.status != Status::Complete) {
            diagnostic = inventory.status == Status::IdentityCapacity
                ? "RHI-WEBGPU-ORIGINAL-STATES-CONTROL-CAPACITY"
                : inventory.status == Status::Alias ? "RHI-WEBGPU-ORIGINAL-STATES-SOURCE-ALIAS"
                                                    : "RHI-WEBGPU-ORIGINAL-STATES-SOURCE-MISSING";
            return {};
        }
        for (std::size_t i = 0u; i < count; ++i)
            if ((diagnostic = observeDiagnostic(sourceAt(graph, i))))
                return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<WebGpuOriginalResourceStates>(
                ConstructionKey{}, std::move(inputs), count, budget);
            for (std::size_t i = 0u; i < count; ++i) {
                const auto source = sourceAt(graph, i);
                if (!source.control || result->find(source.resource))
                    continue;
                result->states_.append({source, *source.control.observe()});
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-ORIGINAL-STATES-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-ORIGINAL-STATES-ALLOCATION";
            return {};
        }
    }

    // Exact immutable source identity only. Pending/current facts are deliberately
    // absent; callers still need validate() or an exact owned transaction.
    const char* validateSourceOwners() const noexcept
    {
        if (const auto* id = validateInputs(inputs_))
            return id;
        const auto& graph = *inputs_->scalars()->usage()->projection();
        std::size_t count = 0u;
        if (!countSources(graph, sourceCount_, count) || count != sourceCount_)
            return "RHI-WEBGPU-ORIGINAL-STATES-COVERAGE";
        for (std::size_t i = 0u; i < count; ++i) {
            const auto source = sourceAt(graph, i);
            if (!source.control) {
                if (source.resource.isValid() || source.owner)
                    return "RHI-WEBGPU-ORIGINAL-STATES-SOURCE-MISSING";
                continue; // Explicit state-free sampler source.
            }
            const auto* original = find(source.resource);
            if (!original || original->source.domain != source.domain
                || original->source.control != source.control
                || !core::SharedOwnerProjection::sameOwner(original->source.owner, source.owner))
                return "RHI-WEBGPU-ORIGINAL-STATES-SOURCE-CHANGED";
        }
        return nullptr;
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateSourceOwners())
            return id;
        for (const auto& original : states_.values()) {
            if (const auto* id = observeDiagnostic(original.source))
                return id;
            if (!original.source.control.matches(original.observation))
                return "RHI-WEBGPU-ORIGINAL-STATES-STALE";
        }
        return nullptr;
    }

    const State* find(rhi::GpuResourceId resource) const noexcept
    {
        for (const auto& state : states_.values())
            if (state.source.resource == resource)
                return &state;
        return nullptr;
    }

    const auto& states() const noexcept { return states_.values(); }

    std::size_t sourceReferences() const noexcept { return sourceCount_; }

    const auto& inputs() const noexcept { return inputs_; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return webGpuGenericRecordingUsageDiagnostic;
    }

private:
    static const char* validateInputs(
        const std::shared_ptr<const WebGpuOriginalCommandInputs>& inputs) noexcept
    {
        if (!inputs || !inputs->hasQuotaBoundSharedOwner(inputs))
            return "RHI-WEBGPU-ORIGINAL-STATES-INPUTS";
        if (const auto* id = inputs->validate())
            return id;
        for (const auto& pair : inputs->scalars()->usage()->projection()->originalReadbacks()) {
            const auto& source = *pair->destination()->source();
            for (std::size_t i = 0u; i < source.retainedNativeAllocationRecordCount(); ++i) {
                const auto* original = source.retainedNativeResourceAt(i);
                if (!original || !original->id.isValid()
                    || bool(original->buffer) == bool(original->texture))
                    return "RHI-WEBGPU-ORIGINAL-STATES-READBACK-SOURCE-MISSING";
            }
        }
        return nullptr;
    }

    static const char* observeDiagnostic(const Source& source) noexcept
    {
        if (!source.resource.isValid() && !source.owner && !source.control)
            return nullptr;
        if (!source.resource.isValid() || !source.owner || !source.control)
            return "RHI-WEBGPU-ORIGINAL-STATES-SOURCE-MISSING";
        if (!source.control.active())
            return "RHI-WEBGPU-ORIGINAL-STATES-EXHAUSTED";
        if (source.control.pending())
            return "RHI-WEBGPU-ORIGINAL-STATES-PENDING";
        if (!rhi::semantics::isKnownResourceState(source.control.state()))
            return "RHI-WEBGPU-ORIGINAL-STATES-STATE-INVALID";
        if (source.domain == Domain::Buffer
            && !rhi::semantics::isBufferResourceState(source.control.state()))
            return "RHI-WEBGPU-ORIGINAL-STATES-STATE-DOMAIN";
        return nullptr;
    }

    static Source fromTransfer(const WebGpuTransferResourceObservation& resource) noexcept
    {
        if (resource.buffer && !resource.texture)
            return {resource.id,
                Domain::Buffer,
                resource.buffer,
                Control{resource.buffer->recordedState()}};
        if (resource.texture && !resource.buffer)
            return {resource.id,
                Domain::UniformWholeTexture,
                resource.texture,
                resource.texture->recordedState()};
        return {resource.id, Domain::Buffer, {}, {}};
    }

    static Source fromOrdinary(const WebGpuOrdinaryResourceObservation& resource) noexcept
    {
        return resource.sampler() ? Source{} : fromTransfer(resource.backing());
    }

    static bool countSources(
        const WebGpuGenericCommandProjection& graph, std::size_t max, std::size_t& count) noexcept
    {
        count = 0u;
        const auto add = [&](std::size_t n) {
            if (n > max - count)
                return false;
            count += n;
            return true;
        };
        if (graph.transfer() && !add(graph.transfer()->resources().size()))
            return false;
        if (graph.resources() && !add(graph.resources()->resources().size()))
            return false;
        for (const auto& source : graph.originalTextureUploads()) {
            (void)source;
            if (!add(1u) || !add(1u))
                return false;
        }
        for (const auto& pair : graph.originalReadbacks())
            if (!add(pair->destination()->source()->retainedNativeAllocationRecordCount())
                || !add(1u))
                return false;
        for (const auto& group : graph.physicalGroups())
            for (const auto& set : group->observedSets())
                if (!add(set.observation->resources().resources().size()))
                    return false;
        return true;
    }

    static Source sourceAt(const WebGpuGenericCommandProjection& graph, std::size_t i) noexcept
    {
        if (graph.transfer()) {
            const auto& resources = graph.transfer()->resources();
            if (i < resources.size())
                return fromTransfer(resources[i]);
            i -= resources.size();
        }
        if (graph.resources()) {
            const auto& resources = graph.resources()->resources();
            if (i < resources.size())
                return fromOrdinary(resources[i]);
            i -= resources.size();
        }
        for (const auto& source : graph.originalTextureUploads()) {
            if (i == 0u)
                return fromTransfer(source->resource());
            if (i == 1u)
                return fromTransfer(source->destination()->resource());
            i -= 2u;
        }
        for (const auto& pair : graph.originalReadbacks()) {
            const auto& source = *pair->destination()->source();
            const auto count = source.retainedNativeAllocationRecordCount();
            if (i < count) {
                // validateInputs proved this original typed observation exists;
                // absent observations never become state-free sampler entries.
                return fromTransfer(*source.retainedNativeResourceAt(i));
            }
            i -= count;
            if (i == 0u) {
                const auto& destination = *pair->destination();
                const auto& buffer = destination.nativeResource();
                return {destination.resourceId(),
                    Domain::Buffer,
                    buffer,
                    Control{buffer->recordedState()}};
            }
            --i;
        }
        for (const auto& group : graph.physicalGroups())
            for (const auto& set : group->observedSets()) {
                const auto& resources = set.observation->resources().resources();
                if (i < resources.size())
                    return fromOrdinary(resources[i]);
                i -= resources.size();
            }
        return {};
    }

    const std::shared_ptr<const WebGpuOriginalCommandInputs> inputs_;
    const std::size_t sourceCount_;
    core::BoundedSequence<State> states_;
};
} // namespace arp::rhi_webgpu
