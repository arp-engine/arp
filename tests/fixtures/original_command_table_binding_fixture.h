#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
class OriginalCommandTableBindingFixture final {
    using Contract = rhi::OriginalCommandDescriptorBindingContract;
    using Binding = rhi::OriginalCommandDescriptorBinding;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(stderr,
                "table declaration expected %s got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    // Authored layout metadata has no accepted pipeline/native authority. Even
    // an exact original table/slot declaration must end in DiagnosticFailure.
    static void verify(const std::shared_ptr<const rhi::OriginalCommandResourceSources>& root,
        std::size_t index,
        const rhi::GpuDevice& device)
    {
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        const auto& table =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorTableSource>>(
                root->sources()[index]);
        Binding binding{K::BindComputeDescriptorTable,
            index,
            SIZE_MAX,
            SIZE_MAX,
            table->originalObject()->originalResourceRegistration().generation(),
            table->frozenContentRevision()};
        const rhi::CommandVariableArguments::Input input{
            *Scalar::capture(binding.kind(), Scalar::DescriptorBinding{0u}), {}};
        const auto& definition = table->frozenDefinition();
        rhi::PipelineLayoutDesc authored;
        rhi::DescriptorTableSlotDesc slot;
        slot.tableName = definition.label;
        slot.type = definition.type;
        slot.capacity = definition.capacity;
        slot.stages = definition.stages;
        slot.depthTexture = definition.depthTexture;
        slot.textureViewDimension = definition.textureViewDimension;
        slot.textureViewDimensionConstrained = definition.textureViewDimensionConstrained;
        authored.descriptorTables.push_back(slot);
        const auto check = [&](const auto& b, const auto& layout) noexcept {
            return Contract::diagnoseTableDeclaration(b, input, layout, *root);
        };
        is(check(binding, authored),
            arp::rhi::semantics::OriginalCommandDescriptorBindingLowering::
                tableProfileDiagnostic());
        auto wrong = binding;
        wrong.sourceIndex = SIZE_MAX;
        is(check(wrong, authored), "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SOURCE");
        wrong = binding;
        wrong.sourceIndex = 0u;
        is(check(wrong, authored), "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SOURCE");
        wrong = binding;
        ++wrong.registrationGeneration;
        is(check(wrong, authored), "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-GENERATION");
        wrong = binding;
        ++wrong.contentRevision;
        is(check(wrong, authored), "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-REVISION");
        wrong = binding;
        wrong.commandKind = K::BindRenderDescriptorTable;
        is(check(wrong, authored), "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-KIND");
        for (const auto mutate : {0u, 1u, 2u, 3u}) {
            auto changed = rhi::PipelineLayoutStateCopy::copy(authored);
            auto& s = changed.descriptorTables[0u];
            if (mutate == 0u)
                ++s.capacity;
            if (mutate == 1u)
                s.tableName = "foreign table";
            if (mutate == 2u)
                s.stages = rhi::ShaderStage::Fragment;
            if (mutate == 3u)
                s.set = 1u;
            is(check(binding, changed), "RHI-ORIGINAL-TYPED-INPUT-TABLE-LAYOUT");
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto* diagnostic = CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{table->originalMetadataQuota()}, [&]() noexcept {
                return check(binding, authored);
            });
        std::pmr::set_default_resource(previous);
        is(diagnostic,
            arp::rhi::semantics::OriginalCommandDescriptorBindingLowering::
                tableProfileDiagnostic());
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandDescriptorBindingLowering::tableLowering(
                    backend);
            assert(!decision.positionalAvailable && !decision.executionAvailable
                && decision.mode == rhi::CapabilityMode::DiagnosticFailure);
            is(decision.diagnostic,
                arp::rhi::semantics::OriginalCommandDescriptorBindingLowering::
                    tableProfileDiagnostic());
        }
        std::printf(
            "BINDLESS_RHI_TABLE_POSITIONAL_DIAGNOSTIC_OK backend%u source2 exact-original-root-generation-revision shape-capacity-stage-slot hot0 arrays-actor-missing no-pipeline-substitute state-native-closed\n",
            static_cast<unsigned>(device.backend()));
    }

    static void verifyInvalidated(
        const rhi::OriginalCommandResourceSources& root, std::size_t index)
    {
        const auto& table =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorTableSource>>(
                root.sources()[index]);
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        const Binding binding{K::BindComputeDescriptorTable,
            index,
            SIZE_MAX,
            SIZE_MAX,
            table->originalObject()->originalResourceRegistration().generation(),
            table->frozenContentRevision()};
        const rhi::CommandVariableArguments::Input input{
            *Scalar::capture(binding.kind(), Scalar::DescriptorBinding{0u}), {}};
        rhi::PipelineLayoutDesc noActor;
        const auto* diagnostic = Contract::diagnoseTableDeclaration(binding, input, noActor, root);
        assert(diagnostic
            && core::StringView(diagnostic)
                != arp::rhi::semantics::OriginalCommandDescriptorBindingLowering::
                    tableProfileDiagnostic()
            && core::StringView(diagnostic) != "RHI-ORIGINAL-TYPED-INPUT-TABLE-LAYOUT");
    }
};
} // namespace arp::test
