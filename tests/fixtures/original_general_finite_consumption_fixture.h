#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/frontend/command_usage.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalGeneralFiniteConsumptionFixture final {
    using C = rhi::common::OriginalCommandDescriptorConsumption;
    using Contract = rhi::OriginalCommandDescriptorConsumptionContract;

public:
    static constexpr C::Budget budget() noexcept
    {
        C::Budget value{4u, true, 1u, 1u, true, true, 1u, 4u};
        value.maxTextureAccesses = 2u;
        value.generalShaderResourcesVersion = 2u;
        value.generalShaderResourcesOptIn = true;
        return value;
    }

    static constexpr C::RenderBindingBudget renderBudget(std::uint64_t commands = 6u) noexcept
    {
        C::RenderBindingBudget value{4u, true, 1u, 1u, true, true, commands, 1u, 4u};
        value.maxTextureAccesses = 2u;
        value.generalShaderResourcesVersion = 2u;
        value.generalShaderResourcesOptIn = true;
        return value;
    }

    static void verifyContract()
    {
        const auto request = budget();
        const auto render = renderBudget();
        assert(!C::validateBudget(request) && !render.diagnostic());
        for (std::uint32_t footprint = 0u; footprint <= 5u; ++footprint)
            for (std::uint32_t inputs = 0u; inputs <= 7u; ++inputs)
                for (std::uint32_t general = 0u; general <= 3u; ++general)
                    for (bool opted : {false, true}) {
                        const bool legacy = !general && !opted && footprint >= 1u && footprint <= 3u
                            && inputs == footprint + 2u;
                        const bool whole = general == 1u && opted
                            && ((footprint == 2u && (inputs == 3u || inputs == 4u))
                                || (footprint == 3u && inputs == 5u));
                        const bool mixed =
                            footprint == 4u && general == 2u && opted && inputs == 6u;
                        assert(Contract::inputsProfileMatches(footprint, general, opted, inputs)
                            == (legacy || whole || mixed));
                    }
        for (std::uint32_t field = 0u; field < 11u; ++field) {
            auto bad = request;
            switch (field) {
            case 0u:
                bad.prototypeOptIn = false;
                break;
            case 1u:
                bad.generalShaderResourcesVersion = 1u;
                break;
            case 2u:
                bad.generalShaderResourcesOptIn = false;
                break;
            case 3u:
                bad.depthTableFootprintVersion = 1u;
                break;
            case 4u:
                bad.depthTableFullBoundViewOptIn = true;
                break;
            case 5u:
                bad.indexedDrawConsumptionVersion = 1u;
                break;
            case 6u:
                bad.vertexStreamConsumptionOptIn = true;
                break;
            case 7u:
                bad.finiteFootprintVersion = 2u;
                break;
            case 8u:
                bad.maxConsumptions = 0u;
                break;
            case 9u:
                bad.maxLogicalAccesses = 0u;
                break;
            case 10u:
                bad.maxTextureAccesses = 5u;
                break;
            }
            assert(C::validateBudget(bad) && !Contract::matches(request, bad));
        }
        for (std::uint32_t field = 0u; field < 8u; ++field) {
            auto bad = render;
            switch (field) {
            case 0u:
                bad.generalShaderResourcesVersion = 1u;
                break;
            case 1u:
                bad.generalShaderResourcesOptIn = false;
                break;
            case 2u:
                bad.depthTableFootprintVersion = 1u;
                break;
            case 3u:
                bad.depthTableFullBoundViewOptIn = true;
                break;
            case 4u:
                bad.maxCommands = 0u;
                break;
            case 5u:
                bad.maxBindings = 0u;
                break;
            case 6u:
                bad.maxLogicalAccesses = 0u;
                break;
            case 7u:
                bad.maxTextureAccesses = 5u;
                break;
            }
            assert(bad.diagnostic() && !render.matches(bad));
        }
        // No buffer permission is inferred for texture/sampler-only requests.
        // The genuine element walker independently refuses any absent buffer
        // footprint opt-in. Zero texture capacity likewise authorizes no texture.
        auto textureOnly = request;
        textureOnly.finiteFootprintVersion = textureOnly.wholeFootprintVersion = 0u;
        textureOnly.finiteFullBoundViewOptIn = textureOnly.wholeFullBoundViewOptIn = false;
        assert(!C::validateBudget(textureOnly));
        auto bufferOnly = request;
        bufferOnly.maxTextureAccesses = 0u;
        assert(!C::validateBudget(bufferOnly));
        assert(!Contract::isConsumption(rhi::CommandUsageKind::DrawIndexed, request));
        assert(!Contract::isConsumption(rhi::CommandUsageKind::DrawIndexedIndirect, request));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto consumption =
                rhi::semantics::OriginalCommandDescriptorConsumptionLowering::lowering(backend, 4u);
            const auto binding = rhi::OriginalRenderDescriptorUsageContract::lowering(backend, 4u);
            for (const auto decision : {consumption, binding}) {
                assert(!decision.stateAvailable && !decision.executionAvailable);
                assert(decision.validatorOnly == (backend == rhi::Backend::Null));
                assert(decision.projectionAvailable == (backend == rhi::Backend::WebGPU));
                assert(bool(decision.diagnostic) != decision.projectionAvailable);
            }
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_CONSUMPTION4_CONTRACT_OK GeneralResources2 Inputs6 exact-pairing legacy-isolated tables-geometry-refused independent-footprints dry-no-state-execution");
    }

    static void verifyUnsupported(const rhi::GpuDevice& device)
    {
        std::uint32_t emitted = 0u;
        const auto visitor = [&](const auto&) noexcept -> const char* {
            ++emitted;
            return nullptr;
        };
        const auto consumption = C::visit({}, device, budget(), visitor);
        const auto render = C::visitRenderBindings({}, device, renderBudget(), visitor);
        assert(consumption.diagnostic && render.diagnostic && !emitted);
        assert(!consumption.nativeExecutionProven() && !render.nativeExecutionProven());
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    static void verifyActual(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        bool dependencies = false)
    {
        const auto& sources = *inputs->originalScalars()->originalUsage()->originalSources();
        const auto& descriptor =
            *std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                sources.sources()[7u]);
        rhi::DescriptorBufferBindingRangeContract::Resolved range;
        const auto& buffer =
            std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(sources.sources()[0u]);
        const auto& element = descriptor.frozenElements()[0u].declaration;
        assert(!rhi::DescriptorBufferBindingRangeContract::resolve(element.write.bufferBindingRange,
            buffer->layout().sizeBytes,
            element.dynamicOffset,
            range));
        std::array<C::Access, 4u> projected{};
        std::size_t emitted = 0u;
        const auto visitor = [&](const C::Access& access) noexcept -> const char* {
            assert(emitted < projected.size());
            projected[emitted++] = access;
            return nullptr;
        };
        const auto result = C::visit(inputs, device, budget(), visitor);
        if (result.diagnostic)
            std::fprintf(stderr, "General Consumption4: %s\n", result.diagnostic);
        assert(!result.diagnostic && emitted == 4u && result.consumptions == 1u
            && result.logicalAccesses == 4u && result.textureAccesses == 2u
            && result.samplerReferences == 1u);
        for (std::size_t i = 0u; i < projected.size(); ++i) {
            const auto& access = projected[i];
            const auto& frozen = descriptor.frozenElements()[i].declaration;
            assert(access.commandIndex == 3u && access.scopeBeginCommandIndex == 0u
                && access.pipelineSourceIndex == 6u && access.pipelineBindCommandIndex == 1u
                && access.descriptorSourceIndex == 7u && access.descriptorBindCommandIndex == 2u
                && access.descriptorContentRevision == descriptor.frozenContentRevision()
                && access.set == 0u && access.binding == frozen.write.binding
                && access.registrationGeneration == frozen.registrationGeneration
                && access.state == frozen.write.resourceState && access.stages == frozen.stages
                && access.access == frozen.access && !access.nativeExecutionProven());
        }
        assert(projected[0].kind == C::Access::Kind::Buffer && projected[0].bufferSourceIndex == 0u
            && projected[0].offsetBytes == range.offsetBytes
            && projected[0].sizeBytes == range.sizeBytes);
        for (std::size_t i = 1u; i < 3u; ++i) {
            const auto viewIndex = i == 1u ? 2u : 4u;
            const auto& view =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    sources.sources()[viewIndex]);
            const auto& layout = view->originalViewLease()->layout();
            const auto& access = projected[i];
            assert(access.kind == C::Access::Kind::TextureView
                && access.bufferSourceIndex == SIZE_MAX
                && access.textureViewSourceIndex == viewIndex
                && access.textureParentSourceIndex == viewIndex - 1u
                && access.textureParentRegistrationGeneration
                    == view->originalParentLease()
                        ->originalObject()
                        ->originalResourceRegistration()
                        .generation()
                && access.textureRange.baseMipLevel == layout.baseMipLevel
                && access.textureRange.mipLevelCount == layout.mipLevelCount
                && access.textureRange.baseArrayLayer == layout.baseArrayLayer
                && access.textureRange.arrayLayerCount == layout.arrayLayerCount
                && access.textureRange.aspect == layout.aspect);
        }
        assert(projected[3].kind == C::Access::Kind::Sampler
            && projected[3].samplerSourceIndex == 5u
            && projected[3].access == rhi::ResourceAccess::None
            && projected[3].state == rhi::ResourceState::Undefined);
        for (std::uint32_t field = 0u; field < 4u; ++field) {
            auto bad = budget();
            if (field == 0u)
                --bad.maxLogicalAccesses;
            if (field == 1u)
                --bad.maxTextureAccesses;
            if (field == 2u) {
                if (element.write.bufferBindingRange.contractVersion == 2u) {
                    bad.finiteFullBoundViewOptIn = false;
                    bad.finiteFootprintVersion = 0u;
                } else {
                    bad.wholeFullBoundViewOptIn = false;
                    bad.wholeFootprintVersion = 0u;
                }
            }
            if (field == 3u) {
                bad.contractVersion = 2u;
                bad.generalShaderResourcesVersion = 1u;
                bad.depthTableFootprintVersion = 1u;
                bad.depthTableFullBoundViewOptIn = true;
            }
            assert(!C::validateBudget(bad));
            emitted = 0u;
            const auto refused = C::visit(inputs, device, bad, visitor);
            assert(refused.diagnostic && !emitted);
        }
        // Both selected command profiles are Compute only. Validate the
        // shared RenderUsage request/input pairing, without claiming a real
        // Graphics binding projection from a program with no render bindings.
        emitted = 0u;
        const auto render =
            C::visitRenderBindings(inputs, device, renderBudget(dependencies ? 9u : 6u), visitor);
        assert(!render.diagnostic && !render.bindings && !render.logicalAccesses && !emitted);
        if (dependencies) {
            const auto shortBudget =
                C::visitRenderBindings(inputs, device, renderBudget(), visitor);
            assert(shortBudget.diagnostic
                && core::StringView(shortBudget.diagnostic)
                    == "RHI-ORIGINAL-RENDER-USAGE-COMMAND-CAPACITY"
                && !emitted);
        }
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota}, [&]() noexcept -> const char* {
                    const auto checked = C::inspect(inputs, device, budget());
                    return checked.diagnostic;
                }));
        });
        std::printf(
            "BINDLESS_RHI_GENERAL_CONSUMPTION4_OK offset%llu bytes%llu accesses4 textures2 sampler1 exact-original-positions preflight-no-output hot0-query0 render-profile-only no-state-execution\n",
            static_cast<unsigned long long>(range.offsetBytes),
            static_cast<unsigned long long>(range.sizeBytes));
    }

    static void verifyInvalidated(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::GpuDevice& device)
    {
        std::uint32_t emitted = 0u;
        const auto visitor = [&](const auto&) noexcept -> const char* {
            ++emitted;
            return nullptr;
        };
        assert(C::visit(inputs, device, budget(), visitor).diagnostic && !emitted);
        assert(
            C::visitRenderBindings(inputs, device, renderBudget(), visitor).diagnostic && !emitted);
        std::puts(
            "BINDLESS_RHI_GENERAL_CONSUMPTION4_TERMINAL_OK original-inputs-invalid no-output-state-execution");
    }
#endif
};
} // namespace arp::test
