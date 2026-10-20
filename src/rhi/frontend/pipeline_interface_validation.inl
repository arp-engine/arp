#pragma once

// Header-only L2 pipeline/shader validation definitions, shared by all backends.

namespace arp::rhi {

inline ShaderReflectionBinding makeShaderReflectionBinding(
    const std::uint32_t set, const DescriptorBindingDesc& descriptor)

{
    return {
        set,
        descriptor.binding,
        descriptor.type,
        descriptor.stages,
        descriptor.arrayCount,
        UINT32_MAX,
        descriptor.depthTexture,
    };
}

inline bool hasMetalGpuCountedIndirectMaskAbi(const ShaderReflectionDesc& reflection)

{
    return reflection.metalGpuCountedIndirectContractVersion
        == kMetalGpuCountedIndirectContractVersion
        && reflection.metalGpuCountedIndirectStatus == "Available"
        && reflection.metalGpuCountedIndirectCountBufferIndex < 31u
        && reflection.metalGpuCountedIndirectUsesDrawId
        && reflection.metalGpuCountedIndirectMasksOutOfRange;
}

inline bool hasMetalTessellationAbi(
    const ShaderReflectionDesc& reflection, std::uint32_t patchControlPointCount)

{
    return reflection.metalTessellationContractVersion == kMetalTessellationContractVersion
        && reflection.metalTessellationStatus == "Available"
        && !reflection.metalTessellationVertexEntryPoint.empty()
        && reflection.metalTessellationFactorBufferIndex < 31u
        && reflection.metalTessellationControlPointCount == patchControlPointCount
        && reflection.metalTessellationPatchType <= 1u
        && reflection.metalTessellationFactorFormat <= 1u
        && reflection.metalTessellationFactorBytesPerPatch != 0u
        && reflection.metalTessellationFactorInstanceStrideBytes
        >= reflection.metalTessellationFactorBytesPerPatch;
}

inline ValidationResult validateSubpassInputBindingAgainstReflection(
    const ShaderReflectionDesc& reflection,
    const semantics::SubpassInputBindingReference& reference)

{
    if (!reference.specified()) {
        return {false, "subpass input attachment is missing an explicit shader binding"};
    }
    if (reference.logicalAttachmentIndex == semantics::kInvalidSubpassShaderBinding) {
        return {false, "subpass input attachment is missing a logical attachment index"};
    }
    for (const auto& binding : reflection.descriptorBindings) {
        if (binding.set != reference.set || binding.binding != reference.binding) {
            continue;
        }
        if (binding.type != DescriptorType::InputAttachment) {
            return {false, "subpass shader binding is not reflected as InputAttachment"};
        }
        if (binding.subpassAttachmentIndex != reference.logicalAttachmentIndex) {
            return {false, "subpass shader binding attachment index disagrees with FrameGraph"};
        }
        if (binding.depthTexture != reference.depthTexture) {
            return {false, "subpass shader binding depth/color semantic disagrees with FrameGraph"};
        }
        if (reference.physicalBinding == semantics::SubpassInputPhysicalBinding::Unsupported) {
            return {false, "subpass input attachment has no executable physical binding"};
        }
        return {true, {}};
    }
    return {false, "subpass shader binding is missing from shader reflection"};
}

inline bool hasShaderPathSuffix(const std::pmr::string& path, const std::pmr::string& suffix)

{
    return path.size() >= suffix.size()
        && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::uint64_t hashShaderString(std::uint64_t hash, const std::pmr::string& value)

{
    constexpr std::uint64_t prime = 1099511628211ull;
    for (const auto ch : value) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= prime;
    }
    hash ^= 0xffu;
    hash *= prime;
    return hash;
}

inline std::uint64_t hashShaderValue(std::uint64_t hash, std::uint64_t value)

{
    constexpr std::uint64_t prime = 1099511628211ull;
    for (std::uint32_t byte = 0; byte < 8; ++byte) {
        hash ^= static_cast<unsigned char>((value >> (byte * 8)) & 0xffu);
        hash *= prime;
    }
    return hash;
}

inline std::uint64_t stableShaderPackageHash(const ShaderPackageDesc& desc)

{
    std::uint64_t hash = 1469598103934665603ull;
    hash = hashShaderString(hash, desc.slangModulePath);
    for (const auto& entrypoint : desc.entrypoints) {
        hash = hashShaderString(hash, entrypoint.modulePath);
        hash = hashShaderString(hash, entrypoint.entryPoint);
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(entrypoint.stage));
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(entrypoint.sourceLanguage));
        hash = hashShaderValue(hash, entrypoint.computeWorkgroupSizeX);
        hash = hashShaderValue(hash, entrypoint.computeWorkgroupSizeY);
        hash = hashShaderValue(hash, entrypoint.computeWorkgroupSizeZ);
        hash = hashShaderValue(hash, entrypoint.writesRenderTargetArrayIndex ? 1u : 0u);
    }
    hash = hashShaderString(hash, desc.artifacts.spirvPath);
    hash = hashShaderString(hash, desc.artifacts.mslPath);
    hash = hashShaderString(hash, desc.artifacts.wgslPath);
    hash = hashShaderString(hash, desc.artifacts.reflectionPath);
    hash = hashShaderValue(hash, desc.reflection.pushConstantByteCount);
    hash = hashShaderString(hash, desc.reflection.reflectionContractVersion);
    hash = hashShaderString(hash, desc.reflection.sourceSha256);
    hash = hashShaderString(hash, desc.reflection.bindlessContractVersion);
    hash = hashShaderString(hash, desc.reflection.bindlessExecution);
    hash = hashShaderString(hash, desc.reflection.bindlessLoweringSha256);
    for (const auto& member : desc.reflection.pushConstantMembers) {
        hash = hashShaderString(hash, member.name);
        hash = hashShaderString(hash, member.scalarType);
        hash = hashShaderValue(hash, member.scalarWidth);
        hash = hashShaderValue(hash, member.vecSize);
        hash = hashShaderValue(hash, member.matrixRows);
        hash = hashShaderValue(hash, member.matrixCols);
        hash = hashShaderValue(hash, member.matrixStride);
        hash = hashShaderValue(hash, member.rowMajor ? 1u : 0u);
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(member.arrayDims.size()));
        for (const auto dimension : member.arrayDims) {
            hash = hashShaderValue(hash, dimension);
        }
        hash = hashShaderValue(hash, member.arrayStride);
        hash = hashShaderValue(hash, member.offset);
        hash = hashShaderValue(hash, member.size);
        hash = hashShaderValue(hash, member.decorations);
    }
    // Track A P5: lowering metadata is part of pipeline identity
    hash = hashShaderString(hash, desc.reflection.pushConstantContractVersion);
    hash = hashShaderString(hash, desc.reflection.pushConstantStages);
    hash = hashShaderValue(hash, desc.reflection.pushConstantWebGpuBindingGroup);
    hash = hashShaderValue(hash, desc.reflection.pushConstantWebGpuBindingIndex);
    hash = hashShaderValue(hash, desc.reflection.pushConstantMetalBufferIndex);
    hash = hashShaderValue(hash, desc.reflection.computeWorkgroupSizeX);
    hash = hashShaderValue(hash, desc.reflection.computeWorkgroupSizeY);
    hash = hashShaderValue(hash, desc.reflection.computeWorkgroupSizeZ);
    hash = hashShaderValue(hash, desc.reflection.tessellationControlPointCount);
    hash = hashShaderString(hash, desc.reflection.metalGpuCountedIndirectContractVersion);
    hash = hashShaderString(hash, desc.reflection.metalGpuCountedIndirectStatus);
    hash = hashShaderValue(hash, desc.reflection.metalGpuCountedIndirectCountBufferIndex);
    hash = hashShaderValue(hash, desc.reflection.metalGpuCountedIndirectUsesDrawId ? 1u : 0u);
    hash = hashShaderValue(hash, desc.reflection.metalGpuCountedIndirectMasksOutOfRange ? 1u : 0u);
    hash = hashShaderString(hash, desc.reflection.metalTessellationContractVersion);
    hash = hashShaderString(hash, desc.reflection.metalTessellationStatus);
    hash = hashShaderString(hash, desc.reflection.metalTessellationVertexEntryPoint);
    hash = hashShaderValue(hash, desc.reflection.metalTessellationFactorBufferIndex);
    hash = hashShaderValue(hash, desc.reflection.metalTessellationControlPointCount);
    hash = hashShaderValue(hash, desc.reflection.metalTessellationPatchType);
    hash = hashShaderValue(hash, desc.reflection.metalTessellationFactorFormat);
    hash = hashShaderValue(hash, desc.reflection.metalTessellationFactorBytesPerPatch);
    hash = hashShaderValue(hash, desc.reflection.metalTessellationFactorInstanceStrideBytes);
    for (const auto& binding : desc.reflection.descriptorBindings) {
        hash = hashShaderValue(hash, binding.set);
        hash = hashShaderValue(hash, binding.binding);
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(binding.type));
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(binding.stages));
        hash = hashShaderValue(hash, binding.arrayCount);
        hash = hashShaderValue(hash, binding.subpassAttachmentIndex);
    }
    return hash;
}

