#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif

namespace arp::test {
class OriginalDescriptorBufferBindingFixture final {
public:
    static void verifyContract()
    {
        verifyFiniteSourceCustodyContract();
        verifyFiniteSourceDiagnosticContract();
        verifyFiniteAccessDiagnostic();
        using Contract = rhi::OriginalDescriptorBufferBindingContract;
        assert(std::string_view(Contract::validateRequest({}))
            == "RHI-ORIGINAL-DESCRIPTOR-BUFFER-VERSION");
        assert(std::string_view(Contract::validateRequest({1u, false}))
            == "RHI-ORIGINAL-DESCRIPTOR-BUFFER-OPT-IN");
        assert(Contract::validateRequest({2u, true}) && !Contract::validateRequest({1u, true}));
        assert(Contract::validateLimits({})
            && Contract::validateLimits({2u, 256u, 256u, 1024u, 1024u}));
        assert(!Contract::validateBinding(
            rhi::DescriptorType::UniformBuffer, 1024u, 0u, {1u, 256u, 256u, 1024u, 2048u}));
        assert(std::string_view(Contract::validateBinding(
                   rhi::DescriptorType::UniformBuffer, 1024u, 256u, {1u, 256u, 256u, 1024u, 2048u}))
            == "RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-RANGE-UNPROVEN");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Contract::lowering(backend);
            assert(!decision.executionAvailable);
            assert(decision.captureAvailable == (backend == rhi::Backend::WebGPU));
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
            assert((decision.mode == rhi::CapabilityMode::Equivalent)
                == (backend == rhi::Backend::WebGPU));
            assert((decision.diagnostic == nullptr) == decision.captureAvailable);
        }
        std::puts(
            "BINDLESS_RHI_ORIGINAL_DESCRIPTOR_LIMITS_CONTRACT_OK request1 opt-in wrapper-custody backends4 execution-closed");
    }

