#pragma once
#include "shader/shader_artifact_files.h"

#include <algorithm>

namespace arp::shader {
// Explicit raw YAML checks before invoking the legacy reflection cache parser.
class ShaderArtifactReflectionContract final {
    using Y = core::YamlContract;

    static bool knownBindingType(core::StringView type) noexcept
    {
        return type == "UniformBuffer" || type == "StorageBuffer" || type == "ReadOnlyStorageBuffer"
            || type == "SampledTexture" || type == "StorageTexture" || type == "Sampler"
            || type == "InputAttachment";
    }

public:
    static void validateUnavailableMetalRuntimeLength(const Y::Node& reflection)
    {
        const auto require = [](bool valid) {
            requireShaderArtifact(valid, "SHADER-ENTRY-METAL-LENGTH-DIAGNOSTIC-REFLECTION");
        };
        require(Y::required(Y::required(reflection, "targets"), "metal").is_null()
            && Y::text(reflection, "metalBindingSpaceUnavailableReason")
                == "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED");
        for (const auto* table :
            {"metalBindingSpace", "metalArgumentBuffer", "metalFramebufferFetch"}) {
            core::pmr::String status(table, &core::defaultCpuMemoryResource());
            status += "Status";
            core::pmr::String version(table, &core::defaultCpuMemoryResource());
            version += "ContractVersion";
            core::pmr::String reason(table, &core::defaultCpuMemoryResource());
            reason += "UnavailableReason";
            require(Y::sequence(reflection, table).empty()
                && Y::text(reflection, status.c_str()) == "Unavailable"
                && Y::required(reflection, version.c_str()).is_null()
                && Y::text(reflection, reason.c_str())
                    == "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED");
        }
        for (const auto* field : {"pushConstantMetalBufferIndex",
                 "metalGpuCountedIndirectContractVersion",
                 "metalGpuCountedIndirectCountBufferIndex",
                 "metalTessellationContractVersion",
                 "metalTessellationVertexEntryPoint",
                 "metalTessellationFactorBufferIndex",
                 "metalTessellationPatchType",
                 "metalTessellationFactorFormat",
                 "metalTessellationFactorBytesPerPatch",
                 "metalTessellationFactorInstanceStrideBytes"})
            require(Y::required(reflection, field).is_null());
        require(Y::text(reflection, "metalGpuCountedIndirectStatus") == "Unavailable"
            && Y::text(reflection, "metalTessellationStatus") == "Unavailable"
            && !Y::boolean(reflection, "metalGpuCountedIndirectUsesDrawId")
            && !Y::boolean(reflection, "metalGpuCountedIndirectMasksOutOfRange")
            && !Y::uint32(reflection, "metalTessellationControlPointCount"));
    }

    static void validateBindings(const Y::Node& reflectionNode,
        std::uint32_t maxBindings,
        std::uint32_t mask,
        bool exactMask)
    {
        const auto& bindings = Y::sequence(reflectionNode, "bindings");
        requireShaderArtifact(bindings.size() <= maxBindings, "SHADER-ENTRY-BINDING-BUDGET");
        for (std::size_t i = 0u; i < bindings.size(); ++i) {
            const auto& binding = bindings[i];
            const auto type = Y::text(binding, "type");
            requireShaderArtifact(knownBindingType(type), "SHADER-ENTRY-BINDING-TYPE");
            if (type == "SampledTexture" || type == "InputAttachment")
                Y::boolean(binding, "depthTexture");
            if (type == "InputAttachment")
                Y::uint32(binding, "attachmentIndex");
            requireShaderArtifact(!Y::text(binding, "name").empty()
                    && Y::uint32(binding, "arrayCount") != 0u
                    && (exactMask ? Y::uint32(binding, "stageMask") == mask
                                  : (Y::uint32(binding, "stageMask") != 0u
                                        && (Y::uint32(binding, "stageMask") & ~mask) == 0u)),
                "SHADER-ENTRY-BINDING-SEMANTICS");
            const auto set = Y::uint32(binding, "set"), slot = Y::uint32(binding, "binding");
            for (std::size_t j = 0u; j < i; ++j)
                requireShaderArtifact(set != Y::uint32(bindings[j], "set")
                        || slot != Y::uint32(bindings[j], "binding"),
                    "SHADER-ENTRY-BINDING-DUPLICATE");
        }
    }

