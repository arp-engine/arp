#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/pipeline_state_copy.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
// Cold metadata ownership only. No authored test reflection/native fixture is
// used as original pipeline provenance or native execution evidence.
class OriginalPipelineMetadataCopyFixture final {
public:
    static void verify()
    {
        auto* resource = &core::defaultCpuMemoryResource();
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        rhi::PipelineLayoutDesc source;
        rhi::DescriptorLayoutDesc descriptor;
        descriptor.name = "original-pipeline-full-descriptor-layout-for-copy";
        rhi::DescriptorBindingDesc binding;
        binding.binding = 7u;
        binding.type = rhi::DescriptorType::SampledTexture;
        binding.stages = rhi::ShaderStage::Fragment;
        binding.arrayCount = 3u;
        binding.depthTexture = true;
        binding.textureViewDimensionConstrained = true;
        binding.textureViewDimension = rhi::TextureViewDimension::Texture2DArray;
        descriptor.bindings.push_back(binding);
        source.descriptorLayouts.push_back(descriptor);
        source.descriptorSets.push_back({5u, descriptor.name});
        rhi::DescriptorTableSlotDesc table;
        table.set = 6u;
        table.tableName = "original-pipeline-full-texture-table-for-copy";
        table.type = rhi::DescriptorType::SampledTexture;
        table.stages = rhi::ShaderStage::Fragment;
        table.capacity = 3u;
        table.depthTexture = true;
        table.textureViewDimensionConstrained = true;
        table.textureViewDimension = rhi::TextureViewDimension::Texture2DArray;
        source.descriptorTables.push_back(table);
        source.pushConstantRanges.push_back({rhi::ShaderStage::Fragment, 0u, 64u});
        const auto layoutFactory = [&] {
            core::CpuMemoryQuotaScope scope(quota);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            core::UniquePtr<rhi::PipelineLayoutDesc> copy;
            try {
                copy = core::makeUnique<rhi::PipelineLayoutDesc>(
                    rhi::PipelineLayoutStateCopy::copy(source));
            } catch (const std::bad_alloc&) {
            }
            std::pmr::set_default_resource(previous);
            return copy;
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto layoutPeak = pressure.measure(layoutFactory);
        {
            auto copy = layoutFactory();
            assert(copy);
            assert(copy->descriptorLayouts.get_allocator().resource() == resource
                && copy->descriptorSets.get_allocator().resource() == resource
                && copy->descriptorTables.get_allocator().resource() == resource
                && copy->pushConstantRanges.get_allocator().resource() == resource);
            const auto& layout = copy->descriptorLayouts[0];
            assert(layout.name == descriptor.name
                && layout.name.get_allocator().resource() == resource
                && layout.bindings.get_allocator().resource() == resource
                && layout.bindings.data() != source.descriptorLayouts[0].bindings.data());
            assert(rhi::descriptorBindingLayoutMatches(binding, layout.bindings[0]));
            assert(copy->descriptorSets[0].layoutName.get_allocator().resource() == resource
                && copy->descriptorSets[0].set == 5u);
            const auto& copiedTable = copy->descriptorTables[0];
            assert(copiedTable.tableName.get_allocator().resource() == resource
                && copiedTable.tableName == table.tableName && copiedTable.set == table.set
                && copiedTable.type == table.type && copiedTable.stages == table.stages
                && copiedTable.capacity == table.capacity
                && copiedTable.depthTexture == table.depthTexture
                && copiedTable.textureViewDimensionConstrained
                    == table.textureViewDimensionConstrained
                && copiedTable.textureViewDimension == table.textureViewDimension);
            source.descriptorLayouts[0].bindings[0].depthTexture = false;
            source.descriptorSets[0].layoutName = "changed";
            source.descriptorTables[0].capacity = 1u;
            assert(copy->descriptorLayouts[0].bindings[0].depthTexture
                && copy->descriptorSets[0].layoutName == descriptor.name
                && copy->descriptorTables[0].capacity == 3u
                && copy->pushConstantRanges[0].sizeBytes == 64u);
            source.descriptorLayouts[0].bindings[0].depthTexture = true;
            source.descriptorSets[0].layoutName = descriptor.name;
            source.descriptorTables[0].capacity = 3u;
        }
        assert(!quota->liveBytes());
        rhi::ShaderPackageDesc package;
        package.label = "original-pipeline-full-shader-package-for-copy";
        package.slangModulePath = "original-pipeline-metadata-copy/complete-source.slang";
        package.artifacts = {"original-pipeline-metadata-copy/complete-source.spv",
            "original-pipeline-metadata-copy/complete-source.metal",
            "original-pipeline-metadata-copy/complete-source.wgsl",
            "original-pipeline-metadata-copy/complete-source.reflection.yaml"};
        package.entrypoints.push_back({package.slangModulePath,
            "vertexEntryForOriginalPipelineMetadataCopy",
            rhi::ShaderStage::Vertex});
        auto& reflection = package.reflection;
        reflection.reflectionContractVersion = "ShaderReflection/3";
        reflection.sourceSha256 = "retained-test-declaration-not-an-artifact-proof";
        reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
        reflection.pushConstantStages = "VS";
        reflection.pushConstantByteCount = 64u;
        rhi::ShaderPushConstantMemberDesc member;
        member.name = "viewProjectionOriginalMetadataCopyMatrix";
        member.scalarType = "float";
        member.scalarWidth = 32u;
        member.matrixRows = member.matrixCols = 4u;
        member.matrixStride = 16u;
        member.size = 64u;
        member.decorations = 5u;
        member.arrayDims.push_back(1u);
        member.arrayStride = 64u;
        reflection.pushConstantMembers.push_back(member);
        reflection.descriptorBindings.push_back({5u,
            7u,
            rhi::DescriptorType::SampledTexture,
            rhi::ShaderStage::Vertex,
            3u,
            UINT32_MAX,
            true});
        const auto packageFactory = [&] {
            core::CpuMemoryQuotaScope scope(quota);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            core::UniquePtr<rhi::ShaderPackageDesc> copy;
            try {
                copy = core::makeUnique<rhi::ShaderPackageDesc>(
                    rhi::ShaderPackageStateCopy::copy(package));
            } catch (const std::bad_alloc&) {
            }
            std::pmr::set_default_resource(previous);
            return copy;
        };
        const auto packagePeak = pressure.measure(packageFactory);
        {
            auto copy = packageFactory();
            assert(copy);
            assert(copy->label == package.label && copy->slangModulePath == package.slangModulePath
                && copy->artifacts.reflectionPath == package.artifacts.reflectionPath
                && !copy->generatedReflectionHydrated);
            assert(copy->entrypoints.get_allocator().resource() == resource
                && copy->entrypoints[0].modulePath.get_allocator().resource() == resource
                && copy->entrypoints[0].entryPoint.get_allocator().resource() == resource
                && copy->entrypoints[0].entryPoint == package.entrypoints[0].entryPoint);
            assert(copy->reflection.sourceSha256 == reflection.sourceSha256
                && copy->reflection.sourceSha256.get_allocator().resource() == resource
                && copy->reflection.descriptorBindings[0].depthTexture
                && copy->reflection.descriptorBindings.get_allocator().resource() == resource);
            const auto& copiedMember = copy->reflection.pushConstantMembers[0];
            assert(copiedMember.name == member.name && copiedMember.matrixRows == 4u
                && copiedMember.matrixCols == 4u && copiedMember.matrixStride == 16u
                && copiedMember.decorations == 5u && !copiedMember.rowMajor
                && copiedMember.name.get_allocator().resource() == resource
                && copiedMember.scalarType.get_allocator().resource() == resource
                && copiedMember.arrayDims.get_allocator().resource() == resource
                && copiedMember.arrayDims.data()
                    != reflection.pushConstantMembers[0].arrayDims.data());
            reflection.pushConstantMembers[0].name = "changed";
            reflection.pushConstantMembers[0].arrayDims[0] = 2u;
            assert(copiedMember.name == member.name && copiedMember.arrayDims[0] == 1u);
            reflection.pushConstantMembers[0].name = member.name;
            reflection.pushConstantMembers[0].arrayDims[0] = 1u;
        }
        const auto reflectionFactory = [&] {
            core::CpuMemoryQuotaScope scope(quota);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            core::UniquePtr<rhi::ShaderReflectionDesc> copy;
            try {
                copy = core::makeUnique<rhi::ShaderReflectionDesc>(
                    rhi::ShaderReflectionStateCopy::copy(reflection));
            } catch (const std::bad_alloc&) {
            }
            std::pmr::set_default_resource(previous);
            return copy;
        };
        const auto reflectionPeak = pressure.measure(reflectionFactory);
        pressure.verify(layoutPeak, layoutFactory);
        pressure.verify(packagePeak, packageFactory);
        pressure.verify(reflectionPeak, reflectionFactory);
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_PIPELINE_METADATA_COPY_OK layout%llu package%llu reflection%llu core-nested-PMR std-default-disabled exact-one-less-exhausted deep-independent metadata-not-factory-proof\n",
            static_cast<unsigned long long>(layoutPeak),
            static_cast<unsigned long long>(packagePeak),
            static_cast<unsigned long long>(reflectionPeak));
    }
};
} // namespace arp::test