    static void verifyFiniteSourceCustodyContract()
    {
        using Source = rhi::OriginalCommandDescriptorSetSource;
        using Sources = rhi::OriginalCommandResourceSources;
        rhi::OriginalCommandDescriptorSourceBudget budget{
            3u, true, 2u, 2u, Source::finiteExclusiveRetainedOwnerCount, 65536u};
        budget.finitePipelineSourceIndex = 1u;
        budget.finiteFootprintContractVersion = 1u;
        budget.finiteFullBoundViewOptIn = true;
        budget.maxFrozenDynamicOffsets = 1u;
        assert(!Source::validateBudget(budget));
        for (const auto version : {1u, 2u}) {
            auto invalid = budget;
            invalid.contractVersion = version;
            assert(Source::validateBudget(invalid));
        }
        assert(rhi::OriginalCommandDescriptorTableSource::validateBudget(budget));
        auto invalid = budget;
        invalid.finiteFullBoundViewOptIn = false;
        assert(Source::validateBudget(invalid));
        invalid = budget;
        invalid.finiteFootprintContractVersion = 2u;
        assert(Source::validateBudget(invalid));
        invalid = budget;
        invalid.maxExclusiveRetainedOwners--;
        assert(Source::validateBudget(invalid));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandDescriptorSourceLowering::finiteLowering(
                    backend);
            assert(!decision.executionAvailable && !decision.positionalAdapterAvailable);
            assert(decision.freezeAvailable == (backend == rhi::Backend::WebGPU));
        }
        assert(!Sources::allowsDescriptorSourceVersion(1u, 3u)
            && !Sources::allowsDescriptorSourceVersion(2u, 3u));
        assert(Sources::allowsDescriptorSourceVersion(3u, 3u)
            && Sources::allowsDescriptorSourceVersion(3u, 2u));
        assert(!Sources::allowsDescriptorSourceVersion(4u, 3u));
        rhi::DescriptorBindingDesc binding;
        binding.type = rhi::DescriptorType::ReadOnlyStorageBuffer;
        binding.stages = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment;
        Source::Element element;
        element.write.type = binding.type;
        element.write.resourceState = rhi::ResourceState::ShaderRead;
        element.write.bufferBindingRange = {2u, 32u, 16u};
        element.dynamicOffset = 16u;
        element.bufferAccessOffset = 48u;
        element.bufferAccessBytes = 16u;
        element.stages = rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader;
        element.access = rhi::ResourceAccess::ShaderRead;
        const auto check = [&](const auto& e, const auto& b) noexcept {
            return rhi::OriginalCommandDescriptorDependencies::validateFullBoundView(e, b, 128u);
        };
        assert(!check(element, binding));
        auto hostile = element;
        --hostile.bufferAccessBytes;
        assert(check(hostile, binding));
        hostile = element;
        ++hostile.bufferAccessBytes;
        assert(core::StringView(check(hostile, binding))
            == "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-COVERAGE");
        hostile = element;
        --hostile.bufferAccessOffset;
        ++hostile.bufferAccessBytes;
        assert(core::StringView(check(hostile, binding))
            == "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-COVERAGE");
        hostile = element;
        ++hostile.bufferAccessOffset;
        assert(check(hostile, binding));
        hostile = element;
        hostile.write.bufferBindingRange.offsetBytes = UINT64_MAX;
        assert(check(hostile, binding));
        hostile = element;
        hostile.stages = rhi::PipelineStage::FragmentShader;
        assert(check(hostile, binding));
        hostile = element;
        hostile.access = rhi::ResourceAccess::ShaderWrite;
        assert(check(hostile, binding));
        auto unsupported = binding;
        unsupported.stages = unsupported.stages | rhi::ShaderStage::TessellationControl;
        assert(check(element, unsupported));
        auto wrong = binding;
        wrong.type = rhi::DescriptorType::UniformBuffer;
        assert(check(element, wrong));
        wrong = binding;
        wrong.arrayCount = 2u;
        assert(check(element, wrong));
        wrong = binding;
        wrong.tableBacked = true;
        assert(check(element, wrong));
        wrong = binding;
        wrong.type = rhi::DescriptorType::StorageBuffer;
        hostile = element;
        hostile.write.type = wrong.type;
        hostile.write.resourceState = rhi::ResourceState::ShaderWrite;
        assert(check(hostile, wrong));
        hostile.access = rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite;
        assert(!check(hostile, wrong));
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                return check(element, binding);
            }));
        hostile = element;
        ++hostile.bufferAccessBytes;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                assert(check(hostile, binding));
                return nullptr;
            }));
        std::puts(
            "BINDLESS_RHI_ORIGINAL_FINITE_SOURCE_CUSTODY_CONTRACT_OK explicit3 full-view1 complete-stages conservative-access backends4 assembly-version-gates metadata-hot0 execution-closed");
    }

    static void verifyFiniteSourceDiagnosticContract()
    {
        using C = rhi::OriginalFiniteDescriptorWriteContract;
        using R = rhi::OriginalFiniteDescriptorSourceDiagnosticRequest;
        R request{1u, true, 1u, 65536u};
        assert(!C::validateSourceDiagnostic(request));
        for (const auto version : {0u, 2u}) {
            auto invalid = request;
            invalid.contractVersion = version;
            assert(C::validateSourceDiagnostic(invalid));
        }
        auto invalid = request;
        invalid.prototypeOptIn = false;
        assert(C::validateSourceDiagnostic(invalid));
        invalid = request;
        invalid.expectedContentRevision = 0u;
        assert(C::validateSourceDiagnostic(invalid));
        invalid = request;
        invalid.sourceCpuBytes = 0u;
        assert(C::validateSourceDiagnostic(invalid));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = C::sourceDiagnosticDecision(backend);
            assert(!decision.executionAvailable
                && decision.preparationAvailable == (backend == rhi::Backend::WebGPU)
                && decision.validatorOnly == (backend == rhi::Backend::Null));
            assert((decision.diagnostic == nullptr) == decision.preparationAvailable);
        }
        assert(C::sourceDiagnosticDecision(static_cast<rhi::Backend>(255u)).diagnostic);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_FINITE_SOURCE_DIAGNOSTIC_CONTRACT_OK explicit1 backends4 diagnostic-only positional-state-native-closed");
    }

    static void verifyFiniteAccessDiagnostic()
    {
        using C = rhi::OriginalDescriptorBufferBindingContract;
        C::FiniteAccessDiagnosticObservation observed;
        const auto check = [&](C::FiniteAccessDiagnosticRequest request,
                               rhi::DescriptorBufferBindingRange range,
                               std::uint64_t buffer,
                               std::uint32_t dynamic,
                               std::uint64_t offset,
                               std::uint64_t bytes,
                               std::uint64_t coverageOffset,
                               std::uint64_t coverageBytes) noexcept {
            observed.binding = {99u, 99u};
            observed.physicalAccess = {99u, 99u};
            const auto* id = C::diagnoseFiniteAccess(request,
                range,
                buffer,
                dynamic,
                offset,
                bytes,
                coverageOffset,
                coverageBytes,
                observed);
            if (id)
                assert(!observed.binding.offsetBytes && !observed.binding.sizeBytes
                    && !observed.physicalAccess.offsetBytes && !observed.physicalAccess.sizeBytes);
            else
                assert(observed.binding.offsetBytes == range.offsetBytes + dynamic
                    && observed.binding.sizeBytes == range.sizeBytes
                    && observed.physicalAccess.offsetBytes == range.offsetBytes + dynamic + offset
                    && observed.physicalAccess.sizeBytes == bytes
                    && !observed.nativeExecutionProven());
            return id;
        };
        // Authored synthetic metadata, deliberately independent of alignment
        // and original/native eligibility; those remain separate prerequisites.
        const rhi::DescriptorBufferBindingRange range{2u, 32u, 16u};
        assert(!check({1u, true}, range, 128u, 16u, 0u, 16u, 48u, 16u));
        assert(!check({1u, true}, range, 128u, 16u, 4u, 8u, 0u, 128u));
        assert(check({1u, true}, range, 128u, 16u, 0u, 16u, 48u, 15u));
        assert(check(
            {1u, true}, range, 128u, 16u, 16u, 1u, 0u, 128u)); // buffer-contained, outside view
        assert(check({1u, true}, range, 128u, 16u, UINT64_MAX, 1u, 0u, 128u));
        assert(check({1u, true}, range, 128u, 16u, 0u, 0u, 0u, 128u));
        assert(check({1u, true}, range, 128u, 16u, 0u, 16u, UINT64_MAX, 16u));
        assert(check({1u, true}, range, 128u, 16u, 0u, 16u, 0u, 0u));
        assert(check({1u, true}, range, 48u, 16u, 0u, 16u, 0u, 48u));
        assert(check({}, range, 128u, 0u, 0u, 16u, 0u, 128u));
        assert(check({1u, false}, range, 128u, 0u, 0u, 16u, 0u, 128u));
        assert(check({2u, true}, range, 128u, 0u, 0u, 16u, 0u, 128u));
        assert(check({1u, true}, {}, 128u, 0u, 0u, 16u, 0u, 128u));
        assert(check({1u, true}, {2u, 32u, 0u}, 128u, 0u, 0u, 16u, 0u, 128u));
        assert(check({1u, true}, {2u, UINT64_MAX, 16u}, UINT64_MAX, 0u, 0u, 16u, 0u, UINT64_MAX));
        assert(!check(
            {1u, true}, {2u, UINT64_MAX - 16u, 16u}, UINT64_MAX, 0u, 15u, 1u, UINT64_MAX - 1u, 1u));
        assert(check(
            {1u, true}, {2u, UINT64_MAX - 16u, 16u}, UINT64_MAX, 1u, 0u, 16u, 0u, UINT64_MAX));
        auto quota = core::makeShared<core::CpuMemoryQuota>(1024u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                return check({1u, true}, range, 128u, 16u, 0u, 16u, 48u, 16u);
            }));
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU})
            assert(!C::intervalDiagnosticLowering(backend).executionAvailable);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_FINITE_ACCESS_DIAGNOSTIC_OK explicit-relative mapped-once unchanged-size coverage-bounds overflow-cleared hot0 no-footprint-or-source-authority");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
