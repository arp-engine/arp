#pragma once

#include "core/math/unsigned_range.h"
#include "rhi/frontend/command_arguments.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/command_recording_order.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/descriptor_validation.h"
#include "rhi/frontend/pipeline_shader_interface_validation.h"
#include "rhi/frontend/query_command_points.h"
#include "rhi/frontend/query_pass_timing.h"
#include "rhi/frontend/query_pool_arguments.h"
#include "rhi/frontend/resource_access_scope.h"
#include "rhi/frontend/resource_access_usage.h"
#include "rhi/frontend/transfer_copy.h"
#include "rhi/frontend/transfer_readback_arguments.h"
#include "rhi/frontend/transfer_upload_input.h"
#include "rhi/semantics/command_input_admission_lowering.h"
#include "rhi/semantics/command_lowering.h"

// region Command usage
// region original_command_usage
namespace arp::rhi {
// Frozen original typed resource/reference/command ownership graph only.
// Missing source families cannot satisfy CommandUsageContract signatures.
// Input/state/native recording/completion still require their own exact graph.
class OriginalCommandUsage final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCommandUsage;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Reference {
        std::uint32_t sourceIndex;
    };

    OriginalCommandUsage(ConstructionKey,
        std::shared_ptr<const OriginalCommandResourceSources> sources,
        std::size_t commands,
        std::size_t references,
        std::uint32_t owners)
        : sources_(std::move(sources)), commands_(commands), references_(references),
          ownerCount_(owners)
    {}

    static std::shared_ptr<const OriginalCommandUsage> prepareOriginal(
        std::shared_ptr<const OriginalCommandResourceSources> sources,
        const CommandUsage* commands,
        std::size_t commandCount,
        const Reference* references,
        std::size_t referenceCount,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = OriginalCommandResourceSources::validateOriginal(sources);
        if (diagnostic)
            return {};
        const auto& budget = sources->originalBudget();
        const auto bytes = budget.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Graph)];
        if (!quota || quota->limitBytes() != bytes) {
            diagnostic = "RHI-ORIGINAL-USAGE-QUOTA";
            return {};
        }
        if (core::SharedOwnerProjection::sameControlBlock(
                quota, sources->originalMetadataQuota())) {
            diagnostic = "RHI-ORIGINAL-USAGE-QUOTA-DOMAIN";
            return {};
        }
        std::uint32_t owners = 0u;
        if ((diagnostic = validateGraphQuota(*sources, quota)))
            return {};
        if ((diagnostic = validateInputs(
                 *sources, commands, commandCount, references, referenceCount, owners)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<OriginalCommandUsage>(
                ConstructionKey{}, std::move(sources), commandCount, referenceCount, owners);
            for (std::size_t i = 0u; i < commandCount; ++i)
                result->commands_.append(commands[i]);
            for (std::size_t i = 0u; i < referenceCount; ++i)
                result->references_.append(references[i]);
            auto published = shareOriginal(std::move(result));
            published->sources_->sealForGraph();
            return published;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-USAGE-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-USAGE-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandUsage>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-USAGE-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = OriginalCommandResourceSources::validateOriginal(sources_))
            return id;
        const auto bytes = sources_->originalBudget().maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Graph)];
        if (!originalMetadataQuota() || originalMetadataQuota()->limitBytes() != bytes)
            return "RHI-ORIGINAL-USAGE-QUOTA";
        return validateGraphQuota(*sources_, originalMetadataQuota());
    }

    const auto& originalSources() const noexcept { return sources_; }

    const auto& commands() const noexcept { return commands_.values(); }

    const auto& references() const noexcept { return references_.values(); }

    std::uint32_t retainedOwnerCount() const noexcept { return ownerCount_; }

    template <class T>
    const char* compareOriginalReference(std::size_t index, const T& object) const noexcept
    {
        static_assert(std::is_same_v<T, GpuBuffer> || std::is_same_v<T, GpuTexture>
            || std::is_same_v<T, GpuTextureView> || std::is_same_v<T, TextureUploadInputSnapshot>
            || std::is_same_v<T, OriginalCommandBufferUploadSource>
            || std::is_same_v<T, TextureReadbackRequestDeclaration>
            || std::is_same_v<T, DescriptorSet> || std::is_same_v<T, DescriptorTable>
            || std::is_same_v<T, RenderPipeline> || std::is_same_v<T, ComputePipeline>
            || std::is_same_v<T, QueryPool>);
        if (const auto* id = validate())
            return id;
        if (index >= references().size())
            return "RHI-ORIGINAL-USAGE-REFERENCE-INDEX";
        const auto& source = sources_->sources()[references()[index].sourceIndex];
        return std::visit(
            [&](const auto& lease) -> const char* {
                if constexpr (std::is_same_v<decltype(lease->originalObject()), const T*>)
                    return lease->originalObject() == &object
                        ? nullptr
                        : "RHI-ORIGINAL-USAGE-REFERENCE-OBJECT";
                else
                    return "RHI-ORIGINAL-USAGE-REFERENCE-DOMAIN";
            },
            source);
    }

    const char* executionDiagnostic(Backend backend) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(backend).diagnostic;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* validateGraphQuota(const OriginalCommandResourceSources& sources,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        for (const auto& source : sources.sources()) {
            const auto* id = std::visit(
                [&](const auto& value) -> const char* {
                    using Lease =
                        std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                    if constexpr (Lease::domain == CommandReferenceDomain::Pipeline
                        || Lease::domain == CommandReferenceDomain::Query)
                        if (const auto* error = value->independentCpuQuotaDiagnostic(quota))
                            return error;
                    if constexpr (Lease::domain == CommandReferenceDomain::Query)
                        if (!core::SharedOwnerProjection::sameOwner(
                                value->originalInput()->originalMetadataQuota(), quota))
                            return "RHI-ORIGINAL-USAGE-QUERY-GRAPH-QUOTA";
                    if constexpr (Lease::domain == CommandReferenceDomain::Upload)
                        if (!core::SharedOwnerProjection::sameOwner(
                                value->originalInputQuota(), quota))
                            return "RHI-ORIGINAL-USAGE-UPLOAD-GRAPH-QUOTA";
                    if constexpr (Lease::domain == CommandReferenceDomain::Readback
                        || std::is_same_v<Lease, OriginalCommandReadbackTextureSource>)
                        if (!core::SharedOwnerProjection::sameOwner(
                                value->originalInputQuota(), quota))
                            return "RHI-ORIGINAL-USAGE-READBACK-GRAPH-QUOTA";
                    return nullptr;
                },
                source);
            if (id)
                return id;
        }
        return nullptr;
    }

    static const char* validateInputs(const OriginalCommandResourceSources& sources,
        const CommandUsage* commands,
        std::size_t commandCount,
        const Reference* references,
        std::size_t referenceCount,
        std::uint32_t& owners) noexcept
    {
        const auto& budget = sources.originalBudget();
        if (!commands || !commandCount || (referenceCount && !references))
            return "RHI-ORIGINAL-USAGE-INPUT";
        if (commandCount > budget.maxCommands || referenceCount > budget.maxReferences)
            return "RHI-ORIGINAL-USAGE-CAPACITY";
        // Count actual retained ownership nodes, independently of CPU bytes:
        // this graph, its source table, and each lease's declared owner closure.
        constexpr std::uint32_t roots = 2u;
        if (budget.maxGraphOwners < roots)
            return "RHI-ORIGINAL-USAGE-OWNER-CAPACITY";
        if (const auto* id = sources.cpuOwnerCount(roots, budget.maxGraphOwners, owners))
            return id;
        for (std::size_t i = 0u; i < referenceCount; ++i)
            if (references[i].sourceIndex >= sources.sources().size())
                return "RHI-ORIGINAL-USAGE-SOURCE-INDEX";
        const CommandUsageSequenceContract::Diagnostics diagnostics{};
        if (const auto* id = CommandUsageSequenceContract::validate(
                commands,
                commandCount,
                referenceCount,
                [&](std::size_t i) {
                    return OriginalCommandResourceSources::domain(
                        sources.sources()[references[i].sourceIndex]);
                },
                diagnostics))
            return id;
        return semantics::CommandRecordingOrder::validate(
            commands, commandCount, budget.maxDebugDepth);
    }

    const std::shared_ptr<const OriginalCommandResourceSources> sources_;
    core::BoundedSequence<CommandUsage> commands_;
    core::BoundedSequence<Reference> references_;
    const std::uint32_t ownerCount_;
};

// endregion original_command_usage
// endregion Command usage

// region Command scalars

// region original_command_scalars

// Exact original scalar/variable ownership binding only. Complete typed input,
// logical registry/state/access, native recording/completion remain independent.
class OriginalCommandScalars final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCommandScalars;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    OriginalCommandScalars(ConstructionKey,
        std::shared_ptr<const OriginalCommandUsage> usage,
        std::shared_ptr<const CommandVariableArguments> arguments)
        : usage_(std::move(usage)), arguments_(std::move(arguments))
    {}

    static std::shared_ptr<const OriginalCommandScalars> prepareOriginal(
        std::shared_ptr<const OriginalCommandUsage> usage,
        std::shared_ptr<const CommandVariableArguments> arguments,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateSources(usage, arguments, quota);
        if (diagnostic)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<OriginalCommandScalars>(
                ConstructionKey{}, std::move(usage), std::move(arguments)));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-SCALARS-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandScalars>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-SCALARS-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        return validateSources(usage_, arguments_, originalMetadataQuota());
    }

    const auto& originalUsage() const noexcept { return usage_; }

    const auto& originalArguments() const noexcept { return arguments_; }

    std::uint32_t retainedOwnerCount() const noexcept
    {
        return usage_->retainedOwnerCount() + additionalOwners;
    }

    const char* compareOriginalArguments(
        std::size_t command, const CommandVariableArguments::Input& input) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arguments_->matches(command, input) ? nullptr : "RHI-ORIGINAL-SCALARS-INPUT";
    }

    const char* executionDiagnostic(Backend backend) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(backend).diagnostic;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static constexpr std::uint32_t additionalOwners =
        2u; // Scalar root and original variable store.

    static const char* validateSources(const std::shared_ptr<const OriginalCommandUsage>& usage,
        const std::shared_ptr<const CommandVariableArguments>& arguments,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        if (const auto* id = OriginalCommandUsage::validateOriginal(usage))
            return id;
        if (!arguments || !arguments->hasQuotaBoundSharedOwner(arguments))
            return "RHI-ORIGINAL-SCALARS-ARGUMENTS-OWNER";
        if (!quota || !core::SharedOwnerProjection::sameOwner(quota, usage->originalMetadataQuota())
            || !core::SharedOwnerProjection::sameOwner(quota, arguments->originalMetadataQuota()))
            return "RHI-ORIGINAL-SCALARS-GRAPH-QUOTA";
        const auto& budget = usage->originalSources()->originalBudget();
        if (budget.maxGraphOwners < additionalOwners
            || usage->retainedOwnerCount() > budget.maxGraphOwners - additionalOwners)
            return "RHI-ORIGINAL-SCALARS-OWNER-CAPACITY";
        if (const auto* id = arguments->validateUsage(usage->commands().data(),
                usage->commands().size(),
                "RHI-ORIGINAL-SCALARS-COUNT",
                "RHI-ORIGINAL-SCALARS-ORDER"))
            return id;
        return arguments->validateCapacity({1u,
            true,
            budget.maxCommands,
            budget.maxDynamicOffsets,
            budget.maxLabelBytes,
            budget.maxPushBytes,
            budget.maxCpuBytes[static_cast<std::size_t>(
                OriginalCommandAdmissionBudget::CpuDomain::Graph)]});
    }

    const std::shared_ptr<const OriginalCommandUsage> usage_;
    const std::shared_ptr<const CommandVariableArguments> arguments_;
};

// endregion original_command_scalars
// endregion Command scalars

// region Command input binding validation

// region original_command_typed_source

// CanonicalOriginalTypedSources/1. Cold L2 declaration validators only. Values,
// successful validation and hydrated/hash labels cannot issue factory ownership,
// artifact identity, shader access proof, state, native work or completion.
// Genuine adapters must freeze and revalidate these declarations against their
// exact factory records before entering the existing canonical Source/Inputs.
class OriginalCommandTypedSourceContract final {
public:
    enum class Kind {
        GraphicsPipeline,
        ComputePipeline,
        Descriptor,
        DescriptorTable,
        Query,
        Unsupported
    };

    static constexpr CommandReferenceDomain domain(Kind kind) noexcept
    {
        switch (kind) {
        case Kind::GraphicsPipeline:
        case Kind::ComputePipeline:
            return CommandReferenceDomain::Pipeline;
        case Kind::Descriptor:
            return CommandReferenceDomain::Descriptor;
        case Kind::DescriptorTable:
            return CommandReferenceDomain::DescriptorTable;
        case Kind::Query:
            return CommandReferenceDomain::Query;
        case Kind::Unsupported:
            return CommandReferenceDomain::Count;
        }
        return CommandReferenceDomain::Count;
    }

