#pragma once

#include "original_general_finite_consumption_fixture.h"
#include "original_general_finite_inputs_fixture.h"
#include "rhi/frontend/command_arguments.h"

namespace arp::test {
class OriginalGeneralFiniteGraphicsInputsFixture final {
    using Inputs = rhi::OriginalCommandInputs;
    using C = rhi::common::OriginalCommandDescriptorConsumption;
    static constexpr std::uint64_t bytes = 65536u;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "finite Graphics Inputs/RenderUsage: %s\n", id);
        assert(!id);
    }

public:
    static constexpr char passLabel[] = "finite Graphics original attachment";
    static constexpr std::uint64_t passTextBytes = sizeof(passLabel) - 1u;

    static void verifyContract()
    {
        OriginalGeneralFiniteInputsFixture::verifyContract();
        OriginalGeneralFiniteConsumptionFixture::verifyContract();
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    static auto verifyActual(const std::shared_ptr<rhi::OriginalCommandResourceSources>& sources,
        const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>& descriptor,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        bool dependencies = false,
        bool initialized = false)
    {
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        const char* id = nullptr;
        const auto pipelineIndex = descriptor->frozenPipelineSourceIndex();
        const auto descriptorIndex = sources->originalLeaseIndex(descriptor);
        assert(pipelineIndex == 8u && descriptorIndex == 9u);
        const auto& pipeline =
            std::get<std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                sources->sources()[pipelineIndex]);
        const auto& attachment =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                sources->sources()[7u]);
        assert(attachment->originalViewLease()->layout().format == rhi::TextureFormat::Rgba16Float);
        assert(!core::SharedOwnerProjection::sameOwner(attachment,
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                sources->sources()[2u])));
        rhi::RenderPassDesc desc;
        desc.label = passLabel;
        rhi::RenderPassColorAttachmentDesc color;
        color.target = attachment->resource();
        color.format = rhi::TextureFormat::Rgba16Float;
        color.loadOp = rhi::LoadOp::Clear;
        color.storeOp = rhi::StoreOp::Store;
        color.clearColor = {0.f, 0.f, 0.f, 0.f};
        color.hasResolveTarget = false;
        desc.colorAttachments.push_back(color);
        desc.hasDepthStencilAttachment = false;
        desc.layeredRendering = false;
        const rhi::RenderPassArgumentsBudget passBudget{
            1u, true, 1u, 0u, 0u, 0u, passTextBytes, bytes};
        auto shortText = passBudget;
        --shortText.maxTextBytes;
        assert(rhi::RenderPassArguments::validateInput(desc, shortText));
        auto pass = rhi::RenderPassArguments::prepareOriginal(desc, passBudget, quota, id);
        require(id);
        assert(pass);
        const std::size_t prefix = initialized ? 2u : 0u;
        const auto passIndex = prefix, pipelineCommand = prefix + 1u, bindCommand = prefix + 4u,
                   drawCommand = prefix + 5u;
        const auto commandCount = prefix + (dependencies ? 12u : 8u),
                   referenceCount = prefix + (dependencies ? 7u : 3u);
        std::array<rhi::CommandUsage, 14u> commands{};
        if (initialized) {
            commands[0] = {K::TextureBarrier, 0u, 1u};
            commands[1] = {K::TextureBarrier, 1u, 1u};
        }
        commands[passIndex] = {K::BeginRenderPass, prefix, 1u};
        commands[pipelineCommand] = {K::BindGraphicsPipeline, prefix + 1u, 1u};
        commands[prefix + 2u] = {K::SetViewport, prefix + 2u, 0u};
        commands[prefix + 3u] = {K::SetScissor, prefix + 2u, 0u};
        commands[bindCommand] = {K::BindRenderDescriptor, prefix + 2u, 1u};
        commands[drawCommand] = {K::Draw, prefix + 3u, 0u};
        commands[prefix + 6u] = {K::EndRenderPass, prefix + 3u, 0u};
        commands[prefix + 7u] = {K::Finish, prefix + 3u, 0u};
        if (dependencies) {
            commands[prefix + 7u] = {K::BufferMemoryBarrier, prefix + 3u, 1u};
            for (std::size_t i = 8u; i < 11u; ++i)
                commands[prefix + i] = {K::TextureMemoryBarrier, prefix + i - 4u, 1u};
            commands[prefix + 11u] = {K::Finish, prefix + 7u, 0u};
        }
        std::array<rhi::OriginalCommandUsage::Reference, 9u> refs{};
        if (initialized) {
            refs[0] = {3u};
            refs[1] = {6u};
        }
        const std::array<std::size_t, 7u> sourceIndices{
            7u, pipelineIndex, descriptorIndex, 0u, 1u, 3u, 6u};
        for (std::size_t i = 0u; i < sourceIndices.size(); ++i)
            refs[prefix + i] = {sourceIndices[i]};
        auto usage = rhi::OriginalCommandUsage::prepareOriginal(
            sources, commands.data(), commandCount, refs.data(), referenceCount, quota, id);
        require(id);
        assert(usage);
        const auto field = [&](std::size_t i) -> rhi::CommandVariableArguments::Input {
            const auto scalar = i == prefix + 2u
                ? Scalar::capture(
                      commands[i].kind, rhi::ViewportState{0.f, 0.f, 1.f, 1.f, 0.f, 1.f})
                : i == prefix + 3u
                ? Scalar::capture(commands[i].kind, rhi::ScissorState{0, 0, 1u, 1u})
                : i == bindCommand
                ? Scalar::capture(commands[i].kind, Scalar::DescriptorBinding{0u})
                : i == drawCommand ? Scalar::capture(commands[i].kind, Scalar::Draw{3u, 1u, 0u, 0u})
                                   : Scalar::capture(commands[i].kind, Scalar::None{});
            assert(scalar);
            return {*scalar, {}};
        };
        std::array<rhi::CommandVariableArguments::Input, 14u> fields{{field(0u),
            field(1u),
            field(2u),
            field(3u),
            field(4u),
            field(5u),
            field(6u),
            field(7u),
            field(8u),
            field(9u),
            field(10u),
            field(11u),
            field(12u),
            field(13u)}};
        const bool dynamic = descriptor->frozenDefinition().bindings[0].dynamicOffset;
        const auto displacement = descriptor->frozenElements()[0].declaration.dynamicOffset;
        fields[bindCommand].offsets = dynamic ? &displacement : nullptr;
        fields[bindCommand].offsetCount = dynamic ? 1u : 0u;
        const auto arguments = rhi::CommandVariableArguments::prepareOriginal(fields.data(),
            commandCount,
            {1u, true, commandCount, dynamic ? 1u : 0u, 0u, 0u, bytes},
            quota,
            id);
        require(id);
        assert(arguments);
        const auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, quota, id);
        require(id);
        assert(scalars);
        std::array<rhi::ResourceMemoryBarrierDesc, 4u> barriers;
        for (std::size_t i = 0u; i < barriers.size(); ++i) {
            auto& memory = barriers[i];
            const auto index = i == 0u ? 0u : i == 1u ? 1u : i == 2u ? 3u : 6u;
            memory.resource =
                rhi::OriginalCommandResourceSources::resource(sources->sources()[index]);
            memory.sourceStages = memory.destinationStages = i == 3u
                ? rhi::PipelineStage::ColorAttachmentOutput
                : rhi::PipelineStage::FragmentShader;
            memory.sourceAccess = memory.destinationAccess = i == 3u
                ? rhi::ResourceAccess::ColorAttachmentRead
                    | rhi::ResourceAccess::ColorAttachmentWrite
                : i == 1u ? rhi::ResourceAccess::ShaderWrite
                          : rhi::ResourceAccess::ShaderRead;
        }
        std::array<Inputs::OriginalDescriptor, 9u> records{{{0u, pass},
            {0u, pass},
            {0u, pass},
            {0u, pass},
            {0u, pass},
            {0u, pass},
            {0u, pass},
            {0u, pass},
            {0u, pass}}};
        if (initialized) {
            rhi::ResourceBarrierDesc sampled;
            sampled.resource =
                rhi::OriginalCommandResourceSources::resource(sources->sources()[3u]);
            sampled.before = rhi::ResourceState::CopyDst;
            sampled.after = rhi::ResourceState::ShaderRead;
            sampled.sourceStages = rhi::PipelineStage::Transfer;
            sampled.sourceAccess = rhi::ResourceAccess::TransferWrite;
            sampled.destinationStages = rhi::PipelineStage::FragmentShader;
            sampled.destinationAccess = rhi::ResourceAccess::ShaderRead;
            const auto& parent = std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                sources->sources()[3u]);
            sampled.textureSubresources =
                rhi::defaultTextureSubresourceRange(parent->originalObject()->desc());
            assert(sampled.textureSubresources.mipLevelCount == 2u);
            auto attachmentBarrier = sampled;
            attachmentBarrier.resource =
                rhi::OriginalCommandResourceSources::resource(sources->sources()[6u]);
            attachmentBarrier.before = rhi::ResourceState::Undefined;
            attachmentBarrier.after = rhi::ResourceState::ColorAttachment;
            attachmentBarrier.sourceStages = rhi::PipelineStage::TopOfPipe;
            attachmentBarrier.sourceAccess = rhi::ResourceAccess::None;
            attachmentBarrier.destinationStages = rhi::PipelineStage::ColorAttachmentOutput;
            attachmentBarrier.destinationAccess = rhi::ResourceAccess::ColorAttachmentRead
                | rhi::ResourceAccess::ColorAttachmentWrite;
            attachmentBarrier.textureSubresources = rhi::defaultTextureSubresourceRange(
                attachment->originalParentLease()->originalObject()->desc());
            records[0] = {
                0u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, sampled)};
            records[1] = {1u,
                *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, attachmentBarrier)};
        }
        records[prefix] = {passIndex, pass};
        records[prefix + 1u] = {pipelineCommand,
            rhi::OriginalCommandPipelineBinding{K::BindGraphicsPipeline,
                pipelineIndex,
                passIndex,
                pipeline->originalObject()->originalResourceRegistration().generation()}};
        records[prefix + 2u] = {bindCommand,
            rhi::OriginalCommandDescriptorBinding{K::BindRenderDescriptor,
                descriptorIndex,
                pipelineIndex,
                passIndex,
                descriptor->originalObject()->originalResourceRegistration().generation(),
                descriptor->frozenContentRevision()}};
        for (std::size_t i = 0u; i < 4u; ++i)
            records[prefix + 3u + i] = {prefix + 7u + i,
                *rhi::ResourceDependencyArguments::capture(
                    i == 0u ? K::BufferMemoryBarrier : K::TextureMemoryBarrier, barriers[i])};
        const rhi::OriginalCommandInputsBudget budget{6u, true, 1u, 1u, 0u, dynamic ? 1u : 0u};
        const auto make = [&] {
            return Inputs::prepareOriginal(
                scalars, records.data(), referenceCount, budget, quota, id);
        };
        for (std::uint32_t version = 1u; version <= 5u; ++version) {
            auto legacy = budget;
            legacy.contractVersion = version;
            assert(
                !Inputs::prepareOriginal(scalars, records.data(), referenceCount, legacy, quota, id)
                && id);
        }
        for (std::uint32_t field = 0u; field < 7u; ++field) {
            auto changed = records;
            auto& binding =
                std::get<rhi::OriginalCommandDescriptorBinding>(changed[prefix + 2u].arguments);
            switch (field) {
            case 0u:
                binding.sourceIndex = pipelineIndex;
                break;
            case 1u:
                binding.pipelineSourceIndex = 7u;
                break;
            case 2u:
                binding.scopeBeginCommandIndex = passIndex + 1u;
                break;
            case 3u:
                ++binding.registrationGeneration;
                break;
            case 4u:
                ++binding.contentRevision;
                break;
            case 5u:
                changed[prefix].commandIndex = passIndex + 3u;
                break;
            case 6u:
                binding.commandKind = K::BindComputeDescriptor;
                break;
            }
            assert(
                !Inputs::prepareOriginal(scalars, changed.data(), referenceCount, budget, quota, id)
                && id);
        }
        const std::uint32_t wrongOffset = displacement ^ 1u;
        auto alteredFields = fields;
        alteredFields[bindCommand].offsets = dynamic ? &wrongOffset : &displacement;
        alteredFields[bindCommand].offsetCount = 1u;
        auto alteredArguments = rhi::CommandVariableArguments::prepareOriginal(alteredFields.data(),
            commandCount,
            {1u, true, commandCount, 1u, 0u, 0u, bytes},
            quota,
            id);
        require(id);
        assert(alteredArguments);
        auto alteredScalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, alteredArguments, quota, id);
        require(id);
        assert(alteredScalars);
        assert(!Inputs::prepareOriginal(
                   alteredScalars, records.data(), referenceCount, budget, quota, id)
            && id);
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measureRetained(make);
        pressure.verify(peak, make);
        auto inputs = make();
        require(id);
        assert(inputs && !inputs->nativeExecutionProven());
        verifyProjection(inputs, descriptor, device, false, initialized);
        verifyProjection(inputs, descriptor, device, true, initialized);
        if (initialized)
            for (std::size_t i = 0u; i < 2u; ++i) {
                const auto& frozen =
                    std::get<rhi::ResourceDependencyArguments>(records[i].arguments);
                rhi::ResourceBarrierDesc original;
                assert(frozen.write(K::TextureBarrier, original));
                const auto& parent =
                    std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                        sources->sources()[i == 0u ? 3u : 6u]);
                const Inputs::IncomingReference operand{parent->originalObject()};
                Inputs::Incoming incomingBarrier{fields[i], frozen, &operand, 1u};
                require(inputs->compareOriginalInput(i, incomingBarrier));
                for (std::size_t mutation = 0u; mutation < 5u; ++mutation) {
                    auto changed = original;
                    if (mutation == 0u)
                        changed.before = rhi::ResourceState::CopySrc;
                    if (mutation == 1u)
                        changed.after = rhi::ResourceState::CopySrc;
                    if (mutation == 2u)
                        changed.sourceStages = rhi::PipelineStage::ComputeShader;
                    if (mutation == 3u)
                        changed.destinationAccess = rhi::ResourceAccess::None;
                    if (mutation == 4u)
                        changed.textureSubresources.baseMipLevel = 1u;
                    incomingBarrier.descriptor =
                        *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, changed);
                    assert(inputs->compareOriginalInput(i, incomingBarrier));
                }
            }
        const Inputs::IncomingReference reference{descriptor->originalObject()};
        Inputs::Incoming incoming{fields[bindCommand], {}, &reference, 1u};
        require(inputs->compareOriginalInput(bindCommand, incoming));
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota}, [&]() noexcept -> const char* {
                    if (const auto* failure = Inputs::validateOriginal(inputs))
                        return failure;
                    if (const auto* failure = inputs->compareOriginalInput(bindCommand, incoming))
                        return failure;
                    return C::inspectRenderBindings(inputs,
                        device,
                        OriginalGeneralFiniteConsumptionFixture::renderBudget(commandCount))
                        .diagnostic;
                }));
        });
        std::printf(
            "BINDLESS_RHI_GENERAL_GRAPHICS_INPUTS6_RENDERUSAGE4_OK %s displacement%u commands%u original-attachment Fragment4 bindings1 draw-consumption1 accesses4 textures2 sampler1 hostile-refused hot0-query0 native-draw-closed\n",
            dynamic ? "dynamic" : "whole",
            displacement,
            static_cast<unsigned>(commandCount));
        return inputs;
    }

    static void verifyInvalidated(
        const std::shared_ptr<const Inputs>& inputs, const rhi::GpuDevice& device)
    {
        verifyInvalidated(inputs, &device);
    }

    static void verifyInvalidated(
        const std::shared_ptr<const Inputs>& inputs, const rhi::GpuDevice* device)
    {
        assert(Inputs::validateOriginal(inputs));
        std::uint32_t emitted = 0u;
        const auto visitor = [&](const auto&) noexcept -> const char* {
            ++emitted;
            return nullptr;
        };
        if (device) {
            assert(C::visitRenderBindings(inputs,
                       *device,
                       OriginalGeneralFiniteConsumptionFixture::renderBudget(
                           inputs->originalScalars()->originalUsage()->commands().size()),
                       visitor)
                       .diagnostic
                && !emitted);
            assert(C::visit(
                       inputs, *device, OriginalGeneralFiniteConsumptionFixture::budget(), visitor)
                       .diagnostic
                && !emitted);
        } else {
            // A destroyed facade cannot be passed to a device-bound visitor.
            // Its retained registration must reject at the original preflight.
            assert(inputs->originalScalars()->originalUsage()->originalSources()->validate());
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_RENDERUSAGE4_TERMINAL_OK original-inputs-retained invalid-no-output native-draw-closed");
    }

private:
    static void verifyProjection(const std::shared_ptr<const Inputs>& inputs,
        const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>& descriptor,
        const rhi::GpuDevice& device,
        bool consumption,
        bool initialized = false)
    {
        const std::size_t prefix = initialized ? 2u : 0u;
        const auto commandCount = inputs->originalScalars()->originalUsage()->commands().size();
        std::array<C::Access, 4u> projected;
        std::size_t emitted = 0u;
        const auto visitor = [&](const C::Access& access) noexcept -> const char* {
            assert(emitted < projected.size());
            projected[emitted++] = access;
            return nullptr;
        };
        if (consumption) {
            const auto result = C::visit(
                inputs, device, OriginalGeneralFiniteConsumptionFixture::budget(), visitor);
            require(result.diagnostic);
            assert(result.consumptions == 1u && result.logicalAccesses == 4u
                && result.textureAccesses == 2u && result.samplerReferences == 1u
                && !result.nativeExecutionProven());
        } else {
            const auto result = C::visitRenderBindings(inputs,
                device,
                OriginalGeneralFiniteConsumptionFixture::renderBudget(commandCount),
                visitor);
            require(result.diagnostic);
            assert(result.bindings == 1u && result.logicalAccesses == 4u
                && result.textureAccesses == 2u && result.samplerReferences == 1u
                && !result.nativeExecutionProven());
        }
        assert(emitted == 4u);
        const auto& sources = *inputs->originalScalars()->originalUsage()->originalSources();
        rhi::DescriptorBufferBindingRangeContract::Resolved interval;
        const auto& buffer =
            std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(sources.sources()[0u]);
        const auto& selected = descriptor->frozenElements()[0].declaration;
        require(
            rhi::DescriptorBufferBindingRangeContract::resolve(selected.write.bufferBindingRange,
                buffer->layout().sizeBytes,
                selected.dynamicOffset,
                interval));
        for (std::size_t i = 0u; i < projected.size(); ++i) {
            const auto& point = projected[i];
            const auto& original = descriptor->frozenElements()[i].declaration;
            assert(point.commandIndex == (prefix + (consumption ? 5u : 4u))
                && point.scopeBeginCommandIndex == prefix && point.pipelineSourceIndex == 8u
                && point.pipelineBindCommandIndex == prefix + 1u
                && point.descriptorSourceIndex == 9u
                && point.descriptorBindCommandIndex == prefix + 4u
                && point.descriptorContentRevision == descriptor->frozenContentRevision()
                && point.set == 0u && point.binding == original.write.binding
                && point.registrationGeneration == original.registrationGeneration
                && point.stages == rhi::PipelineStage::FragmentShader
                && point.access == original.access && point.state == original.write.resourceState
                && !point.nativeExecutionProven());
            if (i == 0u)
                assert(point.kind == C::Access::Kind::Buffer && point.bufferSourceIndex == 0u
                    && point.offsetBytes == interval.offsetBytes
                    && point.sizeBytes == interval.sizeBytes);
            else if (i == 3u)
                assert(point.kind == C::Access::Kind::Sampler && point.samplerSourceIndex == 5u
                    && point.state == rhi::ResourceState::Undefined
                    && point.access == rhi::ResourceAccess::None);
            else {
                const auto viewIndex = i == 1u ? 2u : 4u;
                const auto& view =
                    std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                        sources.sources()[viewIndex]);
                const auto& layout = view->originalViewLease()->layout();
                assert(point.kind == C::Access::Kind::TextureView
                    && point.textureViewSourceIndex == viewIndex
                    && point.textureParentSourceIndex == viewIndex - 1u
                    && point.textureRange.baseMipLevel == layout.baseMipLevel
                    && point.textureRange.mipLevelCount == layout.mipLevelCount
                    && point.textureRange.baseArrayLayer == layout.baseArrayLayer
                    && point.textureRange.arrayLayerCount == layout.arrayLayerCount
                    && point.textureRange.aspect == layout.aspect);
            }
        }
        for (std::uint32_t field = 0u; field < 5u; ++field) {
            auto request = OriginalGeneralFiniteConsumptionFixture::renderBudget(commandCount);
            if (field == 0u)
                --request.maxCommands;
            if (field == 1u)
                request.maxLogicalAccesses = 3u;
            if (field == 2u)
                request.maxTextureAccesses = 1u;
            if (field == 3u)
                request.generalShaderResourcesOptIn = false;
            if (field == 4u)
                request.generalShaderResourcesVersion = 1u;
            emitted = 0u;
            assert(C::visitRenderBindings(inputs, device, request, visitor).diagnostic && !emitted);
        }
    }
#endif
};
} // namespace arp::test
