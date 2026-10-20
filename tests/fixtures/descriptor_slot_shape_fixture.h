#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_usage.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class DescriptorSlotShapeFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using Shape = DescriptorSlotShapeContract;
        using Match = Shape::TableMatch;
        using Typed = OriginalCommandTypedInputContract;
        using K = CommandUsageKind;
        using S = CommandScalarArguments;
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        DescriptorLayoutDesc set;
        set.name = "authored descriptor slot shape with a non-small complete layout name";
        set.bindings = {{7u, DescriptorType::UniformBuffer, ShaderStage::Compute}};
        set.bindings[0].dynamicOffset = true;
        PipelineLayoutDesc pipeline;
        pipeline.descriptorLayouts.push_back(set);
        pipeline.descriptorSets.push_back({2u, set.name});
        DescriptorTableDesc table;
        table.label = "authored original depth table with a non-small complete table name";
        table.type = DescriptorType::SampledTexture;
        table.stages = ShaderStage::Compute;
        table.capacity = 3u;
        table.textureViewDimensionConstrained = true;
        table.textureViewDimension = TextureViewDimension::Texture2D;
        table.depthTexture = true;
        pipeline.descriptorTables.push_back({4u,
            table.label,
            table.type,
            table.stages,
            table.capacity,
            table.textureViewDimensionConstrained,
            table.textureViewDimension,
            table.depthTexture});
        const std::uint32_t offset = 0u;
        const CommandVariableArguments::Input setInput{
            *S::capture(K::BindComputeDescriptor, S::DescriptorBinding{2u}), {}, &offset, 1u};
        const CommandVariableArguments::Input tableInput{
            *S::capture(K::BindComputeDescriptorTable, S::DescriptorBinding{4u}), {}};
        auto setWrongSlot = setInput;
        setWrongSlot.scalar = *S::capture(K::BindComputeDescriptor, S::DescriptorBinding{4u});
        auto tableWrongSlot = tableInput;
        tableWrongSlot.scalar =
            *S::capture(K::BindComputeDescriptorTable, S::DescriptorBinding{2u});
        auto setWrongKind = setInput;
        setWrongKind.scalar = *S::capture(K::BindComputeDescriptorTable, S::DescriptorBinding{2u});
        auto tableWrongKind = tableInput;
        tableWrongKind.scalar = *S::capture(K::BindComputeDescriptor, S::DescriptorBinding{4u});
        auto noOffsets = setInput;
        noOffsets.offsetCount = 0u;
        auto missingOffsets = setInput;
        missingOffsets.offsets = nullptr;
        auto excessOffsets = setInput;
        excessOffsets.offsetCount = 2u;
        auto tableWithOffsets = tableInput;
        tableWithOffsets.offsets = &offset;
        tableWithOffsets.offsetCount = 1u;
        // Every binding field participates in the shared authored equality.
        std::array<DescriptorLayoutDesc, 16> changed;
        for (auto& definition : changed)
            definition = DescriptorStateCopy::copy(set);
        changed[0].name = "foreign layout";
        changed[1].bindings[0].binding = 8u;
        changed[2].bindings[0].type = DescriptorType::StorageBuffer;
        changed[3].bindings[0].stages = ShaderStage::Compute | ShaderStage::Fragment;
        changed[4].bindings[0].arrayCount = 2u;
        changed[5].bindings[0].tableBacked = true;
        changed[6].bindings[0].dynamicOffset = false;
        changed[7].bindings[0].textureViewDimensionConstrained = true;
        changed[8].bindings[0].textureViewDimension = TextureViewDimension::Texture3D;
        changed[9].bindings[0].depthTexture = true;
        changed[10].bindings[0].storageTextureFormat = TextureFormat::R32Float;
        changed[11].bindings[0].storageTextureAccess = StorageTextureAccess::ReadOnly;
        changed[12].bindings[0].sampledTextureFilterable = false;
        changed[13].bindings[0].comparisonSampler = true;
        changed[14].bindings.clear();
        changed[15].bindings.push_back({8u, DescriptorType::UniformBuffer, ShaderStage::Compute});
        std::array<DescriptorTableDesc, 10> changedTables;
        for (auto& definition : changedTables)
            definition = DescriptorStateCopy::copy(table);
        changedTables[0].label = "foreign table";
        changedTables[1].type = DescriptorType::StorageTexture;
        changedTables[2].stages = ShaderStage::Fragment;
        changedTables[3].capacity = 2u;
        changedTables[4].textureViewDimensionConstrained = false;
        changedTables[5].textureViewDimension = TextureViewDimension::Texture3D;
        changedTables[6].depthTexture = false;
        changedTables[7].stages = ShaderStage::Compute | ShaderStage::Fragment;
        changedTables[8].capacity = 4u;
        changedTables[9].stages = ShaderStage::Compute | ShaderStage::Fragment;
        changedTables[9].capacity = 4u;
        // Compatibility adapter retains the legacy result and messages. It is
        // cold; the string-owning ValidationResult is not a hot receipt.
        for (std::size_t i = 7u; i < changedTables.size(); ++i)
            assert(validateDescriptorTableSlot(pipeline, 4u, changedTables[i]).success);
        for (std::size_t i = 0u; i < 7u; ++i) {
            const auto old = validateDescriptorTableSlot(pipeline, 4u, changedTables[i]);
            assert(!old.success
                && old.diagnostics
                    == Shape::validateTable(pipeline, 4u, changedTables[i], Match::Coverage));
        }
        const auto oldMissing = validateDescriptorTableSlot(pipeline, 99u, table);
        assert(!oldMissing.success
            && oldMissing.diagnostics
                == Shape::validateTable(pipeline, 99u, table, Match::Coverage));
        assert(!validateDynamicOffsetArguments(set, nullptr, 1u).success);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                assert(!Shape::validateSet(pipeline, 2u, set));
                assert(!Shape::validateTable(pipeline, 4u, table, Match::ExactOriginal));
                for (std::size_t i = 7u; i < changedTables.size(); ++i) {
                    assert(!Shape::validateTable(pipeline, 4u, changedTables[i], Match::Coverage));
                    assert(std::string_view(Shape::validateTable(
                               pipeline, 4u, changedTables[i], Match::ExactOriginal))
                        == "RHI-DESCRIPTOR-TABLE-EXACT-SLOT-MISMATCH");
                }
                assert(Shape::validateTable(pipeline, 4u, table, static_cast<Match>(255)));
                for (const auto& definition : changed)
                    assert(Typed::validateSetShape(setInput, pipeline, definition));
                for (const auto& definition : changedTables)
                    assert(Typed::validateTableShape(
                        tableInput, pipeline, definition, Match::ExactOriginal));
                assert(!Typed::validateSetShape(setInput, pipeline, set));
                assert(
                    !Typed::validateTableShape(tableInput, pipeline, table, Match::ExactOriginal));
                assert(Typed::validateSetShape(setWrongSlot, pipeline, set));
                assert(Typed::validateSetShape(setWrongKind, pipeline, set));
                assert(Typed::validateTableShape(
                    tableWrongSlot, pipeline, table, Match::ExactOriginal));
                assert(Typed::validateTableShape(
                    tableWrongKind, pipeline, table, Match::ExactOriginal));
                assert(Typed::validateTableShape(
                    tableWithOffsets, pipeline, table, Match::ExactOriginal));
                assert(Typed::validateSetShape(noOffsets, pipeline, set));
                assert(Typed::validateSetShape(missingOffsets, pipeline, set));
                assert(Typed::validateSetShape(excessOffsets, pipeline, set));
                assert(validateDynamicOffsetArgumentsDiagnostic(set, nullptr, 1u));
                assert(!validateDynamicOffsetArgumentsDiagnostic(set, &offset, 1u));
                return nullptr;
            }));
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_SLOT_SHAPE_OK exact-original1 legacy-coverage-preserved full-fields count-pointer exhausted-hostile-pmr hot0 positional-native-closed");
    }
};
} // namespace arp::test
