#pragma once
#include "rhi/frontend/descriptor_state_copy.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_pipeline.h"

#include <cstring>

namespace arp::rhi {
// Cold owning copies for original pipeline definitions. Keep every nested
// allocator explicit: PMR copy construction otherwise selects the ambient
// std default resource. These copies grant no original factory/artifact proof.
class PipelineLayoutStateCopy final {
public:
    static PipelineLayoutDesc copy(const PipelineLayoutDesc& source)
    {
        auto* resource = &core::defaultCpuMemoryResource();
        PipelineLayoutDesc result{core::pmr::Vector<DescriptorLayoutDesc>(resource),
            core::pmr::Vector<DescriptorSetSlotDesc>(resource),
            core::pmr::Vector<DescriptorTableSlotDesc>(resource),
            core::pmr::Vector<PushConstantRange>(resource)};
        result.descriptorLayouts.reserve(source.descriptorLayouts.size());
        for (const auto& original : source.descriptorLayouts) {
            result.descriptorLayouts.push_back(DescriptorStateCopy::copy(original));
        }
        result.descriptorSets.reserve(source.descriptorSets.size());
        for (const auto& original : source.descriptorSets) {
            DescriptorSetSlotDesc set{0u, core::pmr::String(resource)};
            set = original;
            result.descriptorSets.push_back(std::move(set));
        }
        result.descriptorTables.reserve(source.descriptorTables.size());
        for (const auto& original : source.descriptorTables) {
            DescriptorTableSlotDesc table{0u, core::pmr::String(resource)};
            table = original;
            result.descriptorTables.push_back(std::move(table));
        }
        result.pushConstantRanges = source.pushConstantRanges;
        return result;
    }
};

class ShaderReflectionStateCopy final {
public:
    static ShaderReflectionDesc copy(const ShaderReflectionDesc& source)
    {
        ShaderReflectionDesc result;
        // Preconstruct members' names/types/array vectors with their explicit
        // core resources before vector assignment copies nested owners.
        result.pushConstantMembers.resize(source.pushConstantMembers.size());
        result = source;
        return result;
    }
};

class ShaderPackageStateCopy final {
public:
    static ShaderPackageDesc copy(const ShaderPackageDesc& source)
    {
        ShaderPackageDesc result;
        result.entrypoints.resize(source.entrypoints.size());
        result.reflection.pushConstantMembers.resize(source.reflection.pushConstantMembers.size());
        result = source;
        return result;
    }
};

// Preserve each nested core resource during descriptor copying. PMR copy
// construction selects the ambient std default resource, which need not be
// core's allocator. Preconstruct nested owners, then assign into them.
class RenderPipelineStateCopy final {
public:
    static core::pmr::Vector<VertexBufferLayoutDesc> copyVertexBuffers(
        const core::pmr::Vector<VertexBufferLayoutDesc>& source)
    {
        core::pmr::Vector<VertexBufferLayoutDesc> result{&core::defaultCpuMemoryResource()};
        result.resize(source.size());
        result = source;
        return result;
    }

    static RenderPipelineDesc copy(const RenderPipelineDesc& source)
    {
        RenderPipelineDesc result;
        result.vertexBuffers.resize(source.vertexBuffers.size());
        result.renderPassCompatibility.subpasses.resize(
            source.renderPassCompatibility.subpasses.size());
        result = source;
        return result;
    }
};

class ComputePipelineStateCopy final {
public:
    static ComputePipelineDesc copy(const ComputePipelineDesc& source)
    {
        ComputePipelineDesc result;
        result = source;
        return result;
    }
};

// Complete pass descriptor copy. Nested label strings are not allocator-aware
// aggregate members: construct each with core's resource before assignment.
class RenderPassStateCopy final {
public:
    static RenderPassDesc copy(const RenderPassDesc& source)
    {
        auto* resource = &core::defaultCpuMemoryResource();
        RenderPassDesc result{core::pmr::String(resource),
            core::pmr::String(resource),
            core::pmr::Vector<RenderPassColorAttachmentDesc>(resource),
            false,
            RenderPassDepthStencilAttachmentDesc{core::pmr::String(resource)},
            core::pmr::Vector<SubpassDesc>(resource),
            core::pmr::Vector<SubpassDependency>(resource)};
        result.colorAttachments.reserve(source.colorAttachments.size());
        for (std::size_t i = 0u; i < source.colorAttachments.size(); ++i)
            result.colorAttachments.push_back({core::pmr::String(resource)});
        result.subpasses.resize(source.subpasses.size());
        result = source;
        // Preserve inactive fields and signed-zero/NaN object representations.
        for (std::size_t i = 0u; i < source.colorAttachments.size(); ++i)
            std::memcpy(&result.colorAttachments[i].clearColor,
                &source.colorAttachments[i].clearColor,
                sizeof(Color));
        std::memcpy(&result.depthStencilAttachment.depthClearValue,
            &source.depthStencilAttachment.depthClearValue,
            sizeof(float));
        return result;
    }
};
} // namespace arp::rhi