inline ValidationResult validateShaderPackageAuthorization(
    const ShaderPackageDesc& desc, bool authorizedBindless)

{
    // Scalar-expanded WGSL must not silently enter ordinary descriptor paths.
    // Bindless package execution remains closed until pipeline creation and
    // command recording consume a device-owned BindlessCross/1 ticket.
    if (!authorizedBindless
        && (!desc.reflection.bindlessContractVersion.empty()
            || !desc.reflection.bindlessExecution.empty()
            || !desc.reflection.bindlessLoweringSha256.empty()))
        return {false, "RHI-BINDLESS-PIPELINE-AUTHORIZATION-UNAVAILABLE"};
    if (authorizedBindless
        && (desc.reflection.bindlessContractVersion != "BindlessCross/1"
            || (desc.reflection.bindlessExecution != "Native"
                && desc.reflection.bindlessExecution != "Emulated")
            || desc.reflection.bindlessLoweringSha256.empty()))
        return {false, "RHI-BINDLESS-PIPELINE-AUTHORIZATION-INVALID"};
    return {true, {}};
}

inline ValidationResult validateShaderPackageSourcePaths(const ShaderPackageDesc& desc)

{
    if (!hasShaderPathSuffix(desc.slangModulePath, ".slang")) {
        return {false, "shader package requires Slang source"};
    }
    if (!hasShaderPathSuffix(desc.artifacts.spirvPath, ".spv")) {
        return {false, "shader package requires generated SPIR-V artifact"};
    }
    return {true, {}};
}

inline ValidationResult validateShaderPackageAuxiliaryPaths(const ShaderPackageDesc& desc)

{
    if (!desc.artifacts.wgslPath.empty()
        && !hasShaderPathSuffix(desc.artifacts.wgslPath, ".wgsl")) {
        return {false, "shader package WGSL artifact must use .wgsl suffix"};
    }
    if (!hasShaderPathSuffix(desc.artifacts.reflectionPath, ".yaml")
        && !hasShaderPathSuffix(desc.artifacts.reflectionPath, ".yml")) {
        return {false, "shader package requires generated reflection metadata"};
    }
    return {true, {}};
}

inline ValidationResult validateShaderPackageInterface(const ShaderPackageDesc& desc)