    static constexpr auto ownership(Kind kind) noexcept
    {
        return OriginalCommandAdmissionContract::ownership(domain(kind));
    }

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxPipelineDefinitions = 0u, maxDescriptorElements = 0u, maxQuerySlots = 0u;
        std::uint64_t maxShaderArtifactBytes = 0u, maxAccessDeclarations = 0u,
                      maxNativeCommandCalls = 0u;
        std::uint32_t maxNativePayloadObjects = 0u, maxCallbackTickets = 0u,
                      maxCallbackRegistrations = 0u;
        std::uint64_t sourceCpuBytes = 0u, graphCpuBytes = 0u, recordingCpuBytes = 0u,
                      callbackCpuBytes = 0u;
    };

    struct Requirements {
        std::uint64_t pipelineDefinitions = 0u, descriptorElements = 0u, querySlots = 0u;
        std::uint64_t shaderArtifactBytes = 0u, accessDeclarations = 0u, nativeCommandCalls = 0u;
        std::uint32_t nativePayloadObjects = 0u, callbackTickets = 0u, callbackRegistrations = 0u;
    };

    static constexpr const char* validateIntent(
        const Budget& b, const OriginalCommandAdmissionBudget& authored) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-TYPED-SOURCE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-TYPED-SOURCE-OPT-IN";
        if (const auto* id = OriginalCommandAdmissionContract::validateIntent(authored))
            return id;
        if (!b.maxPipelineDefinitions && !b.maxDescriptorElements && !b.maxQuerySlots)
            return "RHI-ORIGINAL-TYPED-SOURCE-BUDGET";
        if (b.maxPipelineDefinitions && !b.maxShaderArtifactBytes)
            return "RHI-ORIGINAL-TYPED-SHADER-BUDGET";
        using C = OriginalCommandAdmissionBudget::CpuDomain;
        if (!b.sourceCpuBytes || !b.graphCpuBytes
            || b.sourceCpuBytes != authored.maxCpuBytes[static_cast<std::size_t>(C::Source)]
            || b.graphCpuBytes != authored.maxCpuBytes[static_cast<std::size_t>(C::Graph)]
            || b.recordingCpuBytes != authored.maxCpuBytes[static_cast<std::size_t>(C::Recording)]
            || b.callbackCpuBytes != authored.maxCpuBytes[static_cast<std::size_t>(C::Callback)])
            return "RHI-ORIGINAL-TYPED-CPU-BUDGET";
        if (b.maxNativeCommandCalls != authored.maxTypedNativeCommandCalls
            || b.maxNativePayloadObjects != authored.maxNativePayloadObjects
            || b.maxCallbackTickets != authored.maxCallbackTickets
            || b.maxCallbackRegistrations != authored.maxCallbackRegistrations)
            return "RHI-ORIGINAL-TYPED-SOURCE-AUTHORED-NATIVE-BUDGET";
        if (b.maxNativeCommandCalls
            && (!b.recordingCpuBytes || !b.maxNativePayloadObjects || !b.maxCallbackTickets
                || !b.maxCallbackRegistrations || !b.callbackCpuBytes))
            return "RHI-ORIGINAL-TYPED-NATIVE-LIFETIME-BUDGET";
        return nullptr;
    }

    static constexpr const char* validateRequirements(const Requirements& r,
        const Budget& b,
        const OriginalCommandAdmissionBudget& authored) noexcept
    {
        if (const auto* id = validateIntent(b, authored))
            return id;
        if (r.pipelineDefinitions > b.maxPipelineDefinitions
            || r.descriptorElements > b.maxDescriptorElements || r.querySlots > b.maxQuerySlots
            || r.shaderArtifactBytes > b.maxShaderArtifactBytes
            || r.accessDeclarations > b.maxAccessDeclarations
            || r.nativeCommandCalls > b.maxNativeCommandCalls
            || r.nativePayloadObjects > b.maxNativePayloadObjects
            || r.callbackTickets > b.maxCallbackTickets
            || r.callbackRegistrations > b.maxCallbackRegistrations)
            return "RHI-ORIGINAL-TYPED-REQUIREMENTS-CAPACITY";
        return nullptr;
    }

    // Quota identities, not equal byte limits, establish independent domains.
    static const char* validateQuotas(const Budget& b,
        const OriginalCommandAdmissionBudget& authored,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u>& quotas) noexcept
    {
        if (const auto* id = validateIntent(b, authored))
            return id;
        const std::array<std::uint64_t, 4u> bytes{
            b.sourceCpuBytes, b.graphCpuBytes, b.recordingCpuBytes, b.callbackCpuBytes};
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> active;
        std::size_t count = 0u;
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            if (bytes[i] ? (!quotas[i] || quotas[i]->limitBytes() != bytes[i]) : bool(quotas[i]))
                return "RHI-ORIGINAL-TYPED-QUOTA";
            if (quotas[i])
                active[count++] = quotas[i];
        }
        return core::SharedOwnerProjection::hasIndependentOwners(count,
                   [&](std::size_t i) noexcept -> const auto& {
                       return active[i];
                   })
            ? nullptr
            : "RHI-ORIGINAL-TYPED-QUOTA-DOMAIN";
    }

    // All declaration validation is cold. Existing L2 validators can allocate
    // diagnostics; call under the original Source quota during preparation.
    static const char* validatePipelineDeclaration(const PipelineLayoutDesc& layout,
        const ShaderPackageDesc& package,
        const ShaderEntrypoint* selected,
        std::size_t count,
        Kind kind)
    {
        if (kind != Kind::GraphicsPipeline && kind != Kind::ComputePipeline)
            return "RHI-ORIGINAL-TYPED-PIPELINE-KIND";
        if (!selected || !count)
            return "RHI-ORIGINAL-TYPED-PIPELINE-ENTRY";
        using Contract = PipelineShaderDeclarationContract;
        return Contract::validate(layout,
            package,
            count,
            kind == Kind::GraphicsPipeline ? Contract::Kind::GraphicsPipeline
                                           : Contract::Kind::ComputePipeline,
            [&](std::size_t i) -> const ShaderEntrypoint& {
                return selected[i];
            });
    }

    // One frozen current write and one positional canonical resource reference
    // per written element. sourceIndex is never a resource ID or native slot.
    // Access is an explicit declaration; a later artifact proof must establish
    // actual shader footprint, and the original state plan must establish state.
    using Element = OriginalCommandDescriptorSetElement;
    using TableElement = OriginalCommandDescriptorTableElement;

    struct SamplerReferenceDecision {
        std::uint32_t contractVersion = 0u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool referenceAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-SAMPLER-REFERENCE-BACKEND-UNKNOWN";
    };

    static constexpr SamplerReferenceDecision samplerReferenceLowering(Backend backend) noexcept
    {
        return semantics::ResourceLowering::GeneralSamplerDescriptorReference_lowering<
            SamplerReferenceDecision>(backend);
    }

    static const char* validateGeneralSamplerFootprint(const Element& element,
        const DescriptorBindingDesc& binding,
        const SamplerResourceLayout& sampler) noexcept
    {
        return validateSamplerFootprint(element, binding, sampler, 0u);
    }

    static const char* validateSamplerFootprint(const Element& element,
        const DescriptorBindingDesc& binding,
        const SamplerResourceLayout& sampler,
        std::uint32_t bindlessArraysVersion) noexcept
    {
        if (bindlessArraysVersion > 1u)
            return "RHI-ORIGINAL-BINDLESS-SAMPLER-VERSION";
        if (binding.type != DescriptorType::Sampler || element.write.type != binding.type
            || !binding.arrayCount
            || (!bindlessArraysVersion && (binding.arrayCount != 1u || binding.tableBacked))
            || binding.dynamicOffset || element.write.binding != binding.binding
            || element.write.arrayElement >= binding.arrayCount
            || element.write.resourceKind != DescriptorResourceKind::Sampler
            || element.write.physicalLowering.enabled || element.bufferAccessOffset
            || element.bufferAccessBytes || element.dynamicOffset
            || !element.write.bufferBindingRange.matches(DescriptorBufferBindingRange{}))
            return "RHI-ORIGINAL-GENERAL-SAMPLER-FOOTPRINT-PROFILE";
        const auto supported = static_cast<std::uint32_t>(
            ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute);
        if ((static_cast<std::uint32_t>(binding.stages) & supported)
                != static_cast<std::uint32_t>(binding.stages)
            || element.stages == PipelineStage::None
            || element.stages != shaderStages(binding.stages))
            return "RHI-ORIGINAL-GENERAL-SAMPLER-FOOTPRINT-STAGES";
        if (element.write.resourceState != ResourceState::Undefined
            || element.access != ResourceAccess::None)
            return "RHI-ORIGINAL-GENERAL-SAMPLER-FOOTPRINT-NONMEMORY";
        if (binding.comparisonSampler != (sampler.compareOp != CompareOp::Never))
            return "RHI-ORIGINAL-GENERAL-SAMPLER-FOOTPRINT-COMPARISON";
        return nullptr;
    }

    // FullBoundView/1 is a conservative footprint, independent of caller
    // relative-access hints and GetDimensions operation placement.
    static const char* validateFullBoundView(const Element& element,
        const DescriptorBindingDesc& binding,
        std::uint64_t bufferBytes,
        std::uint32_t generalVersion = 0u) noexcept
    {
        if (const auto* id = validateBoundBufferFootprint(element, binding, generalVersion))
            return id;
        OriginalDescriptorBufferBindingContract::FiniteAccessDiagnosticObservation observed;
        if (const auto* id =
                OriginalDescriptorBufferBindingContract::diagnoseFiniteAccess({1u, true},
                    element.write.bufferBindingRange,
                    bufferBytes,
                    element.dynamicOffset,
                    0u,
                    element.write.bufferBindingRange.sizeBytes,
                    element.bufferAccessOffset,
                    element.bufferAccessBytes,
                    observed))
            return id;
        return element.bufferAccessOffset == observed.physicalAccess.offsetBytes
                && element.bufferAccessBytes == observed.physicalAccess.sizeBytes
            ? nullptr
            : "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-COVERAGE";
    }

    // Shared full-view stages/access semantics. Coverage never manufactures
    // either an authored finite slice or a legacy whole-buffer binding.
    static const char* validateBoundBufferFootprint(const Element& element,
        const DescriptorBindingDesc& binding,
        std::uint32_t generalVersion = 0u) noexcept
    {
        if (generalVersion > 1u)
            return "RHI-ORIGINAL-CONSUMPTION-GENERAL-VERSION";
        if (binding.arrayCount != 1u || binding.tableBacked || element.write.type != binding.type
            || element.write.binding != binding.binding || element.write.arrayElement
            || (binding.type != DescriptorType::StorageBuffer
                && binding.type != DescriptorType::ReadOnlyStorageBuffer
                && !(generalVersion == 1u && binding.type == DescriptorType::UniformBuffer)))
            return "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-PROFILE";
        const auto supportedStages = static_cast<std::uint32_t>(
            ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute);
        if ((static_cast<std::uint32_t>(binding.stages) & supportedStages)
                != static_cast<std::uint32_t>(binding.stages)
            || element.stages == PipelineStage::None
            || element.stages != shaderStages(binding.stages))
            return "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-STAGES";
        const auto access = binding.type != DescriptorType::StorageBuffer
            ? ResourceAccess::ShaderRead
            : ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        if (element.access != access)
            return "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-ACCESS";
        return semantics::validateResourceAccessScope(
            {element.write.resourceState, element.stages, element.access});
    }

    static const char* validateWholeBoundView(const Element& element,
        const DescriptorBindingDesc& binding,
        std::uint64_t bufferBytes,
        std::uint32_t generalVersion = 0u) noexcept
    {
        if (const auto* id = validateBoundBufferFootprint(element, binding, generalVersion))
            return id;
        if (!element.write.bufferBindingRange.matches(DescriptorBufferBindingRange{})
            || element.dynamicOffset)
            return "RHI-ORIGINAL-WHOLE-FOOTPRINT-SCHEMA";
        return bufferBytes && !element.bufferAccessOffset
                && element.bufferAccessBytes == bufferBytes
            ? nullptr
            : "RHI-ORIGINAL-WHOLE-FOOTPRINT-COVERAGE";
    }

    static const char* validateGeneralTextureFootprint(const Element& element,
        const DescriptorBindingDesc& binding,
        const TextureViewResourceLayout& view) noexcept
    {
        if (binding.arrayCount != 1u || binding.tableBacked || element.write.type != binding.type
            || element.write.binding != binding.binding || element.write.arrayElement
            || element.write.resourceKind != DescriptorResourceKind::TextureView
            || element.bufferAccessOffset || element.bufferAccessBytes || element.dynamicOffset
            || !element.write.bufferBindingRange.matches(DescriptorBufferBindingRange{})
            || (binding.type != DescriptorType::SampledTexture
                && binding.type != DescriptorType::StorageTexture))
            return "RHI-ORIGINAL-GENERAL-TEXTURE-FOOTPRINT-PROFILE";
        const auto stages = shaderStages(binding.stages);
        const auto supportedStages = static_cast<std::uint32_t>(
            ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute);
        if ((static_cast<std::uint32_t>(binding.stages) & supportedStages)
                != static_cast<std::uint32_t>(binding.stages)
            || stages == PipelineStage::None || element.stages != stages)
            return "RHI-ORIGINAL-GENERAL-TEXTURE-FOOTPRINT-STAGES";
        const bool storage = binding.type == DescriptorType::StorageTexture;
        const auto usage = storage ? TextureUsage::Storage : TextureUsage::Sampled;
        if ((view.usage & usage) != usage || !view.mipLevelCount || !view.arrayLayerCount
            || (binding.textureViewDimensionConstrained
                && binding.textureViewDimension != view.dimension))
            return "RHI-ORIGINAL-GENERAL-TEXTURE-FOOTPRINT-VIEW";
        if (storage) {
            if (view.aspect != TextureAspect::Color)
                return "RHI-ORIGINAL-GENERAL-TEXTURE-FOOTPRINT-ASPECT";
            if (const auto* id =
                    validateStorageTextureViewFormat(binding.storageTextureFormat, view.format))
                return id;
            if (binding.storageTextureAccess != StorageTextureAccess::ReadOnly
                && binding.storageTextureAccess != StorageTextureAccess::WriteOnly
                && binding.storageTextureAccess != StorageTextureAccess::ReadWrite)
                return "RHI-ORIGINAL-GENERAL-TEXTURE-FOOTPRINT-ACCESS";
        } else if (!validateSampledDepthView(binding.depthTexture, view.format, view.aspect)
                       .success)
            return "RHI-ORIGINAL-GENERAL-TEXTURE-FOOTPRINT-ASPECT";
        const auto access =
            !storage || binding.storageTextureAccess == StorageTextureAccess::ReadOnly
            ? ResourceAccess::ShaderRead
            : binding.storageTextureAccess == StorageTextureAccess::WriteOnly
            ? ResourceAccess::ShaderWrite
            : ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        if (element.access != access)
            return "RHI-ORIGINAL-GENERAL-TEXTURE-FOOTPRINT-ACCESS";
        return semantics::validateResourceAccessScope(
            {element.write.resourceState, element.stages, element.access});
    }

    static const char* validateSetElements(const DescriptorLayoutDesc& layout,
        const Element* elements,
        std::size_t count,
        const std::shared_ptr<const OriginalCommandResourceSources>& sources,
        const GpuDevice& device,
        std::uint32_t bindlessSamplerArraysVersion = 0u)
    {
        if (bindlessSamplerArraysVersion > 1u)
            return "RHI-ORIGINAL-BINDLESS-SAMPLER-VERSION";
        if (!validateDescriptorLayout(layout).success)
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-LAYOUT";
        if (const auto* id =
                OriginalCommandResourceSources::validateRegisteredLifetime(sources, device))
            return id;
        if (count && !elements)
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ELEMENTS";
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& element = elements[i];
            const auto& write = element.write;
            const auto* binding = findDescriptorSetBinding(layout, write.binding);
            if (!binding || binding->type != write.type
                || write.arrayElement >= binding->arrayCount)
                return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ELEMENT-SHAPE";
            if (write.type == DescriptorType::Sampler)
                if (const auto* id = samplerReferenceLowering(device.backend()).diagnostic)
                    return id;
            for (std::size_t j = 0u; j < i; ++j)
                if (elements[j].write.binding == write.binding
                    && elements[j].write.arrayElement == write.arrayElement)
                    return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ELEMENT-DUPLICATE";
            if (const auto* id = validateElementReference(
                    element, *binding, *sources, bindlessSamplerArraysVersion))
                return id;
        }
        return nullptr;
    }

    static const char* validateTableElements(const DescriptorTableDesc& table,
        const DescriptorTableWrite* writes,
        const std::size_t* sourceIndices,
        const std::uint64_t* generations,
        std::size_t count,
        const std::shared_ptr<const OriginalCommandResourceSources>& sources,
        const GpuDevice& device)
    {
        if (table.label.empty() || !table.capacity || table.stages == ShaderStage::None
            || !validateDescriptorTextureRequirement(
                table.type, table.textureViewDimensionConstrained, table.depthTexture)
                .success)
            return "RHI-ORIGINAL-TYPED-TABLE-DECLARATION";
        // Sampler tables have no approved comparison/reflection mapping.
        if (table.type == DescriptorType::Sampler)
            return "RHI-ORIGINAL-TYPED-SAMPLER-TABLE-ADAPTER-MISSING";
        // L2 tables carry no state/access field; storage needs a new version.
        if (table.type != DescriptorType::SampledTexture && table.type != DescriptorType::Sampler)
            return "RHI-ORIGINAL-TYPED-TABLE-ACCESS-UNDECLARED";
        if (const auto* id =
                OriginalCommandResourceSources::validateRegisteredLifetime(sources, device))
            return id;
        if (count && (!writes || !sourceIndices || !generations))
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ELEMENTS";
        DescriptorBindingDesc binding;
        binding.type = table.type;
        binding.stages = table.stages;
        binding.textureViewDimensionConstrained = table.textureViewDimensionConstrained;
        binding.textureViewDimension = table.textureViewDimension;
        binding.depthTexture = table.depthTexture;
        for (std::size_t i = 0u; i < count; ++i) {
            if (writes[i].index >= table.capacity || writes[i].type != table.type)
                return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ELEMENT-SHAPE";
            for (std::size_t j = 0u; j < i; ++j)
                if (writes[j].index == writes[i].index)
                    return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ELEMENT-DUPLICATE";
            Element element;
            element.write.type = writes[i].type;
            element.write.resource = writes[i].resource;
            element.write.resourceKind = writes[i].resourceKind;
            element.sourceIndex = sourceIndices[i];
            element.registrationGeneration = generations[i];
            element.stages = shaderStages(table.stages);
            element.access = ResourceAccess::ShaderRead;
            if (const auto* id = validateElementReference(element, binding, *sources))
                return id;
        }
        return nullptr;
    }

    static const char* validateTableElements(const DescriptorTableDesc& table,
        const TableElement* elements,
        std::size_t count,
        const std::shared_ptr<const OriginalCommandResourceSources>& sources,
        const GpuDevice& device)
    {
        // Reuse the complete /1 table validator, including empty declarations.
        if (const auto* id =
                validateTableElements(table, nullptr, nullptr, nullptr, 0u, sources, device))
            return id;
        if (count && !elements)
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ELEMENTS";
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& element = elements[i];
            for (std::size_t j = 0u; j < i; ++j)
                if (elements[j].write.index == element.write.index)
                    return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ELEMENT-DUPLICATE";
            if (const auto* id = validateTableElements(table,
                    &element.write,
                    &element.sourceIndex,
                    &element.registrationGeneration,
                    1u,
                    sources,
                    device))
                return id;
        }
        return nullptr;
    }

    static const char* validateQueryDeclaration(
        const QueryPoolOriginalInputArguments& input) noexcept
    {
        return input.validateDeclaration();
    }

private:
    static PipelineStage shaderStages(ShaderStage stages) noexcept
    {
        PipelineStage result = PipelineStage::None;
        if ((stages & ShaderStage::Vertex) != ShaderStage::None)
            result = result | PipelineStage::VertexShader;
        if ((stages & ShaderStage::Fragment) != ShaderStage::None)
            result = result | PipelineStage::FragmentShader;
        if ((stages & ShaderStage::Compute) != ShaderStage::None)
            result = result | PipelineStage::ComputeShader;
        return result;
    }

    static const char* validateElementReference(const Element& element,
        const DescriptorBindingDesc& binding,
        const OriginalCommandResourceSources& sources,
        std::uint32_t bindlessSamplerArraysVersion = 0u)
    {
        const auto& write = element.write;
        if (write.physicalLowering.enabled)
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-LOWERING-UNBOUND";
        if (element.sourceIndex >= sources.sources().size())
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-SOURCE-INDEX";
        const auto& source = sources.sources()[element.sourceIndex];
        if (OriginalCommandResourceSources::resource(source) != write.resource)
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-RESOURCE";
        const auto domain = OriginalCommandResourceSources::domain(source);
        std::uint64_t generation = 0u;
        if (const auto* buffer =
                std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&source))
            generation = (*buffer)->originalObject()->originalResourceRegistration().generation();
        else if (const auto* view =
                     std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(&source))
            generation = (*view)->originalObject()->originalResourceRegistration().generation();
        else if (const auto* sampler =
                     std::get_if<std::shared_ptr<const OriginalCommandSamplerLease>>(&source))
            generation = (*sampler)->originalObject()->originalResourceRegistration().generation();
        if (!generation || generation != element.registrationGeneration)
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-GENERATION";
        switch (write.type) {
        case DescriptorType::UniformBuffer:
        case DescriptorType::StorageBuffer:
        case DescriptorType::ReadOnlyStorageBuffer:
            if (domain != CommandReferenceDomain::Buffer
                || write.resourceKind != DescriptorResourceKind::Buffer)
                return "RHI-ORIGINAL-TYPED-DESCRIPTOR-RESOURCE-DOMAIN";
            {
                const auto& lease =
                    std::get<std::shared_ptr<const OriginalCommandBufferLease>>(source);
                const auto& buffer = lease->layout();
                const auto usage = write.type == DescriptorType::UniformBuffer
                    ? ResourceUsage::UniformBuffer
                    : ResourceUsage::StorageBuffer;
                if ((buffer.usage & usage) != usage)
                    return "RHI-ORIGINAL-TYPED-DESCRIPTOR-RESOURCE-USAGE";
                if (write.type == DescriptorType::StorageBuffer
                    && lease->originalObject()->gpuWritesSealed())
                    return "RHI-BUFFER-GPU-WRITES-SEALED";
                if (!element.bufferAccessBytes
                    || !core::containsUnsignedRange(std::uint64_t{0u},
                        buffer.sizeBytes,
                        element.bufferAccessOffset,
                        element.bufferAccessBytes))
                    return "RHI-ORIGINAL-TYPED-DESCRIPTOR-BUFFER-RANGE";
            }
            break;
        case DescriptorType::SampledTexture:
        case DescriptorType::StorageTexture:
            if (domain != CommandReferenceDomain::TextureView
                || write.resourceKind != DescriptorResourceKind::TextureView)
                return "RHI-ORIGINAL-TYPED-DESCRIPTOR-RESOURCE-DOMAIN";
            {
                const auto& view =
                    std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(source)
                        ->originalObject()
                        ->desc();
                if (element.bufferAccessOffset || element.bufferAccessBytes)
                    return "RHI-ORIGINAL-TYPED-DESCRIPTOR-BUFFER-RANGE";
                const auto usage = write.type == DescriptorType::SampledTexture
                    ? TextureUsage::Sampled
                    : TextureUsage::Storage;
                if ((view.usage & usage) != usage)
                    return "RHI-ORIGINAL-TYPED-DESCRIPTOR-RESOURCE-USAGE";
                if (binding.textureViewDimensionConstrained
                    && binding.textureViewDimension != view.dimension)
                    return "RHI-ORIGINAL-TYPED-DESCRIPTOR-TEXTURE-DIMENSION";
                if (!validateSampledDepthView(binding.depthTexture, view.format, view.aspect)
                        .success)
                    return "RHI-ORIGINAL-TYPED-DESCRIPTOR-TEXTURE-DEPTH";
                if (write.type == DescriptorType::StorageTexture)
                    if (const auto* id = validateStorageTextureViewFormat(
                            binding.storageTextureFormat, view.format))
                        return id;
            }
            break;
        case DescriptorType::Sampler:
            if (domain != CommandReferenceDomain::Sampler
                || write.resourceKind != DescriptorResourceKind::Sampler)
                return "RHI-ORIGINAL-TYPED-DESCRIPTOR-RESOURCE-DOMAIN";
            return validateSamplerFootprint(element,
                binding,
                std::get<std::shared_ptr<const OriginalCommandSamplerLease>>(source)->layout(),
                bindlessSamplerArraysVersion);
        case DescriptorType::InputAttachment:
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-LOWERING-UNBOUND";
        default:
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-TYPE";
        }
        if (element.stages == PipelineStage::None
            || (element.stages & shaderStages(binding.stages)) != element.stages)
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ACCESS-STAGES";
        if (write.type == DescriptorType::UniformBuffer
            || write.type == DescriptorType::ReadOnlyStorageBuffer
            || write.type == DescriptorType::SampledTexture) {
            if (element.access != ResourceAccess::ShaderRead)
                return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ACCESS-MODE";
        } else if (write.type == DescriptorType::StorageBuffer) {
            const auto allowed = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
            if (element.access == ResourceAccess::None
                || (element.access & allowed) != element.access)
                return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ACCESS-MODE";
        } else if (write.type == DescriptorType::StorageTexture) {
            const auto allowed = binding.storageTextureAccess == StorageTextureAccess::ReadOnly
                ? ResourceAccess::ShaderRead
                : binding.storageTextureAccess == StorageTextureAccess::WriteOnly
                ? ResourceAccess::ShaderWrite
                : ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
            if (element.access == ResourceAccess::None
                || (element.access & allowed) != element.access)
                return "RHI-ORIGINAL-TYPED-DESCRIPTOR-ACCESS-MODE";
        }
        return semantics::validateResourceAccessScope(
            {write.resourceState, element.stages, element.access});
    }
};

// endregion original_command_typed_source

// region original_command_typed_input

// CanonicalOriginalTypedInputs/1: complete declared fields/reference families.
// These validators do not add an adapter, mutate CommandInputContract::Scalar,
// or authorize work. Original Inputs must bind positional *owned* typed sources,
// complete preflight/replay and the same original state plan before execution.
class OriginalCommandTypedInputContract final {
public:
    // Declared CPU binding shape only. Positional Sources/Inputs, initialized
    // content, state and native ownership must be joined independently.
    static const char* validateVertexBindingShape(const CommandVariableArguments::Input& input,
        const BufferResourceLayout& buffer,
        const VertexInputConsumptionContract& vertex) noexcept
    {
        if (input.scalar.kind() != CommandUsageKind::BindVertexBuffer)
            return "RHI-ORIGINAL-GEOMETRY-BINDING-KIND";
        if (const auto* id = CommandVariableArguments::validateInput(input))
            return id;
        if ((buffer.usage & ResourceUsage::VertexBuffer) == ResourceUsage::None)
            return "RHI-ORIGINAL-GEOMETRY-VERTEX-USAGE";
        if (const auto* id = vertex.diagnostic())
            return id;
        const auto& binding = *input.scalar.get<CommandScalarArguments::VertexBinding>();
        const auto* stream = vertex.findStream(binding.slot);
        if (!stream)
            return "RHI-VERTEX-INPUT-SLOT-UNDECLARED";
        return core::containsUnsignedRange(
                   std::uint64_t{0u}, buffer.sizeBytes, binding.offset, stream->fetchBytes)
            ? nullptr
            : "RHI-VERTEX-INPUT-BUFFER-RANGE";
    }

    static const char* validateIndexBindingShape(
        const CommandVariableArguments::Input& input, const BufferResourceLayout& buffer) noexcept
    {
        if (input.scalar.kind() != CommandUsageKind::BindIndexBuffer)
            return "RHI-ORIGINAL-GEOMETRY-BINDING-KIND";
        if (const auto* id = CommandVariableArguments::validateInput(input))
            return id;
        if ((buffer.usage & ResourceUsage::IndexBuffer) == ResourceUsage::None)
            return "RHI-ORIGINAL-GEOMETRY-INDEX-USAGE";
        const auto& binding = *input.scalar.get<CommandScalarArguments::IndexBinding>();
        const auto width = IndexInputConsumptionContract::elementBytes(binding.format);
        if (!width)
            return "RHI-INDEX-INPUT-FORMAT-UNKNOWN";
        if (!core::containsUnsignedRange(
                std::uint64_t{0u}, buffer.sizeBytes, binding.offset, std::uint64_t{width}))
            return "RHI-ORIGINAL-GEOMETRY-INDEX-RANGE";
        return binding.offset % width || (buffer.sizeBytes - binding.offset) % width
            ? "RHI-ORIGINAL-GEOMETRY-INDEX-ALIGNMENT"
            : nullptr;
    }

    static const char* validateVertexBindingShape(const CommandVariableArguments::Input& input,
        const BufferResourceLayout& buffer,
        const core::pmr::Vector<VertexBufferLayoutDesc>& streams) noexcept
    {
        if (input.scalar.kind() != CommandUsageKind::BindVertexBuffer)
            return "RHI-ORIGINAL-GEOMETRY-BINDING-KIND";
        if (const auto* id = CommandVariableArguments::validateInput(input))
            return id;
        if ((buffer.usage & ResourceUsage::VertexBuffer) == ResourceUsage::None)
            return "RHI-ORIGINAL-GEOMETRY-VERTEX-USAGE";
        const auto& binding = *input.scalar.get<CommandScalarArguments::VertexBinding>();
        const VertexBufferLayoutDesc* selected = nullptr;
        for (const auto& stream : streams)
            if (stream.slot == binding.slot) {
                if (selected)
                    return "RHI-VERTEX-INPUT-SLOT-DUPLICATE";
                selected = &stream;
            }
        if (!selected)
            return "RHI-VERTEX-INPUT-SLOT-UNDECLARED";
        VertexInputConsumptionContract::Stream shape{};
        if (const auto* id = VertexInputConsumptionContract::inspectStream(*selected, shape))
            return id;
        return core::containsUnsignedRange(
                   std::uint64_t{0u}, buffer.sizeBytes, binding.offset, shape.fetchBytes)
            ? nullptr
            : "RHI-VERTEX-INPUT-BUFFER-RANGE";
    }