private:
    static void beginLimitObservation()
    {
        EM_ASM({
            // clang-format off
                const device = Module.preinitializedWebGPUDevice;
                Module.arpDescriptorLimitsOwn = Object.getOwnPropertyDescriptor(device, 'limits');
                Module.arpDescriptorLimitsBase = device.limits;
                Module.arpDescriptorLimitsReads = 0;
                Module.arpDescriptorLimitsCase = 0;
                Object.defineProperty(device, 'limits', {
                    configurable : true,
                    get() {
                        ++Module.arpDescriptorLimitsReads;
                        return new Proxy(Module.arpDescriptorLimitsBase, {
                            get(target, key) {
                                const mode = Module.arpDescriptorLimitsCase;
                                if (key === 'maxUniformBufferBindingSize') {
                                    if (mode === 1)
                                        return undefined;
                                    if (mode === 2)
                                        return 0;
                                    if (mode === 3)
                                        return -1;
                                    if (mode === 4)
                                        return 1.5;
                                    if (mode === 5)
                                        return Number.MAX_SAFE_INTEGER + 1;
                                    if (mode === 6)
                                        return '1024';
                                    if (mode === 7)
                                        return NaN;
                                    if (mode === 8)
                                        throw new Error('fixture unavailable limits');
                                }
                                if (key === 'minUniformBufferOffsetAlignment' && mode === 9)
                                    return 4294967296;
                                if (key === 'minStorageBufferOffsetAlignment' && mode === 10)
                                    return undefined;
                                return Reflect.get(target, key, target);
                            }
                        });
                    }
                });
// clang-format on
});
}

