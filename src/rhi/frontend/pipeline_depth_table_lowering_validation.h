#pragma once
#include "core/math/unsigned_range.h"
#include "core/ownership/shared_owner_projection.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/pipeline_creation_lowering.h"
#include "shader/original_depth_table_expansion_artifact.h"

// region original_depth_table_expansion_input
namespace arp::rhi {
// OriginalDepthTableExpansionInput/1: selected cold custody, never ordinary
// pipeline admission. Both original input families consume this same request.
class ARP_API OriginalDepthTableExpansionInputContract final {
public:
    using Artifact = shader::OriginalDepthTableExpansionArtifact;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        std::uint64_t maxCpuMetadataBytes = 0u;
        std::uint32_t expansionContractVersion = 0u;
        bool prototypeOptIn = false;
    };

    static constexpr const char* ordinaryProfileDiagnostic =
        "RHI-ORIGINAL-INPUT-TABLE-EXPANSION-PROFILE";

    static const char* validateRequest(const std::shared_ptr<const Artifact>& artifact,
        Budget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        Artifact::Profile profile) noexcept
    {
        if (budget.contractVersion != 3u || budget.expansionContractVersion != 1u)
            return "RHI-ORIGINAL-TABLE-INPUT-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-ORIGINAL-TABLE-INPUT-OPT-IN";
        if (const auto* id = Artifact::validateOriginal(artifact))
            return id;
        if (artifact->profile() != profile)
            return "RHI-ORIGINAL-TABLE-INPUT-PROFILE";
        if (!quota || !budget.maxCpuMetadataBytes
            || quota->limitBytes() != budget.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-TABLE-INPUT-QUOTA";
        if (core::SharedOwnerProjection::sameControlBlock(quota, artifact->quota()))
            return "RHI-ORIGINAL-TABLE-INPUT-QUOTA-DOMAIN";
        return nullptr;
    }

    template <class Input>
    static const char* validateOwner(const std::shared_ptr<const Input>& input,
        Budget budget,
        const std::shared_ptr<const Artifact>& artifact,
        Artifact::Profile profile) noexcept
    {
        if (!input || !input->hasQuotaBoundSharedOwner(input))
            return "RHI-ORIGINAL-TABLE-INPUT-OWNER";
        if (input->originalMetadataQuota()->limitBytes() != budget.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-TABLE-INPUT-QUOTA-LIMIT";
        return validateRequest(artifact, budget, input->originalMetadataQuota(), profile);
    }

    static ValidationResult populateLogicalPackage(const Artifact&, ShaderPackageDesc&);

    template <class Input, class Key>
    static std::shared_ptr<const Input> prepare(const std::shared_ptr<const Artifact>& artifact,
        Budget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        core::pmr::String& diagnostic,
        Artifact::Profile profile,
        Key key)
    {
        diagnostic.clear();
        if (const auto* id = validateRequest(artifact, budget, quota, profile)) {
            diagnostic = id;
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto input = core::makeUnique<Input>(key, budget, artifact);
            const auto valid = populateLogicalPackage(*artifact, input->package_);
            if (!valid.success) {
                diagnostic = valid.diagnostics;
                return {};
            }
            return core::QuotaBoundSharedObjectIdentity::shareOriginal(std::move(input));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-TABLE-INPUT-ALLOCATION";
            return {};
        }
    }
};

// endregion original_depth_table_expansion_input

// region original_depth_table_expansion_pipeline_preflight

// Cold creation prerequisites only. No receipt, capability or execution issuer.
class OriginalDepthTableExpansionPipelinePreflightContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxGroups = 0u, maxBindings = 0u, maxBindingsPerGroup = 0u;
        std::uint32_t maxSampledTexturesPerStage = 0u, maxStorageBuffersPerStage = 0u;
        std::uint32_t maxShaderStages = 0u, maxShaderModules = 0u, maxColorTargets = 0u;
        std::uint64_t maxShaderSourceBytes = 0u;
        std::uint32_t maxVertexBuffers = 0u, maxVertexAttributes = 0u,
                      maxVertexBufferArrayStride = 0u;
    };