    enum class Closure {
        PipelineDefinition,
        DescriptorElements,
        DescriptorTableElements,
        GraphicsConsumption,
        ComputeConsumption,
        QueryCommandPoint,
        ResourceBinding,
        PushBlock,
        Unsupported
    };

    static constexpr Closure requiredClosure(CommandUsageKind kind) noexcept
    {
        using K = CommandUsageKind;
        switch (kind) {
        case K::BindGraphicsPipeline:
        case K::BindComputePipeline:
            return Closure::PipelineDefinition;
        case K::BindRenderDescriptor:
        case K::BindComputeDescriptor:
            return Closure::DescriptorElements;
        case K::BindRenderDescriptorTable:
        case K::BindComputeDescriptorTable:
            return Closure::DescriptorTableElements;
        case K::Draw:
        case K::DrawIndexed:
        case K::DrawIndexedIndirect:
        case K::DrawIndexedIndirectCount:
        case K::SetViewport:
        case K::SetScissor:
            return Closure::GraphicsConsumption;
        case K::Dispatch:
        case K::DispatchIndirect:
            return Closure::ComputeConsumption;
        case K::Timestamp:
        case K::PipelineStatistics:
            return Closure::QueryCommandPoint;
        case K::BindVertexBuffer:
        case K::BindIndexBuffer:
        case K::BindTessellationFactorBuffer:
            return Closure::ResourceBinding;
        case K::PushConstants:
            return Closure::PushBlock;
        default:
            return Closure::Unsupported;
        }
    }

    static const char* validateDeclaredFields(const CommandVariableArguments::Input& input,
        const CommandReferenceDomain* references,
        std::size_t count) noexcept
    {
        if (requiredClosure(input.scalar.kind()) == Closure::Unsupported)
            return "RHI-ORIGINAL-TYPED-INPUT-KIND";
        if (const auto* id = CommandVariableArguments::validateInput(input))
            return id;
        if ((count && !references)
            || !CommandUsageContract::validReferences(
                input.scalar.kind(), count, [&](std::size_t i) noexcept {
                    return references[i];
                }))
            return "RHI-ORIGINAL-TYPED-INPUT-REFERENCES";
        return nullptr;
    }

    static const char* validatePipelineKind(
        CommandUsageKind kind, OriginalCommandTypedSourceContract::Kind source) noexcept
    {
        using K = CommandUsageKind;
        using S = OriginalCommandTypedSourceContract::Kind;
        return (kind == K::BindGraphicsPipeline && source == S::GraphicsPipeline)
                || (kind == K::BindComputePipeline && source == S::ComputePipeline)
            ? nullptr
            : "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-KIND";
    }

    static const char* validateSetBinding(const CommandVariableArguments::Input& input,
        const PipelineLayoutDesc& pipeline,
        const DescriptorSet& descriptor)
    {
        using K = CommandUsageKind;
        if (input.scalar.kind() != K::BindRenderDescriptor
            && input.scalar.kind() != K::BindComputeDescriptor)
            return "RHI-ORIGINAL-TYPED-INPUT-KIND";
        if (const auto* id = CommandVariableArguments::validateInput(input))
            return id;
        const auto set = input.scalar.get<CommandScalarArguments::DescriptorBinding>()->set;
        const auto* slot = findDescriptorSetSlot(pipeline, set);
        const auto* expected = slot ? findDescriptorSetLayout(pipeline, slot->layoutName) : nullptr;
        if (!expected || !validateExactDescriptorSetLayout(*expected, descriptor).success)
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-LAYOUT";
        if (validateDynamicOffsetArgumentsDiagnostic(
                descriptor.layout(), input.offsets, input.offsetCount))
            return "RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-OFFSETS";
        return nullptr;
    }

    // Frozen definition helpers deliberately omit object/content checks. /3
    // must first validate OriginalDescriptorSource/2 and exact canonical joins.
    static const char* validateSetShape(const CommandVariableArguments::Input& input,
        const PipelineLayoutDesc& pipeline,
        const DescriptorLayoutDesc& descriptor) noexcept
    {
        using K = CommandUsageKind;
        if (input.scalar.kind() != K::BindRenderDescriptor
            && input.scalar.kind() != K::BindComputeDescriptor)
            return "RHI-ORIGINAL-TYPED-INPUT-KIND";
        if (const auto* id = CommandVariableArguments::validateInput(input))
            return id;
        const auto set = input.scalar.get<CommandScalarArguments::DescriptorBinding>()->set;
        if (DescriptorSlotShapeContract::validateSet(pipeline, set, descriptor))
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-LAYOUT";
        return validateDynamicOffsetArgumentsDiagnostic(
                   descriptor, input.offsets, input.offsetCount)
            ? "RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-OFFSETS"
            : nullptr;
    }

    static const char* validateTableShape(const CommandVariableArguments::Input& input,
        const PipelineLayoutDesc& pipeline,
        const DescriptorTableDesc& descriptor,
        DescriptorSlotShapeContract::TableMatch match) noexcept
    {
        using K = CommandUsageKind;
        if (input.scalar.kind() != K::BindRenderDescriptorTable
            && input.scalar.kind() != K::BindComputeDescriptorTable)
            return "RHI-ORIGINAL-TYPED-INPUT-KIND";
        if (const auto* id = CommandVariableArguments::validateInput(input))
            return id;
        const auto set = input.scalar.get<CommandScalarArguments::DescriptorBinding>()->set;
        return DescriptorSlotShapeContract::validateTable(pipeline, set, descriptor, match)
            ? "RHI-ORIGINAL-TYPED-INPUT-TABLE-LAYOUT"
            : nullptr;
    }

    static const char* validateTableBinding(const CommandVariableArguments::Input& input,
        const PipelineLayoutDesc& pipeline,
        const DescriptorTable& descriptor)
    {
        using K = CommandUsageKind;
        if (input.scalar.kind() != K::BindRenderDescriptorTable
            && input.scalar.kind() != K::BindComputeDescriptorTable)
            return "RHI-ORIGINAL-TYPED-INPUT-KIND";
        if (const auto* id = CommandVariableArguments::validateInput(input))
            return id;
        const auto set = input.scalar.get<CommandScalarArguments::DescriptorBinding>()->set;
        return validateDescriptorTableSlot(pipeline, set, descriptor.desc()).success
                && validateDescriptorResources(
                    descriptor, DescriptorContentRequirement::WrittenElements)
                       .success
            ? nullptr
            : "RHI-ORIGINAL-TYPED-INPUT-TABLE-LAYOUT";
    }

    static const char* validateQueryBinding(const CommandVariableArguments::Input& input,
        const QueryPoolOriginalInputArguments& declaration) noexcept
    {
        if (const auto* id = CommandVariableArguments::validateInput(input))
            return id;
        if (!input.scalar.matches(declaration.scalar()))
            return "RHI-ORIGINAL-TYPED-INPUT-QUERY-FIELDS";
        return OriginalCommandTypedSourceContract::validateQueryDeclaration(declaration);
    }
};

// endregion original_command_typed_input

// region original_command_pipeline_binding

struct OriginalCommandPipelineBinding {
    CommandUsageKind commandKind = CommandUsageKind::Count;
    std::size_t sourceIndex = SIZE_MAX, scopeBeginCommandIndex = SIZE_MAX;
    std::uint64_t registrationGeneration = 0u;

    CommandUsageKind kind() const noexcept { return commandKind; }
};

struct OriginalCommandInputsBudget {
    std::uint32_t contractVersion = 1u;
    bool prototypeOptIn = false;
    std::uint32_t maxPipelineBindings = 0u;
    std::uint32_t maxDescriptorBindings = 0u, maxTableBindings = 0u, maxFrozenDynamicOffsets = 0u;
    OriginalRetainedReadbackCorrespondence retainedReadback{};
    std::uint32_t maxVertexBindings = 0u, maxIndexBindings = 0u;
    std::uint32_t maxQueryBindings = 0u;
    std::shared_ptr<const QueryCommandPoints> queryPoints;
    // Inputs11 explicitly joins Inputs10 ordinary shader resources with the
    // same canonical query points. Older profiles cannot borrow this opt-in.
    std::optional<OriginalPassTimingBudget> passTiming;
};

// Selected canonical positional /2. No new ownership inventory or state/native
// proof; exact original Sources remain the sole strong typed resource closure.
class OriginalCommandPipelineBindingContract final {
public:
    static constexpr const char* validateBudget(const OriginalCommandInputsBudget& b) noexcept
    {
        if (const auto* id = b.retainedReadback.validate())
            return id;
        if (b.contractVersion != 1u && b.contractVersion != 2u && b.contractVersion != 3u
            && b.contractVersion != 4u && b.contractVersion != 5u && b.contractVersion != 6u
            && b.contractVersion != 7u && b.contractVersion != 8u && b.contractVersion != 9u
            && b.contractVersion != 10u && b.contractVersion != 11u)
            return "RHI-ORIGINAL-INPUTS-VERSION";
        if ((b.contractVersion == 11u) != bool(b.passTiming))
            return "RHI-ORIGINAL-PASS-TIMING-PROFILE";
        if (b.passTiming) {
            if (const auto* id = OriginalPassTimingContract::validateIntent(*b.passTiming))
                return id;
            if (b.maxQueryBindings != b.passTiming->maxTimestampWrites)
                return "RHI-ORIGINAL-PASS-TIMING-WRITE-BUDGET";
        }
        if (b.contractVersion != 9u && b.contractVersion != 11u
            && (b.maxQueryBindings || b.queryPoints))
            return "RHI-ORIGINAL-QUERY-POSITIONAL-PROFILE";
        if ((b.contractVersion == 9u || b.contractVersion == 11u)
            && (!b.maxQueryBindings || !b.queryPoints))
            return "RHI-ORIGINAL-QUERY-POSITIONAL-BUDGET";
        if (b.contractVersion != 5u && b.contractVersion != 7u && b.contractVersion != 8u
            && (b.maxVertexBindings || b.maxIndexBindings))
            return "RHI-ORIGINAL-INPUTS-VERSION";
        // Inputs6 independently joins Source5 mixed ordinary sets. Existing
        // table/geometry profiles cannot borrow this positional version.
        if ((b.contractVersion == 6u || b.contractVersion == 10u || b.contractVersion == 11u)
            && (b.maxTableBindings || !b.maxDescriptorBindings))
            return "RHI-ORIGINAL-INPUTS-GENERAL-FINITE-SCHEMA";
        if (b.contractVersion == 9u && b.maxTableBindings)
            return "RHI-ORIGINAL-INPUTS-GENERAL-FINITE-SCHEMA";
        if ((b.contractVersion == 5u || b.contractVersion == 7u || b.contractVersion == 8u)
            && !b.maxVertexBindings && !b.maxIndexBindings)
            return "RHI-ORIGINAL-GEOMETRY-INPUT-BUDGET";
        // Inputs7 joins geometry to the exact prepared Graphics Root. Ordinary
        // pipelines/descriptors and retained readbacks cannot borrow this ABI.
        if ((b.contractVersion == 7u || b.contractVersion == 8u)
            && (b.maxDescriptorBindings || b.maxTableBindings || b.maxFrozenDynamicOffsets
                || b.retainedReadback.enabled()))
            return "RHI-ORIGINAL-BINDLESS-GEOMETRY-INPUT-SCHEMA";
        if (b.contractVersion < 3u
            && (b.maxDescriptorBindings || b.maxTableBindings || b.maxFrozenDynamicOffsets))
            return "RHI-ORIGINAL-INPUTS-VERSION";
        if (b.contractVersion == 1u)
            return b.prototypeOptIn || b.maxPipelineBindings ? "RHI-ORIGINAL-INPUTS-VERSION"
                                                             : nullptr;
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-TYPED-INPUT-OPT-IN";
        if (b.contractVersion == 9u)
            return nullptr;
        if (b.contractVersion >= 3u && b.contractVersion != 5u && b.contractVersion != 7u
            && b.contractVersion != 8u && !b.maxDescriptorBindings && !b.maxTableBindings)
            return "RHI-ORIGINAL-TYPED-INPUT-BUDGET";
        if (b.contractVersion == 4u && !b.maxTableBindings)
            return "RHI-ORIGINAL-TYPED-INPUT-BUDGET";
        return b.maxPipelineBindings ? nullptr : "RHI-ORIGINAL-TYPED-INPUT-BUDGET";
    }

    static constexpr bool isPipelineBinding(CommandUsageKind kind) noexcept
    {
        return kind == CommandUsageKind::BindGraphicsPipeline
            || kind == CommandUsageKind::BindComputePipeline;
    }

    static const char* validate(const OriginalCommandPipelineBinding& binding,
        const OriginalCommandUsage& usage,
        std::size_t commandIndex,
        std::size_t scopeBeginCommandIndex,
        semantics::CommandRecordingOrder::Scope scope) noexcept
    {
        if (commandIndex >= usage.commands().size())
            return "RHI-ORIGINAL-TYPED-INPUT-COMMAND";
        const auto& command = usage.commands()[commandIndex];
        if (!isPipelineBinding(command.kind) || binding.kind() != command.kind)
            return "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-KIND";
        if (binding.scopeBeginCommandIndex != scopeBeginCommandIndex
            || scopeBeginCommandIndex >= commandIndex
            || (command.kind == CommandUsageKind::BindGraphicsPipeline
                && scope != semantics::CommandRecordingOrder::Scope::Render)
            || (command.kind == CommandUsageKind::BindComputePipeline
                && scope != semantics::CommandRecordingOrder::Scope::Compute))
            return "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-SCOPE";
        if (command.referenceCount != 1u
            || binding.sourceIndex >= usage.originalSources()->sources().size()
            || binding.sourceIndex != usage.references()[command.firstReference].sourceIndex)
            return "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-SOURCE";
        const auto& source = usage.originalSources()->sources()[binding.sourceIndex];
        const auto join = [&](const auto& adapter) -> const char* {
            using Adapter = typename std::decay_t<decltype(adapter)>::element_type;
            if (const auto* id = Adapter::validateOriginal(adapter))
                return id;
            constexpr auto sourceKind =
                std::is_same_v<Adapter, const OriginalCommandGraphicsPipelineSource>
                ? OriginalCommandTypedSourceContract::Kind::GraphicsPipeline
                : OriginalCommandTypedSourceContract::Kind::ComputePipeline;
            if (const auto* id = OriginalCommandTypedInputContract::validatePipelineKind(
                    command.kind, sourceKind))
                return id;
            if (binding.registrationGeneration
                != adapter->originalObject()->originalResourceRegistration().generation())
                return "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-GENERATION";
            return nullptr;
        };
        if (const auto* graphics =
                std::get_if<std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(&source))
            return join(*graphics);
        if (const auto* compute =
                std::get_if<std::shared_ptr<const OriginalCommandComputePipelineSource>>(&source))
            return join(*compute);
        return "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-SOURCE";
    }
};

// endregion original_command_pipeline_binding

// region original_command_geometry_binding
// Pointer-free positions; the original Sources/Usage retain both exact owners.
struct OriginalCommandGeometryBinding {
    CommandUsageKind commandKind = CommandUsageKind::Count;
    std::size_t sourceIndex = SIZE_MAX, pipelineSourceIndex = SIZE_MAX,
                pipelineBindCommandIndex = SIZE_MAX, scopeBeginCommandIndex = SIZE_MAX;
    std::uint64_t registrationGeneration = 0u, pipelineRegistrationGeneration = 0u;
    // Inputs7 binds the authentic prepared snapshot generation instead of
    // impersonating the unrelated OriginalGraphicsPipelineCreation issuer.
    std::uint64_t preparedGeneration = 0u;

    CommandUsageKind kind() const noexcept { return commandKind; }
};

class OriginalCommandGeometryBindingContract final {
public:
    using AccessBudget = OriginalGeometryUsageBudget;

    // Borrow the authentic pipeline already retained by Sources. This adds no
    // pipeline Source, owner, native handle or alternate pipeline epoch.
    static const RenderPipeline* graphicsPipeline(const OriginalCommandResourceSources& sources,
        std::size_t sourceIndex,
        bool preparedRoot = false) noexcept
    {
        if (sourceIndex >= sources.sources().size())
            return nullptr;
        const auto& source = sources.sources()[sourceIndex];
        if (!preparedRoot) {
            const auto* pipeline =
                std::get_if<std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(&source);
            return pipeline && *pipeline ? (*pipeline)->originalObject() : nullptr;
        }
        const auto* root =
            std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(&source);
        if (!root || !*root)
            return nullptr;
        const auto* prepared = std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            &(*root)->originalPrepared());
        return prepared && *prepared && (*prepared)->owners()
            ? (*prepared)->owners()->desc().pipeline.get()
            : nullptr;
    }

    static const core::pmr::Vector<VertexBufferLayoutDesc>* vertexBuffers(
        const RenderPipeline& pipeline, bool preparedRoot = false) noexcept
    {
        if (preparedRoot)
            return pipeline.bindlessVertexBuffers(1u);
        const auto* definition = pipeline.originalRenderPipelineDefinition();
        return definition ? &definition->vertexBuffers : nullptr;
    }

    // Binding usage covers the complete bound tail, including shadowed and
    // unused bindings. Actual vertex/index content and draw fetches are separate.
    struct Access {
        OriginalCommandGeometryBinding binding;
        std::size_t commandIndex = SIZE_MAX, scopeEndCommandIndex = SIZE_MAX,
                    bindingCommandIndex = SIZE_MAX;
        GpuResourceId resource;
        std::uint64_t bufferBytes = 0u, offsetBytes = 0u, sizeBytes = 0u;
        std::uint32_t vertexSlot = 0u;
        semantics::ResourceAccessScope scope;

        bool nativeExecutionProven() const noexcept { return false; }

        bool drawConsumption() const noexcept { return commandIndex != bindingCommandIndex; }
    };

    struct AccessResult {
        const char* diagnostic = nullptr;
        std::size_t commandIndex = 0u;
        std::uint64_t bindings = 0u, pairChecks = 0u;
        std::uint64_t draws = 0u, drawAccesses = 0u, bindingScans = 0u;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr const char* validateAccessBudget(const AccessBudget& budget) noexcept
    {
        return budget.diagnostic();
    }

    static constexpr bool isBinding(CommandUsageKind kind) noexcept
    {
        return kind == CommandUsageKind::BindVertexBuffer
            || kind == CommandUsageKind::BindIndexBuffer;
    }

    static constexpr auto lowering(Backend backend) noexcept
    {
        return semantics::OriginalCommandTypedSourceLowering::lowering(backend);
    }

    static const char* validate(const OriginalCommandGeometryBinding& binding,
        const OriginalCommandScalars& scalars,
        std::size_t commandIndex,
        std::size_t activePipeline,
        std::size_t pipelineBind,
        std::size_t scopeBegin,
        semantics::CommandRecordingOrder::Scope scope,
        bool preparedRoot = false) noexcept
    {
        const auto& usage = *scalars.originalUsage();
        const auto& sources = usage.originalSources()->sources();
        if (commandIndex >= usage.commands().size())
            return "RHI-ORIGINAL-GEOMETRY-COMMAND";
        const auto& command = usage.commands()[commandIndex];
        if (!isBinding(command.kind) || binding.kind() != command.kind)
            return "RHI-ORIGINAL-GEOMETRY-KIND";
        if (scope != semantics::CommandRecordingOrder::Scope::Render || scopeBegin >= pipelineBind
            || pipelineBind >= commandIndex || binding.scopeBeginCommandIndex != scopeBegin)
            return "RHI-ORIGINAL-GEOMETRY-SCOPE";
        if (binding.pipelineSourceIndex != activePipeline || activePipeline >= sources.size()
            || binding.pipelineBindCommandIndex != pipelineBind)
            return "RHI-ORIGINAL-GEOMETRY-PIPELINE-EPOCH";
        const auto& pipelineCommand = usage.commands()[pipelineBind];
        if (pipelineCommand.kind
                != (preparedRoot ? CommandUsageKind::BindPreparedGraphicsBinding
                                 : CommandUsageKind::BindGraphicsPipeline)
            || pipelineCommand.referenceCount != 1u
            || usage.references()[pipelineCommand.firstReference].sourceIndex != activePipeline)
            return "RHI-ORIGINAL-GEOMETRY-PIPELINE-EPOCH";
        if (command.referenceCount != 1u || binding.sourceIndex >= sources.size()
            || usage.references()[command.firstReference].sourceIndex != binding.sourceIndex)
            return "RHI-ORIGINAL-GEOMETRY-SOURCE";
        const auto* buffer = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
            &sources[binding.sourceIndex]);
        const auto* pipeline =
            graphicsPipeline(*usage.originalSources(), activePipeline, preparedRoot);
        if (!buffer || !pipeline)
            return "RHI-ORIGINAL-GEOMETRY-SOURCE";
        if (const auto* id = OriginalCommandBufferLease::validateOriginal(*buffer))
            return id;
        if (!preparedRoot) {
            if (const auto* id = OriginalCommandGraphicsPipelineSource::validateOriginal(
                    std::get<std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(
                        sources[activePipeline])))
                return id;
            const auto pipelineRegistration = pipeline->originalResourceRegistration();
            if (const auto* id = pipelineRegistration.diagnostic())
                return id;
        }
        const auto bufferRegistration = (*buffer)->originalObject()->originalResourceRegistration();
        if (const auto* id = bufferRegistration.diagnostic())
            return id;
        if (!bufferRegistration.matchesIdentity((*buffer)->originalObject(),
                (*buffer)->resource().value,
                OriginalRegisteredResourceKind::Buffer))
            return "RHI-ORIGINAL-GEOMETRY-REGISTRATION";
        if (binding.registrationGeneration != bufferRegistration.generation())
            return "RHI-ORIGINAL-GEOMETRY-GENERATION";
        if (preparedRoot) {
            const auto& root = std::get<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                sources[activePipeline]);
            if (binding.pipelineRegistrationGeneration
                || binding.preparedGeneration != root->originalSnapshot()->generation())
                return "RHI-ORIGINAL-BINDLESS-GEOMETRY-PREPARED-GENERATION";
        } else if (binding.preparedGeneration
            || binding.pipelineRegistrationGeneration
                != pipeline->originalResourceRegistration().generation())
            return "RHI-ORIGINAL-GEOMETRY-GENERATION";
        const auto& arguments = *scalars.originalArguments();
        const auto& record = arguments.records()[commandIndex];
        const CommandVariableArguments::Input input{record.scalar,
            arguments.text(commandIndex),
            arguments.offsets(commandIndex),
            static_cast<std::uint32_t>(record.offsetCount),
            arguments.bytes(commandIndex),
            static_cast<std::uint32_t>(record.byteCount)};
        if (command.kind == CommandUsageKind::BindIndexBuffer)
            return OriginalCommandTypedInputContract::validateIndexBindingShape(
                input, (*buffer)->layout());
        const auto* definition = vertexBuffers(*pipeline, preparedRoot);
        if (!definition)
            return "RHI-ORIGINAL-GEOMETRY-PIPELINE-DEFINITION";
        return OriginalCommandTypedInputContract::validateVertexBindingShape(
            input, (*buffer)->layout(), *definition);
    }
};