{
    if (!desc.backendFallbacksHiddenFromUpperLayer) {
        return {false, "shader package backend fallback details must stay hidden"};
    }
    if (desc.nativeHandleExposedToUpperLayer) {
        return {false, "shader package must not expose native handles"};
    }
    if (desc.entrypoints.empty()) {
        return {false, "shader package requires at least one entrypoint"};
    }

    for (std::size_t index = 0; index < desc.entrypoints.size(); ++index) {
        const auto& entrypoint = desc.entrypoints[index];
        if (entrypoint.sourceLanguage != ShaderSourceLanguage::Slang) {
            return {false, "shader package entrypoint must use Slang source"};
        }
        if (entrypoint.modulePath != desc.slangModulePath) {
            return {false, "shader package entrypoint module does not match package source"};
        }
        if (entrypoint.entryPoint.empty()) {
            return {false, "shader package entrypoint name is empty"};
        }
        if (entrypoint.stage == ShaderStage::None) {
            return {false, "shader package entrypoint has no shader stage"};
        }
        // A single Slang module legitimately exposes multiple entry points at the
        // same stage - the canonical case is a post-process module with several
        // fragment mains (bloomDownsample / bloomComposite / tonemap / grade),
        // where each RenderPipeline selects its fragment entry point via
        // RenderPipelineDesc::fragmentShader. Backends resolve entry points by
        // (modulePath, entryPoint, stage) exact match, so multiple same-stage
        // entries are well-defined. We only reject a true duplicate: the same
        // entry-point name at the same stage in the same module.
        for (std::size_t other = index + 1; other < desc.entrypoints.size(); ++other) {
            const auto& otherEntrypoint = desc.entrypoints[other];
            if (otherEntrypoint.stage == entrypoint.stage
                && otherEntrypoint.entryPoint == entrypoint.entryPoint
                && otherEntrypoint.modulePath == entrypoint.modulePath) {
                return {false, "shader package entrypoint is duplicated"};
            }
        }
    }

    std::uint32_t packageStages = 0;
    for (const auto& entrypoint : desc.entrypoints) {
        packageStages |= static_cast<std::uint32_t>(entrypoint.stage);
    }
    for (std::size_t index = 0; index < desc.reflection.descriptorBindings.size(); ++index) {
        const auto& binding = desc.reflection.descriptorBindings[index];
        if (binding.stages == ShaderStage::None) {
            return {false, "shader reflection binding has no shader stage visibility"};
        }
        if ((static_cast<std::uint32_t>(binding.stages) & ~packageStages) != 0u) {
            return {false, "shader reflection binding stages are absent from shader entrypoints"};
        }
        if (binding.arrayCount == 0) {
            return {false, "shader reflection binding array count must be greater than zero"};
        }
        if (binding.type == DescriptorType::InputAttachment
            && binding.subpassAttachmentIndex == UINT32_MAX) {
            return {false, "input attachment reflection is missing logical attachment index"};
        }
        for (std::size_t other = index + 1; other < desc.reflection.descriptorBindings.size();
            ++other) {
            const auto& otherBinding = desc.reflection.descriptorBindings[other];
            if (otherBinding.set == binding.set && otherBinding.binding == binding.binding) {
                return {false, "shader reflection binding is duplicated"};
            }
        }
    }

    // Track A P5: when push constants are present, lowering metadata is required
    if (desc.reflection.pushConstantByteCount > 0) {
        if (desc.reflection.pushConstantContractVersion != "AtomicFullBlockPush/1") {
            return {
                false, "push-constant shader requires AtomicFullBlockPush/1 reflection contract"};
        }
        const auto reflectionStages =
            parseShaderReflectionPushConstantStages(desc.reflection.pushConstantStages);
        if (!reflectionStages) {
            return {false, "push-constant shader requires stage visibility in reflection"};
        }
        if ((static_cast<std::uint32_t>(*reflectionStages) & ~packageStages) != 0u) {
            return {false, "push-constant reflection stages are absent from shader entrypoints"};
        }
        if (desc.reflection.pushConstantMembers.empty()) {
            return {false, "push-constant shader requires member-level reflection metadata"};
        }
        std::uint64_t expectedOffset = 0u;
        for (const auto& member : desc.reflection.pushConstantMembers) {
            if (member.name.empty() || member.scalarType.empty() || member.size == 0u
                || member.offset != expectedOffset) {
                return {false, "push-constant member reflection is incomplete or non-contiguous"};
            }
            std::uint64_t elementSize = 0u;
            if (member.matrixRows != 0u || member.matrixCols != 0u || member.matrixStride != 0u) {
                if (member.matrixRows == 0u || member.matrixCols == 0u || member.matrixStride == 0u
                    || member.scalarWidth == 0u) {
                    return {false, "push-constant matrix member reflection is incomplete"};
                }
                elementSize = static_cast<std::uint64_t>(member.matrixStride)
                    * (member.rowMajor ? member.matrixRows : member.matrixCols);
            } else if (member.vecSize != 0u) {
                if (member.scalarWidth == 0u || (member.scalarWidth % 8u) != 0u) {
                    return {
                        false, "push-constant vector member reflection has invalid scalar width"};
                }
                elementSize = static_cast<std::uint64_t>(member.scalarWidth / 8u) * member.vecSize;
            } else {
                if (member.scalarWidth == 0u || (member.scalarWidth % 8u) != 0u) {
                    return {
                        false, "push-constant scalar member reflection has invalid scalar width"};
                }
                elementSize = member.scalarWidth / 8u;
            }
            std::uint64_t expectedSize = elementSize;
            if (!member.arrayDims.empty()) {
                if (member.arrayStride < elementSize) {
                    return {false, "push-constant array member reflection has invalid stride"};
                }
                std::uint64_t elementCount = 1u;
                for (const auto dimension : member.arrayDims) {
                    if (dimension == 0u || elementCount > UINT32_MAX / dimension) {
                        return {
                            false, "push-constant array member reflection has invalid dimensions"};
                    }
                    elementCount *= dimension;
                }
                expectedSize = static_cast<std::uint64_t>(member.arrayStride) * elementCount;
            }
            if (expectedSize != member.size) {
                return {false, "push-constant member reflection size disagrees with type layout"};
            }
            expectedOffset += member.size;
            if (expectedOffset > UINT32_MAX) {
                return {false, "push-constant member reflection overflows the byte range"};
            }
        }
        if (expectedOffset != desc.reflection.pushConstantByteCount) {
            return {false, "push-constant member reflection does not cover the reflected block"};
        }
    }

    return {true, ""};
}

inline ValidationResult validateShaderPackageImpl(
    const ShaderPackageDesc& desc, bool authorizedBindless)

{
    const auto authorization = validateShaderPackageAuthorization(desc, authorizedBindless);
    if (!authorization.success)
        return authorization;
    const auto source = validateShaderPackageSourcePaths(desc);
    if (!source.success)
        return source;
    if (!hasShaderPathSuffix(desc.artifacts.mslPath, ".metal")
        && !hasShaderPathSuffix(desc.artifacts.mslPath, ".msl"))
        return {false, "shader package requires generated MSL artifact"};
    const auto auxiliary = validateShaderPackageAuxiliaryPaths(desc);
    if (!auxiliary.success)
        return auxiliary;
    return validateShaderPackageInterface(desc);
}

inline ValidationResult validateShaderPackage(const ShaderPackageDesc& desc)

{
    return validateShaderPackageImpl(desc, false);
}

inline ValidationResult validateAuthorizedBindlessShaderPackage(const ShaderPackageDesc& desc)

{
    return validateShaderPackageImpl(desc, true);
}

inline const DescriptorLayoutDesc* findPipelineDescriptorLayout(
    const PipelineLayoutDesc& layout, const std::pmr::string& layoutName)

{
    for (const auto& descriptorLayout : layout.descriptorLayouts) {
        if (descriptorLayout.name == layoutName) {
            return &descriptorLayout;
        }
    }
    return nullptr;
}

inline const DescriptorBindingDesc* findDescriptorBinding(
    const DescriptorLayoutDesc& layout, const ShaderReflectionBinding& binding)

{
    for (const auto& descriptor : layout.bindings) {
        if (descriptor.binding == binding.binding) {
            return &descriptor;
        }
    }
    return nullptr;
}

inline ValidationResult validateDescriptorSetBindingAgainstReflection(
    const PipelineLayoutDesc& layout, const ShaderReflectionBinding& binding)

{
    for (const auto& slot : layout.descriptorSets) {
        if (slot.set != binding.set) {
            continue;
        }
        const auto* descriptorLayout = findPipelineDescriptorLayout(layout, slot.layoutName);
        if (!descriptorLayout) {
            return {false,
                "shader reflection descriptor set references unknown pipeline descriptor layout"};
        }
        const auto* descriptor = findDescriptorBinding(*descriptorLayout, binding);
        if (!descriptor) {
            return {false,
                "shader reflection descriptor binding is missing from pipeline descriptor layout"};
        }
        if (descriptor->type != binding.type
            || (descriptor->stages & binding.stages) != binding.stages) {
            return {false,
                "shader reflection descriptor binding does not match pipeline descriptor layout"};
        }
        if (descriptor->arrayCount != binding.arrayCount) {
            return {false,
                "shader reflection descriptor array count does not match pipeline descriptor "
                "layout"};
        }
        if (descriptor->depthTexture != binding.depthTexture) {
            return {false,
                "shader reflection descriptor depth/color semantic does not match pipeline "
                "descriptor layout"};
        }
        return {true, ""};
    }
    return {false, "shader reflection descriptor set is missing from pipeline layout"};
}