    // WebGpuBindingSpace/5 raw metadata. This does not certify target bytes or
    // descriptor-array ABI; it prevents the cache parser from inventing facts.
    static void validateWebGpuPhysicalTable(
        const Y::Node& reflectionNode, std::uint32_t maxBindings)
    {
        const auto& mappings = Y::sequence(reflectionNode, "webGpuBindingSpace");
        const auto& logical = Y::sequence(reflectionNode, "bindings");
        requireShaderArtifact(mappings.size() <= maxBindings, "SHADER-ENTRY-BINDING-BUDGET");
        const auto groups = Y::uint32(reflectionNode, "webGpuBindingSpaceGroupCount");
        const auto& push = Y::required(reflectionNode, "pushConstantWebGpuBinding");
        const auto status = Y::text(reflectionNode, "webGpuBindingSpaceStatus");
        if (status == "Unavailable") {
            requireShaderArtifact(mappings.empty() && !groups && push.is_null(),
                "SHADER-ENTRY-WEBGPU-UNAVAILABLE-MAP");
            return;
        }
        requireShaderArtifact(status == "Available"
                && Y::text(reflectionNode, "webGpuBindingSpaceContractVersion")
                    == "WebGpuBindingSpace/5",
            "SHADER-ENTRY-WEBGPU-PHYSICAL-CONTRACT");
        std::uint64_t extent = 0u;
        for (std::size_t i = 0u; i < mappings.size(); ++i) {
            const auto& m = mappings[i];
            const auto name = Y::text(m, "name");
            requireShaderArtifact(!name.empty(), "SHADER-ENTRY-PHYSICAL-NAME");
            const auto set = Y::uint32(m, "set"), binding = Y::uint32(m, "binding");
            for (std::size_t j = 0u; j < i; ++j)
                requireShaderArtifact(set != Y::uint32(mappings[j], "set")
                        || binding != Y::uint32(mappings[j], "binding"),
                    "SHADER-ENTRY-WEBGPU-LOGICAL-DUPLICATE");
            if (!Y::boolean(m, "active")) {
                for (const auto* field :
                    {"group", "webGpuBinding", "physicalType", "capacity", "arrayElement"})
                    requireShaderArtifact(
                        !m.contains(field), "SHADER-ENTRY-WEBGPU-INACTIVE-PAYLOAD");
                continue;
            }
            const auto group = Y::uint32(m, "group"), slot = Y::uint32(m, "webGpuBinding");
            requireShaderArtifact(
                group < groups && slot != UINT32_MAX, "SHADER-ENTRY-WEBGPU-PHYSICAL-LOCATION");
            extent = std::max(extent, std::uint64_t(group) + 1u);
            const auto type = Y::text(m, "physicalType");
            requireShaderArtifact(knownBindingType(type), "SHADER-ENTRY-PHYSICAL-TYPE");
            const auto capacity = Y::uint32(m, "capacity"), element = Y::uint32(m, "arrayElement");
            requireShaderArtifact(capacity && !element, "SHADER-ENTRY-WEBGPU-PHYSICAL-CARDINALITY");
            const Y::Node* declaration = nullptr;
            for (const auto& b : logical)
                if (Y::uint32(b, "set") == set && Y::uint32(b, "binding") == binding)
                    declaration = &b;
            requireShaderArtifact(declaration && name == Y::text(*declaration, "name")
                    && capacity == Y::uint32(*declaration, "arrayCount"),
                "SHADER-ENTRY-WEBGPU-LOGICAL-MISMATCH");
            const auto logicalType = Y::text(*declaration, "type");
            requireShaderArtifact(type
                    == (logicalType == "InputAttachment" ? core::StringView("SampledTexture")
                                                         : core::StringView(logicalType)),
                "SHADER-ENTRY-WEBGPU-LOGICAL-TYPE");
            if (logicalType == "InputAttachment")
                requireShaderArtifact(Y::text(m, "loadSemantics") == "CurrentFragment"
                        && !Y::boolean(m, "requiresSampler")
                        && Y::boolean(m, "depthTexture")
                            == Y::boolean(*declaration, "depthTexture"),
                    "SHADER-ENTRY-WEBGPU-INPUT-ATTACHMENT");
            for (std::size_t j = 0u; j < i; ++j)
                if (Y::boolean(mappings[j], "active"))
                    requireShaderArtifact(group != Y::uint32(mappings[j], "group")
                            || slot != Y::uint32(mappings[j], "webGpuBinding"),
                        "SHADER-ENTRY-WEBGPU-PHYSICAL-DUPLICATE");
        }
        for (const auto& b : logical) {
            std::size_t active = 0u;
            for (const auto& m : mappings)
                if (Y::boolean(m, "active") && Y::uint32(m, "set") == Y::uint32(b, "set")
                    && Y::uint32(m, "binding") == Y::uint32(b, "binding"))
                    ++active;
            requireShaderArtifact(active == 1u, "SHADER-ENTRY-WEBGPU-LOGICAL-COVERAGE");
        }
        const auto pushBytes = Y::uint32(reflectionNode, "pushConstantSizeBytes");
        if (pushBytes) {
            requireShaderArtifact(push.is_mapping() && push.size() == 2u
                    && Y::text(reflectionNode, "pushConstantContractVersion")
                        == "AtomicFullBlockPush/1",
                "SHADER-ENTRY-WEBGPU-PUSH-MAPPING");
            const auto group = Y::uint32(push, "group"), slot = Y::uint32(push, "binding");
            requireShaderArtifact(
                group < groups && slot != UINT32_MAX, "SHADER-ENTRY-WEBGPU-PHYSICAL-LOCATION");
            extent = std::max(extent, std::uint64_t(group) + 1u);
            for (const auto& m : mappings)
                if (Y::boolean(m, "active"))
                    requireShaderArtifact(
                        group != Y::uint32(m, "group") || slot != Y::uint32(m, "webGpuBinding"),
                        "SHADER-ENTRY-WEBGPU-PUSH-COLLISION");
        } else
            requireShaderArtifact(push.is_null(), "SHADER-ENTRY-WEBGPU-PUSH-MAPPING");
        requireShaderArtifact(extent == groups, "SHADER-ENTRY-WEBGPU-GROUP-EXTENT");
    }