// endregion original_command_geometry_binding

// region original_command_descriptor_binding

// Values only; the existing Usage root owns all original objects and arguments.
struct OriginalCommandDescriptorBinding {
    CommandUsageKind commandKind = CommandUsageKind::Count;
    std::size_t sourceIndex = SIZE_MAX, pipelineSourceIndex = SIZE_MAX,
                scopeBeginCommandIndex = SIZE_MAX;
    std::uint64_t registrationGeneration = 0u, contentRevision = 0u;

    CommandUsageKind kind() const noexcept { return commandKind; }
};

class OriginalCommandDescriptorBindingContract final {
public:
    static constexpr bool isTable(CommandUsageKind kind) noexcept
    {
        return kind == CommandUsageKind::BindRenderDescriptorTable
            || kind == CommandUsageKind::BindComputeDescriptorTable;
    }

    static constexpr bool isBinding(CommandUsageKind kind) noexcept
    {
        return isTable(kind) || kind == CommandUsageKind::BindRenderDescriptor
            || kind == CommandUsageKind::BindComputeDescriptor;
    }

    static constexpr std::uint32_t tableLoweringContractVersion = 1u;

    static constexpr const char* validateSetProfile(
        std::uint32_t inputsVersion, std::uint32_t sourceVersion) noexcept
    {
        if (inputsVersion == 10u || inputsVersion == 11u)
            return sourceVersion == 5u || sourceVersion == 7u
                ? nullptr
                : "RHI-ORIGINAL-TYPED-INPUT-SYNCHRONOUS-SOURCE";
        if (inputsVersion == 6u || inputsVersion == 9u)
            return sourceVersion == 5u ? nullptr : "RHI-ORIGINAL-TYPED-INPUT-GENERAL-FINITE-SOURCE";
        if (inputsVersion != 3u && inputsVersion != 4u && inputsVersion != 5u)
            return "RHI-ORIGINAL-INPUTS-VERSION";
        return sourceVersion == 2u
                || OriginalCommandDescriptorSetSource::finiteVersion(sourceVersion)
            ? nullptr
            : "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-VERSION";
    }

    // Complete original declaration diagnosis, never a position/native proof.
    // The scoped entry below additionally joins the genuine active pipeline.
    static const char* diagnoseTableDeclaration(const OriginalCommandDescriptorBinding& binding,
        const CommandVariableArguments::Input& input,
        const PipelineLayoutDesc& layout,
        const OriginalCommandResourceSources& sources) noexcept
    {
        if (!isTable(binding.kind()) || binding.kind() != input.scalar.kind())
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-KIND";
        if (binding.sourceIndex >= sources.sources().size())
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SOURCE";
        const auto* table =
            std::get_if<std::shared_ptr<const OriginalCommandDescriptorTableSource>>(
                &sources.sources()[binding.sourceIndex]);
        if (!table)
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SOURCE";
        if (const auto* id = validateSource(binding, *table, sources))
            return id;
        if (const auto* id = OriginalCommandTypedInputContract::validateTableShape(input,
                layout,
                (*table)->frozenDefinition(),
                DescriptorSlotShapeContract::TableMatch::ExactOriginal))
            return id;
        return arp::rhi::semantics::OriginalCommandDescriptorBindingLowering::
            tableProfileDiagnostic();
    }

    static const char* validate(const OriginalCommandDescriptorBinding& binding,
        const OriginalCommandScalars& scalars,
        std::size_t commandIndex,
        std::size_t activePipeline,
        std::size_t scopeBegin,
        semantics::CommandRecordingOrder::Scope scope,
        std::uint32_t inputsVersion = 3u) noexcept
    {
        if (inputsVersion != 3u && inputsVersion != 4u && inputsVersion != 5u && inputsVersion != 6u
            && inputsVersion != 9u && inputsVersion != 10u && inputsVersion != 11u)
            return "RHI-ORIGINAL-INPUTS-VERSION";
        const auto& usage = *scalars.originalUsage();
        const auto& sources = *usage.originalSources();
        if (commandIndex >= usage.commands().size())
            return "RHI-ORIGINAL-TYPED-INPUT-COMMAND";
        const auto& command = usage.commands()[commandIndex];
        if (!isBinding(command.kind) || binding.kind() != command.kind)
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-KIND";
        const bool graphics = command.kind == CommandUsageKind::BindRenderDescriptor
            || command.kind == CommandUsageKind::BindRenderDescriptorTable;
        if (scopeBegin >= commandIndex || binding.scopeBeginCommandIndex != scopeBegin
            || scope
                != (graphics ? semantics::CommandRecordingOrder::Scope::Render
                             : semantics::CommandRecordingOrder::Scope::Compute))
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SCOPE";
        if (activePipeline >= sources.sources().size()
            || binding.pipelineSourceIndex != activePipeline)
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-PIPELINE";
        if (command.referenceCount != 1u || binding.sourceIndex >= sources.sources().size()
            || binding.sourceIndex != usage.references()[command.firstReference].sourceIndex)
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SOURCE";
        const auto& arguments = *scalars.originalArguments();
        const auto& record = arguments.records()[commandIndex];
        const CommandVariableArguments::Input input{record.scalar,
            arguments.text(commandIndex),
            arguments.offsets(commandIndex),
            static_cast<std::uint32_t>(record.offsetCount),
            arguments.bytes(commandIndex),
            static_cast<std::uint32_t>(record.byteCount)};
        const auto joinPipeline = [&](const auto& pipeline) -> const char* {
            using PipelineSource = typename std::decay_t<decltype(pipeline)>::element_type;
            if (const auto* id = PipelineSource::validateOriginal(pipeline))
                return id;
            constexpr bool render =
                std::is_same_v<PipelineSource, const OriginalCommandGraphicsPipelineSource>;
            if (render != graphics)
                return "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-KIND";
            const auto& original = sources.sources()[binding.sourceIndex];
            if (isTable(command.kind)) {
                if (inputsVersion == 6u || inputsVersion == 9u || inputsVersion == 11u)
                    return "RHI-ORIGINAL-INPUTS-GENERAL-FINITE-SCHEMA";
                if (inputsVersion == 4u || inputsVersion == 5u)
                    return validateDepthTable(binding, input, pipeline, sources);
                return diagnoseTableDeclaration(
                    binding, input, pipeline->originalLayout()->desc(), sources);
            }
            const auto* set =
                std::get_if<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(&original);
            if (!set)
                return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SOURCE";
            if (const auto* id = validateSource(binding, *set, sources, inputsVersion))
                return id;
            const auto& source = **set;
            if (const auto* id = OriginalCommandTypedInputContract::validateSetShape(
                    input, pipeline->originalLayout()->desc(), source.frozenDefinition()))
                return id;
            // Source custody freezes exactly the successful writes; positional
            // binding additionally requires every authored logical element.
            for (const auto& layout : source.frozenDefinition().bindings)
                for (std::uint32_t element = 0u; element < layout.arrayCount; ++element) {
                    bool found = false;
                    for (const auto& frozen : source.frozenElements())
                        if (frozen.declaration.write.binding == layout.binding
                            && frozen.declaration.write.arrayElement == element) {
                            found = true;
                            break;
                        }
                    if (!found)
                        return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-INCOMPLETE";
                }
            if (OriginalCommandDescriptorSetSource::pipelineBoundVersion(
                    source.sourceContractVersion())) {
                if (activePipeline != source.frozenPipelineSourceIndex())
                    return "RHI-ORIGINAL-TYPED-INPUT-FINITE-PIPELINE";
                if (input.scalar.get<CommandScalarArguments::DescriptorBinding>()->set
                    != source.frozenLogicalSet())
                    return "RHI-ORIGINAL-TYPED-INPUT-FINITE-SET";
                std::size_t consumed = 0u;
                if (inputsVersion == 6u || inputsVersion == 9u || inputsVersion == 11u) {
                    // The same mixed sequence supplies binding identity and
                    // ordered offsets. Do not fill the legacy finite-only list.
                    for (const auto& frozen : source.frozenElements()) {
                        const auto& declaration = frozen.declaration;
                        const auto* layout = findDescriptorSetBinding(
                            source.frozenDefinition(), declaration.write.binding);
                        if (!layout)
                            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-LAYOUT";
                        if (layout->dynamicOffset) {
                            if (consumed >= input.offsetCount
                                || input.offsets[consumed++] != declaration.dynamicOffset)
                                return "RHI-ORIGINAL-TYPED-INPUT-FINITE-DYNAMICS";
                        } else if (declaration.dynamicOffset)
                            return "RHI-ORIGINAL-TYPED-INPUT-FINITE-DYNAMICS";
                    }
                    return consumed == input.offsetCount
                        ? nullptr
                        : "RHI-ORIGINAL-TYPED-INPUT-FINITE-DYNAMICS";
                }
                for (const auto& declaration : source.frozenFiniteDeclarations()) {
                    const auto* layout = findDescriptorSetBinding(
                        source.frozenDefinition(), declaration.write.binding);
                    if (!layout)
                        return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-LAYOUT";
                    if (layout->dynamicOffset) {
                        if (consumed >= input.offsetCount
                            || input.offsets[consumed++] != declaration.dynamicOffset)
                            return "RHI-ORIGINAL-TYPED-INPUT-FINITE-DYNAMICS";
                    } else if (declaration.dynamicOffset)
                        return "RHI-ORIGINAL-TYPED-INPUT-FINITE-DYNAMICS";
                }
                return consumed == input.offsetCount ? nullptr
                                                     : "RHI-ORIGINAL-TYPED-INPUT-FINITE-DYNAMICS";
            }
            if (!semantics::DescriptorWholeBufferBinding::preservesRanges(
                    input.offsets, input.offsetCount))
                return "RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-RANGE-UNPROVEN";
            for (const auto& element : source.frozenElements()) {
                if (const auto* buffer =
                        std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                            &element.resourceOwner))
                    if (const auto* id =
                            pipeline->originalObject()->originalDescriptorBufferBindingDiagnostic(
                                {1u, true},
                                element.declaration.write.type,
                                (*buffer)->layout().sizeBytes,
                                0u))
                        return id;
            }
            return nullptr;
        };
        const auto& pipeline = sources.sources()[activePipeline];
        if (const auto* render =
                std::get_if<std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(
                    &pipeline))
            return joinPipeline(*render);
        if (const auto* compute =
                std::get_if<std::shared_ptr<const OriginalCommandComputePipelineSource>>(&pipeline))
            return joinPipeline(*compute);
        return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-PIPELINE";
    }

private:
    template <class PipelineSource>
    static const char* validateDepthTable(const OriginalCommandDescriptorBinding& binding,
        const CommandVariableArguments::Input& input,
        const std::shared_ptr<const PipelineSource>& pipeline,
        const OriginalCommandResourceSources& sources) noexcept
    {
        const auto* table =
            std::get_if<std::shared_ptr<const OriginalCommandDescriptorTableSource>>(
                &sources.sources()[binding.sourceIndex]);
        if (!table)
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SOURCE";
        if (const auto* id = validateSource(binding, *table, sources))
            return id;
        const auto& layout = pipeline->originalLayout()->desc();
        if (const auto* id = OriginalCommandTypedInputContract::validateTableShape(input,
                layout,
                (*table)->frozenDefinition(),
                DescriptorSlotShapeContract::TableMatch::ExactOriginal))
            return id;
        const auto& frozen = (*table)->frozenElements();
        std::array<DescriptorTableWrite, 3u> writes;
        if (frozen.size() != writes.size())
            return "RHI-ORIGINAL-TABLE-WRITE-COVERAGE";
        for (std::size_t i = 0u; i < writes.size(); ++i)
            writes[i] = frozen[i].declaration.write;
        if (const auto* id = PipelineShaderDeclarationContract::validateDepthTableExpansionWrites(
                {1u, true}, layout, (*table)->frozenDefinition(), writes.data(), writes.size()))
            return id;
        // Exact retained publication/pipeline identity is backend custody, not
        // a comparison of caller-authored package hashes or layout metadata.
        return (*table)->originalObject()->originalDepthTablePipelineDiagnostic(
            *pipeline->originalObject());
    }

    template <class Source>
    static const char* validateSource(const OriginalCommandDescriptorBinding& binding,
        const std::shared_ptr<const Source>& source,
        const OriginalCommandResourceSources& sources,
        std::uint32_t inputsVersion = 3u) noexcept
    {
        if (const auto* id = Source::validateOriginal(source))
            return id;
        if (!source->matchesCanonicalSources(sources))
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-INVENTORY";
        if (const auto* id = validateSetProfile(inputsVersion, source->sourceContractVersion()))
            return id;
        if (binding.registrationGeneration
            != source->originalObject()->originalResourceRegistration().generation())
            return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-GENERATION";
        return binding.contentRevision == source->frozenContentRevision()
            ? nullptr
            : "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-REVISION";
    }
};

// endregion original_command_descriptor_binding

// region original_command_stream

// OriginalCommandStreamCompilation/1: exact, immutable CPU input program.
// Counts are authored descriptor expansions, never native payload/staging,
// submission, callback or result-publication reservations. Zero disables an
// absent category. All limits are independent of the Source admission limits.
struct OriginalCommandStreamBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCommands = 0u, maxDescriptors = 0u, maxReferences = 0u;
    std::uint64_t maxCopyElements = 0u, maxBufferUploads = 0u;
    std::uint64_t maxTextureUploadMips = 0u, maxTextureReadbackMips = 0u;
    std::uint64_t maxRenderPasses = 0u, maxAttachmentReferences = 0u;
    std::uint64_t maxBufferUploadDeclaredBytes = 0u, maxTextureUploadPixelBytes = 0u,
                  maxReadbackDeclaredBytes = 0u;
    std::uint32_t maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    std::uint64_t maxPipelineBinds = 0u, maxDescriptorBinds = 0u, maxConsumptions = 0u,
                  maxImplicitAccesses = 0u;
    OriginalCommandDescriptorConsumptionContract::Budget descriptorConsumption;
    OriginalCommandStateSequenceBudget stateSequence;
    OriginalRenderDescriptorUsageBudget renderDescriptorUsage;
    OriginalGeometryUsageBudget geometryUsage;
    // Independent RootState /2 program join. Base Stream /1 and its legacy
    // callers do not inherit prepared binding/state authority.
    OriginalBindlessRootStateBudget bindlessRoots;
};

class OriginalCommandStreamContract final {
public:
    static constexpr const char* validateIntent(const OriginalCommandStreamBudget& b) noexcept
    {
        if (b.contractVersion != arp::rhi::semantics::OriginalCommandStreamLowering::version
            && b.contractVersion != 2u && b.contractVersion != 3u && b.contractVersion != 4u
            && b.contractVersion != 5u)
            return "RHI-ORIGINAL-STREAM-VERSION";
        if (b.contractVersion < 3u && !b.renderDescriptorUsage.empty())
            return "RHI-ORIGINAL-STREAM-RENDER-LEGACY-SCHEMA";
        const bool rootGeometry = b.contractVersion == 1u && b.bindlessRoots.geometryJoined();
        if (b.contractVersion != 4u && !rootGeometry && !b.geometryUsage.empty())
            return "RHI-ORIGINAL-STREAM-GEOMETRY-LEGACY-SCHEMA";
        if (b.contractVersion == 4u || rootGeometry)
            if (const auto* id = b.geometryUsage.diagnostic())
                return id;
        if (rootGeometry && b.geometryUsage.drawConsumptionEmpty())
            return "RHI-ORIGINAL-BINDLESS-GEOMETRY-CONSUMPTION-REQUIRED";
        const auto& s = b.stateSequence;
        const bool preparedRoots = !b.bindlessRoots.empty();
        if (preparedRoots) {
            if (b.contractVersion != 1u
                || (b.bindlessRoots.contractVersion < 2u || b.bindlessRoots.contractVersion > 6u)
                || !OriginalCommandDescriptorConsumptionContract::empty(b.descriptorConsumption)
                || b.maxDescriptorBinds || !b.renderDescriptorUsage.empty()
                || (!rootGeometry && !b.geometryUsage.empty()))
                return "RHI-ORIGINAL-BINDLESS-STREAM-PROFILE";
            if (const auto* id = b.bindlessRoots.diagnostic())
                return id;
            if (!b.maxPipelineBinds || !b.maxConsumptions || !b.maxImplicitAccesses)
                return "RHI-ORIGINAL-BINDLESS-STREAM-BUDGET";
            if (s.contractVersion != 1u
                || s.bindlessRootStateVersion != b.bindlessRoots.contractVersion)
                return "RHI-ORIGINAL-BINDLESS-STREAM-SEQUENCE-PROFILE";
            if (const auto* id = OriginalCommandStateSequenceContract::validateIntent(s))
                return id;
        }
        if (b.contractVersion == arp::rhi::semantics::OriginalCommandStreamLowering::version
            && !preparedRoots
            && (b.maxPipelineBinds || b.maxDescriptorBinds || b.maxConsumptions
                || b.maxImplicitAccesses
                || !OriginalCommandDescriptorConsumptionContract::empty(b.descriptorConsumption)
                || s.contractVersion || s.prototypeOptIn || s.maxCommands || s.maxDebugDepth
                || s.maxStateCells || s.maxSteps || s.maxImplicitAccesses
                || s.bindlessRootStateVersion || s.bindlessRootStateOptIn))
            return "RHI-ORIGINAL-STREAM-LEGACY-SCHEMA";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-STREAM-OPT-IN";
        if (!b.maxCommands || !b.maxRetainedOwners || !b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-STREAM-BUDGET";
        if (b.contractVersion >= 2u) {
            if (!b.maxPipelineBinds || !b.maxDescriptorBinds || !b.maxConsumptions
                || !b.maxImplicitAccesses)
                return "RHI-ORIGINAL-STREAM-TYPED-BUDGET";
            if (b.descriptorConsumption.contractVersion != b.contractVersion - 1u)
                return "RHI-ORIGINAL-STREAM-CONSUMPTION-PROFILE";
            if (const auto* id = OriginalCommandDescriptorConsumptionContract::validateBudget(
                    b.descriptorConsumption))
                return id;
            if (s.contractVersion != b.contractVersion)
                return "RHI-ORIGINAL-STREAM-SEQUENCE-PROFILE";
            if (const auto* id = OriginalCommandStateSequenceContract::validateIntent(s))
                return id;
            if (b.contractVersion >= 3u) {
                if (b.renderDescriptorUsage.contractVersion != b.contractVersion - 1u)
                    return "RHI-ORIGINAL-STREAM-RENDER-PROFILE";
                if (const auto* id = b.renderDescriptorUsage.diagnostic())
                    return id;
            }
        }
        return nullptr;
    }
};

// endregion original_command_stream
// endregion Command input binding validation

// region Command inputs

// region original_command_inputs

struct OriginalCommandQueryBinding {
    std::size_t sourceIndex = SIZE_MAX;
    std::uint64_t registrationGeneration = 0u;
    QueryPoolOriginalInputArguments original;

