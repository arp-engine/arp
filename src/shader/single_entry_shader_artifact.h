#pragma once

#include "core/ownership/quota_bound_shared_identity.h"
#include "shader/runtime_array_length_operations.h"
#include "shader/shader_artifact_files.h"
#include "shader/shader_reflection_cache.h"
#include "shader/spirv_buffer_declaration_layout.h"
#include "shader/target_buffer_memory_declarations.h"

#include <array>
#include <filesystem>

namespace arp::shader {

struct SingleEntryShaderLoadBudget {
    std::uint64_t maxFileBytes = 0u;
    std::uint64_t maxTotalBytes = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    std::uint32_t maxBindings = 0u;
    const TargetBufferMemoryLoadRequest* targetMemory = nullptr;
    const RuntimeLengthOperationLoadRequest* runtimeLength = nullptr;
};

// Startup-only immutable identity; no pipeline/execution authorization.
class ARP_API SingleEntryShaderArtifact final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class SingleEntryShaderArtifact;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    explicit SingleEntryShaderArtifact(
        ConstructionKey, const SingleEntryShaderLoadBudget& budget) noexcept
        : cpuMetadataBytes_(budget.maxCpuMetadataBytes)
    {}

    SingleEntryShaderArtifact(const SingleEntryShaderArtifact&) = delete;
    SingleEntryShaderArtifact& operator=(const SingleEntryShaderArtifact&) = delete;
    static std::shared_ptr<const SingleEntryShaderArtifact> load(
        const std::filesystem::path& publication,
        const SingleEntryShaderLoadBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String& diagnostic);

    static const char* validateOriginal(
        const std::shared_ptr<const SingleEntryShaderArtifact>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "SHADER-ENTRY-ORIGINAL-OWNER";
        return self->quota()->limitBytes() == self->cpuMetadataBytes_
            ? nullptr
            : "SHADER-ENTRY-ORIGINAL-QUOTA-LIMIT";
    }

    static const char* runtimeLengthAvailableTargetDiagnostic(
        const std::shared_ptr<const SingleEntryShaderArtifact>& self) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        if (self->targetDiagnostic(ShaderArtifactRole::Metal)
                != "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED"
            || self->available(ShaderArtifactRole::Metal)
            || !self->filename(ShaderArtifactRole::Metal).empty()
            || !self->available(ShaderArtifactRole::Spirv)
            || !self->available(ShaderArtifactRole::Wgsl) || self->runtimeLengthDiagnostic_
            || self->runtimeLength_.empty() || self->targetMemory_.diagnostic())
            return "SHADER-ENTRY-RUNTIME-LENGTH-AVAILABLE-TARGET-PROFILE";
        for (const auto& record : self->targetMemory_.records())
            if (record.diagnostic)
                return record.diagnostic;
        if (self->module_.reflection.pushConstantSizeBytes && self->targetMemory_.pushDiagnostic())
            return self->targetMemory_.pushDiagnostic();
        return nullptr;
    }

    const core::pmr::String& entry() const noexcept { return entry_; }

    ShaderStage stage() const noexcept { return stage_; }

    const ShaderReflectionModule& module() const noexcept { return module_; }

    const core::ByteBuffer& identity() const noexcept { return identity_; }

    const core::ByteBuffer& publicationBytes() const noexcept { return publication_; }

    const core::ByteBuffer& bytes(ShaderArtifactRole role) const
    {
        return files_.at(static_cast<std::size_t>(role)).bytes;
    }

    const core::pmr::String& filename(ShaderArtifactRole role) const
    {
        return files_.at(static_cast<std::size_t>(role)).name;
    }

    bool available(ShaderArtifactRole role) const { return !bytes(role).empty(); }

    const core::pmr::String& targetDiagnostic(ShaderArtifactRole role) const
    {
        return files_.at(static_cast<std::size_t>(role)).diagnostic;
    }

    std::uint64_t inputBytes() const noexcept { return inputBytes_; }

    const std::shared_ptr<core::CpuMemoryQuota>& quota() const noexcept
    {
        return originalMetadataQuota();
    }

    bool nativeExecutionProven() const noexcept { return false; }

    // Original-owner, memory-only lookup of immutable binary declaration facts.
    // A caller copy carries no target binding minimum or execution authority.
    static const char* originalBufferDeclarationLayout(
        const std::shared_ptr<const SingleEntryShaderArtifact>& self,
        std::uint32_t set,
        std::uint32_t binding,
        SpirvBufferDeclarationLayout& output) noexcept
    {
        output = {};
        if (const auto* id = validateOriginal(self))
            return id;
        if (!self->bufferDeclarationsAvailable_)
            return "SHADER-ENTRY-BUFFER-LAYOUT-UNAVAILABLE";
        for (const auto& facts : self->bufferDeclarations_)
            if (facts.set == set && facts.binding == binding) {
                output = facts;
                return nullptr;
            }
        return "SHADER-ENTRY-BUFFER-LAYOUT-BINDING";
    }

    bool bufferDeclarationLayoutsAvailable() const noexcept { return bufferDeclarationsAvailable_; }

    static const char* originalTargetBufferMemory(
        const std::shared_ptr<const SingleEntryShaderArtifact>& self,
        std::uint32_t set,
        std::uint32_t binding,
        TargetBufferMemoryLayout& output) noexcept
    {
        output = {};
        if (const auto* id = validateOriginal(self))
            return id;
        return self->targetMemory_.lookup(set, binding, output);
    }

    static const char* originalTargetPushMemory(
        const std::shared_ptr<const SingleEntryShaderArtifact>& self,
        TargetPushMemoryLayout& output) noexcept
    {
        output = {};
        if (const auto* id = validateOriginal(self))
            return id;
        return self->targetMemory_.lookupPush(output);
    }

    static const char* originalRuntimeLengthOperation(
        const std::shared_ptr<const SingleEntryShaderArtifact>& self,
        std::size_t ordinal,
        RuntimeArrayLengthOperations::Facts& output) noexcept
    {
        output = {};
        if (const auto* id = validateOriginal(self))
            return id;
        if (self->runtimeLengthDiagnostic_)
            return self->runtimeLengthDiagnostic_;
        if (ordinal >= self->runtimeLength_.size())
            return "SHADER-ENTRY-RUNTIME-LENGTH-ORDINAL";
        output = self->runtimeLength_[ordinal];
        return nullptr;
    }

    static const char* originalRuntimeLengthOperationCount(
        const std::shared_ptr<const SingleEntryShaderArtifact>& self, std::size_t& output) noexcept
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
    friend class GraphicsShaderModuleArtifact;
    ShaderArtifactFiles files_;
    core::pmr::String entry_{&core::defaultCpuMemoryResource()};
    ShaderStage stage_ = ShaderStage::Vertex;
    ShaderReflectionModule module_;
    core::ByteBuffer identity_{&core::defaultCpuMemoryResource()};
    core::ByteBuffer publication_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SpirvBufferDeclarationLayout> bufferDeclarations_{
        &core::defaultCpuMemoryResource()};
    bool bufferDeclarationsAvailable_ = false;
    TargetBufferMemoryDeclarations targetMemory_;
    core::pmr::Vector<RuntimeArrayLengthOperations::Facts> runtimeLength_{
        &core::defaultCpuMemoryResource()};
    const char* runtimeLengthDiagnostic_ = "SHADER-ENTRY-RUNTIME-LENGTH-NOT-REQUESTED";
    std::uint64_t inputBytes_ = 0u;
    const std::uint64_t cpuMetadataBytes_;
};

} // namespace arp::shader
