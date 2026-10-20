#pragma once
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/descriptor_lowering.h"
#include "shader/graphics_shader_module_artifact.h"

#include <array>
#include <type_traits>

// region original_target_buffer_declaration
namespace arp::rhi {
// Shared immutable original declaration join only. No operation, length,
// descriptor write or command authority follows from these copied facts.
class OriginalTargetBufferDeclarationContract final {
public:
    struct Facts {
        shader::WgslBufferDeclarationLayout target;
        std::uint32_t stageMask = 0u;
    };

    template <class Artifact>
    static const char* diagnose(const std::shared_ptr<const Artifact>& original,
        std::uint32_t set,
        std::uint32_t binding,
        DescriptorType type,
        Facts& output) noexcept
    {
        output = {};
        Facts facts;
        if constexpr (std::is_same_v<Artifact, shader::SingleEntryShaderArtifact>) {
            shader::TargetBufferMemoryLayout memory;
            if (const auto* id =
                    Artifact::originalTargetBufferMemory(original, set, binding, memory))
                return id;
            const auto converted = descriptorTypeFromShaderBinding(memory.type);
            if (!converted || *converted != type)
                return "RHI-ORIGINAL-RUNTIME-INTERVAL-TYPE";
            const auto stage = shaderStageFromCompilerStage(original->stage());
            if (!stage)
                return "RHI-ORIGINAL-RUNTIME-INTERVAL-STAGE";
            facts.target = memory.target;
            facts.stageMask = static_cast<std::uint32_t>(*stage);
            if (memory.source.hasRuntimeArray != facts.target.hasRuntimeArray
                || (memory.source.hasRuntimeArray
                    && (memory.source.runtimeArrayOffsetBytes
                            != facts.target.runtimeArrayOffsetBytes
                        || memory.source.runtimeArrayStrideBytes
                            != facts.target.runtimeArrayStrideBytes)))
                return "RHI-ORIGINAL-RUNTIME-INTERVAL-SOURCE-MEMBER";
        } else {
            static_assert(std::is_same_v<Artifact, shader::GraphicsShaderModuleArtifact>);
            shader::GraphicsTargetBufferMemoryLayout memory;
            if (const auto* id =
                    Artifact::originalTargetBufferMemory(original, set, binding, memory))
                return id;
            const auto converted = descriptorTypeFromShaderBinding(memory.type);
            if (!converted || *converted != type)
                return "RHI-ORIGINAL-RUNTIME-INTERVAL-TYPE";
            facts.target = memory.target;
            facts.stageMask = memory.stageMask;
            for (const auto& child : std::array{original->vertex(), original->fragment()}) {
                if (!child)
                    continue;
                const auto stage = shaderStageFromCompilerStage(child->stage());
                if (!stage)
                    return "RHI-ORIGINAL-RUNTIME-INTERVAL-STAGE";
                if (!(facts.stageMask & static_cast<std::uint32_t>(*stage)))
                    continue;
                Facts selected;
                if (const auto* id = diagnose(child, set, binding, type, selected))
                    return id;
                if (selected.target.group != facts.target.group
                    || selected.target.binding != facts.target.binding
                    || selected.target.runtimeArrayOffsetBytes
                        != facts.target.runtimeArrayOffsetBytes
                    || selected.target.runtimeArrayStrideBytes
                        != facts.target.runtimeArrayStrideBytes
                    || selected.target.minimumBindingBytes > facts.target.minimumBindingBytes)
                    return "RHI-ORIGINAL-RUNTIME-INTERVAL-STAGE-MEMBER";
            }
        }
        if (!facts.stageMask || !facts.target.minimumBindingBytes
            || (facts.target.hasRuntimeArray && !facts.target.runtimeArrayStrideBytes))
            return "RHI-ORIGINAL-RUNTIME-INTERVAL-TARGET-MEMBER";
        output = facts;
        return nullptr;
    }
};

// endregion original_target_buffer_declaration

// region original_target_memory_interval

// Independent diagnostic prerequisite. No finite candidate, runtime-length
// operation, Source custody or execution may be issued by this contract.
class OriginalTargetMemoryIntervalContract final {
public:
    struct Request {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };

    struct Observation {
        DescriptorBufferBindingRangeContract::Resolved range;
        std::uint64_t minimumBindingBytes = 0u, runtimeStrideBytes = 0u;
        std::uint32_t group = 0u, binding = 0u, stageMask = 0u;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr const char* validateIntent(const Request& r) noexcept
    {
        if (r.contractVersion != 1u && r.contractVersion != 2u)
            return "RHI-ORIGINAL-TARGET-INTERVAL-VERSION";
        return r.prototypeOptIn ? nullptr : "RHI-ORIGINAL-TARGET-INTERVAL-OPT-IN";
    }

