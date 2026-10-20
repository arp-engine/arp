#pragma once
#include "core/shared_owner_projection.h"
#include "core/versioned_shared_owner.h"
#include "rhi/frontend/pipeline_definition_identity.h"
#include "rhi/frontend/pipeline_depth_table_lowering_validation.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/pipeline_creation_lowering.h"
#include "shader/single_entry_shader_artifact.h"

namespace arp::rhi {
// Cold immutable memory input. No pipeline-consumption/execution authority.
class ARP_API OriginalSingleEntryPipelineInput final : public core::QuotaBoundSharedObjectIdentity {
    friend class OriginalDepthTableExpansionInputContract;

    class ConstructionKey {
        friend class OriginalSingleEntryPipelineInput;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        std::uint64_t maxCpuMetadataBytes = 0u;
        std::uint32_t availableTargetContractVersion = 0u;
        bool prototypeOptIn = false;
    };

    struct Target {
        const core::ByteBuffer* bytes = nullptr;
        const char* diagnostic = nullptr;
        bool validatorOnly = false;
    };

    using ExpansionBudget = OriginalDepthTableExpansionInputContract::Budget;

    explicit OriginalSingleEntryPipelineInput(ConstructionKey,
        Budget budget,
        std::shared_ptr<const shader::SingleEntryShaderArtifact> artifact) noexcept
        : owner_(budget, std::move(artifact))
    {}

    OriginalSingleEntryPipelineInput(ConstructionKey,
        ExpansionBudget budget,
        std::shared_ptr<const shader::OriginalDepthTableExpansionArtifact> expansion) noexcept
        : owner_(Budget{3u,
                     budget.maxCpuMetadataBytes,
                     budget.expansionContractVersion,
                     budget.prototypeOptIn},
              std::move(expansion))
    {}

    OriginalSingleEntryPipelineInput(const OriginalSingleEntryPipelineInput&) = delete;
    OriginalSingleEntryPipelineInput& operator=(const OriginalSingleEntryPipelineInput&) = delete;
    static std::shared_ptr<const OriginalSingleEntryPipelineInput> prepare(
        const std::shared_ptr<const shader::SingleEntryShaderArtifact>&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String& diagnostic);
    static std::shared_ptr<const OriginalSingleEntryPipelineInput> prepareTableExpansion(
        const std::shared_ptr<const shader::OriginalDepthTableExpansionArtifact>&,
        ExpansionBudget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String& diagnostic);

    static const char* validateTableExpansion(
        const std::shared_ptr<const OriginalSingleEntryPipelineInput>& self) noexcept
    {
        if (!self)
            return "RHI-ORIGINAL-TABLE-INPUT-OWNER";
        const ExpansionBudget budget{self->owner_.request().contractVersion,
            self->owner_.request().maxCpuMetadataBytes,
            self->owner_.request().availableTargetContractVersion,
            self->owner_.request().prototypeOptIn};
        return OriginalDepthTableExpansionInputContract::validateOwner(self,
            budget,
            self->tableExpansion(),
            shader::OriginalDepthTableExpansionArtifact::Profile::Compute);
    }

    static const char* tableExpansionPackageDiagnostic(
        const std::shared_ptr<const OriginalSingleEntryPipelineInput>& self,
        const ShaderPackageDesc& candidate) noexcept
    {
        if (const auto* id = validateTableExpansion(self))
            return id;
        return PipelineDefinitionIdentity::matches(self->package_, candidate)
            ? nullptr
            : "RHI-ORIGINAL-TABLE-INPUT-PACKAGE-DEFINITION";
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalSingleEntryPipelineInput>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-ARTIFACT-INPUT-OWNER";
        if (self->originalMetadataQuota()->limitBytes()
            != self->owner_.request().maxCpuMetadataBytes)
            return "RHI-ORIGINAL-ARTIFACT-INPUT-QUOTA-LIMIT";
        if (self->owner_.request().contractVersion == 3u)
            return OriginalDepthTableExpansionInputContract::ordinaryProfileDiagnostic;
        return self->owner_.request().contractVersion == 2u
            ? shader::SingleEntryShaderArtifact::runtimeLengthAvailableTargetDiagnostic(
                  self->artifact())
            : shader::SingleEntryShaderArtifact::validateOriginal(self->artifact());
    }

    // Exact immutable package equality paired with the original input. Caller
    // copies alone grant no policy, creation or native execution authority.
    static const char* packageDiagnostic(
        const std::shared_ptr<const OriginalSingleEntryPipelineInput>& self,
        const ShaderPackageDesc& candidate) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        return PipelineDefinitionIdentity::matches(self->package_, candidate)
            ? nullptr
            : "RHI-ORIGINAL-ARTIFACT-INPUT-PACKAGE-DEFINITION";
    }

    static Target selectTarget(const std::shared_ptr<const OriginalSingleEntryPipelineInput>& self,
        Backend backend) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return {nullptr, id, false};
        using Role = shader::ShaderArtifactRole;
        const auto selected = semantics::PipelineCreationLowering::artifactRole(backend);
        Role role;
        switch (selected) {
        case semantics::PipelineCreationLowering::ArtifactRole::ValidatorOnly:
            return {nullptr, nullptr, true};
        case semantics::PipelineCreationLowering::ArtifactRole::Spirv:
            role = Role::Spirv;
            break;
        case semantics::PipelineCreationLowering::ArtifactRole::Metal:
            role = Role::Metal;
            break;
        case semantics::PipelineCreationLowering::ArtifactRole::Wgsl:
            role = Role::Wgsl;
            break;
        default:
            return {nullptr, "RHI-ORIGINAL-ARTIFACT-INPUT-BACKEND", false};
        }
        const auto& artifact = self->artifact();
        const auto& bytes = artifact->bytes(role);
        if (bytes.empty()) {
            const auto& diagnostic = artifact->targetDiagnostic(role);
            return {nullptr,
                diagnostic.empty() ? "RHI-ORIGINAL-ARTIFACT-INPUT-TARGET-MISSING"
                                   : diagnostic.c_str(),
                false};
        }
        return {&bytes, nullptr, false};
    }

    const std::shared_ptr<const shader::SingleEntryShaderArtifact>& artifact() const noexcept
    {
        return owner_.ordinary();
    }

    const std::shared_ptr<const shader::OriginalDepthTableExpansionArtifact>&
    tableExpansion() const noexcept
    {
        return owner_.expanded();
    }

    const ShaderPackageDesc& package() const noexcept { return package_; }

    const auto& quota() const noexcept { return originalMetadataQuota(); }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const core::VersionedSharedOwner<Budget,
        shader::SingleEntryShaderArtifact,
        shader::OriginalDepthTableExpansionArtifact,
        3u>
        owner_;
    ShaderPackageDesc package_;
};
} // namespace arp::rhi