static void endLimitObservation()
{
    EM_ASM({
        // clang-format off
            const device = Module.preinitializedWebGPUDevice;
            if (Module.arpDescriptorLimitsOwn)
                Object.defineProperty(device, 'limits', Module.arpDescriptorLimitsOwn);
            else
                delete device.limits;
            delete Module.arpDescriptorLimitsOwn;
            delete Module.arpDescriptorLimitsBase;
            delete Module.arpDescriptorLimitsReads;
            delete Module.arpDescriptorLimitsCase;
        // clang-format on
    });
}

public:
template <class Hot> static void verifyNoHotQueries(Hot&& hot)
{
    beginLimitObservation();
    EM_ASM({
        // clang-format off
            Module.arpDescriptorLimitsCase = 8;
        // clang-format on
    });
    hot();
    assert(EM_ASM_INT({
        // clang-format off
            return Module.arpDescriptorLimitsReads === 0;
        // clang-format on
    }));
    endLimitObservation();
}

template <class Owner, std::size_t Count>
static void verifyActual(
    const Owner& pipeline, const std::array<std::shared_ptr<core::CpuMemoryQuota>, Count>& quotas)
{
    using Pipeline = typename Owner::element_type;
    using Contract = rhi::OriginalDescriptorBufferBindingContract;
    const auto limits = rhi_webgpu::webGpuNativeDeviceLimits();
    assert(pipeline && limits.maxUniformBufferBindingSize && limits.maxStorageBufferBindingSize);
    const auto check = [&](const Contract::Request& request,
                           rhi::DescriptorType type,
                           std::uint64_t bytes,
                           std::uint32_t offset) noexcept {
        return pipeline->originalDescriptorBufferBindingDiagnostic(request, type, bytes, offset);
    };
    beginLimitObservation();
    // Frozen facts must survive native getter failure; hot replay does not
    // consult that getter or caller-supplied capability objects.
    EM_ASM({
        // clang-format off
            Module.arpDescriptorLimitsCase = 8;
        // clang-format on
    });
    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
        assert(check({}, rhi::DescriptorType::UniformBuffer, 1u, 0u));
        assert(check({2u, true}, rhi::DescriptorType::UniformBuffer, 1u, 0u));
        assert(check({1u, false}, rhi::DescriptorType::UniformBuffer, 1u, 0u));
        for (const auto type : {rhi::DescriptorType::UniformBuffer,
                 rhi::DescriptorType::StorageBuffer,
                 rhi::DescriptorType::ReadOnlyStorageBuffer}) {
            const auto maximum = type == rhi::DescriptorType::UniformBuffer
                ? limits.maxUniformBufferBindingSize
                : limits.maxStorageBufferBindingSize;
            assert(!check({1u, true}, type, maximum, 0u));
            assert(std::string_view(
                       check({1u, true}, type, static_cast<std::uint64_t>(maximum) + 1u, 0u))
                == "RHI-DESCRIPTOR-WHOLE-BUFFER-BINDING-SIZE");
            assert(std::string_view(check({1u, true}, type, 64u, 256u))
                == "RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-RANGE-UNPROVEN");
        }
        auto forged = rhi::DeviceCapabilities{};
        forged.computeResources.maxStorageBufferBindingSizeBytes = UINT64_MAX;
        assert(forged.computeResources.maxStorageBufferBindingSizeBytes
            > limits.maxStorageBufferBindingSize);
        assert(check({1u, true},
            rhi::DescriptorType::StorageBuffer,
            static_cast<std::uint64_t>(limits.maxStorageBufferBindingSize) + 1u,
            0u));
        return check({1u, true}, rhi::DescriptorType::UniformBuffer, 64u, 0u);
    }));
    assert(EM_ASM_INT({
        // clang-format off
            return Module.arpDescriptorLimitsReads === 0;
        // clang-format on
    }) == 1);
    // Live lineage remains mandatory even though the facts are frozen.
    EM_ASM({
        // clang-format off
            Module.arpDescriptorLimitsPhysical = Module.preinitializedWebGPUDevice;
            Module.preinitializedWebGPUDevice = {};
        // clang-format on
    });
    assert(check({1u, true}, rhi::DescriptorType::UniformBuffer, 64u, 0u));
    EM_ASM({
        // clang-format off
            Module.preinitializedWebGPUDevice = Module.arpDescriptorLimitsPhysical;
            delete Module.arpDescriptorLimitsPhysical;
        // clang-format on
    });
    assert(!check({1u, true}, rhi::DescriptorType::UniformBuffer, 64u, 0u));
    endLimitObservation();
    std::printf(
        "BINDLESS_RHI_ORIGINAL_DESCRIPTOR_LIMITS_NATIVE_OK %s frozen-facts exact-lineage fake-caps-rejected hot0 queries0\n",
        std::is_same_v<Pipeline, rhi::ComputePipeline> ? "compute" : "graphics");
}

