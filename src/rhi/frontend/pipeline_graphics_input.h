#pragma once
#include "core/ownership/shared_owner_projection.h"
#include "core/ownership/versioned_shared_owner.h"
#include "rhi/frontend/pipeline_definition_identity.h"
#include "rhi/frontend/pipeline_depth_table_lowering_validation.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/pipeline_creation_lowering.h"
#include "shader/graphics_shader_module_artifact.h"

namespace arp::rhi {
// GraphicsModule/1 selects exactly Vertex+Fragment. Combined native targets
// and two selected SPIR-V children retain their distinct authored identities.
class ARP_API OriginalGraphicsPipelineInput final : public core::QuotaBoundSharedObjectIdentity {
    friend class OriginalDepthTableExpansionInputContract;

    class ConstructionKey {
        friend class OriginalGraphicsPipelineInput;
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

    using ExpansionBudget = OriginalDepthTableExpansionInputContract::Budget;

    struct Target {
        std::array<const core::ByteBuffer*, 2> sources{};
        std::uint32_t sourceCount = 0u;
        bool combinedModule = false, validatorOnly = false;
        const char* diagnostic = nullptr;
    };

    OriginalGraphicsPipelineInput(ConstructionKey,
        Budget budget,
        std::shared_ptr<const shader::GraphicsShaderModuleArtifact> module) noexcept
        : owner_(budget, std::move(module))
    {}

    OriginalGraphicsPipelineInput(ConstructionKey,
        ExpansionBudget budget,
        std::shared_ptr<const shader::OriginalDepthTableExpansionArtifact> expansion) noexcept
        : owner_(Budget{3u,
                     budget.maxCpuMetadataBytes,
                     budget.expansionContractVersion,
                     budget.prototypeOptIn},
              std::move(expansion))
    {}

    static std::shared_ptr<const OriginalGraphicsPipelineInput> prepare(
        const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String& diagnostic);
    static std::shared_ptr<const OriginalGraphicsPipelineInput> prepareTableExpansion(
        const std::shared_ptr<const shader::OriginalDepthTableExpansionArtifact>&,
        ExpansionBudget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String& diagnostic);

    static const char* validateTableExpansion(
        const std::shared_ptr<const OriginalGraphicsPipelineInput>& self) noexcept
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
            shader::OriginalDepthTableExpansionArtifact::Profile::Graphics);
    }

    static const char* tableExpansionPackageDiagnostic(
        const std::shared_ptr<const OriginalGraphicsPipelineInput>& self,
        const ShaderPackageDesc& candidate) noexcept
    {
        if (const auto* id = validateTableExpansion(self))
            return id;
        return PipelineDefinitionIdentity::matches(self->package_, candidate)
            ? nullptr
            : "RHI-ORIGINAL-TABLE-INPUT-PACKAGE-DEFINITION";
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalGraphicsPipelineInput>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-GRAPHICS-INPUT-OWNER";
        if (self->quota()->limitBytes() != self->owner_.request().maxCpuMetadataBytes)
            return "RHI-ORIGINAL-GRAPHICS-INPUT-QUOTA-LIMIT";
        if (self->owner_.request().contractVersion == 3u)
            return OriginalDepthTableExpansionInputContract::ordinaryProfileDiagnostic;
        return self->owner_.request().contractVersion == 2u
            ? shader::GraphicsShaderModuleArtifact::runtimeLengthAvailableTargetDiagnostic(
                  self->artifact())
            : shader::GraphicsShaderModuleArtifact::validateOriginal(self->artifact());
    }

    // Exact immutable package equality paired with the original input. Caller
    // copies alone grant no policy, creation or native execution authority.
    static const char* packageDiagnostic(
        const std::shared_ptr<const OriginalGraphicsPipelineInput>& self,
        const ShaderPackageDesc& candidate) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        return PipelineDefinitionIdentity::matches(self->package_, candidate)
            ? nullptr
            : "RHI-ORIGINAL-GRAPHICS-INPUT-PACKAGE-DEFINITION";
    }

    static Target selectTarget(
        const std::shared_ptr<const OriginalGraphicsPipelineInput>& self, Backend backend) noexcept
    {
        Target result;
        if ((result.diagnostic = validateOriginal(self)))
            return result;
        using Role = shader::ShaderArtifactRole;
        const auto selected = semantics::PipelineCreationLowering::artifactRole(backend);
        if (selected == semantics::PipelineCreationLowering::ArtifactRole::ValidatorOnly) {
            result.validatorOnly = true;
            return result;
        }
        if (selected == semantics::PipelineCreationLowering::ArtifactRole::Spirv) {
            const auto& module = self->artifact();
            const auto& first = module->vertex();
            const auto& second = module->fragment();
            for (const auto& child : std::array{first, second})
                if (!child->available(Role::Spirv)) {
                    const auto& id = child->targetDiagnostic(Role::Spirv);
                    result.diagnostic =
                        id.empty() ? "RHI-ORIGINAL-GRAPHICS-INPUT-TARGET-MISSING" : id.c_str();
                    return result;
                }
            result.sources = {&first->bytes(Role::Spirv), &second->bytes(Role::Spirv)};
            result.sourceCount = 2u;
            return result;
        }
        Role role;
        switch (selected) {
        case semantics::PipelineCreationLowering::ArtifactRole::Metal:
            role = Role::Metal;
            break;
        case semantics::PipelineCreationLowering::ArtifactRole::Wgsl:
            role = Role::Wgsl;
            break;
        default:
            result.diagnostic = "RHI-ORIGINAL-GRAPHICS-INPUT-BACKEND";
            return result;
        }
        const auto& module = self->artifact();
        const auto& bytes = module->bytes(role);
        if (bytes.empty()) {
            const auto& id = module->targetDiagnostic(role);
            result.diagnostic =
                id.empty() ? "RHI-ORIGINAL-GRAPHICS-INPUT-TARGET-MISSING" : id.c_str();
            return result;
        }
        result.sources[0] = &bytes;
        result.sourceCount = 1u;
        result.combinedModule = true;
        return result;
    }

    const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>& artifact() const noexcept
    {
        return owner_.ordinary();
    }

    const std::shared_ptr<const shader::OriginalDepthTableExpansionArtifact>&
    tableExpansion() const noexcept
    {
        return owner_.expanded();
    }

    const ShaderPackageDesc& package() const noexcept { return package_; }

    const std::shared_ptr<core::CpuMemoryQuota>& quota() const noexcept
    {
        return originalMetadataQuota();
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const core::VersionedSharedOwner<Budget,
        shader::GraphicsShaderModuleArtifact,
        shader::OriginalDepthTableExpansionArtifact,
        3u>
        owner_;
    ShaderPackageDesc package_;
};
} // namespace arp::rhi
