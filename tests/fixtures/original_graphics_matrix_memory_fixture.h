#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/semantics/bindless_graphics_member_memory_lowering.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
// Metadata inspection over the same retained Root, never an execution substitute.
class OriginalGraphicsMatrixMemoryFixture final {
public:
    template <class Sources>
    static void inspect(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const Sources& sources,
        rhi::GpuDevice* device,
        bool fault,
        bool undersized = false,
        std::array<std::uint64_t, 2u> ordinaryBytes = {320u, 16u})
    {
        using Root = rhi::OriginalCommandBindlessRootSource;
        shader::TargetBufferMemoryLayout facts;
        rhi::ShaderBufferMemoryRequirement requirement;
        rhi::ShaderBufferMemberMemory members;
        rhi::BindlessResourceAccess access;
        std::size_t source = 0u;
        const auto fragment = fault ? "faultFragment" : "cleanFragment";
        if (!device) {
            members = {1u, 1u, rhi::ShaderBufferMemberMemory::Evidence::ValidatedTargetMembers};
            assert(Root::originalOrdinaryBufferMemberMemory(
                       root, fragment, 0u, 0u, members, access, source)
                && !members.sourceOccupiedBytes && !members.targetMinimumBytes
                && members.evidence == rhi::ShaderBufferMemberMemory::Evidence::None
                && !access.resource.isValid() && source == SIZE_MAX);
            assert(Root::validateOriginalGraphicsBufferRanges(root));
            assert(Root::originalOrdinaryBufferRequirement(
                       root, fragment, 0u, 0u, requirement, access, source)
                && !requirement.minimumBytes && !access.resource.isValid() && source == SIZE_MAX);
            assert(Root::originalOrdinaryBufferMemory(
                       root, fragment, 0u, 0u, facts, access, source, 2u)
                && !facts.target.minimumBindingBytes && !access.resource.isValid()
                && source == SIZE_MAX);
            return;
        }
        const auto& prepared =
            std::get<std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>>(
                root->originalPrepared());
        const auto& pipeline = *prepared->owners()->desc().pipeline;
        const auto& package = root->authorization()->package();
        const auto quota = root->originalMetadataQuota();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                const auto* admission = Root::validateOriginalGraphicsBufferRanges(root);
                assert(undersized ? admission
                            && core::StringView(admission) == "RHI-BINDLESS-ORDINARY-MEMORY-RANGE"
                                  : !admission);
                for (const auto version : {0u, 2u, 99u})
                    assert(Root::validateOriginalGraphicsBufferRanges(root, version));
                for (const auto stage : {rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment}) {
                    const auto name = stage == rhi::ShaderStage::Vertex ? "vertexMain" : fragment;
                    rhi::ShaderStage selected = rhi::ShaderStage::Compute;
                    const char* diagnostic = nullptr;
                    const auto* module = rhi::selectBindlessGraphicsEntryAccess(
                        package, pipeline, name, selected, diagnostic);
                    assert(module && !diagnostic && selected == stage && module->entry == name
                        && module == pipeline.bindlessShaderModule(stage, 1u));
                    assert(!pipeline.bindlessShaderModule(stage, 0u)
                        && !pipeline.bindlessShaderModule(stage, 2u));
                    for (std::uint32_t binding = 0u; binding != 2u; ++binding) {
                        const bool hasMemberProof = device->backend() == rhi::Backend::Vulkan
                            || package.artifact().execution == rhi::BindlessExecution::Emulated
                            || (device->backend() == rhi::Backend::Metal
                                && module->nativeMemory.version() == 1u);
                        const auto* memberId = Root::originalOrdinaryBufferMemberMemory(
                            root, name, 0u, binding, members, access, source);
                        if (!hasMemberProof || (undersized && !binding)) {
                            assert(memberId && !members.sourceOccupiedBytes
                                && !members.targetMinimumBytes
                                && members.evidence == rhi::ShaderBufferMemberMemory::Evidence::None
                                && !access.resource.isValid() && source == SIZE_MAX);
                        } else {
                            const auto bytes = ordinaryBytes[binding];
                            const auto decision =
                                rhi::semantics::OriginalBindlessGraphicsMemberMemoryLowering::
                                    lowering(device->backend());
                            assert(!memberId && members.sourceOccupiedBytes == bytes
                                && members.targetMinimumBytes == bytes
                                && members.evidence == decision.evidence
                                && access.bufferSize == bytes && access.stages == stage
                                && source < sources->sources().size());
                            const auto* original =
                                std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                                    &sources->sources()[source]);
                            assert(original && (*original)->resource() == access.resource);
                        }
                        const auto* member2 = Root::originalOrdinaryBufferMemberMemory(
                            root, name, 0u, binding, members, access, source, 2u);
                        if (!hasMemberProof || (undersized && !binding)) {
                            assert(member2 && !members.sourceOccupiedBytes
                                && !members.targetMinimumBytes && !members.sourceMinimumBytes
                                && !members.hasRuntimeArray && !members.sourceElementCapacity
                                && !access.resource.isValid() && source == SIZE_MAX);
                        } else {
                            assert(!member2 && members.sourceOccupiedBytes == ordinaryBytes[binding]
                                && members.sourceMinimumBytes == ordinaryBytes[binding]
                                && !members.hasRuntimeArray && !members.sourceElementCapacity
                                && !members.sourceRuntimeArrayOffsetBytes
                                && !members.sourceRuntimeArrayStrideBytes
                                && !members.targetRuntimeArrayOffsetBytes
                                && !members.targetRuntimeArrayStrideBytes
                                && access.bufferSize == ordinaryBytes[binding]
                                && source < sources->sources().size());
                        }
                        for (const auto version : {0u, 3u, 99u})
                            assert(pipeline.bindlessOrdinaryBufferMemberMemory(
                                       stage, 0u, binding, members, version)
                                && !members.sourceOccupiedBytes && !members.targetMinimumBytes
                                && members.evidence
                                    == rhi::ShaderBufferMemberMemory::Evidence::None);
                        const auto* rangeId = Root::originalOrdinaryBufferRequirement(
                            root, name, 0u, binding, requirement, access, source);
                        if (undersized && binding == 0u) {
                            assert(rangeId && !requirement.minimumBytes
                                && !access.resource.isValid() && source == SIZE_MAX);
                        } else {
                            const auto bytes = ordinaryBytes[binding];
                            using Evidence = rhi::ShaderBufferMemoryRequirement::Evidence;
                            const auto evidence = device->backend() == rhi::Backend::Vulkan
                                ? Evidence::BinaryDeclaration
                                : device->backend() == rhi::Backend::Metal
                                ? Evidence::DriverQuery
                                : Evidence::ValidatedTargetDeclaration;
                            assert(!rangeId && requirement.sourceOccupiedBytes == bytes
                                && requirement.minimumBytes == bytes
                                && requirement.evidence == evidence && access.bufferSize == bytes
                                && access.stages == stage && source < sources->sources().size());
                            if (device->backend() == rhi::Backend::Metal)
                                assert(requirement.alignmentBytes
                                    && !(access.bufferOffset % requirement.alignmentBytes));
                            const auto* original =
                                std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                                    &sources->sources()[source]);
                            assert(original && (*original)->resource() == access.resource);
                        }
                        const auto* range2 = Root::originalOrdinaryBufferRequirement(
                            root, name, 0u, binding, requirement, access, source, 2u);
                        assert(undersized && binding == 0u ? range2 && !requirement.minimumBytes
                                    && !access.resource.isValid() && source == SIZE_MAX
                                                           : !range2
                                    && requirement.sourceMinimumBytes == ordinaryBytes[binding]
                                    && !requirement.hasRuntimeArray
                                    && !requirement.sourceElementCapacity);
                        for (const auto version : {0u, 3u, 99u}) {
                            assert(pipeline.bindlessOrdinaryBufferRequirement(
                                       stage, 0u, binding, requirement, version)
                                && !requirement.minimumBytes);
                        }
                        const auto* id = Root::originalOrdinaryBufferMemory(
                            root, name, 0u, binding, facts, access, source, 2u);
                        // Native declaration-to-target memory proof is a separate
                        // contract. Do not fabricate WGSL facts for MSL/SPIR-V.
                        if (package.artifact().execution == rhi::BindlessExecution::Native) {
                            assert(id
                                && core::StringView(id) == "RHI-BINDLESS-PACKAGE-MEMORY-BINDING"
                                && !facts.target.minimumBindingBytes && !access.resource.isValid()
                                && source == SIZE_MAX);
                        } else if (undersized && binding == 0u) {
                            assert(id
                                && core::StringView(id) == "RHI-BINDLESS-ORDINARY-MEMORY-RANGE"
                                && !facts.target.minimumBindingBytes && !access.resource.isValid()
                                && source == SIZE_MAX);
                        } else {
                            const auto bytes = ordinaryBytes[binding];
                            assert(!id && facts.target.minimumBindingBytes == bytes
                                && facts.source.staticOccupiedExtentBytes == bytes
                                && access.bufferSize == bytes && access.stages == stage
                                && source < sources->sources().size());
                            const auto* original =
                                std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                                    &sources->sources()[source]);
                            assert(original && (*original)->resource() == access.resource);
                        }
                    }
                }
                rhi::ShaderStage selected = rhi::ShaderStage::Compute;
                const char* diagnostic = nullptr;
                assert(!rhi::selectBindlessGraphicsEntryAccess(package,
                           pipeline,
                           fault ? "cleanFragment" : "faultFragment",
                           selected,
                           diagnostic)
                    && diagnostic && selected == rhi::ShaderStage::None);
                assert(!pipeline.bindlessShaderModule(rhi::ShaderStage::Compute, 1u)
                    && !pipeline.bindlessShaderModule(
                        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 1u));
                for (const auto name : {fault ? "cleanFragment" : "faultFragment", "absent"})
                    assert(Root::originalOrdinaryBufferMemory(
                               root, name, 0u, 0u, facts, access, source, 2u)
                        && !facts.target.minimumBindingBytes && !access.resource.isValid()
                        && source == SIZE_MAX);
                auto forged = std::shared_ptr<const Root>(quota, root.get());
                assert(Root::originalOrdinaryBufferMemberMemory(
                           forged, fragment, 0u, 0u, members, access, source)
                    && !members.sourceOccupiedBytes && !members.targetMinimumBytes
                    && members.evidence == rhi::ShaderBufferMemberMemory::Evidence::None
                    && !access.resource.isValid() && source == SIZE_MAX);
                for (const auto version : {0u, 3u, 99u})
                    assert(Root::originalOrdinaryBufferMemberMemory(
                               root, fragment, 0u, 0u, members, access, source, version)
                        && !members.sourceOccupiedBytes && !members.targetMinimumBytes
                        && !access.resource.isValid() && source == SIZE_MAX);
                for (const auto name : {fault ? "cleanFragment" : "faultFragment", "absent"})
                    assert(Root::originalOrdinaryBufferMemberMemory(
                               root, name, 0u, 0u, members, access, source)
                        && !members.sourceOccupiedBytes && !members.targetMinimumBytes
                        && !access.resource.isValid() && source == SIZE_MAX);
                assert(Root::originalOrdinaryBufferMemberMemory(
                           root, fragment, 0u, UINT32_MAX, members, access, source)
                    && !members.sourceOccupiedBytes && !members.targetMinimumBytes
                    && !access.resource.isValid() && source == SIZE_MAX);
                for (const auto stage : {rhi::ShaderStage::Compute,
                         rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment})
                    assert(pipeline.bindlessOrdinaryBufferMemberMemory(stage, 0u, 0u, members, 1u)
                        && !members.sourceOccupiedBytes && !members.targetMinimumBytes);
                assert(Root::validateOriginalGraphicsBufferRanges(forged));
                for (const auto name : {fault ? "cleanFragment" : "faultFragment", "absent"})
                    assert(Root::originalOrdinaryBufferRequirement(
                               root, name, 0u, 0u, requirement, access, source)
                        && !requirement.minimumBytes && !access.resource.isValid()
                        && source == SIZE_MAX);
                assert(Root::originalOrdinaryBufferRequirement(
                           forged, fragment, 0u, 0u, requirement, access, source)
                    && !requirement.minimumBytes && !access.resource.isValid()
                    && source == SIZE_MAX);
                for (const auto version : {0u, 3u, 99u})
                    assert(Root::originalOrdinaryBufferRequirement(
                               root, fragment, 0u, 0u, requirement, access, source, version)
                        && !requirement.minimumBytes && !access.resource.isValid()
                        && source == SIZE_MAX);
                assert(Root::originalOrdinaryBufferMemory(
                           forged, fragment, 0u, 0u, facts, access, source, 2u)
                    && !facts.target.minimumBindingBytes && !access.resource.isValid()
                    && source == SIZE_MAX);
                for (const auto version : {0u, 1u, 99u})
                    assert(Root::originalOrdinaryBufferMemory(
                               root, fragment, 0u, 0u, facts, access, source, version)
                        && !facts.target.minimumBindingBytes && !access.resource.isValid()
                        && source == SIZE_MAX);
                return nullptr;
            }));
        std::printf(
            "BINDLESS_GRAPHICS_MEMBER_MEMORY1_OK backend%u fault%u undersized%u original-static-members hot0 no-execution-grant\n",
            static_cast<unsigned>(device->backend()),
            fault ? 1u : 0u,
            undersized ? 1u : 0u);
        std::printf(
            "BINDLESS_GRAPHICS_RANGE_ADMISSION1_OK backend%u fault%u undersized%u all-selected-buffers hot0\n",
            static_cast<unsigned>(device->backend()),
            fault ? 1u : 0u,
            undersized ? 1u : 0u);
        std::printf(
            "BINDLESS_GRAPHICS_BUFFER_RANGE1_OK backend%u fault%u undersized%u binary-target-driver hot0 original-range no-layout-equivalence-grant\n",
            static_cast<unsigned>(device->backend()),
            fault ? 1u : 0u,
            undersized ? 1u : 0u);
        std::printf(
            "BINDLESS_GRAPHICS_ROOT_MEMORY2_OK backend%u fault%u undersized%u selected-entry1 original-owner hot0 no-execution-grant\n",
            static_cast<unsigned>(device->backend()),
            fault ? 1u : 0u,
            undersized ? 1u : 0u);
    }
};
} // namespace arp::test