    struct Requirements {
        std::uint32_t groups = 0u, bindings = 0u, bindingsPerGroup = 0u;
        std::uint32_t sampledTexturesPerStage = 0u, storageBuffersPerStage = 0u;
        std::uint32_t shaderStages = 0u, shaderModules = 0u, colorTargets = 0u;
        std::uint64_t shaderSourceBytes = 0u;
        bool compute = false;
        std::uint32_t workgroupX = 0u, workgroupY = 0u, workgroupZ = 0u;
        std::uint32_t vertexInputContractVersion = 0u;
        std::uint32_t vertexBuffers = 0u, vertexAttributes = 0u, vertexBufferArrayStride = 0u;
    };

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, preflightEligible = false;
        bool creationAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TABLE-PREFLIGHT-BACKEND";
    };

    static const char* validateRequirements(const Requirements& r) noexcept
    {
        if (r.groups != (r.compute ? 2u : 1u) || r.bindings != (r.compute ? 4u : 3u)
            || r.bindingsPerGroup != 3u || r.sampledTexturesPerStage != 3u
            || r.storageBuffersPerStage != (r.compute ? 1u : 0u)
            || r.shaderStages != (r.compute ? 1u : 2u) || r.shaderModules != r.shaderStages
            || !r.shaderSourceBytes
            || (r.compute ? (!r.workgroupX || !r.workgroupY || !r.workgroupZ || r.colorTargets)
                          : (r.workgroupX || r.workgroupY || r.workgroupZ)))
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-REQUIREMENTS";
        if (r.vertexInputContractVersion > 1u
            || (r.vertexInputContractVersion == 1u
                    ? (r.compute || r.vertexBuffers != 2u || r.vertexAttributes != 3u
                          || r.vertexBufferArrayStride != 24u)
                    : (r.vertexBuffers || r.vertexAttributes || r.vertexBufferArrayStride)))
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-VERTEX-REQUIREMENTS";
        return nullptr;
    }

    // The complete compiler-pinned variant supplies this version. Pipeline
    // authors cannot opt a procedural shader into stream consumption.
    static const char* validateVertexInputs(
        const RenderPipelineDesc& d, std::uint32_t version) noexcept
    {
        if (!version)
            return d.vertexBuffers.empty() ? nullptr
                                           : "RHI-ORIGINAL-TABLE-PREFLIGHT-VERTEX-PROFILE";
        if (version != 1u || d.vertexBuffers.size() != 2u)
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-VERTEX-ABI";
        const auto& vertex = d.vertexBuffers[0u];
        const auto& instance = d.vertexBuffers[1u];
        const auto attribute = [](const VertexAttributeDesc& a,
                                   std::uint32_t location,
                                   std::uint32_t offset,
                                   VertexFormat format) noexcept {
            return a.shaderLocation == location && a.offsetBytes == offset && a.format == format;
        };
        if (vertex.slot != 0u || vertex.strideBytes != 8u
            || vertex.inputRate != VertexInputRate::Vertex || vertex.attributes.size() != 1u
            || instance.slot != 1u || instance.strideBytes != 24u
            || instance.inputRate != VertexInputRate::Instance || instance.attributes.size() != 2u
            || !attribute(vertex.attributes[0u], 0u, 0u, VertexFormat::Float32x2)
            || !attribute(instance.attributes[0u], 1u, 0u, VertexFormat::Float32x2)
            || !attribute(instance.attributes[1u], 2u, 8u, VertexFormat::Float32x4))
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-VERTEX-ABI";
        return nullptr;
    }

    static const char* validateBudget(const Budget& b, const Requirements& r) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-OPT-IN";
        if (const auto* id = validateRequirements(r))
            return id;
        if (r.groups > b.maxGroups || r.bindings > b.maxBindings
            || r.bindingsPerGroup > b.maxBindingsPerGroup
            || r.sampledTexturesPerStage > b.maxSampledTexturesPerStage
            || r.storageBuffersPerStage > b.maxStorageBuffersPerStage
            || r.shaderStages > b.maxShaderStages || r.shaderModules > b.maxShaderModules
            || r.colorTargets > b.maxColorTargets || r.shaderSourceBytes > b.maxShaderSourceBytes)
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-BUDGET";
        if (r.vertexBuffers > b.maxVertexBuffers || r.vertexAttributes > b.maxVertexAttributes
            || r.vertexBufferArrayStride > b.maxVertexBufferArrayStride)
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-VERTEX-BUDGET";
        return nullptr;
    }

    // Limits are query results supplied by backend lowering, not authored intent.
    template <class Limits>
    static const char* validateLimits(const Requirements& r, const Limits& l) noexcept
    {
        if (const auto* id = validateRequirements(r))
            return id;
        if (!l.maxBindGroups || r.groups > l.maxBindGroups)
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-GROUP-LIMIT";
        if (!l.maxDescriptorBindings || r.bindingsPerGroup > l.maxDescriptorBindings)
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-BINDING-LIMIT";
        if (!l.maxSampledTexturesPerShaderStage
            || r.sampledTexturesPerStage > l.maxSampledTexturesPerShaderStage)
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-SAMPLED-LIMIT";
        if (r.storageBuffersPerStage
            && (!l.maxStorageBuffersPerShaderStage
                || r.storageBuffersPerStage > l.maxStorageBuffersPerShaderStage))
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-STORAGE-LIMIT";
        if (r.colorTargets && (!l.maxColorAttachments || r.colorTargets > l.maxColorAttachments))
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-COLOR-LIMIT";
        if (r.vertexBuffers
            && (!l.maxVertexBuffers || r.vertexBuffers > l.maxVertexBuffers
                || !l.maxVertexAttributes || r.vertexAttributes > l.maxVertexAttributes
                || !l.maxVertexBufferArrayStride
                || r.vertexBufferArrayStride > l.maxVertexBufferArrayStride))
            return "RHI-ORIGINAL-TABLE-PREFLIGHT-VERTEX-LIMIT";
        if (r.compute) {
            if (!r.workgroupX || !r.workgroupY || !r.workgroupZ || !l.maxComputeWorkgroupSizeX
                || !l.maxComputeWorkgroupSizeY || !l.maxComputeWorkgroupSizeZ
                || r.workgroupX > l.maxComputeWorkgroupSizeX
                || r.workgroupY > l.maxComputeWorkgroupSizeY
                || r.workgroupZ > l.maxComputeWorkgroupSizeZ)
                return "RHI-ORIGINAL-TABLE-PREFLIGHT-WORKGROUP-LIMIT";
            const auto xy = std::uint64_t{r.workgroupX} * r.workgroupY;
            if (!core::canMultiplyUnsigned(xy, std::uint64_t{r.workgroupZ})
                || !l.maxComputeInvocationsPerWorkgroup
                || xy * r.workgroupZ > l.maxComputeInvocationsPerWorkgroup)
                return "RHI-ORIGINAL-TABLE-PREFLIGHT-INVOCATION-LIMIT";
        }
        return nullptr;
    }

    template <class Facts, class Limits>
    static Decision lowering(
        Backend backend, const Requirements& r, const Facts& f, const Limits& l) noexcept
    {
        return semantics::PipelineCreationLowering::
            OriginalDepthTableExpansionPipelinePreflight_lowering<Decision>(backend,
                r,
                f,
                l,
                [](const Requirements& requirements, const Limits& limits) noexcept {
                    return validateLimits(requirements, limits);
                });
    }
};