    static constexpr const char* validateType(
        DescriptorType type, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return "RHI-ORIGINAL-TARGET-INTERVAL-VERSION";
        return type == DescriptorType::StorageBuffer
                || type == DescriptorType::ReadOnlyStorageBuffer
                || (version == 2u && type == DescriptorType::UniformBuffer)
            ? nullptr
            : "RHI-ORIGINAL-TARGET-INTERVAL-TYPE";
    }

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, diagnosticAvailable = false, writeAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-TARGET-INTERVAL-BACKEND";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        return semantics::DescriptorLowering::OriginalTargetMemoryInterval_lowering<Decision>(
            backend);
    }
};

// endregion original_target_memory_interval

// region original_runtime_length_interval

// Stateless original declaration/operation join. Outputs are metadata only.
class OriginalRuntimeLengthIntervalContract final {
public:
    using Facts = OriginalTargetBufferDeclarationContract::Facts;

    template <class Artifact>
    static const char* diagnose(const std::shared_ptr<const Artifact>& original,
        std::uint32_t set,
        std::uint32_t binding,
        DescriptorType type,
        Facts& output) noexcept
    {
        return diagnoseImpl(original, set, binding, type, output, true);
    }

    // Complete declared binding in the same diagnosed original profile. A
    // writable output can have no length operation; it still needs its full
    // original source/target declaration. This does not generalize profiles.
    template <class Artifact>
    static const char* diagnoseBinding(const std::shared_ptr<const Artifact>& original,
        std::uint32_t set,
        std::uint32_t binding,
        DescriptorType type,
        Facts& output) noexcept
    {
        return diagnoseImpl(original, set, binding, type, output, false);
    }

private:
    template <class Artifact>
    static const char* diagnoseImpl(const std::shared_ptr<const Artifact>& original,
        std::uint32_t set,
        std::uint32_t binding,
        DescriptorType type,
        Facts& output,
        bool requireOperation) noexcept
    {
        output = {};
        if (const auto* id = Artifact::runtimeLengthAvailableTargetDiagnostic(original))
            return id;
        Facts facts;
        if (const auto* id = OriginalTargetBufferDeclarationContract::diagnose(
                original, set, binding, type, facts))
            return id;
        if (requireOperation && !facts.target.hasRuntimeArray)
            return "RHI-ORIGINAL-RUNTIME-INTERVAL-TARGET-MEMBER";
        if constexpr (std::is_same_v<Artifact, shader::GraphicsShaderModuleArtifact>) {
            // Every selected child still proves its original operation profile.
            for (const auto& child : std::array{original->vertex(), original->fragment()}) {
                if (!child)
                    continue;
                const auto stage = shaderStageFromCompilerStage(child->stage());
                if (!stage)
                    return "RHI-ORIGINAL-RUNTIME-INTERVAL-STAGE";
                if (!(facts.stageMask & static_cast<std::uint32_t>(*stage)))
                    continue;
                Facts selected;
                if (const auto* id =
                        diagnoseImpl(child, set, binding, type, selected, requireOperation))
                    return id;
            }
        }
        std::size_t count = 0u;
        if (const auto* id = Artifact::originalRuntimeLengthOperationCount(original, count))
            return id;
        bool matched = false;
        for (std::size_t i = 0u; i < count; ++i) {
            shader::RuntimeArrayLengthOperations::Facts operation;
            if (const auto* id = Artifact::originalRuntimeLengthOperation(original, i, operation))
                return id;
            if (operation.set != set || operation.binding != binding)
                continue;
            if (operation.group != facts.target.group
                || operation.targetBinding != facts.target.binding
                || operation.runtimeOffsetBytes != facts.target.runtimeArrayOffsetBytes
                || operation.runtimeStrideBytes != facts.target.runtimeArrayStrideBytes)
                return "RHI-ORIGINAL-RUNTIME-INTERVAL-OPERATION-MEMBER";
            matched = true;
        }
        if (requireOperation && !matched)
            return "RHI-ORIGINAL-RUNTIME-INTERVAL-OPERATION-MISSING";
        output = facts;
        return nullptr;
    }
};
} // namespace arp::rhi

// endregion original_runtime_length_interval