    CommandUsageKind kind() const noexcept { return original.kind(); }
};

// OriginalCommandInputs/1: complete positional CPU field ownership binding.
// Unsupported original source adapters reject before allocation. Successful
// copy/dependency bindings prove neither transfer legality nor registry/state/
// access/native recording/completion; execution stays closed on four backends.
class OriginalCommandInputs final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCommandInputs;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Family = CommandInputContract::Family;
    using Descriptor = std::variant<TransferCopyArguments,
        ResourceDependencyArguments,
        std::shared_ptr<const RenderPassArguments>,
        BufferUploadArguments,
        TextureUploadOriginalInputArguments,
        TextureReadbackOriginalInputArguments,
        std::shared_ptr<const TransferCopyInputSnapshot>,
        OriginalCommandPipelineBinding,
        OriginalCommandDescriptorBinding,
        OriginalCommandGeometryBinding,
        OriginalCommandQueryBinding>;

    struct OriginalDescriptor {
        std::size_t commandIndex;
        Descriptor arguments;

        CommandUsageKind kind() const noexcept
        {
            return std::visit(
                [](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::shared_ptr<const RenderPassArguments>>)
                        return CommandUsageKind::BeginRenderPass;
                    else if constexpr (std::is_same_v<T,
                                           std::shared_ptr<const TransferCopyInputSnapshot>>)
                        return value ? value->kind() : CommandUsageKind::Count;
                    else
                        return value.kind();
                },
                arguments);
        }
    };

    using IncomingReference = std::variant<const GpuBuffer*,
        const GpuTexture*,
        const GpuTextureView*,
        const TextureUploadInputSnapshot*,
        const OriginalCommandBufferUploadSource*,
        const TextureReadbackRequestDeclaration*,
        const DescriptorSet*,
        const DescriptorTable*,
        const RenderPipeline*,
        const ComputePipeline*,
        const QueryPool*>;
    using IncomingCopy = std::variant<const BufferCopyDesc*,
        const BufferToTextureCopyDesc*,
        const TextureToBufferCopyDesc*,
        const TextureToTextureCopyDesc*>;

    struct Incoming {
        CommandVariableArguments::Input scalar;
        std::variant<std::monostate,
            TransferCopyArguments,
            ResourceDependencyArguments,
            const RenderPassDesc*,
            BufferUploadArguments,
            TextureUploadOriginalInputArguments,
            TextureReadbackOriginalInputArguments,
            IncomingCopy,
            const UploadTextureDesc*,
            const UploadBufferDesc*,
            const ReadbackTextureDesc*,
            QueryPoolOriginalInputArguments>
            descriptor;
        const IncomingReference* references = nullptr;
        std::size_t referenceCount = 0u;
    };

    OriginalCommandInputs(ConstructionKey,
        std::shared_ptr<const OriginalCommandScalars> scalars,
        std::size_t count,
        std::uint32_t owners,
        OriginalCommandInputsBudget budget)
        : scalars_(std::move(scalars)), descriptors_(count), ownerCount_(owners), budget_(budget)
    {}

    // A complete field representation cannot supply missing staging/generation
    // or readback request/result ownership authority.
    static constexpr const char* sourceAdapterDiagnostic(Family family) noexcept
    {
        switch (family) {
        case Family::Scalar:
        case Family::Copy:
        case Family::Dependency:
        case Family::Pass:
        case Family::TextureUpload:
        case Family::BufferUpload:
        case Family::TextureReadback:
            return nullptr;
        case Family::Unsupported:
            return "RHI-ORIGINAL-INPUTS-UNSUPPORTED";
        }
        return "RHI-ORIGINAL-INPUTS-UNSUPPORTED";
    }

    static std::shared_ptr<const OriginalCommandInputs> prepareOriginal(
        std::shared_ptr<const OriginalCommandScalars> scalars,
        const OriginalDescriptor* descriptors,
        std::size_t count,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        return prepareOriginal(std::move(scalars),
            descriptors,
            count,
            OriginalCommandInputsBudget{},
            std::move(quota),
            diagnostic);
    }

    static std::shared_ptr<const OriginalCommandInputs> prepareOriginal(
        std::shared_ptr<const OriginalCommandScalars> scalars,
        const OriginalDescriptor* descriptors,
        std::size_t count,
        OriginalCommandInputsBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        std::uint32_t owners = 0u;
        diagnostic = validateSources(scalars, descriptors, count, quota, budget, &owners);
        if (diagnostic)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<OriginalCommandInputs>(
                ConstructionKey{}, std::move(scalars), count, owners, budget);
            for (std::size_t i = 0u; i < count; ++i)
                result->descriptors_.append(descriptors[i]);
            auto shared = shareOriginal(std::move(result));
            if ((diagnostic = shared->validate()))
                return {};
            return shared;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-INPUTS-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-INPUTS-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandInputs>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-INPUTS-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        return validateSources(scalars_,
            descriptors_.values().data(),
            descriptors_.values().size(),
            originalMetadataQuota(),
            budget_);
    }

    const auto& originalScalars() const noexcept { return scalars_; }

    const auto& descriptors() const noexcept { return descriptors_.values(); }

    // Sparse positional records include typed Scalar-family commands. Their
    // position is the original command index, never a non-Scalar ordinal.
    const Descriptor* descriptorAt(std::size_t command) const noexcept
    {
        const auto* record = descriptors_.findIf([&](const auto& value) noexcept {
            return value.commandIndex == command;
        });
        return record ? &record->arguments : nullptr;
    }

    std::uint32_t retainedOwnerCount() const noexcept { return ownerCount_; }

    // Latest binding in the current pipeline epoch; rebind invalidates every
    // slot, even when it binds the identical pipeline Source again.
    const OriginalDescriptor* latestDescriptorSetBinding(std::uint32_t set,
        std::size_t afterPipelineBind,
        std::size_t beforeConsumption) const noexcept
    {
        return latestDescriptorBinding(set, false, afterPipelineBind, beforeConsumption);
    }

    const OriginalDescriptor* latestDescriptorTableBinding(std::uint32_t set,
        std::size_t afterPipelineBind,
        std::size_t beforeConsumption) const noexcept
    {
        return latestDescriptorBinding(set, true, afterPipelineBind, beforeConsumption);
    }

    struct GeometryBindingLookup {
        const OriginalDescriptor* record = nullptr;
        bool scanCapacityExceeded = false;
    };

    // Borrowed lookup only. The caller validates Inputs5 and supplies the
    // actual active pipeline epoch; this does not prove state, fetch or native
    // execution. All calls share the caller's independent total scan ceiling.
    GeometryBindingLookup latestGeometryBinding(CommandUsageKind selected,
        std::uint32_t slot,
        std::size_t afterPipelineBind,
        std::size_t beforeConsumption,
        std::uint64_t maxScans,
        std::uint64_t& inspected) const noexcept
    {
        if ((budget_.contractVersion != 5u && budget_.contractVersion != 7u
                && budget_.contractVersion != 8u)
            || !OriginalCommandGeometryBindingContract::isBinding(selected)
            || afterPipelineBind >= beforeConsumption
            || beforeConsumption >= scalars_->originalUsage()->commands().size())
            return {};
        bool exhausted = false;
        const auto* found = descriptors_.findLastIf([&](const auto& record) noexcept {
            // Stop the existing bounded sequence search before inspecting a
            // record beyond the caller's ceiling; discard this stop sentinel.
            if (inspected >= maxScans) {
                exhausted = true;
                return true;
            }
            ++inspected;
            if (record.commandIndex <= afterPipelineBind
                || record.commandIndex >= beforeConsumption)
                return false;
            const auto* binding = std::get_if<OriginalCommandGeometryBinding>(&record.arguments);
            if (!binding || binding->kind() != selected)
                return false;
            if (selected == CommandUsageKind::BindVertexBuffer) {
                const auto* fields =
                    scalars_->originalArguments()
                        ->records()[record.commandIndex]
                        .scalar.template get<CommandScalarArguments::VertexBinding>();
                if (!fields || fields->slot != slot)
                    return false;
            }
            return true;
        });
        return {exhausted ? nullptr : found, exhausted};
    }

private:
    const OriginalDescriptor* latestDescriptorBinding(std::uint32_t set,
        bool table,
        std::size_t afterPipelineBind,
        std::size_t beforeConsumption) const noexcept
    {
        return descriptors_.findLastIf([&](const auto& value) noexcept {
            if (value.commandIndex <= afterPipelineBind || value.commandIndex >= beforeConsumption)
                return false;
            const auto* binding = std::get_if<OriginalCommandDescriptorBinding>(&value.arguments);
            if (!binding
                || OriginalCommandDescriptorBindingContract::isTable(binding->kind()) != table)
                return false;
            const auto* scalar =
                scalars_->originalArguments()
                    ->records()[value.commandIndex]
                    .scalar.template get<CommandScalarArguments::DescriptorBinding>();
            return scalar && scalar->set == set;
        });
    }

