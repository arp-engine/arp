#pragma once

#include "rhi/gpu_pipeline.h"

namespace arp::rhi {

// One artifact-owned field projection for copy and immutable source matching.
// Logical descriptor bindings are deliberately owned by the package author.
class ArtifactOwnedShaderReflection final {
    template <class Destination, class Source, class Visit>
    static void fields(Destination& destination, const Source& source, Visit&& visit)
    {
        visit(destination.reflectionContractVersion, source.reflectionContractVersion);
        visit(destination.sourceSha256, source.sourceSha256);
        visit(destination.bindlessContractVersion, source.bindlessContractVersion);
        visit(destination.bindlessExecution, source.bindlessExecution);
        visit(destination.bindlessLoweringSha256, source.bindlessLoweringSha256);
        visit(destination.pushConstantByteCount, source.pushConstantSizeBytes);
        visit(destination.pushConstantContractVersion, source.pushConstantContractVersion);
        visit(destination.pushConstantStages, source.pushConstantStages);
        visit(destination.pushConstantWebGpuBindingGroup, source.pushConstantWebGpuBindingGroup);
        visit(destination.pushConstantWebGpuBindingIndex, source.pushConstantWebGpuBindingIndex);
        visit(destination.pushConstantMetalBufferIndex, source.pushConstantMetalBufferIndex);
        visit(destination.computeWorkgroupSizeX, source.computeWorkgroupSizeX);
        visit(destination.computeWorkgroupSizeY, source.computeWorkgroupSizeY);
        visit(destination.computeWorkgroupSizeZ, source.computeWorkgroupSizeZ);
        visit(destination.tessellationControlPointCount, source.tessellationControlPointCount);
        visit(destination.metalGpuCountedIndirectContractVersion,
            source.metalGpuCountedIndirectContractVersion);
        visit(destination.metalGpuCountedIndirectStatus, source.metalGpuCountedIndirectStatus);
        visit(destination.metalGpuCountedIndirectCountBufferIndex,
            source.metalGpuCountedIndirectCountBufferIndex);
        visit(destination.metalGpuCountedIndirectUsesDrawId,
            source.metalGpuCountedIndirectUsesDrawId);
        visit(destination.metalGpuCountedIndirectMasksOutOfRange,
            source.metalGpuCountedIndirectMasksOutOfRange);
        visit(
            destination.metalTessellationContractVersion, source.metalTessellationContractVersion);
        visit(destination.metalTessellationStatus, source.metalTessellationStatus);
        visit(destination.metalTessellationVertexEntryPoint,
            source.metalTessellationVertexEntryPoint);
        visit(destination.metalTessellationFactorBufferIndex,
            source.metalTessellationFactorBufferIndex);
        visit(destination.metalTessellationControlPointCount,
            source.metalTessellationControlPointCount);
        visit(destination.metalTessellationPatchType, source.metalTessellationPatchType);
        visit(destination.metalTessellationFactorFormat, source.metalTessellationFactorFormat);
        visit(destination.metalTessellationFactorBytesPerPatch,
            source.metalTessellationFactorBytesPerPatch);
        visit(destination.metalTessellationFactorInstanceStrideBytes,
            source.metalTessellationFactorInstanceStrideBytes);
    }

    template <class Destination, class Source, class Visit>
    static void memberFields(Destination& destination, const Source& source, Visit&& visit)
    {
        visit(destination.name, source.name);
        visit(destination.scalarType, source.scalarType);
        visit(destination.scalarWidth, source.scalarWidth);
        visit(destination.vecSize, source.vecSize);
        visit(destination.matrixRows, source.matrixRows);
        visit(destination.matrixCols, source.matrixCols);
        visit(destination.matrixStride, source.matrixStride);
        visit(destination.rowMajor, source.rowMajor);
        visit(destination.arrayDims, source.arrayDims);
        visit(destination.arrayStride, source.arrayStride);
        visit(destination.offset, source.offset);
        visit(destination.size, source.size);
        visit(destination.decorations, source.decorations);
    }

public:
    static void copy(ShaderReflectionDesc& destination, const shader::ShaderReflection& source)
    {
        const auto assign = [](auto& a, const auto& b) {
            a = b;
        };
        fields(destination, source, assign);
        destination.pushConstantMembers.clear();
        destination.pushConstantMembers.reserve(source.pushConstantMembers.size());
        for (const auto& member : source.pushConstantMembers) {
            ShaderPushConstantMemberDesc copied;
            memberFields(copied, member, assign);
            destination.pushConstantMembers.push_back(std::move(copied));
        }
    }

    static bool matches(
        const ShaderReflectionDesc& destination, const shader::ShaderReflection& source) noexcept
    {
        bool equal = true;
        const auto compare = [&](const auto& a, const auto& b) {
            equal = equal && a == b;
        };
        fields(destination, source, compare);
        if (!equal || destination.pushConstantMembers.size() != source.pushConstantMembers.size())
            return false;
        for (std::size_t i = 0; i < source.pushConstantMembers.size(); ++i)
            memberFields(
                destination.pushConstantMembers[i], source.pushConstantMembers[i], compare);
        return equal;
    }
};

} // namespace arp::rhi