    static void validatePhysicalTables(
        const Y::Node& reflectionNode, std::uint32_t maxBindings, bool strictWebGpu = false)
    {
        // The legacy cache parser accepts missing/unknown physical enums.
        // Seal only explicit physical facts before invoking that parser.
        for (const auto* table :
            {"metalBindingSpace", "metalArgumentBuffer", "webGpuBindingSpace"}) {
            const auto& mappings = Y::sequence(reflectionNode, table);
            requireShaderArtifact(mappings.size() <= maxBindings, "SHADER-ENTRY-BINDING-BUDGET");
            for (const auto& mapping : mappings) {
                requireShaderArtifact(
                    !Y::text(mapping, "name").empty(), "SHADER-ENTRY-PHYSICAL-NAME");
                Y::uint32(mapping, "set");
                Y::uint32(mapping, "binding");
                if (!Y::boolean(mapping, "active"))
                    continue;
                const auto type = Y::text(mapping, "physicalType");
                requireShaderArtifact(knownBindingType(type), "SHADER-ENTRY-PHYSICAL-TYPE");
                if (core::StringView(table) == "metalBindingSpace")
                    Y::uint32(mapping, "metalSlot");
                if (core::StringView(table) == "metalArgumentBuffer") {
                    Y::uint32(mapping, "argumentBufferSlot");
                    Y::uint32(mapping, "argumentIndex");
                    requireShaderArtifact(
                        Y::uint32(mapping, "capacity") != 0u, "SHADER-ENTRY-PHYSICAL-CAPACITY");
                }
            }
        }
        if (strictWebGpu)
            validateWebGpuPhysicalTable(reflectionNode, maxBindings);
    }
};
} // namespace arp::shader