public:
    const auto& originalBudget() const noexcept { return budget_; }

    // Complete dry traversal precedes caller output. Reuses the existing Inputs
    // root and sparse positional records; no new retained access graph is built.
    template <class Visitor>
    OriginalCommandGeometryBindingContract::AccessResult visitGeometryAccesses(
        const OriginalCommandGeometryBindingContract::AccessBudget& budget,
        Visitor&& visitor) const noexcept
    {
        auto result = walkGeometryAccesses(budget, [](const auto&) noexcept -> const char* {
            return nullptr;
        });
        if (result.diagnostic)
            return result;
        return walkGeometryAccesses(budget, std::forward<Visitor>(visitor));
    }

    const char* compareOriginalInput(std::size_t index, const Incoming& incoming) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (const auto* id = scalars_->compareOriginalArguments(index, incoming.scalar))
            return id;
        const auto& usage = *scalars_->originalUsage();
        const auto& command = usage.commands()[index];
        if (incoming.referenceCount != command.referenceCount
            || (incoming.referenceCount && !incoming.references))
            return "RHI-ORIGINAL-INPUTS-REFERENCES";
        for (std::size_t operand = 0u; operand < incoming.referenceCount; ++operand) {
            const auto* id = std::visit(
                [&](const auto* object) -> const char* {
                    return object
                        ? usage.compareOriginalReference(command.firstReference + operand, *object)
                        : "RHI-ORIGINAL-INPUTS-REFERENCES";
                },
                incoming.references[operand]);
            if (id)
                return id;
        }
        if (CommandInputContract::family(incoming.scalar.scalar.kind()) == Family::Scalar
            && !isQuery(incoming.scalar.scalar.kind()))
            return std::holds_alternative<std::monostate>(incoming.descriptor)
                ? nullptr
                : "RHI-ORIGINAL-INPUTS-PAYLOAD";
        for (const auto& original : descriptors())
            if (original.commandIndex == index)
                return std::visit(
                    [&](const auto& value) -> const char* {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, TransferCopyArguments>
                            || std::is_same_v<T, ResourceDependencyArguments>) {
                            const auto* input = std::get_if<T>(&incoming.descriptor);
                            return input && value.matches(*input) ? nullptr
                                                                  : "RHI-ORIGINAL-INPUTS-PAYLOAD";
                        } else if constexpr (std::is_same_v<T,
                                                 std::shared_ptr<
                                                     const TransferCopyInputSnapshot>>) {
                            const auto* input = std::get_if<IncomingCopy>(&incoming.descriptor);
                            return input
                                    && std::visit(
                                        [&](const auto* desc) {
                                            return desc && value->matches(*desc);
                                        },
                                        *input)
                                ? nullptr
                                : "RHI-ORIGINAL-INPUTS-PAYLOAD";
                        } else if constexpr (std::is_same_v<T,
                                                 std::shared_ptr<const RenderPassArguments>>) {
                            const auto* input =
                                std::get_if<const RenderPassDesc*>(&incoming.descriptor);
                            return input && *input && value->matches(**input)
                                ? nullptr
                                : "RHI-ORIGINAL-INPUTS-PAYLOAD";
                        } else if constexpr (std::is_same_v<T, BufferUploadArguments>) {
                            if (const auto* input = std::get_if<T>(&incoming.descriptor))
                                return value.matches(*input) ? nullptr
                                                             : "RHI-ORIGINAL-INPUTS-PAYLOAD";
                            const auto* input =
                                std::get_if<const UploadBufferDesc*>(&incoming.descriptor);
                            return input && *input && value.matchesScalarFields(**input)
                                    && (*input)->label == incoming.scalar.text
                                ? nullptr
                                : "RHI-ORIGINAL-INPUTS-PAYLOAD";
                        } else if constexpr (std::is_same_v<T, TextureUploadOriginalInputArguments>
                            || std::is_same_v<T, TextureReadbackOriginalInputArguments>) {
                            if (const auto* input = std::get_if<T>(&incoming.descriptor))
                                return value.matches(*input) ? nullptr
                                                             : "RHI-ORIGINAL-INPUTS-PAYLOAD";
                            using Raw = std::conditional_t<
                                std::is_same_v<T, TextureUploadOriginalInputArguments>,
                                const UploadTextureDesc*,
                                const ReadbackTextureDesc*>;
                            const auto* input = std::get_if<Raw>(&incoming.descriptor);
                            return input && *input && value.originalInput()->matches(**input)
                                ? nullptr
                                : "RHI-ORIGINAL-INPUTS-PAYLOAD";
                        } else if constexpr (std::is_same_v<T, OriginalCommandQueryBinding>) {
                            const auto* input =
                                std::get_if<QueryPoolOriginalInputArguments>(&incoming.descriptor);
                            return input && value.original.matches(*input)
                                ? nullptr
                                : "RHI-ORIGINAL-INPUTS-PAYLOAD";
                        } else
                            return sourceAdapterDiagnostic(
                                CommandInputContract::family(original.kind()));
                    },
                    original.arguments);
        return "RHI-ORIGINAL-INPUTS-ORDER";
    }

    const char* executionDiagnostic(Backend backend) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(backend).diagnostic;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static constexpr bool isQuery(CommandUsageKind kind) noexcept
    {
        return kind == CommandUsageKind::Timestamp || kind == CommandUsageKind::PipelineStatistics;
    }

    template <class Visitor>
    OriginalCommandGeometryBindingContract::AccessResult walkGeometryAccesses(
        const OriginalCommandGeometryBindingContract::AccessBudget& budget,
        Visitor&& visitor) const noexcept
    {
        using C = OriginalCommandGeometryBindingContract;
        C::AccessResult result;
        if ((result.diagnostic = C::validateAccessBudget(budget)))
            return result;
        if ((result.diagnostic = validate()))
            return result;
        if (budget_.contractVersion != 5u && budget_.contractVersion != 7u
            && budget_.contractVersion != 8u) {
            result.diagnostic = "RHI-ORIGINAL-GEOMETRY-ACCESS-INPUTS-PROFILE";
            return result;
        }
        const auto& usage = *scalars_->originalUsage();
        const auto& sources = *usage.originalSources();
        if (usage.commands().size() > budget.maxCommands
            || budget.maxCommands > sources.originalBudget().maxCommands) {
            result.diagnostic = "RHI-ORIGINAL-GEOMETRY-ACCESS-COMMAND-CAPACITY";
            return result;
        }
        const std::uint64_t ceiling =
            static_cast<std::uint64_t>(budget_.maxVertexBindings) + budget_.maxIndexBindings;
        if (budget.maxBindings > ceiling) {
            result.diagnostic = "RHI-ORIGINAL-GEOMETRY-ACCESS-INPUT-CAPACITY";
            return result;
        }
        for (const auto& record : descriptors())
            if (std::holds_alternative<OriginalCommandGeometryBinding>(record.arguments)) {
                if (result.bindings == budget.maxBindings) {
                    result.diagnostic = "RHI-ORIGINAL-GEOMETRY-ACCESS-BINDING-CAPACITY";
                    return result;
                }
                ++result.bindings;
            }
        if (!result.bindings) {
            result.diagnostic = "RHI-ORIGINAL-GEOMETRY-ACCESS-EMPTY";
            return result;
        }
        std::uint64_t pairs = 0u;
        if (!core::countUnorderedPairsUnsigned(result.bindings, pairs)
            || pairs > budget.maxPairChecks) {
            result.diagnostic = "RHI-ORIGINAL-GEOMETRY-ACCESS-PAIR-CAPACITY";
            return result;
        }
        result.pairChecks = pairs;
        const auto project = [&](const OriginalDescriptor& record,
                                 C::Access& out) noexcept -> const char* {
            const auto& binding = std::get<OriginalCommandGeometryBinding>(record.arguments);
            const auto& lease = *std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                sources.sources()[binding.sourceIndex]);
            out.binding = binding;
            out.commandIndex = record.commandIndex;
            out.bindingCommandIndex = record.commandIndex;
            out.resource = lease.resource();
            out.bufferBytes = lease.layout().sizeBytes;
            const auto& scalar =
                scalars_->originalArguments()->records()[record.commandIndex].scalar;
            if (binding.kind() == CommandUsageKind::BindVertexBuffer) {
                const auto* fields = scalar.get<CommandScalarArguments::VertexBinding>();
                if (!fields)
                    return "RHI-ORIGINAL-GEOMETRY-ACCESS-SCALAR";
                out.offsetBytes = fields->offset;
                out.vertexSlot = fields->slot;
                out.scope.access = ResourceAccess::VertexAttributeRead;
            } else {
                const auto* fields = scalar.get<CommandScalarArguments::IndexBinding>();
                if (!fields)
                    return "RHI-ORIGINAL-GEOMETRY-ACCESS-SCALAR";
                out.offsetBytes = fields->offset;
                out.scope.access = ResourceAccess::IndexRead;
            }
            if (out.offsetBytes >= out.bufferBytes)
                return "RHI-ORIGINAL-GEOMETRY-ACCESS-RANGE";
            out.sizeBytes = out.bufferBytes - out.offsetBytes;
            out.scope.state = ResourceState::ShaderRead;
            out.scope.stages = PipelineStage::VertexInput;
            for (std::size_t end = record.commandIndex + 1u; end < usage.commands().size(); ++end)
                if (usage.commands()[end].kind == CommandUsageKind::EndRenderPass) {
                    out.scopeEndCommandIndex = end;
                    break;
                }
            if (out.scopeEndCommandIndex == SIZE_MAX)
                return "RHI-ORIGINAL-GEOMETRY-ACCESS-SCOPE";
            if (const auto* id = semantics::validateResourceAccessScope(out.scope))
                return id;
            return semantics::ResourceAccessUsageValidation::validate(
                lease.originalObject()->desc(), out.scope.access);
        };
        for (std::size_t i = 0u; i < descriptors().size(); ++i) {
            const auto& record = descriptors()[i];
            if (!std::holds_alternative<OriginalCommandGeometryBinding>(record.arguments))
                continue;
            result.commandIndex = record.commandIndex;
            C::Access current;
            if ((result.diagnostic = project(record, current)))
                return result;
            for (std::size_t j = 0u; j < i; ++j) {
                const auto& previous = descriptors()[j];
                if (!std::holds_alternative<OriginalCommandGeometryBinding>(previous.arguments))
                    continue;
                C::Access prior;
                if ((result.diagnostic = project(previous, prior)))
                    return result;
                if (current.resource == prior.resource
                    && (current.bufferBytes != prior.bufferBytes
                        || current.binding.registrationGeneration
                            != prior.binding.registrationGeneration)) {
                    result.diagnostic = "RHI-ORIGINAL-GEOMETRY-ACCESS-IDENTITY";
                    return result;
                }
            }
            if ((result.diagnostic = visitor(current)))
                return result;
        }
        if (!budget.drawConsumptionEmpty()) {
            std::size_t pipelineBind = SIZE_MAX, pipelineSource = SIZE_MAX, scopeBegin = SIZE_MAX;
            for (std::size_t command = 0u; command < usage.commands().size(); ++command) {
                result.commandIndex = command;
                const auto kind = usage.commands()[command].kind;
                if (kind == CommandUsageKind::BeginRenderPass
                    || kind == CommandUsageKind::EndRenderPass) {
                    pipelineBind = pipelineSource = SIZE_MAX;
                    scopeBegin = kind == CommandUsageKind::BeginRenderPass ? command : SIZE_MAX;
                }
                if (kind == CommandUsageKind::BindGraphicsPipeline
                    || ((budget_.contractVersion == 7u || budget_.contractVersion == 8u)
                        && kind == CommandUsageKind::BindPreparedGraphicsBinding)) {
                    if (kind == CommandUsageKind::BindPreparedGraphicsBinding) {
                        const auto& commandRecord = usage.commands()[command];
                        if (commandRecord.referenceCount != 1u)
                            return {"RHI-ORIGINAL-GEOMETRY-CONSUMPTION-PIPELINE"};
                        pipelineBind = command;
                        pipelineSource =
                            usage.references()[commandRecord.firstReference].sourceIndex;
                    } else {
                        const auto* binding =
                            std::get_if<OriginalCommandPipelineBinding>(descriptorAt(command));
                        if (!binding) {
                            result.diagnostic = "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-PIPELINE";
                            return result;
                        }
                        pipelineBind = command;
                        pipelineSource = binding->sourceIndex;
                    }
                }
                const bool indirect =
                    budget_.contractVersion == 8u && kind == CommandUsageKind::DrawIndexedIndirect;
                if (kind != CommandUsageKind::Draw && kind != CommandUsageKind::DrawIndexed
                    && !indirect)
                    continue;
                if (result.draws == budget.maxDraws) {
                    result.diagnostic = "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-DRAW-CAPACITY";
                    return result;
                }
                if (scopeBegin == SIZE_MAX || pipelineSource >= sources.sources().size()) {
                    result.diagnostic = "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-PIPELINE";
                    return result;
                }
                const auto* pipeline = C::graphicsPipeline(sources,
                    pipelineSource,
                    (budget_.contractVersion == 7u || budget_.contractVersion == 8u));
                const auto* definition = pipeline
                    ? C::vertexBuffers(*pipeline,
                          (budget_.contractVersion == 7u || budget_.contractVersion == 8u))
                    : nullptr;
                if (!definition
                    || pipeline->primitiveTopology() != PrimitiveTopology::TriangleList) {
                    result.diagnostic = "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-PIPELINE-PROFILE";
                    return result;
                }
                const auto& scalar = scalars_->originalArguments()->records()[command].scalar;
                const auto* direct = scalar.template get<CommandScalarArguments::Draw>();
                const auto* indexed = scalar.template get<CommandScalarArguments::DrawIndexed>();
                if ((kind == CommandUsageKind::Draw
                        && (!direct || !direct->vertices || !direct->instances))
                    || (kind == CommandUsageKind::DrawIndexed
                        && (!indexed || !indexed->indices || !indexed->instances))) {
                    result.diagnostic = "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-DRAW-EMPTY";
                    return result;
                }
                ++result.draws;
                const auto consume = [&](CommandUsageKind selected,
                                         std::uint32_t slot) noexcept -> const char* {
                    if (result.drawAccesses == budget.maxDrawAccesses)
                        return "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-ACCESS-CAPACITY";
                    const auto found = latestGeometryBinding(selected,
                        slot,
                        pipelineBind,
                        command,
                        budget.maxBindingScans,
                        result.bindingScans);
                    if (found.scanCapacityExceeded)
                        return "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-SCAN-CAPACITY";
                    if (!found.record)
                        return "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-UNBOUND";
                    C::Access access;
                    if (const auto* id = project(*found.record, access))
                        return id;
                    if (access.binding.pipelineSourceIndex != pipelineSource
                        || access.binding.pipelineBindCommandIndex != pipelineBind
                        || access.binding.scopeBeginCommandIndex != scopeBegin)
                        return "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-EPOCH";
                    access.commandIndex = command;
                    ++result.drawAccesses;
                    return visitor(access);
                };
                if ((kind == CommandUsageKind::DrawIndexed || indirect)
                    && (result.diagnostic = consume(CommandUsageKind::BindIndexBuffer, 0u)))
                    return result;
                for (const auto& layout : *definition) {
                    VertexInputConsumptionContract::Stream stream{};
                    if ((result.diagnostic =
                                VertexInputConsumptionContract::inspectStream(layout, stream)))
                        return result;
                    if ((result.diagnostic =
                                consume(CommandUsageKind::BindVertexBuffer, stream.slot)))
                        return result;
                }
            }
            // All draw points borrow already validated original bindings. Their
            // identities introduce no new sources; reserve the complete pair
            // domain for downstream usage/hazard inspection, including draws.
            if (result.drawAccesses > UINT64_MAX - result.bindings
                || !core::countUnorderedPairsUnsigned(result.bindings + result.drawAccesses, pairs)
                || pairs > budget.maxPairChecks) {
                result.diagnostic = "RHI-ORIGINAL-GEOMETRY-ACCESS-PAIR-CAPACITY";
                return result;
            }
            result.pairChecks = pairs;
        }
        result.commandIndex = usage.commands().size();
        return result;
    }

    static bool resourceMatches(const OriginalCommandUsage& usage,
        std::size_t command,
        std::size_t operand,
        GpuResourceId id) noexcept
    {
        const auto& entry = usage.commands()[command];
        if (operand >= entry.referenceCount || !id.isValid())
            return false;
        const auto sourceIndex = usage.references()[entry.firstReference + operand].sourceIndex;
        return OriginalCommandResourceSources::resource(
                   usage.originalSources()->sources()[sourceIndex])
            == id;
    }

    static const char* validatePass(const OriginalCommandUsage& usage,
        std::size_t command,
        const RenderPassArguments& pass) noexcept
    {
        const auto& entry = usage.commands()[command];
        return RenderPassReferenceSequenceContract::validate(
            pass.desc(),
            entry.referenceCount,
            [&](std::size_t operand, GpuResourceId target, TextureFormat format, bool depth)
                -> const char* {
                const auto index = usage.references()[entry.firstReference + operand].sourceIndex;
                const auto* original =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &usage.originalSources()->sources()[index]);
                if (!original || (*original)->resource() != target)
                    return "RHI-ORIGINAL-INPUTS-PASS-REFERENCE";
                const auto& view = (*original)->originalViewLease()->layout();
                const auto& parent = (*original)->originalParentLease()->layout();
                if ((view.usage & TextureUsage::RenderAttachment) == TextureUsage::None
                    || (parent.usage & TextureUsage::RenderAttachment) == TextureUsage::None
                    || view.format == TextureFormat::Unknown
                    || view.aspect != (depth ? TextureAspect::Depth : TextureAspect::Color)
                    || isDepthTextureFormat(view.format) != depth
                    || (format != TextureFormat::Unknown && format != view.format))
                    return "RHI-ORIGINAL-INPUTS-PASS-VIEW";
                return nullptr;
            },
            "RHI-ORIGINAL-INPUTS-PASS-REFERENCE");
    }

    static const char* validateSources(const std::shared_ptr<const OriginalCommandScalars>& scalars,
        const OriginalDescriptor* descriptors,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const OriginalCommandInputsBudget& inputBudget,
        std::uint32_t* retainedOwners = nullptr) noexcept
    {
        if (const auto* id = OriginalCommandPipelineBindingContract::validateBudget(inputBudget))
            return id;
        if (const auto* id = OriginalCommandScalars::validateOriginal(scalars))
            return id;
        if (!quota
            || !core::SharedOwnerProjection::sameOwner(quota, scalars->originalMetadataQuota()))
            return "RHI-ORIGINAL-INPUTS-GRAPH-QUOTA";
        const auto& usage = *scalars->originalUsage();
        const auto& budget = usage.originalSources()->originalBudget();
        if (!inputBudget.retainedReadback.matches(
                usage.originalSources()->retainedReadbackCorrespondence()))
            return "RHI-ORIGINAL-INPUTS-RETAINED-READBACK-PROFILE";
        if (inputBudget.contractVersion >= 3u
            && (inputBudget.maxDescriptorBindings > budget.maxInputDescriptors
                || inputBudget.maxTableBindings > budget.maxInputDescriptors
                || inputBudget.maxFrozenDynamicOffsets > budget.maxDynamicOffsets))
            return "RHI-ORIGINAL-TYPED-INPUT-GRAPH-CAPACITY";
        if (inputBudget.maxVertexBindings > budget.maxInputDescriptors
            || inputBudget.maxIndexBindings > budget.maxInputDescriptors)
            return "RHI-ORIGINAL-GEOMETRY-GRAPH-CAPACITY";
        if (inputBudget.maxQueryBindings > budget.maxInputDescriptors)
            return "RHI-ORIGINAL-QUERY-GRAPH-CAPACITY";
        if (inputBudget.contractVersion == 9u || inputBudget.contractVersion == 11u) {
            if (const auto* id = QueryCommandPoints::validateOriginalStream(
                    inputBudget.queryPoints, scalars->originalArguments()))
                return id;
            if (!core::SharedOwnerProjection::sameOwner(
                    inputBudget.queryPoints->originalMetadataQuota(), quota))
                return "RHI-ORIGINAL-QUERY-POINTS-GRAPH-QUOTA";
            if (inputBudget.queryPoints->points().size() > inputBudget.maxQueryBindings)
                return "RHI-ORIGINAL-QUERY-INPUT-CAPACITY";
            if (inputBudget.passTiming) {
                const auto timing = OriginalPassTimingContract::inspect(
                    inputBudget.queryPoints, scalars->originalArguments(), *inputBudget.passTiming);
                if (timing.diagnostic)
                    return timing.diagnostic;
            }
        }
        if (count > budget.maxInputDescriptors || (count && !descriptors))
            return "RHI-ORIGINAL-INPUTS-CAPACITY";
        // Pointer-free fields add no owners; each distinct canonical copy/pass
        // snapshot adds one. Upload/readback snapshots/labels already belong to the
        // canonical source closure; counting descriptor aliases again is forbidden.
        if (scalars->retainedOwnerCount() >= budget.maxGraphOwners)
            return "RHI-ORIGINAL-INPUTS-OWNER-CAPACITY";
        RenderPassArgumentsBudget remainingPass{1u,
            true,
            budget.maxPassColorAttachments,
            budget.maxPassSubpasses,
            budget.maxPassAttachmentIndices,
            budget.maxPassDependencies,
            budget.maxLabelBytes - scalars->originalArguments()->storedTextBytes(),
            quota->limitBytes()};
        std::size_t consumed = 0u, mips = 0u, passReferences = 0u, queryBindings = 0u;
        std::size_t pipelineBindings = 0u, descriptorBindings = 0u, tableBindings = 0u,
                    frozenOffsets = 0u, scopeBegin = SIZE_MAX, activePipeline = SIZE_MAX,
                    pipelineBind = SIZE_MAX, vertexBindings = 0u, indexBindings = 0u;
        semantics::CommandRecordingOrder order(budget.maxDebugDepth);
        std::uint64_t uploadBytes = 0u, resultBytes = 0u;
        for (std::size_t i = 0u; i < usage.commands().size(); ++i) {
            const auto kind = usage.commands()[i].kind;
            const auto required = CommandInputContract::family(kind);
            if (const auto* id = sourceAdapterDiagnostic(required))
                return id;
            if (inputBudget.contractVersion >= 2u) {
                if (const auto* id = order.append(kind))
                    return id;
                if (kind == CommandUsageKind::BeginRenderPass
                    || kind == CommandUsageKind::BeginComputePass
                    || kind == CommandUsageKind::BeginTransferPass) {
                    scopeBegin = i;
                    activePipeline = SIZE_MAX;
                    pipelineBind = SIZE_MAX;
                }
                if (kind == CommandUsageKind::EndRenderPass
                    || kind == CommandUsageKind::EndComputePass
                    || kind == CommandUsageKind::EndTransferPass) {
                    scopeBegin = SIZE_MAX;
                    activePipeline = SIZE_MAX;
                    pipelineBind = SIZE_MAX;
                }
            }
            if ((inputBudget.contractVersion == 7u || inputBudget.contractVersion == 8u)
                && (kind == CommandUsageKind::BindGraphicsPipeline
                    || kind == CommandUsageKind::BindComputePipeline
                    || (inputBudget.contractVersion == 7u
                        && kind == CommandUsageKind::BindPreparedComputeBinding)))
                return "RHI-ORIGINAL-BINDLESS-GEOMETRY-INPUT-PIPELINE";
            if (inputBudget.contractVersion == 8u
                && kind == CommandUsageKind::BindPreparedComputeBinding) {
                const auto& command = usage.commands()[i];
                if (pipelineBindings == inputBudget.maxPipelineBindings)
                    return "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-CAPACITY";
                if (command.referenceCount != 1u
                    || order.scope() != semantics::CommandRecordingOrder::Scope::Compute)
                    return "RHI-ORIGINAL-BINDLESS-INDIRECT-INPUT-COMPUTE";
                const auto source = usage.references()[command.firstReference].sourceIndex;
                const auto* root =
                    std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                        &usage.originalSources()->sources()[source]);
                if (!root
                    || !std::holds_alternative<
                        std::shared_ptr<const BindlessPreparedComputeBinding>>(
                        (*root)->originalPrepared()))
                    return "RHI-ORIGINAL-BINDLESS-INDIRECT-INPUT-COMPUTE";
                activePipeline = source;
                pipelineBind = i;
                ++pipelineBindings;
                continue;
            }
            if ((inputBudget.contractVersion == 7u || inputBudget.contractVersion == 8u)
                && kind == CommandUsageKind::BindPreparedGraphicsBinding) {
                const auto& command = usage.commands()[i];
                if (pipelineBindings == inputBudget.maxPipelineBindings)
                    return "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-CAPACITY";
                if (command.referenceCount != 1u
                    || order.scope() != semantics::CommandRecordingOrder::Scope::Render)
                    return "RHI-ORIGINAL-BINDLESS-GEOMETRY-INPUT-PIPELINE";
                activePipeline = usage.references()[command.firstReference].sourceIndex;
                if (!OriginalCommandGeometryBindingContract::graphicsPipeline(
                        *usage.originalSources(), activePipeline, true))
                    return "RHI-ORIGINAL-BINDLESS-GEOMETRY-INPUT-PIPELINE";
                pipelineBind = i;
                ++pipelineBindings;
                continue;
            }
            if (kind == CommandUsageKind::BindGraphicsPipeline
                || kind == CommandUsageKind::BindComputePipeline) {
                if (inputBudget.contractVersion < 2u)
                    return "RHI-ORIGINAL-TYPED-INPUT-POSITIONAL-JOIN-MISSING";
                if (pipelineBindings == inputBudget.maxPipelineBindings)
                    return "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-CAPACITY";
                if (const auto* id = CommandInputContract::consumeDescriptor(i,
                        kind,
                        descriptors,
                        count,
                        consumed,
                        "RHI-ORIGINAL-INPUTS-ORDER",
                        "RHI-ORIGINAL-INPUTS-KIND"))
                    return id;
                const auto* binding = std::get_if<OriginalCommandPipelineBinding>(
                    &descriptors[consumed - 1u].arguments);
                if (!binding)
                    return "RHI-ORIGINAL-INPUTS-KIND";
                if (const auto* id = OriginalCommandPipelineBindingContract::validate(
                        *binding, usage, i, scopeBegin, order.scope()))
                    return id;
                activePipeline = binding->sourceIndex;
                pipelineBind = i;
                ++pipelineBindings;
                continue;
            }
            if (OriginalCommandGeometryBindingContract::isBinding(kind)) {
                if (inputBudget.contractVersion != 5u && inputBudget.contractVersion != 7u
                    && inputBudget.contractVersion != 8u)
                    return "RHI-ORIGINAL-GEOMETRY-POSITIONAL-PROFILE";
                const bool vertex = kind == CommandUsageKind::BindVertexBuffer;
                auto& bindings = vertex ? vertexBindings : indexBindings;
                if (bindings
                    == (vertex ? inputBudget.maxVertexBindings : inputBudget.maxIndexBindings))
                    return "RHI-ORIGINAL-GEOMETRY-BINDING-CAPACITY";
                if (const auto* id = CommandInputContract::consumeDescriptor(i,
                        kind,
                        descriptors,
                        count,
                        consumed,
                        "RHI-ORIGINAL-INPUTS-ORDER",
                        "RHI-ORIGINAL-INPUTS-KIND"))
                    return id;
                const auto* binding = std::get_if<OriginalCommandGeometryBinding>(
                    &descriptors[consumed - 1u].arguments);
                if (!binding)
                    return "RHI-ORIGINAL-INPUTS-KIND";
                if (const auto* id = OriginalCommandGeometryBindingContract::validate(*binding,
                        *scalars,
                        i,
                        activePipeline,
                        pipelineBind,
                        scopeBegin,
                        order.scope(),
                        (inputBudget.contractVersion == 7u || inputBudget.contractVersion == 8u)))
                    return id;
                ++bindings;
                continue;
            }
            if (OriginalCommandDescriptorBindingContract::isBinding(kind)) {
                if (inputBudget.contractVersion < 3u)
                    return "RHI-ORIGINAL-TYPED-INPUT-POSITIONAL-JOIN-MISSING";
                const bool table = OriginalCommandDescriptorBindingContract::isTable(kind);
                auto& bindings = table ? tableBindings : descriptorBindings;
                if (bindings
                    == (table ? inputBudget.maxTableBindings : inputBudget.maxDescriptorBindings))
                    return "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-CAPACITY";
                const auto offsets = scalars->originalArguments()->records()[i].offsetCount;
                if (offsets > inputBudget.maxFrozenDynamicOffsets - frozenOffsets)
                    return "RHI-ORIGINAL-TYPED-INPUT-OFFSET-CAPACITY";
                if (const auto* id = CommandInputContract::consumeDescriptor(i,
                        kind,
                        descriptors,
                        count,
                        consumed,
                        "RHI-ORIGINAL-INPUTS-ORDER",
                        "RHI-ORIGINAL-INPUTS-KIND"))
                    return id;
                const auto* binding = std::get_if<OriginalCommandDescriptorBinding>(
                    &descriptors[consumed - 1u].arguments);
                if (!binding)
                    return "RHI-ORIGINAL-INPUTS-KIND";
                if (const auto* id = OriginalCommandDescriptorBindingContract::validate(*binding,
                        *scalars,
                        i,
                        activePipeline,
                        scopeBegin,
                        order.scope(),
                        inputBudget.contractVersion))
                    return id;
                frozenOffsets += offsets;
                ++bindings;
                continue;
            }
            if (inputBudget.contractVersion == 8u
                && (kind == CommandUsageKind::DispatchIndirect
                    || kind == CommandUsageKind::DrawIndexedIndirectCount
                    || kind == CommandUsageKind::IndirectProcessorDraw))
                return "RHI-ORIGINAL-BINDLESS-INDIRECT-INPUT-CONSUMER";
            if (isQuery(kind)) {
                if (inputBudget.contractVersion != 9u && inputBudget.contractVersion != 11u)
                    return "RHI-ORIGINAL-QUERY-POSITIONAL-PROFILE";
                const auto& points = inputBudget.queryPoints->points();
                if (queryBindings >= inputBudget.maxQueryBindings || queryBindings >= points.size())
                    return "RHI-ORIGINAL-QUERY-INPUT-CAPACITY";
                if (const auto* id = CommandInputContract::consumeDescriptor(i,
                        kind,
                        descriptors,
                        count,
                        consumed,
                        "RHI-ORIGINAL-INPUTS-ORDER",
                        "RHI-ORIGINAL-INPUTS-KIND"))
                    return id;
                const auto* binding =
                    std::get_if<OriginalCommandQueryBinding>(&descriptors[consumed - 1u].arguments);
                if (!binding)
                    return "RHI-ORIGINAL-INPUTS-KIND";
                const auto& point = points[queryBindings];
                if (point.commandOrdinal != i || !point.original.matches(binding->original)
                    || point.passIndex >= inputBudget.queryPoints->passes().size()
                    || inputBudget.queryPoints->passes()[point.passIndex].begin != scopeBegin
                    || point.pipelineBindOrdinal
                        != (pipelineBind == SIZE_MAX ? std::optional<std::size_t>{}
                                                     : std::optional<std::size_t>{pipelineBind}))
                    return "RHI-ORIGINAL-QUERY-POINT-BINDING";
                const auto& command = usage.commands()[i];
                if (command.referenceCount != 1u
                    || binding->sourceIndex >= usage.originalSources()->sources().size()
                    || binding->sourceIndex
                        != usage.references()[command.firstReference].sourceIndex)
                    return "RHI-ORIGINAL-QUERY-INPUT-SOURCE";
                const auto* source =
                    std::get_if<std::shared_ptr<const OriginalCommandQueryPoolSource>>(
                        &usage.originalSources()->sources()[binding->sourceIndex]);
                if (!source
                    || !core::SharedOwnerProjection::sameOwner(
                        (*source)->originalInput(), binding->original.originalInput()))
                    return "RHI-ORIGINAL-QUERY-INPUT-SOURCE";
                if (binding->registrationGeneration
                    != (*source)->originalObject()->originalResourceRegistration().generation())
                    return "RHI-ORIGINAL-QUERY-INPUT-GENERATION";
                const auto& fields = scalars->originalArguments()->records()[i].scalar;
                if (const auto* id = OriginalCommandTypedInputContract::validateQueryBinding(
                        {fields}, binding->original))
                    return id;
                ++queryBindings;
                continue;
            }
            if (required == Family::Scalar)
                continue;
            if (const auto* id = CommandInputContract::consumeDescriptor(i,
                    kind,
                    descriptors,
                    count,
                    consumed,
                    "RHI-ORIGINAL-INPUTS-ORDER",
                    "RHI-ORIGINAL-INPUTS-KIND"))
                return id;
            const auto& value = descriptors[consumed - 1u].arguments;
            if (required == Family::Copy) {
                GpuResourceId source, destination;
                if (const auto* copy = std::get_if<TransferCopyArguments>(&value)) {
                    source = copy->source();
                    destination = copy->destination();
                } else if (const auto* copy =
                               std::get_if<std::shared_ptr<const TransferCopyInputSnapshot>>(
                                   &value)) {
                    if (const auto* id = TransferCopyInputSnapshot::validateOriginal(*copy))
                        return id;
                    if (!core::SharedOwnerProjection::sameOwner(
                            (*copy)->originalMetadataQuota(), quota))
                        return "RHI-ORIGINAL-INPUTS-COPY-QUOTA";
                    if ((*copy)->mipMappingCount() > budget.maxMipMappings - mips)
                        return "RHI-ORIGINAL-INPUTS-MIP-CAPACITY";
                    mips += (*copy)->mipMappingCount();
                    source = (*copy)->source();
                    destination = (*copy)->destination();
                } else
                    return "RHI-ORIGINAL-INPUTS-KIND";
                if (!resourceMatches(usage, i, 0u, source)
                    || !resourceMatches(usage, i, 1u, destination))
                    return "RHI-ORIGINAL-INPUTS-RESOURCE";
            } else if (required == Family::BufferUpload) {
                const auto* arguments = std::get_if<BufferUploadArguments>(&value);
                if (!arguments)
                    return "RHI-ORIGINAL-INPUTS-KIND";
                const auto& entry = usage.commands()[i];
                const auto index = usage.references()[entry.firstReference + 2u].sourceIndex;
                const auto* source =
                    std::get_if<std::shared_ptr<const OriginalCommandBufferUploadSource>>(
                        &usage.originalSources()->sources()[index]);
                if (!source || !arguments->matches((*source)->arguments()))
                    return "RHI-ORIGINAL-INPUTS-BUFFER-UPLOAD-INPUT";
                if (!resourceMatches(usage, i, 0u, arguments->source())
                    || !resourceMatches(usage, i, 1u, arguments->destination()))
                    return "RHI-ORIGINAL-INPUTS-RESOURCE";
                if ((*source)->label() != scalars->originalArguments()->text(i))
                    return "RHI-ORIGINAL-INPUTS-UPLOAD-LABEL";
                if ((*source)->sizeBytes() > budget.maxRetainedUploadBytes - uploadBytes)
                    return "RHI-ORIGINAL-INPUTS-UPLOAD-CAPACITY";
                if ((*source)->label().size() > remainingPass.maxTextBytes)
                    return "RHI-ORIGINAL-INPUTS-UPLOAD-TEXT-CAPACITY";
                uploadBytes += (*source)->sizeBytes();
                remainingPass.maxTextBytes -= (*source)->label().size();
            } else if (required == Family::TextureUpload) {
                const auto* arguments = std::get_if<TextureUploadOriginalInputArguments>(&value);
                if (!arguments)
                    return "RHI-ORIGINAL-INPUTS-KIND";
                const auto& entry = usage.commands()[i];
                const auto index = usage.references()[entry.firstReference + 1u].sourceIndex;
                const auto* source =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureUploadSource>>(
                        &usage.originalSources()->sources()[index]);
                if (!source
                    || !core::SharedOwnerProjection::sameOwner(
                        arguments->originalInput(), (*source)->originalInput()))
                    return "RHI-ORIGINAL-INPUTS-UPLOAD-OWNER";
                const auto& input = *(*source)->originalInput();
                if (!resourceMatches(usage, i, 0u, input.scalarFields().destination()))
                    return "RHI-ORIGINAL-INPUTS-RESOURCE";
                if (input.label() != scalars->originalArguments()->text(i))
                    return "RHI-ORIGINAL-INPUTS-UPLOAD-LABEL";
                const auto mappings = semantics::TextureUploadLayoutValidation::mipCount(input);
                if (mappings > budget.maxMipMappings - mips)
                    return "RHI-ORIGINAL-INPUTS-MIP-CAPACITY";
                if (input.pixelBytes().size() > budget.maxRetainedUploadBytes - uploadBytes)
                    return "RHI-ORIGINAL-INPUTS-UPLOAD-CAPACITY";
                if (input.label().size() > remainingPass.maxTextBytes)
                    return "RHI-ORIGINAL-INPUTS-UPLOAD-TEXT-CAPACITY";
                mips += mappings;
                uploadBytes += input.pixelBytes().size();
                remainingPass.maxTextBytes -= input.label().size();
            } else if (required == Family::TextureReadback) {
                const auto* arguments = std::get_if<TextureReadbackOriginalInputArguments>(&value);
                if (!arguments)
                    return "RHI-ORIGINAL-INPUTS-KIND";
                const auto& entry = usage.commands()[i];
                const auto index = usage.references()[entry.firstReference + 1u].sourceIndex;
                const auto* source =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureReadbackSource>>(
                        &usage.originalSources()->sources()[index]);
                if (!source
                    || !core::SharedOwnerProjection::sameOwner(
                        arguments->originalInput(), (*source)->originalInput()))
                    return "RHI-ORIGINAL-INPUTS-READBACK-OWNER";
                const auto textureIndex = usage.references()[entry.firstReference].sourceIndex;
                const auto& textureEntry = usage.originalSources()->sources()[textureIndex];
                const bool sameOriginal = [&] {
                    if (inputBudget.retainedReadback.enabled()) {
                        const auto* texture =
                            std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(
                                &textureEntry);
                        return texture && (*source)->contractVersion() == 2u
                            && core::SharedOwnerProjection::sameOwner(*texture,
                                (*source)
                                    ->originalSource()
                                    ->originalLease()
                                    ->originalTextureOwner());
                    }
                    const auto* texture =
                        std::get_if<std::shared_ptr<const OriginalCommandReadbackTextureSource>>(
                            &textureEntry);
                    return texture
                        && core::SharedOwnerProjection::sameOwner(
                            *texture, (*source)->originalSource());
                }();
                if (!sameOriginal)
                    return "RHI-ORIGINAL-INPUTS-READBACK-SOURCE";
                const auto& input = *(*source)->originalInput();
                if (input.label() != scalars->originalArguments()->text(i))
                    return "RHI-ORIGINAL-INPUTS-READBACK-LABEL";
                const auto mappings = (*source)
                                          ->originalSource()
                                          ->originalRequest()
                                          ->originalLayout()
                                          ->mipCopies()
                                          .size();
                if (mappings > budget.maxMipMappings - mips)
                    return "RHI-ORIGINAL-INPUTS-MIP-CAPACITY";
                if (input.scalarFields().byteCapacity()
                    > budget.maxUnpublishedResultBytes - resultBytes)
                    return "RHI-ORIGINAL-INPUTS-READBACK-RESULT-CAPACITY";
                if (input.label().size() > remainingPass.maxTextBytes)
                    return "RHI-ORIGINAL-INPUTS-READBACK-TEXT-CAPACITY";
                mips += mappings;
                resultBytes += input.scalarFields().byteCapacity();
                remainingPass.maxTextBytes -= input.label().size();
            } else if (required == Family::Pass) {
                const auto* pass = std::get_if<std::shared_ptr<const RenderPassArguments>>(&value);
                if (!pass || !*pass || !(*pass)->hasQuotaBoundSharedOwner(*pass))
                    return "RHI-ORIGINAL-INPUTS-PASS-OWNER";
                if (!core::SharedOwnerProjection::sameOwner(
                        (*pass)->originalMetadataQuota(), quota))
                    return "RHI-ORIGINAL-INPUTS-PASS-QUOTA";
                const auto refs = usage.commands()[i].referenceCount;
                if (refs > budget.maxPassAttachmentReferences - passReferences)
                    return "RHI-ORIGINAL-INPUTS-PASS-REFERENCE-CAPACITY";
                if (const auto* id = (*pass)->consumeCapacity(remainingPass))
                    return id;
                if (const auto* id = validatePass(usage, i, **pass))
                    return id;
                passReferences += refs;
            } else {
                const auto* dependency = std::get_if<ResourceDependencyArguments>(&value);
                if (!dependency)
                    return "RHI-ORIGINAL-INPUTS-KIND";
                if (!resourceMatches(usage, i, 0u, dependency->firstResource())
                    || (kind == CommandUsageKind::AliasBarrier
                        && !resourceMatches(usage, i, 1u, dependency->secondResource())))
                    return "RHI-ORIGINAL-INPUTS-RESOURCE";
            }
        }
        if (inputBudget.contractVersion >= 2u && !order.complete())
            return "RHI-COMMAND-ORDER-INCOMPLETE";
        if (consumed != count)
            return "RHI-ORIGINAL-INPUTS-ORDER";
        const auto extraOwners = inputBudget.queryPoints ? 2u : 1u;
        if (extraOwners > budget.maxGraphOwners - scalars->retainedOwnerCount())
            return "RHI-ORIGINAL-INPUTS-OWNER-CAPACITY";
        if (inputBudget.queryPoints && queryBindings != inputBudget.queryPoints->points().size())
            return "RHI-ORIGINAL-QUERY-POINT-COVERAGE";
        const auto baseOwners = scalars->retainedOwnerCount() + extraOwners;
        const auto owners = core::SharedIdentityProjection::inspect(count,
            count,
            budget.maxGraphOwners - baseOwners,
            [&](std::size_t i) -> core::SharedIdentityProjection::Record {
                const auto* copy = std::get_if<std::shared_ptr<const TransferCopyInputSnapshot>>(
                    &descriptors[i].arguments);
                // Canonical shared ownership was validated above. This retained
                // object key only deduplicates live CPU nodes; it proves no GPU
                // identity/provenance and cannot outlive that exact owner.
                if (copy)
                    return {
                        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(copy->get())),
                        1u,
                        *copy,
                        copy->get()};
                const auto* pass = std::get_if<std::shared_ptr<const RenderPassArguments>>(
                    &descriptors[i].arguments);
                if (pass)
                    return {
                        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pass->get())),
                        2u,
                        *pass,
                        pass->get()};
                return {};
            });
        if (owners.status != core::SharedIdentityProjection::Status::Complete)
            return "RHI-ORIGINAL-INPUTS-OWNER-CAPACITY";
        if (retainedOwners)
            *retainedOwners = baseOwners + static_cast<std::uint32_t>(owners.identities);
        return nullptr;
    }

    const std::shared_ptr<const OriginalCommandScalars> scalars_;
    core::BoundedSequence<OriginalDescriptor> descriptors_;
    const std::uint32_t ownerCount_;
    const OriginalCommandInputsBudget budget_;
};