inline ValidationResult validateDescriptorTableBindingAgainstReflection(
    const PipelineLayoutDesc& layout, const ShaderReflectionBinding& binding)

{
    for (const auto& slot : layout.descriptorTables) {
        if (slot.set != binding.set) {
            continue;
        }
        if (slot.type != binding.type || (slot.stages & binding.stages) != binding.stages) {
            return {false,
                "shader reflection descriptor table binding does not match pipeline descriptor "
                "layout"};
        }
        if (binding.binding >= slot.capacity
            || binding.arrayCount > slot.capacity - binding.binding) {
            return {false,
                "shader reflection descriptor array exceeds pipeline descriptor table capacity"};
        }
        if (slot.depthTexture != binding.depthTexture)
            return {false,
                "RHI-DESCRIPTOR-TABLE-DEPTH-MISMATCH: shader reflection depth identity does not "
                "match table slot"};
        return {true, ""};
    }
    return {false, "shader reflection descriptor table is missing from pipeline layout"};
}

inline std::optional<ShaderStage> parseShaderReflectionPushConstantStages(
    const std::pmr::string& encoded)

{
    if (encoded.empty()) {
        return std::nullopt;
    }

    ShaderStage stages = ShaderStage::None;
    std::size_t tokenStart = 0;
    while (tokenStart < encoded.size()) {
        const auto separator = encoded.find('|', tokenStart);
        const auto tokenEnd = separator == std::pmr::string::npos ? encoded.size() : separator;
        const auto token = std::string_view(encoded).substr(tokenStart, tokenEnd - tokenStart);
        ShaderStage stage = ShaderStage::None;
        if (token == "VS") {
            stage = ShaderStage::Vertex;
        } else if (token == "FS") {
            stage = ShaderStage::Fragment;
        } else if (token == "CS") {
            stage = ShaderStage::Compute;
        } else if (token == "HS") {
            stage = ShaderStage::TessellationControl;
        } else if (token == "DS") {
            stage = ShaderStage::TessellationEvaluation;
        } else {
            return std::nullopt;
        }
        if ((static_cast<std::uint32_t>(stages) & static_cast<std::uint32_t>(stage)) != 0u) {
            return std::nullopt;
        }
        stages = stages | stage;
        if (separator == std::pmr::string::npos) {
            break;
        }
        tokenStart = tokenEnd + 1u;
        if (tokenStart == encoded.size()) {
            return std::nullopt;
        }
    }
    return stages == ShaderStage::None ? std::nullopt : std::optional<ShaderStage>(stages);
}

inline ValidationResult validatePipelineLayoutAgainstShaderReflection(
    const PipelineLayoutDesc& layout, const ShaderReflectionDesc& reflection)

{
    const auto layoutValidation = validatePipelineLayout(layout);
    if (!layoutValidation.success) {
        return layoutValidation;
    }
    if (reflection.pushConstantByteCount > 0u) {
        if (reflection.pushConstantContractVersion != "AtomicFullBlockPush/1") {
            return {false, "shader reflection push-constant contract version is unsupported"};
        }
        if (layout.pushConstantRanges.empty()) {
            return {false, "shader reflection push constants are missing from pipeline layout"};
        }
        if (layout.pushConstantRanges.size() != 1u) {
            return {false, "shader reflection push constants require exactly one pipeline range"};
        }
        const auto reflectionStages =
            parseShaderReflectionPushConstantStages(reflection.pushConstantStages);
        if (!reflectionStages) {
            return {false, "shader reflection push-constant stages are invalid"};
        }
        const auto& range = layout.pushConstantRanges.front();
        if (range.offsetBytes != 0u) {
            return {false, "shader reflection push-constant pipeline range must start at offset 0"};
        }
        const auto layoutMask = static_cast<std::uint32_t>(range.stages);
        const auto reflectionMask = static_cast<std::uint32_t>(*reflectionStages);
        if (range.sizeBytes != reflection.pushConstantByteCount
            || (layoutMask & reflectionMask) != reflectionMask) {
            return {
                false, "shader reflection push-constant contract does not match pipeline layout"};
        }
    }
    for (const auto& binding : reflection.descriptorBindings) {
        auto setResult = validateDescriptorSetBindingAgainstReflection(layout, binding);
        if (setResult.success) {
            continue;
        }
        auto tableResult = validateDescriptorTableBindingAgainstReflection(layout, binding);
        if (tableResult.success) {
            continue;
        }
        if (setResult.diagnostics.find("array") != std::pmr::string::npos
            || tableResult.diagnostics.find("array") != std::pmr::string::npos) {
            return {false, "shader reflection descriptor array does not match pipeline layout"};
        }
        auto diagnostics = std::pmr::string(
            "shader reflection descriptor binding does not match pipeline descriptor layout");
        diagnostics += " (set=";
        diagnostics += std::to_string(binding.set);
        diagnostics += ", binding=";
        diagnostics += std::to_string(binding.binding);
        diagnostics += ", reflectedType=";
        diagnostics += std::to_string(static_cast<std::uint32_t>(binding.type));
        diagnostics += ", reflectedStages=";
        diagnostics += std::to_string(static_cast<std::uint32_t>(binding.stages));
        diagnostics += ")";
        return {false, std::move(diagnostics)};
    }
    return {true, ""};
}

inline PipelineLayoutDesc buildPipelineLayoutDescFromShaderReflection(
    const ShaderReflectionDesc& reflection)

{
    auto* resource = &core::defaultCpuMemoryResource();
    PipelineLayoutDesc layout{core::pmr::Vector<DescriptorLayoutDesc>(resource),
        core::pmr::Vector<DescriptorSetSlotDesc>(resource),
        core::pmr::Vector<DescriptorTableSlotDesc>(resource),
        core::pmr::Vector<PushConstantRange>(resource)};
    for (const auto& reflectedBinding : reflection.descriptorBindings) {
        core::pmr::String layoutName("ReflectionSet", resource);
        core::appendUnsigned(layoutName, reflectedBinding.set);
        DescriptorLayoutDesc* descriptorLayout = nullptr;
        for (auto& existing : layout.descriptorLayouts) {
            if (existing.name == layoutName) {
                descriptorLayout = &existing;
                break;
            }
        }
        if (!descriptorLayout) {
            layout.descriptorLayouts.push_back({core::pmr::String(layoutName, resource),
                core::pmr::Vector<DescriptorBindingDesc>(resource)});
            layout.descriptorSets.push_back(
                {reflectedBinding.set, core::pmr::String(layoutName, resource)});
            descriptorLayout = &layout.descriptorLayouts.back();
        }
        descriptorLayout->bindings.push_back({
            reflectedBinding.binding,
            reflectedBinding.type,
            reflectedBinding.stages,
            reflectedBinding.arrayCount,
            false,
            false,
            false,
            TextureViewDimension::Texture2D,
            reflectedBinding.depthTexture,
        });
    }
    if (reflection.pushConstantByteCount > 0) {
        layout.pushConstantRanges.push_back({
            ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute,
            0,
            reflection.pushConstantByteCount,
        });
    }
    return layout;
}

