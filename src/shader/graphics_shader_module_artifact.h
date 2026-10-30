#pragma once
#include "core/ownership/shared_owner_projection.h"
#include "shader/single_entry_shader_artifact.h"

namespace arp::shader {
struct GraphicsTargetMemoryLoadRequest {
    std::uint32_t contractVersion = 0u;
    TargetBufferMemoryLoadRequest vertex, fragment;
    BufferDeclarationTypeLayouts::Budget sourceTypes, targetTypes;
    std::size_t maxPairs = 0u;
    // /2 requires explicit combined push binary rules. The child proofs and
    // exact original graphs are still checked independently.
    const PushBinaryMemoryPlan* combinedPushBinary = nullptr;
};

struct GraphicsTargetBufferMemoryLayout {
    std::uint32_t set = 0u, binding = 0u, stageMask = 0u;
    BindingType type = BindingType::UniformBuffer;
    WgslBufferDeclarationLayout target;
    std::uint64_t combinedTargetDeclarationBytes = 0u;
};

struct GraphicsRuntimeLengthOperationLoadRequest {
    std::uint32_t contractVersion = 0u;
    RuntimeLengthOperationLoadRequest vertex, fragment, combined;
    TargetBufferMemoryLoadRequest combinedMemory;
};

struct GraphicsShaderModuleLoadBudget {
    std::uint64_t maxFileBytes = 0u, maxTotalInputBytes = 0u, maxCpuMetadataBytes = 0u;
    std::uint32_t maxModuleBindings = 0u, maxEntryBindings = 0u, maxReferencePathBytes = 0u,
                  maxReferenceDepth = 0u;
    const GraphicsTargetMemoryLoadRequest* targetMemory = nullptr;
    const GraphicsRuntimeLengthOperationLoadRequest* runtimeLength = nullptr;
    // Independently authored selected VS/FS declarations; null preserves the
    // existing creation profile without native member-memory proof.
    std::array<const NativeBufferMemoryLoadRequest*, 2u> nativeMemory{};
    const ShaderArtifactSource* source = nullptr;
};

// Startup-only original combined bytes and selected-entry provenance. This
// publication is not a mathematical/access proof or execution authorization.
class ARP_API GraphicsShaderModuleArtifact final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class GraphicsShaderModuleArtifact;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    explicit GraphicsShaderModuleArtifact(
        ConstructionKey, const GraphicsShaderModuleLoadBudget& budget) noexcept
        : cpuMetadataBytes_(budget.maxCpuMetadataBytes)
    {}

    GraphicsShaderModuleArtifact(const GraphicsShaderModuleArtifact&) = delete;
    GraphicsShaderModuleArtifact& operator=(const GraphicsShaderModuleArtifact&) = delete;
    static std::shared_ptr<const GraphicsShaderModuleArtifact> load(const std::filesystem::path&,
        const GraphicsShaderModuleLoadBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String& diagnostic);

    static const char* validateOriginal(
        const std::shared_ptr<const GraphicsShaderModuleArtifact>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "SHADER-MODULE-ORIGINAL-OWNER";
        if (self->quota()->limitBytes() != self->cpuMetadataBytes_)
            return "SHADER-MODULE-ORIGINAL-QUOTA-LIMIT";
        for (const auto& entry : self->entries_) {
            if (const auto* id = SingleEntryShaderArtifact::validateOriginal(entry))
                return id;
            if (!core::SharedOwnerProjection::sameOwner(entry->quota(), self->quota()))
                return "SHADER-MODULE-ORIGINAL-CHILD-QUOTA";
        }
        return nullptr;
    }