// endregion original_depth_table_expansion_pipeline_preflight

// region original_depth_table_expansion_native_emission

class OriginalDepthTableExpansionNativeEmissionContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxTargetBytes = 0u, maxCpuMetadataBytes = 0u;
    };

    static const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-TABLE-EMISSION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-TABLE-EMISSION-OPT-IN";
        if (!b.maxTargetBytes || !b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-TABLE-EMISSION-BUDGET";
        return nullptr;
    }

    static constexpr const char* creationDiagnostic() noexcept
    {
        return "RHI-ORIGINAL-TABLE-EMISSION-CREATION-RECEIPT-MISSING";
    }

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, emissionAvailable = false;
        bool acceptanceAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TABLE-EMISSION-BACKEND";
    };

    static Decision lowering(Backend backend,
        const OriginalDepthTableExpansionPipelinePreflightContract::Decision& preflight) noexcept
    {
        return semantics::PipelineCreationLowering::
            OriginalDepthTableExpansionNativeEmission_lowering<Decision>(backend, preflight);
    }

    // Only declared values are legal, including inactive fields consumed by the
    // shared native state emitter. Profile/state validators remain prerequisites.
    static const char* validateState(const RenderPipelineDesc& d) noexcept
    {
        if (!isSupportedCullMode(d.cullMode))
            return "RHI-ORIGINAL-TABLE-EMISSION-STATE";
        if (!isSupportedFrontFace(d.frontFace))
            return "RHI-ORIGINAL-TABLE-EMISSION-STATE";
        const auto blend = [](const BlendComponent& b) noexcept {
            return b.sourceFactor >= BlendFactor::Zero
                && b.sourceFactor <= BlendFactor::OneMinusDstAlpha
                && b.destinationFactor >= BlendFactor::Zero
                && b.destinationFactor <= BlendFactor::OneMinusDstAlpha
                && b.operation >= BlendOp::Add && b.operation <= BlendOp::Max;
        };
        for (std::uint32_t i = 0u; i < effectiveColorTargetCount(d); ++i) {
            const auto t = effectiveColorTargetAt(d, i);
            if ((static_cast<std::uint32_t>(t.writeMask)
                    & ~static_cast<std::uint32_t>(ColorWriteMask::All))
                || !blend(t.colorBlend) || !blend(t.alphaBlend))
                return "RHI-ORIGINAL-TABLE-EMISSION-STATE";
        }
        const auto face = [](const StencilFaceState& s) noexcept {
            return s.failOp >= StencilOp::Keep && s.failOp <= StencilOp::Invert
                && s.depthFailOp >= StencilOp::Keep && s.depthFailOp <= StencilOp::Invert
                && s.passOp >= StencilOp::Keep && s.passOp <= StencilOp::Invert
                && s.compare >= CompareOp::Never && s.compare <= CompareOp::Always;
        };
        if (d.depthStencil.depthCompare < CompareOp::Never
            || d.depthStencil.depthCompare > CompareOp::Always || !face(d.depthStencil.front)
            || !face(d.depthStencil.back))
            return "RHI-ORIGINAL-TABLE-EMISSION-STATE";
        return nullptr;
    }
};
} // namespace arp::rhi

// endregion original_depth_table_expansion_native_emission