inline bool hasColorWriteMask(ColorWriteMask mask)

{
    return static_cast<std::uint32_t>(mask) != 0u;
}

inline bool isColorRenderTargetFormat(TextureFormat format)

{
    return format == TextureFormat::Bgra8Unorm || format == TextureFormat::Rgba8Unorm
        || format == TextureFormat::Rgba8UnormSrgb || format == TextureFormat::R8Unorm
        || format == TextureFormat::Rgba16Float || format == TextureFormat::Rg16Float;
}

inline bool isDepthStencilFormat(TextureFormat format)

{
    return format == TextureFormat::D32Float || format == TextureFormat::Depth24Stencil8;
}

inline bool hasStencilAspect(TextureFormat format)

{
    return format == TextureFormat::Depth24Stencil8;
}

inline bool isSupportedMultisampleCount(std::uint32_t sampleCount)

{
    return sampleCount == 1u || sampleCount == 2u || sampleCount == 4u || sampleCount == 8u;
}

inline bool isSupportedPrimitiveTopology(PrimitiveTopology topology)

{
    return topology == PrimitiveTopology::TriangleList
        || topology == PrimitiveTopology::TriangleStrip || topology == PrimitiveTopology::LineList
        || topology == PrimitiveTopology::PointList || topology == PrimitiveTopology::PatchList;
}

inline bool isSupportedCullMode(CullMode cullMode)

{
    return cullMode == CullMode::None || cullMode == CullMode::Front || cullMode == CullMode::Back;
}

inline bool isSupportedFrontFace(FrontFace frontFace)

{
    return frontFace == FrontFace::CounterClockwise || frontFace == FrontFace::Clockwise;
}

inline std::uint32_t pipelineFloatBits(float value)

{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float hashing assumes 32-bit float");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline ColorTargetState fallbackColorTargetState(TextureFormat format)

{
    ColorTargetState target;
    target.format = format;
    return target;
}

inline std::uint32_t effectiveColorTargetCount(const RenderPipelineDesc& desc)

{
    if (!desc.colorTargets.empty()) {
        return static_cast<std::uint32_t>(desc.colorTargets.size());
    }
    return desc.colorFormat == TextureFormat::Unknown ? 0u : 1u;
}

inline ColorTargetState effectiveColorTargetAt(const RenderPipelineDesc& desc, std::uint32_t index)

{
    if (!desc.colorTargets.empty()) {
        return desc.colorTargets[index];
    }
    return fallbackColorTargetState(desc.colorFormat);
}

inline TextureFormat effectiveDepthStencilFormat(const RenderPipelineDesc& desc)

{
    if (desc.depthStencil.format != TextureFormat::Unknown) {
        return desc.depthStencil.format;
    }
    return desc.depthFormat;
}

inline bool renderPipelineHasDepthStencilState(const RenderPipelineDesc& desc)

{
    return desc.depthStencil.depthTestEnabled || desc.depthStencil.depthWriteEnabled
        || desc.depthStencil.stencilEnabled || desc.depthStencil.depthBiasEnabled;
}

inline bool isDepthOnlyRenderPipeline(const RenderPipelineDesc& desc)

{
    return effectiveColorTargetCount(desc) == 0u
        && isDepthStencilFormat(effectiveDepthStencilFormat(desc));
}

inline std::uint64_t stableRenderPipelineStateKey(const RenderPipelineDesc& desc)

{
    std::uint64_t hash = 1469598103934665603ull;
    hash = hashShaderString(hash, desc.label);
    hash = hashShaderString(hash, desc.vertexShader.modulePath);
    hash = hashShaderString(hash, desc.vertexShader.entryPoint);
    hash = hashShaderString(hash, desc.fragmentShader.modulePath);
    hash = hashShaderString(hash, desc.fragmentShader.entryPoint);
    hash = hashShaderString(hash, desc.tessellationControlShader.modulePath);
    hash = hashShaderString(hash, desc.tessellationControlShader.entryPoint);
    hash = hashShaderString(hash, desc.tessellationEvaluationShader.modulePath);
    hash = hashShaderString(hash, desc.tessellationEvaluationShader.entryPoint);
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.tessellationControlShader.stage));
    hash =
        hashShaderValue(hash, static_cast<std::uint32_t>(desc.tessellationEvaluationShader.stage));
    hash = hashShaderValue(hash, desc.patchControlPointCount);
    if (desc.shaderPackage) {
        hash = hashShaderValue(hash, stableShaderPackageHash(*desc.shaderPackage));
    }
    hash = hashShaderValue(hash, effectiveColorTargetCount(desc));
    for (std::uint32_t index = 0; index < effectiveColorTargetCount(desc); ++index) {
        const auto target = effectiveColorTargetAt(desc, index);
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(target.format));
        hash = hashShaderValue(hash, target.blendEnabled ? 1u : 0u);
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(target.colorBlend.sourceFactor));
        hash =
            hashShaderValue(hash, static_cast<std::uint32_t>(target.colorBlend.destinationFactor));
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(target.colorBlend.operation));
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(target.alphaBlend.sourceFactor));
        hash =
            hashShaderValue(hash, static_cast<std::uint32_t>(target.alphaBlend.destinationFactor));
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(target.alphaBlend.operation));
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(target.writeMask));
        hash = hashShaderValue(hash, target.resolveTargetRequired ? 1u : 0u);
    }
    const auto depthFormat = effectiveDepthStencilFormat(desc);
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(depthFormat));
    hash = hashShaderValue(hash, desc.depthStencil.depthTestEnabled ? 1u : 0u);
    hash = hashShaderValue(hash, desc.depthStencil.depthWriteEnabled ? 1u : 0u);
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.depthStencil.depthCompare));
    hash = hashShaderValue(hash, desc.depthStencil.stencilEnabled ? 1u : 0u);
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.depthStencil.front.failOp));
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.depthStencil.front.depthFailOp));
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.depthStencil.front.passOp));
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.depthStencil.front.compare));
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.depthStencil.back.failOp));
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.depthStencil.back.depthFailOp));
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.depthStencil.back.passOp));
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.depthStencil.back.compare));
    hash = hashShaderValue(hash, desc.depthStencil.stencilReadMask);
    hash = hashShaderValue(hash, desc.depthStencil.stencilWriteMask);
    hash = hashShaderValue(hash, desc.depthStencil.depthBiasEnabled ? 1u : 0u);
    hash = hashShaderValue(hash, pipelineFloatBits(desc.depthStencil.depthBiasConstantFactor));
    hash = hashShaderValue(hash, pipelineFloatBits(desc.depthStencil.depthBiasClamp));
    hash = hashShaderValue(hash, pipelineFloatBits(desc.depthStencil.depthBiasSlopeFactor));
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.cullMode));
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.frontFace));
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.topology));
    hash = hashShaderValue(hash, desc.multisample.sampleCount);
    hash = hashShaderValue(hash, desc.multisample.sampleMask);
    hash = hashShaderValue(hash, desc.multisample.alphaToCoverageEnabled ? 1u : 0u);
    hash = hashShaderValue(hash, desc.hasViewport ? 1u : 0u);
    if (desc.hasViewport) {
        hash = hashShaderValue(hash, pipelineFloatBits(desc.viewport.x));
        hash = hashShaderValue(hash, pipelineFloatBits(desc.viewport.y));
        hash = hashShaderValue(hash, pipelineFloatBits(desc.viewport.width));
        hash = hashShaderValue(hash, pipelineFloatBits(desc.viewport.height));
        hash = hashShaderValue(hash, pipelineFloatBits(desc.viewport.minDepth));
        hash = hashShaderValue(hash, pipelineFloatBits(desc.viewport.maxDepth));
    }
    hash = hashShaderValue(hash, desc.hasScissor ? 1u : 0u);
    if (desc.hasScissor) {
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.scissor.x));
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.scissor.y));
        hash = hashShaderValue(hash, desc.scissor.width);
        hash = hashShaderValue(hash, desc.scissor.height);
    }
    // Fold the dynamicViewportScissor opt-in into the cache key so the
    // static-bake and dynamic-state Vulkan pipeline variants do not collide
    // in the pipeline cache (they have different VkPipelineDynamicStateCreateInfo).
    hash = hashShaderValue(hash, desc.dynamicViewportScissor ? 1u : 0u);
    hash = hashShaderValue(hash, desc.vertexBuffers.size());
    for (const auto& vertexBuffer : desc.vertexBuffers) {
        hash = hashShaderValue(hash, vertexBuffer.slot);
        hash = hashShaderValue(hash, vertexBuffer.strideBytes);
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(vertexBuffer.inputRate));
        hash = hashShaderValue(hash, vertexBuffer.attributes.size());
        for (const auto& attribute : vertexBuffer.attributes) {
            hash = hashShaderValue(hash, attribute.shaderLocation);
            hash = hashShaderValue(hash, attribute.offsetBytes);
            hash = hashShaderValue(hash, static_cast<std::uint32_t>(attribute.format));
        }
    }
    // Fold the FULL pipeline layout into the cache key. Hashing only the
    // coarse descriptorSetCount/descriptorTableCount/pushConstantByteCount
    // would collide between layouts that share those counts but differ in
    // binding configuration (binding numbers, types, stages, array counts),
    // yielding an incompatible cached pipeline with the wrong descriptor /
    // push-constant layout. desc.layout->desc() exposes the complete
    // PipelineLayoutDesc the backend compiled from; stablePipelineLayoutKey
    // hashes every descriptor layout, set, table and push-constant range.
    hash = hashShaderValue(hash, desc.layout ? 1u : 0u);
    if (desc.layout) {
        hash = hashShaderValue(hash, stablePipelineLayoutKey(desc.layout->desc()));
    }
    hash = hashShaderValue(hash, desc.subpassIndex);
    hash = hashShaderValue(hash, desc.renderPassCompatibility.colorAttachmentFormats.size());
    for (const auto format : desc.renderPassCompatibility.colorAttachmentFormats) {
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(format));
    }
    hash = hashShaderValue(
        hash, static_cast<std::uint32_t>(desc.renderPassCompatibility.depthStencilFormat));
    hash = hashShaderValue(hash, desc.renderPassCompatibility.subpasses.size());
    for (const auto& subpass : desc.renderPassCompatibility.subpasses) {
        hash = hashShaderValue(hash, subpass.colorAttachmentIndices.size());
        for (const auto index : subpass.colorAttachmentIndices) {
            hash = hashShaderValue(hash, index);
        }
        hash = hashShaderValue(hash, subpass.inputAttachmentIndices.size());
        for (const auto index : subpass.inputAttachmentIndices) {
            hash = hashShaderValue(hash, index);
        }
        hash = hashShaderValue(hash, subpass.useDepthStencilAttachment ? 1u : 0u);
        hash = hashShaderValue(hash, subpass.readDepthInputAttachment ? 1u : 0u);
    }
    hash = hashShaderValue(hash, desc.renderPassCompatibility.subpassDependencies.size());
    for (const auto& dependency : desc.renderPassCompatibility.subpassDependencies) {
        hash = hashShaderValue(hash, dependency.srcSubpass);
        hash = hashShaderValue(hash, dependency.dstSubpass);
    }
    return hash == 0u ? 1u : hash;
}