    static const char* runtimeLengthAvailableTargetDiagnostic(
        const std::shared_ptr<const GraphicsShaderModuleArtifact>& self) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        if (self->targetDiagnostic(ShaderArtifactRole::Metal)
                != "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED"
            || !self->bytes(ShaderArtifactRole::Metal).empty()
            || !self->filename(ShaderArtifactRole::Metal).empty()
            || self->bytes(ShaderArtifactRole::Spirv).empty()
            || self->bytes(ShaderArtifactRole::Wgsl).empty() || self->runtimeLengthDiagnostic_
            || self->runtimeLength_.empty() || self->targetMemoryDiagnostic_)
            return "SHADER-MODULE-RUNTIME-LENGTH-AVAILABLE-TARGET-PROFILE";
        for (const auto& record : self->targetMemoryRecords_)
            if (record.diagnostic)
                return record.diagnostic;
        if (self->module_.reflection.pushConstantSizeBytes && self->targetPushDiagnostic_)
            return self->targetPushDiagnostic_;
        for (const auto& child : self->entries_)
            if (const auto* id =
                    SingleEntryShaderArtifact::runtimeLengthAvailableTargetDiagnostic(child))
                return id;
        return nullptr;
    }

    const core::ByteBuffer& bytes(ShaderArtifactRole role) const
    {
        return files_.at(static_cast<std::size_t>(role)).bytes;
    }

    const core::pmr::String& filename(ShaderArtifactRole role) const
    {
        return files_.at(static_cast<std::size_t>(role)).name;
    }

    const auto& identity() const noexcept { return identity_; }

    const core::ByteBuffer& publicationBytes() const noexcept { return publication_; }

    const auto& vertex() const noexcept { return entries_[0]; }

    const auto& fragment() const noexcept { return entries_[1]; }

    const ShaderReflectionModule& module() const noexcept { return module_; }

    const core::pmr::String& targetDiagnostic(ShaderArtifactRole role) const
    {
        return files_.at(static_cast<std::size_t>(role)).diagnostic;
    }

    const std::shared_ptr<core::CpuMemoryQuota>& quota() const noexcept
    {
        return originalMetadataQuota();
    }

    bool nativeExecutionProven() const noexcept { return false; }

    std::uint64_t inputBytes() const noexcept { return inputBytes_; }

    static const char* originalTargetBufferMemory(
        const std::shared_ptr<const GraphicsShaderModuleArtifact>& self,
        std::uint32_t set,
        std::uint32_t binding,
        GraphicsTargetBufferMemoryLayout& output) noexcept
    {
        output = {};
        if (const auto* id = validateOriginal(self))
            return id;
        if (self->targetMemoryDiagnostic_)
            return self->targetMemoryDiagnostic_;
        for (const auto& record : self->targetMemoryRecords_)
            if (record.facts.set == set && record.facts.binding == binding) {
                if (record.diagnostic)
                    return record.diagnostic;
                output = record.facts;
                return nullptr;
            }
        return "SHADER-MODULE-TARGET-MEMORY-BINDING";
    }

    static const char* originalTargetPushMemory(
        const std::shared_ptr<const GraphicsShaderModuleArtifact>& self,
        TargetPushMemoryLayout& output) noexcept
    {
        output = {};
        if (const auto* id = validateOriginal(self))
            return id;
        if (self->targetMemoryDiagnostic_)
            return self->targetMemoryDiagnostic_;
        if (self->targetPushDiagnostic_)
            return self->targetPushDiagnostic_;
        output = self->targetPushFacts_;
        return nullptr;
    }

    static const char* originalRuntimeLengthOperation(
        const std::shared_ptr<const GraphicsShaderModuleArtifact>& self,
        std::size_t ordinal,
        RuntimeArrayLengthOperations::Facts& output) noexcept
    {
        output = {};
        if (const auto* id = validateOriginal(self))
            return id;
        if (self->runtimeLengthDiagnostic_)
            return self->runtimeLengthDiagnostic_;
        if (ordinal >= self->runtimeLength_.size())
            return "SHADER-MODULE-RUNTIME-LENGTH-ORDINAL";
        output = self->runtimeLength_[ordinal];
        return nullptr;
    }

    static const char* originalRuntimeLengthOperationCount(
        const std::shared_ptr<const GraphicsShaderModuleArtifact>& self,
        std::size_t& output) noexcept
    {
        output = 0u;
        if (const auto* id = validateOriginal(self))
            return id;
        if (self->runtimeLengthDiagnostic_)
            return self->runtimeLengthDiagnostic_;
        output = self->runtimeLength_.size();
        return nullptr;
    }

private:
    const char* buildTargetMemory(
        const GraphicsShaderModuleLoadBudget&, const GraphicsTargetMemoryLoadRequest&);
    void buildRuntimeLength(const GraphicsShaderModuleLoadBudget&,
        const GraphicsRuntimeLengthOperationLoadRequest&,
        bool unavailableMetal);

    struct TargetMemoryRecord {
        GraphicsTargetBufferMemoryLayout facts;
        const char* diagnostic = nullptr;
    };

    ShaderArtifactFiles files_;
    ShaderReflectionModule module_;
    core::ByteBuffer identity_{&core::defaultCpuMemoryResource()};
    core::ByteBuffer publication_{&core::defaultCpuMemoryResource()};
    std::array<std::shared_ptr<const SingleEntryShaderArtifact>, 2> entries_;
    BufferDeclarationTypeLayouts sourceTypes_, targetTypes_;
    core::pmr::Vector<TargetMemoryRecord> targetMemoryRecords_{&core::defaultCpuMemoryResource()};
    TargetPushMemoryLayout targetPushFacts_;
    const char* targetMemoryDiagnostic_ = "SHADER-MODULE-TARGET-MEMORY-NOT-REQUESTED";
    const char* targetPushDiagnostic_ = "SHADER-MODULE-TARGET-PUSH-ABSENT";
    core::pmr::Vector<RuntimeArrayLengthOperations::Facts> runtimeLength_{
        &core::defaultCpuMemoryResource()};
    const char* runtimeLengthDiagnostic_ = "SHADER-MODULE-RUNTIME-LENGTH-NOT-REQUESTED";
    std::uint64_t inputBytes_ = 0u;
    const std::uint64_t cpuMetadataBytes_;
};
} // namespace arp::shader