template <class Factory> static void verifyCaptures(Factory&& factory, const char* kind)
{
    beginLimitObservation();
    for (int mode = 1; mode <= 10; ++mode) {
        EM_ASM(
            {
                // clang-format off
                Module.arpDescriptorLimitsCase = $0;
                // clang-format on
            },
            mode);
        auto pipeline = factory();
        if (!pipeline)
            std::fprintf(
                stderr, "original descriptor limits capture failed %s mode%d\n", kind, mode);
        assert(pipeline);
        assert(std::string_view(pipeline->originalDescriptorBufferBindingDiagnostic(
                   {1u, true}, rhi::DescriptorType::UniformBuffer, 64u, 0u))
            == "RHI-ORIGINAL-DESCRIPTOR-BUFFER-LIMITS-UNAVAILABLE");
    }
    EM_ASM({
        // clang-format off
            Module.arpDescriptorLimitsCase = 0;
        // clang-format on
    });
    auto retry = factory();
    assert(retry);
    assert(!retry->originalDescriptorBufferBindingDiagnostic(
        {1u, true}, rhi::DescriptorType::UniformBuffer, 64u, 0u));
    assert(EM_ASM_INT({
        // clang-format off
            return Module.arpDescriptorLimitsReads > 0;
        // clang-format on
    }) == 1);
    endLimitObservation();
    std::printf(
        "BINDLESS_RHI_ORIGINAL_DESCRIPTOR_LIMITS_CAPTURE_OK %s malformed10-unavailable genuine-retry\n",
        kind);
}

template <class Pipeline> static void verifyLoss(const std::shared_ptr<Pipeline>& pipeline)
{
    assert(pipeline && rhi_webgpu::webGpuNativeDeviceLost());
    assert(pipeline->originalDescriptorBufferBindingDiagnostic(
        {1u, true}, rhi::DescriptorType::UniformBuffer, 64u, 0u));
    std::printf(
        "BINDLESS_RHI_ORIGINAL_DESCRIPTOR_LIMITS_LOSS_OK %s existing-custody physical-loss-rejected\n",
        std::is_same_v<Pipeline, rhi::ComputePipeline> ? "compute" : "graphics");
}
#endif
}
;
} // namespace arp::test