inline bool renderPipelineRequiresResolve(const RenderPipelineDesc& desc)

{
    for (std::uint32_t index = 0; index < effectiveColorTargetCount(desc); ++index) {
        if (effectiveColorTargetAt(desc, index).resolveTargetRequired) {
            return true;
        }
    }
    return false;
}

inline PipelineStateValidationResult validateRenderPipelineState(const RenderPipelineDesc& desc)

{
    PipelineStateValidationResult result;
    result.colorTargetCount = effectiveColorTargetCount(desc);
    result.colorFormats.reserve(result.colorTargetCount);
    for (std::uint32_t index = 0; index < result.colorTargetCount; ++index) {
        result.colorFormats.push_back(effectiveColorTargetAt(desc, index).format);
    }
    result.depthStencilFormat = effectiveDepthStencilFormat(desc);
    result.sampleCount = desc.multisample.sampleCount;
    result.pipelineCacheKey = stableRenderPipelineStateKey(desc);

    const auto& compatibility = desc.renderPassCompatibility;
    if (!compatibility.enabled()) {
        if (desc.subpassIndex != 0u) {
            result.diagnostics =
                "render pipeline non-zero subpass requires render-pass compatibility metadata";
            result.unsupportedStateDiagnosed = true;
            return result;
        }
    } else {
        if (compatibility.subpasses.size() > RenderPassCompatibilityDesc::kMaxSubpasses) {
            result.diagnostics =
                "render pipeline compatibility metadata exceeds native subpass capacity";
            result.unsupportedStateDiagnosed = true;
            return result;
        }
        if (desc.subpassIndex >= compatibility.subpasses.size()) {
            result.diagnostics =
                "render pipeline subpass index exceeds render-pass compatibility metadata";
            result.unsupportedStateDiagnosed = true;
            return result;
        }
        if (compatibility.colorAttachmentFormats.empty() && result.colorTargetCount > 0u) {
            result.diagnostics =
                "render pipeline compatibility metadata is missing color attachment formats";
            result.unsupportedStateDiagnosed = true;
            return result;
        }
        const auto& subpass = compatibility.subpasses[desc.subpassIndex];
        if (subpass.colorAttachmentIndices.size() != result.colorTargetCount) {
            result.diagnostics =
                "render pipeline color targets do not match selected compatible subpass";
            result.unsupportedStateDiagnosed = true;
            return result;
        }
        for (std::uint32_t targetIndex = 0; targetIndex < result.colorTargetCount; ++targetIndex) {
            const auto attachmentIndex = subpass.colorAttachmentIndices[targetIndex];
            if (attachmentIndex >= compatibility.colorAttachmentFormats.size()
                || compatibility.colorAttachmentFormats[attachmentIndex]
                    != effectiveColorTargetAt(desc, targetIndex).format) {
                result.diagnostics =
                    "render pipeline color target format disagrees with compatible subpass";
                result.unsupportedStateDiagnosed = true;
                return result;
            }
        }
        const bool pipelineUsesDepth = renderPipelineHasDepthStencilState(desc);
        if (pipelineUsesDepth != subpass.useDepthStencilAttachment) {
            result.diagnostics = "render pipeline depth usage disagrees with compatible subpass";
            result.unsupportedStateDiagnosed = true;
            return result;
        }
        if (pipelineUsesDepth
            && compatibility.depthStencilFormat != effectiveDepthStencilFormat(desc)) {
            result.diagnostics =
                "render pipeline depth format disagrees with render-pass compatibility metadata";
            result.unsupportedStateDiagnosed = true;
            return result;
        }
        for (const auto& dependency : compatibility.subpassDependencies) {
            if (dependency.srcSubpass >= compatibility.subpasses.size()
                || dependency.dstSubpass >= compatibility.subpasses.size()
                || dependency.srcSubpass == dependency.dstSubpass) {
                result.diagnostics =
                    "render pipeline compatibility metadata contains an invalid subpass dependency";
                result.unsupportedStateDiagnosed = true;
                return result;
            }
        }
    }

    if (result.colorTargetCount == 0u) {
        const auto depthFormat = effectiveDepthStencilFormat(desc);
        if (!isDepthStencilFormat(depthFormat)) {
            result.diagnostics =
                "depth-only render pipeline requires explicit depth/stencil format";
            return result;
        }
        if (!desc.depthStencil.depthTestEnabled || !desc.depthStencil.depthWriteEnabled) {
            result.diagnostics =
                "depth-only render pipeline requires explicit depth test and depth write state";
            return result;
        }
        result.depthOnlyStateValidated = true;
    } else if (result.colorTargetCount > 8u) {
        result.diagnostics = "render pipeline MRT color target capacity exceeded";
        result.unsupportedStateDiagnosed = true;
        return result;
    }
    for (std::uint32_t index = 0; index < result.colorTargetCount; ++index) {
        const auto target = effectiveColorTargetAt(desc, index);
        if (!isColorRenderTargetFormat(target.format)) {
            result.diagnostics = "render pipeline color target format is unsupported";
            result.unsupportedStateDiagnosed = true;
            return result;
        }
        if (!hasColorWriteMask(target.writeMask)) {
            result.diagnostics =
                "render pipeline color write mask must enable at least one channel";
            return result;
        }
        if (target.blendEnabled && target.writeMask == ColorWriteMask::None) {
            result.diagnostics = "render pipeline blend state requires a color write mask";
            return result;
        }
    }
    result.blendStateValidated = result.colorTargetCount > 0u;
    result.colorWriteMaskValidated = result.colorTargetCount > 0u;

    const auto depthFormat = effectiveDepthStencilFormat(desc);
    if (renderPipelineHasDepthStencilState(desc)) {
        if (!isDepthStencilFormat(depthFormat)) {
            result.diagnostics = "render pipeline depth/stencil state requires a depth format";
            return result;
        }
    }
    if (desc.depthStencil.stencilEnabled && !hasStencilAspect(depthFormat)) {
        result.diagnostics = "render pipeline stencil state requires a stencil-capable format";
        return result;
    }
    if (desc.depthStencil.depthBiasEnabled && !desc.depthStencil.depthTestEnabled) {
        result.diagnostics = "render pipeline depth bias requires depth testing";
        return result;
    }
    result.stencilStateValidated = true;
    result.depthBiasStateValidated = true;

    if (!isSupportedCullMode(desc.cullMode)) {
        result.diagnostics = "render pipeline cull mode is unsupported";
        result.unsupportedStateDiagnosed = true;
        return result;
    }
    if (!isSupportedFrontFace(desc.frontFace)) {
        result.diagnostics = "render pipeline front face is unsupported";
        result.unsupportedStateDiagnosed = true;
        return result;
    }
    result.rasterStateValidated = true;

    if (!isSupportedPrimitiveTopology(desc.topology)) {
        result.diagnostics = "render pipeline topology is unsupported";
        result.unsupportedStateDiagnosed = true;
        return result;
    }
    const bool hasTessellationControl = desc.tessellationControlShader.stage != ShaderStage::None
        || !desc.tessellationControlShader.entryPoint.empty();
    const bool hasTessellationEvaluation =
        desc.tessellationEvaluationShader.stage != ShaderStage::None
        || !desc.tessellationEvaluationShader.entryPoint.empty();
    if (desc.topology == PrimitiveTopology::PatchList) {
        if (desc.patchControlPointCount == 0u || desc.patchControlPointCount > 32u
            || desc.vertexShader.stage != ShaderStage::Vertex
            || desc.tessellationControlShader.stage != ShaderStage::TessellationControl
            || desc.tessellationEvaluationShader.stage != ShaderStage::TessellationEvaluation
            || desc.fragmentShader.stage != ShaderStage::Fragment || !hasTessellationControl
            || !hasTessellationEvaluation) {
            result.diagnostics =
                "PatchList requires vertex, tessellation-control, tessellation-evaluation, "
                "fragment entrypoints and a control-point count in [1,32]";
            result.unsupportedStateDiagnosed = true;
            return result;
        }
    } else if (desc.patchControlPointCount != 0u || hasTessellationControl
        || hasTessellationEvaluation) {
        result.diagnostics =
            "tessellation entrypoints and patch control-point count require PatchList topology";
        result.unsupportedStateDiagnosed = true;
        return result;
    }
    result.topologyValidated = true;

    if (!isSupportedMultisampleCount(desc.multisample.sampleCount)) {
        result.diagnostics = "render pipeline MSAA sample count is unsupported";
        result.unsupportedStateDiagnosed = true;
        return result;
    }
    result.msaaStateValidated = true;
    if (renderPipelineRequiresResolve(desc) && desc.multisample.sampleCount <= 1u) {
        result.diagnostics = "render pipeline resolve target requires MSAA";
        return result;
    }
    result.resolveStateValidated = true;

    if (desc.hasViewport
        && (desc.viewport.width <= 0.0f || desc.viewport.height <= 0.0f
            || desc.viewport.minDepth > desc.viewport.maxDepth)) {
        result.diagnostics = "render pipeline viewport state is invalid";
        return result;
    }
    if (desc.hasScissor && (desc.scissor.width == 0u || desc.scissor.height == 0u)) {
        result.diagnostics = "render pipeline scissor state is invalid";
        return result;
    }
    // Dynamic viewport/scissor opt-in: the values are supplied at record time
    // via RenderPassEncoder::setViewport/setScissor, so the static fields are
    // irrelevant. Reject mixing static + dynamic on the same pipeline - that
    // would leave two sources of truth for the same rasterizer state and
    // violates AGENTS.md RHI Rule 4 (no native defaults, no implicit state).
    // The [VS-E017] prefix is the stable diagnostic code asserted by
    // rhi_dynamic_viewport_scissor_contract_tests.
    if (desc.dynamicViewportScissor && (desc.hasViewport || desc.hasScissor)) {
        result.diagnostics =
            "[VS-E017] dynamicViewportScissor conflicts with static viewport/scissor";
        result.unsupportedStateDiagnosed = true;
        return result;
    }
    for (const auto& vertexBuffer : desc.vertexBuffers) {
        if (vertexBuffer.strideBytes == 0u || vertexBuffer.attributes.empty()) {
            result.diagnostics = "render pipeline vertex buffer layout is invalid";
            return result;
        }
        if (vertexBuffer.inputRate != VertexInputRate::Vertex
            && vertexBuffer.inputRate != VertexInputRate::Instance) {
            result.diagnostics = "render pipeline vertex buffer input rate is unsupported";
            result.unsupportedStateDiagnosed = true;
            return result;
        }
    }
    result.vertexInputStateValidated = true;
    result.viewportScissorStateValidated = true;

    result.success = true;
    return result;
}