// endregion original_command_inputs
// endregion Command inputs

// region Command descriptor consumption

class OriginalCommandResourceStates;
} // namespace arp::rhi

namespace arp::rhi::common {
// Dry ephemeral projection only. No state snapshot, owner, journal, retained
// receipt or command authority. /1 consumes Inputs3 buffer footprints;
// /2 consumes Inputs4 and explicitly opted-in exact full-view depth footprints;
// GeneralResources/1 also admits ordinary Inputs3. /3 keeps geometry Inputs5.
// /4 pairs GeneralResources2/Inputs6 and projects each Source5 element.
class OriginalCommandDescriptorConsumption final {
    friend class rhi::OriginalCommandResourceStates;
    using K = CommandUsageKind;
    using Inputs = OriginalCommandInputs;

public:
    using Budget = OriginalCommandDescriptorConsumptionContract::Budget;

    struct Access {
        enum class Kind { Buffer, TextureView, Sampler };
        Kind kind = Kind::Buffer;
        std::size_t commandIndex = SIZE_MAX, scopeBeginCommandIndex = SIZE_MAX;
        std::size_t pipelineSourceIndex = SIZE_MAX, pipelineBindCommandIndex = SIZE_MAX;
        std::size_t descriptorSourceIndex = SIZE_MAX, descriptorBindCommandIndex = SIZE_MAX,
                    bufferSourceIndex = SIZE_MAX;
        std::uint32_t set = 0u, binding = 0u;
        std::uint64_t registrationGeneration = 0u, descriptorContentRevision = 0u, offsetBytes = 0u,
                      sizeBytes = 0u;
        ResourceState state = ResourceState::Undefined;
        PipelineStage stages = PipelineStage::None;
        ResourceAccess access = ResourceAccess::None;
        std::size_t textureViewSourceIndex = SIZE_MAX, textureParentSourceIndex = SIZE_MAX;
        std::uint32_t tableElementIndex = 0u;
        std::uint64_t textureParentRegistrationGeneration = 0u;
        TextureSubresourceRange textureRange;
        std::size_t samplerSourceIndex = SIZE_MAX;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    struct Result {
        const char* diagnostic = nullptr;
        std::size_t commandIndex = 0u;
        std::uint64_t consumptions = 0u, logicalAccesses = 0u;
        std::uint64_t textureAccesses = 0u;
        std::uint64_t samplerReferences = 0u;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    using RenderBindingBudget = OriginalRenderDescriptorUsageContract::Budget;

    struct RenderBindingResult {
        const char* diagnostic = nullptr;
        std::size_t commandIndex = 0u;
        std::uint64_t bindings = 0u, logicalAccesses = 0u;
        std::uint64_t textureAccesses = 0u;
        std::uint64_t samplerReferences = 0u;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr const char* validateBudget(const Budget& budget) noexcept
    {
        return OriginalCommandDescriptorConsumptionContract::validateBudget(budget);
    }

    static Result inspect(const std::shared_ptr<const Inputs>& inputs,
        const GpuDevice& device,
        const Budget& budget) noexcept
    {
        return walk(inputs, device, budget, [](const Access&) noexcept -> const char* {
            return nullptr;
        });
    }

    template <class Visitor>
    static Result visit(const std::shared_ptr<const Inputs>& inputs,
        const GpuDevice& device,
        const Budget& budget,
        Visitor&& visitor) noexcept
    {
        auto result = inspect(inputs, device, budget);
        if (result.diagnostic)
            return result; // No output until complete preflight.
        return walk(inputs, device, budget, std::forward<Visitor>(visitor));
    }

    static RenderBindingResult inspectRenderBindings(const std::shared_ptr<const Inputs>& inputs,
        const GpuDevice& device,
        const RenderBindingBudget& budget) noexcept
    {
        return renderBindings(inputs, device, budget, [](const Access&) noexcept -> const char* {
            return nullptr;
        });
    }

    template <class Visitor>
    static RenderBindingResult visitRenderBindings(const std::shared_ptr<const Inputs>& inputs,
        const GpuDevice& device,
        const RenderBindingBudget& budget,
        Visitor&& visitor) noexcept
    {
        const auto result = inspectRenderBindings(inputs, device, budget);
        if (result.diagnostic)
            return result;
        return renderBindings(inputs, device, budget, std::forward<Visitor>(visitor));
    }

private:
    template <class Visitor>
    static RenderBindingResult renderBindings(const std::shared_ptr<const Inputs>& inputs,
        const GpuDevice& device,
        const RenderBindingBudget& budget,
        Visitor&& visitor) noexcept
    {
        RenderBindingResult result;
        const auto fail = [&](const char* id) noexcept {
            return RenderBindingResult{id, result.commandIndex, 0u, 0u};
        };
        if (const auto* id = OriginalRenderDescriptorUsageContract::validateBudget(budget))
            return fail(id);
        if (const auto* id = OriginalRenderDescriptorUsageContract::lowering(
                device.backend(), budget.contractVersion)
                .diagnostic)
            return fail(id);
        if (const auto* id = Inputs::validateOriginal(inputs))
            return fail(id);
        if (!OriginalCommandDescriptorConsumptionContract::inputsProfileMatches(
                budget.contractVersion,
                budget.generalShaderResourcesVersion,
                budget.generalShaderResourcesOptIn,
                inputs->originalBudget().contractVersion))
            return fail("RHI-ORIGINAL-RENDER-USAGE-INPUTS-VERSION");
        const auto& usage = *inputs->originalScalars()->originalUsage();
        if (const auto* id = OriginalCommandResourceSources::validateRegisteredLifetime(
                usage.originalSources(), device))
            return fail(id);
        return renderBindingsOriginal(inputs, budget, std::forward<Visitor>(visitor));
    }

    // Same original render-binding walker after the State2 facade's captured
    // device/source join. Only that existing friend can enter this helper.
    template <bool Joined = false, bool IncludeCompute = false, class Visitor>
    static RenderBindingResult renderBindingsOriginal(const std::shared_ptr<const Inputs>& inputs,
        const RenderBindingBudget& budget,
        Visitor&& visitor) noexcept
    {
        RenderBindingResult result;
        const auto fail = [&](const char* id) noexcept {
            return RenderBindingResult{id, result.commandIndex, 0u, 0u};
        };
        if (const auto* id = OriginalRenderDescriptorUsageContract::validateBudget(budget))
            return fail(id);
        if constexpr (!Joined)
            if (const auto* id = Inputs::validateOriginal(inputs))
                return fail(id);
        if (!OriginalCommandDescriptorConsumptionContract::inputsProfileMatches(
                budget.contractVersion,
                budget.generalShaderResourcesVersion,
                budget.generalShaderResourcesOptIn,
                inputs->originalBudget().contractVersion))
            return fail("RHI-ORIGINAL-RENDER-USAGE-INPUTS-VERSION");
        const auto& usage = *inputs->originalScalars()->originalUsage();
        const auto& sources = *usage.originalSources();
        if (usage.commands().size() > budget.maxCommands)
            return fail("RHI-ORIGINAL-RENDER-USAGE-COMMAND-CAPACITY");
        semantics::CommandRecordingOrder order(sources.originalBudget().maxDebugDepth);
        Access point;
        for (std::size_t command = 0u; command < usage.commands().size(); ++command) {
            result.commandIndex = command;
            const auto kind = usage.commands()[command].kind;
            if (const auto* id = order.append(kind))
                return fail(id);
            // RenderUsage3 enumerates binding footprints only; State4 still
            // requires independently opted-in indexed shader consumption.
            if (!supported(kind, budget.contractVersion, budget.contractVersion == 3u)
                && !passTimingTimestamp(kind, *inputs, budget.contractVersion))
                return fail("RHI-ORIGINAL-RENDER-USAGE-COMMAND-PROFILE");
            if (kind == K::BeginRenderPass || kind == K::BeginComputePass
                || kind == K::BeginTransferPass) {
                point = {};
                point.scopeBeginCommandIndex = command;
            }
            if (kind == K::EndRenderPass || kind == K::EndComputePass || kind == K::EndTransferPass)
                point = {};
            if (OriginalCommandPipelineBindingContract::isPipelineBinding(kind)) {
                const auto* value = inputs->descriptorAt(command);
                const auto* pipeline =
                    value ? std::get_if<OriginalCommandPipelineBinding>(value) : nullptr;
                if (!pipeline)
                    return fail("RHI-ORIGINAL-CONSUMPTION-PIPELINE");
                point.pipelineSourceIndex = pipeline->sourceIndex;
                point.pipelineBindCommandIndex = command;
            }
            const bool compute = IncludeCompute
                && (kind == K::BindComputeDescriptor
                    || (budget.contractVersion >= 2u && kind == K::BindComputeDescriptorTable));
            if (!compute && kind != K::BindRenderDescriptor
                && (budget.contractVersion < 2u || kind != K::BindRenderDescriptorTable))
                continue;
            if (result.bindings == budget.maxBindings)
                return fail("RHI-ORIGINAL-RENDER-USAGE-BINDING-CAPACITY");
            ++result.bindings;
            point.commandIndex = command;
            const auto* value = inputs->descriptorAt(command);
            const auto* binding =
                value ? std::get_if<OriginalCommandDescriptorBinding>(value) : nullptr;
            if (!binding)
                return fail("RHI-ORIGINAL-RENDER-USAGE-DESCRIPTOR");
            if constexpr (!Joined)
                if (const auto* id = OriginalCommandDescriptorBindingContract::validate(*binding,
                        *inputs->originalScalars(),
                        command,
                        point.pipelineSourceIndex,
                        point.scopeBeginCommandIndex,
                        compute ? semantics::CommandRecordingOrder::Scope::Compute
                                : semantics::CommandRecordingOrder::Scope::Render,
                        inputs->originalBudget().contractVersion))
                    return fail(id);
            const auto* set = inputs->originalScalars()
                                  ->originalArguments()
                                  ->records()[command]
                                  .scalar.template get<CommandScalarArguments::DescriptorBinding>();
            if (!set)
                return fail("RHI-ORIGINAL-RENDER-USAGE-SET");
            point.descriptorSourceIndex = binding->sourceIndex;
            point.descriptorBindCommandIndex = command;
            point.descriptorContentRevision = binding->contentRevision;
            point.set = set->set;
            if (kind == K::BindRenderDescriptorTable
                || (compute && kind == K::BindComputeDescriptorTable)) {
                const auto project = [&](const auto& pipeline) noexcept -> const char* {
                    return projectBoundTable(
                        pipeline, *inputs, *binding, command, budget, point, result, visitor);
                };
                if (compute) {
                    const auto& pipeline =
                        *std::get<std::shared_ptr<const OriginalCommandComputePipelineSource>>(
                            sources.sources()[point.pipelineSourceIndex]);
                    if (const auto* id = project(pipeline))
                        return fail(id);
                } else {
                    const auto& pipeline =
                        *std::get<std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(
                            sources.sources()[point.pipelineSourceIndex]);
                    if (const auto* id = project(pipeline))
                        return fail(id);
                }
                continue;
            }
            const auto& descriptor =
                *std::get<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
                    sources.sources()[binding->sourceIndex]);
            if (const auto* id =
                    projectBoundView(descriptor, sources, budget, point, result, visitor))
                return fail(id);
        }
        if (!order.complete())
            return fail("RHI-COMMAND-ORDER-INCOMPLETE");
        result.commandIndex = usage.commands().size();
        return result;
    }

    static constexpr bool isConsumption(K kind, const Budget& budget) noexcept
    {
        return OriginalCommandDescriptorConsumptionContract::isConsumption(kind, budget);
    }

    static constexpr bool supported(
        K kind, std::uint32_t version = 1u, bool indexed = false) noexcept
    {
        switch (kind) {
        case K::BeginRenderPass:
        case K::EndRenderPass:
        case K::BeginComputePass:
        case K::EndComputePass:
        case K::BeginTransferPass:
        case K::EndTransferPass:
        case K::BindGraphicsPipeline:
        case K::BindComputePipeline:
        case K::BindRenderDescriptor:
        case K::BindComputeDescriptor:
        case K::Draw:
        case K::Dispatch:
        case K::BufferBarrier:
        case K::TextureBarrier:
        case K::BufferMemoryBarrier:
        case K::TextureMemoryBarrier:
        case K::BufferCopy:
        case K::BufferToTextureCopy:
        case K::TextureToBufferCopy:
        case K::TextureToTextureCopy:
        case K::BufferUpload:
        case K::TextureUpload:
        case K::TextureReadback:
        case K::RetainTexture:
        case K::SetViewport:
        case K::SetScissor:
        case K::PushDebugGroup:
        case K::PopDebugGroup:
        case K::InsertDebugMarker:
        case K::Finish:
            return true;
        case K::BindRenderDescriptorTable:
        case K::BindComputeDescriptorTable:
            return version == 2u || version == 3u;
        case K::BindVertexBuffer:
        case K::BindIndexBuffer:
            return version == 3u;
        case K::DrawIndexed:
            return version == 3u && indexed;
        default:
            return false;
        }
    }

    // A timestamp contributes no descriptor footprint. Only the independently
    // validated Inputs11 pair projection may pass through the existing /4
    // walkers; ordinary versions and other query kinds stay unsupported.
    static bool passTimingTimestamp(K kind, const Inputs& inputs, std::uint32_t version) noexcept
    {
        const auto& original = inputs.originalBudget();
        return kind == K::Timestamp && version == 4u && original.contractVersion == 11u
            && original.passTiming && original.queryPoints;
    }

    template <class Footprint, class Counts, class Visitor>
    static const char* projectBoundView(const OriginalCommandDescriptorSetSource& descriptor,
        const OriginalCommandResourceSources& sources,
        const Footprint& budget,
        Access point,
        Counts& result,
        Visitor&& visitor) noexcept
    {
        const bool mixed = budget.contractVersion == 4u
            && budget.generalShaderResourcesVersion == 2u && budget.generalShaderResourcesOptIn;
        const bool finite =
            OriginalCommandDescriptorSetSource::finiteVersion(descriptor.sourceContractVersion());
        if (!mixed && (finite ? !budget.finiteFullBoundViewOptIn : !budget.wholeFullBoundViewOptIn))
            return "RHI-ORIGINAL-CONSUMPTION-SOURCE-FOOTPRINT-OPT-IN";
        if (mixed ? (descriptor.sourceContractVersion() != 5u
                        && descriptor.sourceContractVersion() != 7u)
                  : (!finite && descriptor.sourceContractVersion() != 2u))
            return "RHI-ORIGINAL-CONSUMPTION-SOURCE-VERSION";
        for (const auto& frozen : descriptor.frozenElements()) {
            const auto& element = frozen.declaration;
            const auto* declared =
                findDescriptorSetBinding(descriptor.frozenDefinition(), element.write.binding);
            if (!declared)
                return "RHI-ORIGINAL-CONSUMPTION-LAYOUT";
            auto projected = point;
            const auto* owner = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                &frozen.resourceOwner);
            if (owner) {
                const bool finiteElement =
                    mixed ? element.write.bufferBindingRange.contractVersion == 2u : finite;
                if (finiteElement ? !budget.finiteFullBoundViewOptIn
                                  : !budget.wholeFullBoundViewOptIn)
                    return "RHI-ORIGINAL-CONSUMPTION-SOURCE-FOOTPRINT-OPT-IN";
                const auto bytes = (*owner)->layout().sizeBytes;
                const auto semanticsVersion = mixed ? 1u : budget.generalShaderResourcesVersion;
                if (const auto* id = finiteElement
                        ? OriginalCommandTypedSourceContract::validateFullBoundView(
                              element, *declared, bytes, semanticsVersion)
                        : OriginalCommandTypedSourceContract::validateWholeBoundView(
                              element, *declared, bytes, semanticsVersion))
                    return id;
                DescriptorBufferBindingRangeContract::Resolved range;
                if (const auto* id = DescriptorBufferBindingRangeContract::resolve(
                        element.write.bufferBindingRange, bytes, element.dynamicOffset, range))
                    return id;
                projected.bufferSourceIndex = element.sourceIndex;
                projected.offsetBytes = range.offsetBytes;
                projected.sizeBytes = range.sizeBytes;
            } else if (const auto* sampler =
                           std::get_if<std::shared_ptr<const OriginalCommandSamplerLease>>(
                               &frozen.resourceOwner)) {
                if ((!mixed && budget.generalShaderResourcesVersion != 1u)
                    || !budget.generalShaderResourcesOptIn || finite)
                    return "RHI-ORIGINAL-CONSUMPTION-SAMPLER-PROFILE";
                if (!*sampler || sources.originalLeaseIndex(*sampler) != element.sourceIndex)
                    return "RHI-ORIGINAL-CONSUMPTION-SAMPLER-SOURCE";
                if (const auto* id =
                        OriginalCommandTypedSourceContract::validateGeneralSamplerFootprint(
                            element, *declared, (*sampler)->layout()))
                    return id;
                projected.kind = Access::Kind::Sampler;
                projected.bufferSourceIndex = SIZE_MAX;
                projected.samplerSourceIndex = element.sourceIndex;
                ++result.samplerReferences;
            } else {
                if ((!mixed && budget.generalShaderResourcesVersion != 1u)
                    || !budget.generalShaderResourcesOptIn || finite)
                    return "RHI-ORIGINAL-CONSUMPTION-BUFFER-PROFILE";
                const auto* view =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &frozen.resourceOwner);
                if (!view || !*view || sources.originalLeaseIndex(*view) != element.sourceIndex)
                    return "RHI-ORIGINAL-CONSUMPTION-TEXTURE-SOURCE";
                const auto& parent = (*view)->originalParentLease();
                const auto parentIndex = sources.originalLeaseIndex(parent);
                if (parentIndex == SIZE_MAX)
                    return "RHI-ORIGINAL-CONSUMPTION-TEXTURE-PARENT";
                const auto& layout = (*view)->originalViewLease()->layout();
                if (const auto* id =
                        OriginalCommandTypedSourceContract::validateGeneralTextureFootprint(
                            element, *declared, layout))
                    return id;
                if (result.textureAccesses == budget.maxTextureAccesses)
                    return "RHI-ORIGINAL-CONSUMPTION-TEXTURE-CAPACITY";
                projected.kind = Access::Kind::TextureView;
                projected.bufferSourceIndex = SIZE_MAX;
                projected.textureViewSourceIndex = element.sourceIndex;
                projected.textureParentSourceIndex = parentIndex;
                projected.textureParentRegistrationGeneration =
                    parent->originalObject()->originalResourceRegistration().generation();
                projected.textureRange = {layout.baseMipLevel,
                    layout.mipLevelCount,
                    layout.baseArrayLayer,
                    layout.arrayLayerCount,
                    layout.aspect};
                ++result.textureAccesses;
            }
            projected.binding = element.write.binding;
            projected.registrationGeneration = element.registrationGeneration;
            projected.state = element.write.resourceState;
            projected.stages = element.stages;
            projected.access = element.access;
            if (result.logicalAccesses == budget.maxLogicalAccesses)
                return "RHI-ORIGINAL-CONSUMPTION-ACCESS-CAPACITY";
            ++result.logicalAccesses;
            if (const auto* id = visitor(projected))
                return id;
        }
        return nullptr;
    }

