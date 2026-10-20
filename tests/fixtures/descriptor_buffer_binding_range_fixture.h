#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_descriptor_dynamic_offset_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string_view>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace arp::test {
class DescriptorBufferBindingRangeFixture final {
    using Range = rhi::DescriptorBufferBindingRange;
    using Contract = rhi::DescriptorBufferBindingRangeContract;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verifyContract()
    {
        // Backend metadata negative only: an unsupported range must fail before
        // missing native payload or limit facts could obscure its stable cause.
        {
            rhi_webgpu::WebGpuNativeDescriptorGroupPayload group;
            group.bindings.emplace_back();
            auto& binding = group.bindings.back();
            binding.binding.binding = 7u;
            binding.binding.type = rhi::DescriptorType::StorageBuffer;
            binding.binding.dynamicOffset = true;
            binding.write.type = binding.binding.type;
            binding.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
            binding.write.bufferBindingRange = {2u, 4u, 16u};
            is(rhi_webgpu::WebGpuDescriptorDynamicOffsetValidation::validateBinding(
                   group, 7u, 0u, {}),
                "RHI-DESCRIPTOR-BUFFER-SUBRANGE-LOWERING-MISSING");
            binding.write.bufferBindingRange = {99u, 0u, 16u};
            is(rhi_webgpu::WebGpuDescriptorDynamicOffsetValidation::validateBinding(
                   group, 7u, 0u, {}),
                "RHI-DESCRIPTOR-BUFFER-RANGE-VERSION");
            binding.write.bufferBindingRange = {};
            is(rhi_webgpu::WebGpuDescriptorDynamicOffsetValidation::validateBinding(
                   group, 7u, 0u, {}),
                "RHI-WEBGPU-DESCRIPTOR-DYNAMIC-BUFFER-MISSING");
        }
        auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, []() noexcept -> const char* {
                assert(!Contract::validateSchema({}, true) && !Contract::validateSchema({}, false));
                for (const auto version : std::array{0u, 3u, UINT32_MAX})
                    is(Contract::validateSchema({version, 0u, 0u}, true),
                        "RHI-DESCRIPTOR-BUFFER-RANGE-VERSION");
                for (const auto range :
                    std::array<Range, 3>{{{1u, 1u, 0u}, {1u, 0u, 1u}, {1u, 1u, 1u}}})
                    is(Contract::validateSchema(range, true),
                        "RHI-DESCRIPTOR-BUFFER-WHOLE-NONCANONICAL");
                is(Contract::validateSchema({2u, 0u, 16u}, false),
                    "RHI-DESCRIPTOR-BUFFER-RANGE-DOMAIN");
                is(Contract::validateSchema({2u, 0u, 0u}, true),
                    "RHI-DESCRIPTOR-BUFFER-RANGE-EMPTY");
                assert(!Contract::validateSchema({2u, 8u, 16u}, true));
                is(Contract::writeDiagnostic({2u, 8u, 16u}, true),
                    "RHI-DESCRIPTOR-BUFFER-SUBRANGE-LOWERING-MISSING");
                Contract::Resolved resolved{99u, 99u};
                assert(!Contract::resolve({}, 64u, 0u, resolved) && resolved.offsetBytes == 0u
                    && resolved.sizeBytes == 64u);
                is(Contract::resolve({}, 64u, 1u, resolved),
                    "RHI-DESCRIPTOR-WHOLE-BUFFER-DYNAMIC-RANGE-UNPROVEN");
                assert(!resolved.offsetBytes && !resolved.sizeBytes);
                is(Contract::resolve({}, 0u, 0u, resolved), "RHI-DESCRIPTOR-BUFFER-RANGE-BOUNDS");
                assert(!Contract::resolve({2u, 8u, 16u}, 32u, 8u, resolved));
                assert(resolved.offsetBytes == 16u && resolved.sizeBytes == 16u);
                is(Contract::resolve({2u, 8u, 16u}, 32u, 9u, resolved),
                    "RHI-DESCRIPTOR-BUFFER-DYNAMIC-RANGE-BOUNDS");
                assert(!resolved.offsetBytes && !resolved.sizeBytes);
                constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
                for (const auto range : std::array<Range, 3>{
                         {{2u, maximum, 1u}, {2u, maximum - 3u, 8u}, {2u, 1u, maximum}}}) {
                    resolved = {99u, 99u};
                    assert(Contract::resolve(range, maximum, 0u, resolved));
                    assert(!resolved.offsetBytes && !resolved.sizeBytes);
                }
                assert(!Contract::resolve({2u, maximum - 16u, 8u}, maximum, 8u, resolved));
                assert(resolved.offsetBytes == maximum - 8u && resolved.sizeBytes == 8u);
                assert(Contract::resolve({2u, maximum - 16u, 8u}, maximum, 9u, resolved));
                // Independent small-domain oracle checks both static and shifted
                // intervals, including exact-end and zero-size boundaries.
                for (std::uint64_t total = 0u; total < 12u; ++total)
                    for (std::uint64_t base = 0u; base < 14u; ++base)
                        for (std::uint64_t size = 0u; size < 14u; ++size)
                            for (std::uint32_t dynamic = 0u; dynamic < 14u; ++dynamic) {
                                const bool expected = size > 0u && base + size <= total
                                    && base + dynamic + size <= total;
                                const auto* id =
                                    Contract::resolve({2u, base, size}, total, dynamic, resolved);
                                assert((id == nullptr) == expected);
                                if (expected)
                                    assert(resolved.offsetBytes == base + dynamic
                                        && resolved.sizeBytes == size);
                                else
                                    assert(!resolved.offsetBytes && !resolved.sizeBytes);
                            }
                rhi::DescriptorBindingWrite original;
                auto changed = original;
                assert(rhi::descriptorBindingWriteMatches(original, changed));
                changed.bufferBindingRange.contractVersion = 2u;
                assert(!rhi::descriptorBindingWriteMatches(original, changed));
                changed = original;
                changed.bufferBindingRange.offsetBytes = 8u;
                assert(!rhi::descriptorBindingWriteMatches(original, changed));
                changed = original;
                changed.bufferBindingRange.sizeBytes = 16u;
                assert(!rhi::descriptorBindingWriteMatches(original, changed));
                using Finite = rhi::OriginalDescriptorBufferBindingContract;
                // Authored toy limits exercise both distinct uniform/storage policies.
                constexpr Finite::Limits limits{1u, 8u, 4u, 16u, 32u};
                const auto checkWithFacts = [&](rhi::DescriptorType type,
                                                Range range,
                                                std::uint64_t extent,
                                                std::uint32_t dynamic,
                                                std::uint64_t minimum,
                                                Finite::Limits facts) noexcept {
                    resolved = {99u, 99u};
                    const auto* id = Finite::validateFiniteInterval(
                        type, range, extent, dynamic, minimum, facts, resolved);
                    if (id)
                        assert(!resolved.offsetBytes && !resolved.sizeBytes);
                    else
                        assert(resolved.offsetBytes == range.offsetBytes + dynamic
                            && resolved.sizeBytes == range.sizeBytes);
                    return id;
                };
                const auto check = [&](rhi::DescriptorType type,
                                       Range range,
                                       std::uint64_t extent,
                                       std::uint32_t dynamic,
                                       std::uint64_t minimum) noexcept {
                    return checkWithFacts(type, range, extent, dynamic, minimum, limits);
                };
                const auto storage = rhi::DescriptorType::StorageBuffer,
                           uniform = rhi::DescriptorType::UniformBuffer;
                assert(!check(storage, {2u, 4u, 8u}, 16u, 4u, 8u));
                assert(!check(uniform, {2u, 8u, 16u}, 32u, 8u, 16u));
                assert(check(uniform, {2u, 4u, 8u}, 16u, 4u, 8u));
                is(check(storage, {2u, 1u, 8u}, 16u, 3u, 8u),
                    "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-ALIGNMENT");
                is(check(storage, {2u, 0u, 8u}, 16u, 1u, 8u),
                    "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-ALIGNMENT");
                is(check(storage, {}, 16u, 0u, 8u), "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-SCHEMA");
                is(check(storage, {2u, 0u, 8u}, 16u, 0u, 0u),
                    "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-MINIMUM-UNAVAILABLE");
                is(check(storage, {2u, 0u, 4u}, 16u, 0u, 8u),
                    "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-SIZE");
                is(check(storage, {2u, 0u, 36u}, 64u, 0u, 8u),
                    "RHI-ORIGINAL-DESCRIPTOR-BUFFER-FINITE-SIZE");
                assert(check(rhi::DescriptorType::SampledTexture, {2u, 0u, 8u}, 16u, 0u, 8u));
                for (const auto type :
                    std::array{storage, uniform, rhi::DescriptorType::ReadOnlyStorageBuffer}) {
                    assert(checkWithFacts(type, {2u, 0u, 8u}, 16u, 0u, 8u, {}));
                    assert(checkWithFacts(type, {2u, 0u, 8u}, 16u, 0u, 8u, {2u, 8u, 4u, 16u, 32u}));
                }
                assert(check(storage, {2u, maximum - 3u, 8u}, maximum, 0u, 8u));
                assert(!check(storage, {2u, maximum - 15u, 8u}, maximum, 4u, 8u));
                assert(check(storage, {2u, maximum - 15u, 8u}, maximum, 8u, 8u));
                for (std::uint64_t total = 0u; total <= 20u; ++total)
                    for (std::uint64_t base = 0u; base <= 20u; ++base)
                        for (std::uint64_t size = 0u; size <= 20u; ++size)
                            for (std::uint32_t dynamic = 0u; dynamic <= 20u; ++dynamic) {
                                const bool expected = size >= 8u && base % 4u == 0u
                                    && dynamic % 4u == 0u && base + size <= total
                                    && base + dynamic + size <= total;
                                assert((check(storage, {2u, base, size}, total, dynamic, 8u)
                                           == nullptr)
                                    == expected);
                            }
                // These scalar results never open the separately versioned request
                // or the non-overridable final descriptor write gate.
                assert(Finite::validateRequest({2u, true}));
                assert(Contract::writeDiagnostic({2u, 4u, 8u}, true));
                assert(Finite::validateIntervalDiagnosticRequest({})
                    && Finite::validateIntervalDiagnosticRequest({2u, true})
                    && Finite::validateIntervalDiagnosticRequest({1u, false})
                    && !Finite::validateIntervalDiagnosticRequest({1u, true}));
                for (const auto backend : std::array{rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto decision = Finite::intervalDiagnosticLowering(backend);
                    assert(!decision.executionAvailable
                        && decision.captureAvailable == (backend == rhi::Backend::WebGPU));
                    assert(decision.validatorOnly == (backend == rhi::Backend::Null));
                    assert(bool(decision.diagnostic) != decision.captureAvailable);
                }
                return nullptr;
            }));
        assert(!quota->liveBytes());
        auto device = rhi::createNullDevice();
        assert(device);
        rhi::OriginalDescriptorBufferBindingContract::IntervalDiagnosticObservation observation;
        observation.range = {99u, 99u};
        observation.runtimeLength = 99u;
        is(device->diagnoseOriginalRuntimeLengthInterval({1u, true},
               std::shared_ptr<const rhi::ComputePipeline>{},
               {},
               0u,
               0u,
               {2u, 0u, 16u},
               0u,
               observation),
            "RHI-ORIGINAL-RUNTIME-INTERVAL-NULL-VALIDATOR-ONLY");
        assert(!observation.range.sizeBytes && !observation.runtimeLength
            && !observation.nativeExecutionProven());
        std::puts(
            "BINDLESS_RHI_BUFFER_BINDING_RANGE_CONTRACT_OK versions1-2 whole-preserved explicit-base-plus-dynamic exact-size overflow-rejected oracle32928 allocation0 identity3 native-closed");
    }

    static void verifyWriteGate(rhi::GpuDevice& device)
    {
        rhi::DescriptorArenaDesc arenaDesc;
        arenaDesc.maxSets = 1u;
        arenaDesc.maxDescriptors = 1u;
        auto arena = device.createDescriptorArena(arenaDesc);
        assert(arena);
        rhi::DescriptorSetDesc setDesc;
        setDesc.layout.name = "RangeGate";
        setDesc.layout.bindings.push_back(
            {0u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Compute, 1u});
        auto set = arena->allocateDescriptorSet(setDesc);
        assert(set);
        rhi::BufferDesc desc;
        desc.sizeBytes = 64u;
        desc.usage = rhi::ResourceUsage::UniformBuffer;
        auto buffer = device.createBuffer(desc);
        assert(buffer);
        rhi::DescriptorBindingWrite write{0u,
            rhi::DescriptorType::UniformBuffer,
            buffer->id(),
            rhi::DescriptorResourceKind::Buffer};
        assert(set->write(write).success);
        const auto revision = set->contentRevision();
        const char* diagnostic = nullptr;
        auto receipt = set->observeSuccessfulBindingWrite(0u, 0u, diagnostic);
        assert(receipt && !diagnostic);
#if defined(__EMSCRIPTEN__)
        const auto groups = EM_ASM_INT({
            // clang-format off
                return Module.webGpuNativeGroupCreateCount || 0;
            // clang-format on
        });
#endif
        const auto reject = [&](Range range, const char* expected) {
            auto candidate = write;
            candidate.bufferBindingRange = range;
            const auto result = set->write(candidate);
            assert(!result.success);
            is(result.diagnostics.c_str(), expected);
            assert(set->contentRevision() == revision && !receipt->diagnostic());
            const auto current = set->observeSuccessfulBindingWrite(0u, 0u, diagnostic);
            assert(current);
            assert(rhi::descriptorBindingWriteMatches(write, current->write()));
        };
        reject({2u, 16u, 16u}, "RHI-DESCRIPTOR-BUFFER-SUBRANGE-LOWERING-MISSING");
        reject({2u, 0u, 0u}, "RHI-DESCRIPTOR-BUFFER-RANGE-EMPTY");
        reject({1u, 8u, 0u}, "RHI-DESCRIPTOR-BUFFER-WHOLE-NONCANONICAL");
        reject({1u, 0u, 16u}, "RHI-DESCRIPTOR-BUFFER-WHOLE-NONCANONICAL");
        reject({3u, 0u, 16u}, "RHI-DESCRIPTOR-BUFFER-RANGE-VERSION");
        auto wrongDomain = write;
        wrongDomain.type = rhi::DescriptorType::SampledTexture;
        wrongDomain.resourceKind = rhi::DescriptorResourceKind::TextureView;
        wrongDomain.bufferBindingRange = {2u, 0u, 16u};
        const auto result = set->write(wrongDomain);
        assert(!result.success);
        is(result.diagnostics.c_str(), "RHI-DESCRIPTOR-BUFFER-RANGE-DOMAIN");
        assert(set->contentRevision() == revision && !receipt->diagnostic());
#if defined(__EMSCRIPTEN__)
        assert(EM_ASM_INT({
            // clang-format off
                return Module.webGpuNativeGroupCreateCount || 0;
            // clang-format on
        }) == groups);
#endif
        std::printf(
            "BINDLESS_RHI_BUFFER_BINDING_RANGE_WRITE_GATE_OK backend%u rejections6 revision-and-receipt-preserved native-group-calls0\n",
            static_cast<unsigned>(device.backend()));
    }
};
} // namespace arp::test