inline std::uint64_t stableComputePipelineStateKey(const ComputePipelineDesc& desc)

{
    std::uint64_t hash = 1469598103934665603ull;
    hash = hashShaderString(hash, desc.label);
    hash = hashShaderString(hash, desc.computeShader.modulePath);
    hash = hashShaderString(hash, desc.computeShader.entryPoint);
    hash = hashShaderValue(hash, static_cast<std::uint32_t>(desc.computeShader.stage));
    if (desc.shaderPackage) {
        hash = hashShaderValue(hash, stableShaderPackageHash(*desc.shaderPackage));
    }
    // Include the FULL pipeline layout identity (every descriptor set,
    // binding, table and push-constant range) to prevent cache collisions
    // between compute pipelines that share the same shader but have
    // different descriptor set layouts or push constant ranges. Hashing only
    // descriptorSetCount/pushConstantByteCount would collide between layouts
    // with the same coarse counts but different binding configurations.
    hash = hashShaderValue(hash, desc.layout ? 1u : 0u);
    if (desc.layout) {
        hash = hashShaderValue(hash, stablePipelineLayoutKey(desc.layout->desc()));
    }
    return hash == 0u ? 1u : hash;
}

inline ComputePipelineStateValidationResult validateComputePipelineState(
    const ComputePipelineDesc& desc)