    template <class Footprint, class Counts, class Visitor>
    static const char* projectDepthTable(const OriginalCommandDescriptorTableSource& table,
        const OriginalCommandResourceSources& sources,
        const Footprint& budget,
        Access point,
        Counts& result,
        Visitor&& visitor) noexcept
    {
        if (table.sourceContractVersion() != 2u)
            return "RHI-ORIGINAL-CONSUMPTION-TABLE-SOURCE-VERSION";
        const auto& definition = table.frozenDefinition();
        if (definition.stages != ShaderStage::Compute && definition.stages != ShaderStage::Fragment)
            return "RHI-ORIGINAL-CONSUMPTION-TABLE-STAGES";
        // The positional contract has already joined the approved depth D2
        // expansion and exact pipeline. Project every possible logical index.
        for (const auto& frozen : table.frozenElements()) {
            const auto& element = frozen.declaration;
            const auto* owner =
                std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                    &frozen.resourceOwner);
            const auto viewIndex = owner ? sources.originalLeaseIndex(*owner) : SIZE_MAX;
            if (viewIndex == SIZE_MAX || viewIndex != element.sourceIndex)
                return "RHI-ORIGINAL-CONSUMPTION-TEXTURE-SOURCE";
            const auto& parent = (*owner)->originalParentLease();
            const auto parentIndex = sources.originalLeaseIndex(parent);
            if (parentIndex == SIZE_MAX)
                return "RHI-ORIGINAL-CONSUMPTION-TEXTURE-PARENT";
            const auto& view = (*owner)->originalViewLease()->layout();
            point.kind = Access::Kind::TextureView;
            point.bufferSourceIndex = SIZE_MAX;
            point.textureViewSourceIndex = element.sourceIndex;
            point.textureParentSourceIndex = parentIndex;
            point.registrationGeneration = element.registrationGeneration;
            point.textureParentRegistrationGeneration =
                parent->originalObject()->originalResourceRegistration().generation();
            point.tableElementIndex = element.write.index;
            point.offsetBytes = point.sizeBytes = 0u;
            point.textureRange = {view.baseMipLevel,
                view.mipLevelCount,
                view.baseArrayLayer,
                view.arrayLayerCount,
                view.aspect};
            point.state = ResourceState::ShaderRead;
            point.stages = definition.stages == ShaderStage::Compute
                ? PipelineStage::ComputeShader
                : PipelineStage::FragmentShader;
            point.access = ResourceAccess::ShaderRead;
            if (result.logicalAccesses == budget.maxLogicalAccesses)
                return "RHI-ORIGINAL-CONSUMPTION-ACCESS-CAPACITY";
            if (result.textureAccesses == budget.maxTextureAccesses)
                return "RHI-ORIGINAL-CONSUMPTION-TEXTURE-CAPACITY";
            ++result.logicalAccesses;
            ++result.textureAccesses;
            if (const auto* id = visitor(point))
                return id;
        }
        return nullptr;
    }

    template <class Pipeline, class Footprint, class Counts, class Visitor>
    static const char* projectBoundTable(const Pipeline& pipeline,
        const Inputs& inputs,
        const OriginalCommandDescriptorBinding& binding,
        std::size_t bindingCommandIndex,
        const Footprint& budget,
        Access point,
        Counts& result,
        Visitor&& visitor) noexcept
    {
        if (const auto* id = OriginalCommandDescriptorBindingContract::validate(binding,
                *inputs.originalScalars(),
                bindingCommandIndex,
                point.pipelineSourceIndex,
                point.scopeBeginCommandIndex,
                std::is_same_v<Pipeline, OriginalCommandGraphicsPipelineSource>
                    ? semantics::CommandRecordingOrder::Scope::Render
                    : semantics::CommandRecordingOrder::Scope::Compute,
                inputs.originalBudget().contractVersion))
            return id;
        const auto& sources = *inputs.originalScalars()->originalUsage()->originalSources();
        const auto& table = *std::get<std::shared_ptr<const OriginalCommandDescriptorTableSource>>(
            sources.sources()[binding.sourceIndex]);
        point.descriptorSourceIndex = binding.sourceIndex;
        point.descriptorBindCommandIndex = bindingCommandIndex;
        point.descriptorContentRevision = binding.contentRevision;
        const auto& artifact = [&]() -> const auto& {
            if constexpr (std::is_same_v<Pipeline, OriginalCommandGraphicsPipelineSource>)
                return pipeline.originalObject()
                    ->originalGraphicsCreation()
                    ->originalInput()
                    ->tableExpansion();
            else
                return pipeline.originalObject()
                    ->originalComputeCreation()
                    ->originalInput()
                    ->tableExpansion();
        }();
        const auto logical = std::find_if(artifact->logicalBindings().begin(),
            artifact->logicalBindings().end(),
            [&](const auto& value) noexcept {
                return value.set == point.set && value.name == table.frozenDefinition().label;
            });
        if (logical == artifact->logicalBindings().end())
            return "RHI-ORIGINAL-CONSUMPTION-TABLE-MAPPING";
        point.binding = logical->binding;
        return projectDepthTable(table, sources, budget, point, result, visitor);
    }

    template <bool Joined, class Pipeline, class Visitor>
    static const char* consume(const Pipeline& pipeline,
        const Inputs& inputs,
        const Budget& budget,
        Access point,
        Result& result,
        Visitor&& visitor) noexcept
    {
        const auto& sources = *inputs.originalScalars()->originalUsage()->originalSources();
        const auto& layout = pipeline.originalLayout()->desc();
        if (!layout.descriptorTables.empty()
            && (budget.contractVersion < 2u || budget.contractVersion == 4u))
            return "RHI-ORIGINAL-CONSUMPTION-TABLE-PROFILE";
        if (!layout.pushConstantRanges.empty())
            return "RHI-ORIGINAL-CONSUMPTION-PUSH-PROFILE";
        if constexpr (std::is_same_v<Pipeline, OriginalCommandGraphicsPipelineSource>) {
            const auto& definition = *pipeline.originalObject()->originalRenderPipelineDefinition();
            if (const auto* id =
                    OriginalCommandDescriptorConsumptionContract::validateVertexStreams(budget,
                        inputs.originalBudget().contractVersion,
                        inputs.originalScalars()
                            ->originalUsage()
                            ->commands()[point.commandIndex]
                            .kind,
                        definition.vertexBuffers.size()))
                return id;
            if (definition.topology == PrimitiveTopology::PatchList
                || definition.patchControlPointCount
                || !definition.tessellationControlShader.entryPoint.empty()
                || !definition.tessellationEvaluationShader.entryPoint.empty())
                return "RHI-ORIGINAL-CONSUMPTION-TESSELLATION-PROFILE";
        }
        for (const auto& slot : layout.descriptorSets) {
            const auto* record = inputs.latestDescriptorSetBinding(
                slot.set, point.pipelineBindCommandIndex, point.commandIndex);
            if (!record)
                return "RHI-ORIGINAL-CONSUMPTION-DESCRIPTOR-MISSING";
            const auto& binding = std::get<OriginalCommandDescriptorBinding>(record->arguments);
            if constexpr (!Joined)
                if (const auto* id = OriginalCommandDescriptorBindingContract::validate(binding,
                        *inputs.originalScalars(),
                        record->commandIndex,
                        point.pipelineSourceIndex,
                        point.scopeBeginCommandIndex,
                        std::is_same_v<Pipeline, OriginalCommandGraphicsPipelineSource>
                            ? semantics::CommandRecordingOrder::Scope::Render
                            : semantics::CommandRecordingOrder::Scope::Compute,
                        inputs.originalBudget().contractVersion))
                    return id;
            const auto& descriptor =
                *std::get<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
                    sources.sources()[binding.sourceIndex]);
            point.descriptorSourceIndex = binding.sourceIndex;
            point.descriptorBindCommandIndex = record->commandIndex;
            point.descriptorContentRevision = binding.contentRevision;
            point.set = slot.set;
            if (const auto* id =
                    projectBoundView(descriptor, sources, budget, point, result, visitor))
                return id;
        }
        for (const auto& slot : layout.descriptorTables) {
            const auto* record = inputs.latestDescriptorTableBinding(
                slot.set, point.pipelineBindCommandIndex, point.commandIndex);
            if (!record)
                return "RHI-ORIGINAL-CONSUMPTION-TABLE-MISSING";
            const auto& binding = std::get<OriginalCommandDescriptorBinding>(record->arguments);
            point.set = slot.set;
            if (const auto* id = projectBoundTable(pipeline,
                    inputs,
                    binding,
                    record->commandIndex,
                    budget,
                    point,
                    result,
                    visitor))
                return id;
        }
        return nullptr;
    }

    template <class Visitor>
    static Result walk(const std::shared_ptr<const Inputs>& inputs,
        const GpuDevice& device,
        const Budget& budget,
        Visitor&& visitor) noexcept
    {
        Result result;
        if ((result.diagnostic = validateBudget(budget)))
            return result;
        if ((result.diagnostic = semantics::OriginalCommandDescriptorConsumptionLowering::lowering(
                 device.backend(), budget.contractVersion)
                    .diagnostic))
            return result;
        if (budget.indexedDrawConsumptionVersion
            && (result.diagnostic =
                    semantics::OriginalCommandDescriptorConsumptionLowering::indexedDrawLowering(
                        device.backend())
                        .diagnostic))
            return result;
        if (budget.vertexStreamConsumptionVersion
            && (result.diagnostic = semantics::OriginalCommandDescriptorConsumptionLowering::
                    vertexStreamShaderLowering(
                        device.backend(), budget.vertexStreamConsumptionVersion)
                        .diagnostic))
            return result;
        if ((result.diagnostic = Inputs::validateOriginal(inputs)))
            return result;
        if (!OriginalCommandDescriptorConsumptionContract::inputsProfileMatches(
                budget.contractVersion,
                budget.generalShaderResourcesVersion,
                budget.generalShaderResourcesOptIn,
                inputs->originalBudget().contractVersion)) {
            result.diagnostic = "RHI-ORIGINAL-CONSUMPTION-INPUTS-VERSION";
            return result;
        }
        const auto& usage = *inputs->originalScalars()->originalUsage();
        const auto& sources = *usage.originalSources();
        if ((result.diagnostic = OriginalCommandResourceSources::validateRegisteredLifetime(
                 usage.originalSources(), device)))
            return result;
        return walkOriginal(inputs, budget, std::forward<Visitor>(visitor));
    }

    // Only the existing state facade may call this after its exact cold device
    // join and subsequent ticket/registration checks. No new proof issuer.
    template <bool Joined = false, class Visitor>
    static Result walkOriginal(const std::shared_ptr<const Inputs>& inputs,
        const Budget& budget,
        Visitor&& visitor) noexcept
    {
        Result result;
        if ((result.diagnostic = validateBudget(budget)))
            return result;
        if constexpr (!Joined)
            if ((result.diagnostic = Inputs::validateOriginal(inputs)))
                return result;
        if (!OriginalCommandDescriptorConsumptionContract::inputsProfileMatches(
                budget.contractVersion,
                budget.generalShaderResourcesVersion,
                budget.generalShaderResourcesOptIn,
                inputs->originalBudget().contractVersion)) {
            result.diagnostic = "RHI-ORIGINAL-CONSUMPTION-INPUTS-VERSION";
            return result;
        }
        const auto& usage = *inputs->originalScalars()->originalUsage();
        const auto& sources = *usage.originalSources();
        if (budget.maxConsumptions > sources.originalBudget().maxCommands) {
            result.diagnostic = "RHI-ORIGINAL-CONSUMPTION-COMMAND-CAPACITY";
            return result;
        }
        semantics::CommandRecordingOrder order(sources.originalBudget().maxDebugDepth);
        Access point;
        for (std::size_t command = 0u; command < usage.commands().size(); ++command) {
            result.commandIndex = command;
            const auto kind = usage.commands()[command].kind;
            if ((result.diagnostic = order.append(kind)))
                return result;
            if (!supported(kind, budget.contractVersion, budget.indexedDrawConsumptionOptIn)
                && !passTimingTimestamp(kind, *inputs, budget.contractVersion)) {
                result.diagnostic = "RHI-ORIGINAL-CONSUMPTION-COMMAND-PROFILE";
                return result;
            }
            if (kind == K::BeginRenderPass || kind == K::BeginComputePass
                || kind == K::BeginTransferPass) {
                point = {};
                point.scopeBeginCommandIndex = command;
            }
            if (kind == K::EndRenderPass || kind == K::EndComputePass || kind == K::EndTransferPass)
                point = {};
            if (OriginalCommandPipelineBindingContract::isPipelineBinding(kind)) {
                const auto* value = inputs->descriptorAt(command);
                const auto* pipeline =
                    value ? std::get_if<OriginalCommandPipelineBinding>(value) : nullptr;
                if (!pipeline) {
                    result.diagnostic = "RHI-ORIGINAL-CONSUMPTION-PIPELINE";
                    return result;
                }
                point.pipelineSourceIndex = pipeline->sourceIndex;
                point.pipelineBindCommandIndex = command;
            }
            if (!isConsumption(kind, budget))
                continue;
            if (result.consumptions == budget.maxConsumptions) {
                result.diagnostic = "RHI-ORIGINAL-CONSUMPTION-CAPACITY";
                return result;
            }
            ++result.consumptions;
            point.commandIndex = command;
            if (point.pipelineSourceIndex >= sources.sources().size()) {
                result.diagnostic = "RHI-ORIGINAL-CONSUMPTION-PIPELINE";
                return result;
            }
            const auto& original = sources.sources()[point.pipelineSourceIndex];
            if (const auto* pipeline =
                    std::get_if<std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(
                        &original))
                result.diagnostic = (kind == K::Draw || kind == K::DrawIndexed)
                    ? consume<Joined>(**pipeline, *inputs, budget, point, result, visitor)
                    : "RHI-ORIGINAL-CONSUMPTION-PIPELINE-KIND";
            else if (const auto* pipeline =
                         std::get_if<std::shared_ptr<const OriginalCommandComputePipelineSource>>(
                             &original))
                result.diagnostic = kind == K::Dispatch
                    ? consume<Joined>(**pipeline, *inputs, budget, point, result, visitor)
                    : "RHI-ORIGINAL-CONSUMPTION-PIPELINE-KIND";
            else
                result.diagnostic = "RHI-ORIGINAL-CONSUMPTION-PIPELINE";
            if (result.diagnostic)
                return result;
        }
        if (!order.complete())
            result.diagnostic = "RHI-COMMAND-ORDER-INCOMPLETE";
        result.commandIndex = usage.commands().size();
        return result;
    }
};
} // namespace arp::rhi::common

// endregion Command descriptor consumption