{
    ComputePipelineStateValidationResult result;
    result.pipelineCacheKey = stableComputePipelineStateKey(desc);
    result.pipelineCacheKeyStable = true;

    if (!desc.layout) {
        result.diagnostics = "compute pipeline requires pipeline layout";
        return result;
    }
    if (!desc.shaderPackage) {
        result.diagnostics = "compute pipeline requires shader package";
        return result;
    }
    if (desc.computeShader.stage != ShaderStage::Compute) {
        result.diagnostics = "compute pipeline shader must be a Compute-stage entrypoint";
        result.unsupportedStateDiagnosed = true;
        return result;
    }
    if (desc.computeShader.sourceLanguage != ShaderSourceLanguage::Slang) {
        result.diagnostics = "compute pipeline shader must be a Slang source entrypoint";
        result.unsupportedStateDiagnosed = true;
        return result;
    }
    if (desc.computeShader.entryPoint.empty()) {
        result.diagnostics = "compute pipeline shader entrypoint name must not be empty";
        return result;
    }
    result.shaderStateValidated = true;

    // Push-constant capacity: the L2 ceiling is the bound; per-backend native
    // limits (Vulkan maxPushConstantsSize, Metal setBytes, WebGPU uniform
    // lowering) are enforced by the capability contract / lowering decision,
    // not here. A layout whose declared push-constant range exceeds the L2
    // atomic limit is a contract violation regardless of backend.
    if (desc.layout->pushConstantByteCount() > kL2AtomicPushConstantLimitBytes) {
        result.diagnostics = "compute pipeline layout push-constant bytes exceed L2 atomic limit";
        result.unsupportedStateDiagnosed = true;
        return result;
    }
    result.layoutStateValidated = true;
    result.pushConstantStateValidated = true;
    result.descriptorBindingStateValidated = true;

    result.success = true;
    return result;
}

inline std::uint64_t stablePipelineLayoutKey(const PipelineLayoutDesc& desc)

{
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto& layout : desc.descriptorLayouts) {
        hash = hashShaderString(hash, layout.name);
        hash = hashShaderValue(hash, layout.bindings.size());
        for (const auto& binding : layout.bindings) {
            hash = hashShaderValue(hash, binding.binding);
            hash = hashShaderValue(hash, static_cast<std::uint32_t>(binding.type));
            hash = hashShaderValue(hash, static_cast<std::uint32_t>(binding.stages));
            hash = hashShaderValue(hash, binding.arrayCount);
            hash = hashShaderValue(hash, binding.tableBacked ? 1u : 0u);
            hash = hashShaderValue(hash, binding.dynamicOffset ? 1u : 0u);
            hash = hashShaderValue(hash, binding.textureViewDimensionConstrained ? 1u : 0u);
            hash = hashShaderValue(hash, static_cast<std::uint32_t>(binding.textureViewDimension));
            hash = hashShaderValue(hash, binding.depthTexture ? 1u : 0u);
        }
    }
    for (const auto& set : desc.descriptorSets) {
        hash = hashShaderValue(hash, set.set);
        hash = hashShaderString(hash, set.layoutName);
    }
    for (const auto& table : desc.descriptorTables) {
        hash = hashShaderValue(hash, table.set);
        hash = hashShaderString(hash, table.tableName);
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(table.type));
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(table.stages));
        hash = hashShaderValue(hash, table.capacity);
        hash = hashShaderValue(hash, table.textureViewDimensionConstrained ? 1u : 0u);
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(table.textureViewDimension));
        hash = hashShaderValue(hash, table.depthTexture ? 1u : 0u);
    }
    hash = hashShaderValue(hash, desc.pushConstantRanges.size());
    for (const auto& range : desc.pushConstantRanges) {
        hash = hashShaderValue(hash, static_cast<std::uint32_t>(range.stages));
        hash = hashShaderValue(hash, range.offsetBytes);
        hash = hashShaderValue(hash, range.sizeBytes);
    }
    return hash == 0u ? 1u : hash;
}

} // namespace arp::rhi
