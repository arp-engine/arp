#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/frontend/descriptor_general_write_validation.h"
#include "rhi/gpu_device.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_error_scope_handoff_fixture.h"

namespace arp::test {
class OriginalGeneralDescriptorWriteFixture final {
    using C = rhi::OriginalGeneralDescriptorWriteContract;

public:
    static void verifyContract()
    {
        rhi::OriginalGeneralDescriptorWriteRequest r{1u,
            true,
            4u,
            static_cast<std::uint32_t>(C::ownerSlots(4u)),
            C::callbackCount,
            {65536u, 65536u, 65536u},
            32u};
        assert(!C::validate(r));
        auto finite = r;
        finite.contractVersion = 2u;
        assert(!C::validate(finite));
        for (std::uint32_t field = 0u; field < 8u; ++field) {
            auto bad = r;
            switch (field) {
            case 0u:
                bad.contractVersion = 3u;
                break;
            case 1u:
                bad.prototypeOptIn = false;
                break;
            case 2u:
                bad.maxBindings = 0u;
                break;
            case 3u:
                --bad.maxRetainedOwnerSlots;
                break;
            case 4u:
                --bad.maxCallbacks;
                break;
            case 5u:
                bad.maxCpuBytes[0] = 0u;
                break;
            case 6u:
                bad.maxCpuBytes[2] = 0u;
                break;
            case 7u:
                bad.maxRetainedAllocationBytes = 0u;
                break;
            }
            assert(C::validate(bad));
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = C::decide(backend, {true, true, true, true, true});
            assert(d.preparationAvailable == (backend == rhi::Backend::WebGPU));
            assert(d.validatorOnly == (backend == rhi::Backend::Null) && !d.executionAvailable);
            assert((d.diagnostic == nullptr) == d.preparationAvailable);
        }
        assert(C::decide(rhi::Backend::WebGPU).diagnostic);
        using I = rhi::OriginalDescriptorNativeIdentityContract;
        using Profile = rhi::OriginalDescriptorNativeIdentityRequest::Profile;
        assert(!I::validateIntent({3u, true, Profile::GeneralWrite1}));
        assert(I::validateIntent({1u, true, Profile::GeneralWrite1}));
        assert(I::validateIntent({2u, true, Profile::GeneralWrite1}));
        assert(I::validateIntent({3u, false, Profile::GeneralWrite1}));
        assert(I::validateIntent({3u, true, Profile::WholeWrite1}));
        assert(!I::validateIntent({4u, true, Profile::GeneralWrite2}));
        assert(I::validateIntent({3u, true, Profile::GeneralWrite2}));
        assert(I::validateIntent({4u, true, Profile::GeneralWrite1}));
        std::puts(
            "BINDLESS_RHI_GENERAL_WRITE_CONTRACT_OK explicit1 four-backends domains3 bounded-parent-allocation identity3 execution-closed");
    }

    static void verifyUnsupported(rhi::GpuDevice& device)
    {
        if (device.backend() == rhi::Backend::WebGPU)
            return;
        rhi::DescriptorArenaDesc desc;
        desc.maxSets = 1u;
        desc.maxDescriptors = 1u;
        auto arena = device.createDescriptorArena(desc);
        assert(arena);
        rhi::DescriptorSetDesc sd;
        sd.layout.name = "general write validator";
        rhi::DescriptorBindingDesc b;
        b.type = rhi::DescriptorType::UniformBuffer;
        b.stages = rhi::ShaderStage::Compute;
        sd.layout.bindings.push_back(b);
        auto object = arena->allocateDescriptorSet(sd);
        assert(object);
        rhi::OriginalGeneralDescriptorWriteRequest r{1u,
            true,
            1u,
            static_cast<std::uint32_t>(C::ownerSlots(1u)),
            C::callbackCount,
            {65536u, 65536u, 65536u},
            16u};
        const auto revision = object->contentRevision();
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> quotas{};
        const rhi::OriginalCommandDescriptorSetElement e;
        const auto* id = device.beginOriginalGeneralDescriptorWrite(
            *object, std::shared_ptr<rhi::ComputePipeline>{}, {}, 0u, &e, 1u, r, quotas);
        assert(id && core::StringView(id) == C::decide(device.backend()).diagnostic);
        r.contractVersion = 2u;
        id = device.beginOriginalGeneralDescriptorWrite(
            *object, std::shared_ptr<rhi::ComputePipeline>{}, {}, 0u, &e, 1u, r, quotas);
        assert(id && core::StringView(id) == C::decide(device.backend()).diagnostic);
        assert(object->finishOriginalGeneralWrite() && object->cancelOriginalGeneralWrite()
            && !object->hasOriginalGeneralContent() && !object->originalGeneralWriteScopesDrained()
            && object->contentRevision() == revision);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    template <class Pipeline, std::size_t Count>
    static void verifyActual(rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        rhi::DescriptorSet& object,
        const std::array<rhi::OriginalCommandDescriptorSetElement, Count>& elements,
        std::uint64_t retainedAllocationBytes,
        std::uint32_t version = 1u)
    {
        using Scopes = WebGpuErrorScopeHandoffFixture<1005u>;
        constexpr std::uint64_t bytes = 65536u;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(bytes);
        // The caller authors this named workload's actual buffer/parent-texture
        // allocations. Sampler has no buffer/texture allocation charge.
        rhi::OriginalGeneralDescriptorWriteRequest request{version,
            true,
            Count,
            static_cast<std::uint32_t>(C::ownerSlots(Count)),
            C::callbackCount,
            {bytes, bytes, bytes},
            retainedAllocationBytes};
        const auto sourceOwners = sources.use_count();
        const auto revision = object.contentRevision();
        const auto old = rhi_webgpu::webGpuNativePayloadFor(object);
        const auto begin = [&](const auto& input) {
            return device.beginOriginalGeneralDescriptorWrite(
                object, pipeline, sources, 0u, input.data(), input.size(), request, quotas);
        };
        const auto drain = [&] {
            assert(WebGpuAsyncWaitFixture::until([&] {
                return object.originalGeneralWriteScopesDrained();
            }));
        };
        const auto prepareCancel = [&] {
            if (begin(elements))
                return false;
            drain();
            assert(!object.cancelOriginalGeneralWrite());
            assert(object.contentRevision() == revision
                && rhi_webgpu::webGpuNativePayloadFor(object) == old);
            return true;
        };
        std::array<std::uint64_t, 3u> peaks{};
        for (std::size_t i = 0u; i < quotas.size(); ++i)
            peaks[i] = CpuQuotaFactoryFixture(quotas[i]).measure(prepareCancel);
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            CpuQuotaFactoryFixture(quotas[i]).verify(peaks[i], prepareCancel);
            {
                core::CpuMemoryQuotaLease full(quotas[i], bytes);
                assert(!prepareCancel());
            }
            assert(!quotas[i]->liveBytes());
        }
        auto bad = elements;
        bad[0].registrationGeneration++;
        assert(begin(bad));
        bad = elements;
        bad[0].dynamicOffset = 1u;
        assert(begin(bad));
        bad = elements;
        bad[1].write.binding = bad[0].write.binding;
        assert(begin(bad));
        bad = elements;
        bad[2].write.resource = {};
        assert(begin(bad));
        auto saved = request;
        --request.maxRetainedAllocationBytes;
        assert(begin(elements));
        request = saved;
        --request.maxRetainedOwnerSlots;
        assert(begin(elements));
        request = saved;
        --request.maxBindings;
        assert(begin(elements));
        request = saved;
        const auto originalQuota = quotas[1];
        quotas[1] = quotas[0];
        assert(begin(elements));
        quotas[1] = originalQuota;
        Scopes::holdScopes();
        const auto* id = begin(elements);
        if (id)
            std::fprintf(stderr, "general scoped write: %s\n", id);
        assert(!id);
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(sources.use_count() == sourceOwners && object.originalScopedContentDiagnostic());
        assert(object.finishOriginalGeneralWrite() && object.cancelOriginalGeneralWrite());
        assert(object.finishOriginalWholeWrite() && object.cancelOriginalWholeWrite());
        assert(object.finishOriginalFiniteWrite() && object.cancelOriginalFiniteWrite());
        assert(!object.write(elements[0].write).success && begin(elements));
        for (std::uint32_t i = 0u; i < C::callbackCount; ++i) {
            Scopes::releaseScope(i);
            assert(WebGpuAsyncWaitFixture::until([&] {
                return object.originalGeneralWriteScopeObserved(i);
            }));
            if (i + 1u < C::callbackCount)
                assert(object.finishOriginalGeneralWrite());
            assert(object.contentRevision() == revision
                && rhi_webgpu::webGpuNativePayloadFor(object) == old);
        }
        Scopes::releaseScopes();
        drain();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return object.finishOriginalGeneralWrite();
            }));
        });
        assert(object.contentRevision() == revision + 1u && object.hasOriginalGeneralContent()
            && !object.hasOriginalFiniteContent() && !object.hasOriginalWholeContent()
            && !object.originalScopedContentDiagnostic() && sources.use_count() == sourceOwners);
        auto payload = rhi_webgpu::webGpuNativePayloadFor(object);
        assert(payload && payload->group && payload->layout);
        using Profile = rhi::OriginalDescriptorNativeIdentityRequest::Profile;
        const auto identity = [&] {
            return payload->diagnoseOriginalNativeIdentity(version == 2u
                    ? rhi::OriginalDescriptorNativeIdentityRequest{4u, true, Profile::GeneralWrite2}
                    : rhi::OriginalDescriptorNativeIdentityRequest{
                          3u, true, Profile::GeneralWrite1});
        };
        assert(!identity() && payload->ordinaryTextureAccessDiagnostic());
        assert(payload->diagnoseOriginalNativeIdentity(version == 2u
                ? rhi::OriginalDescriptorNativeIdentityRequest{3u, true, Profile::GeneralWrite1}
                : rhi::OriginalDescriptorNativeIdentityRequest{4u, true, Profile::GeneralWrite2}));
        assert(payload->diagnoseOriginalNativeIdentity({1u, true, Profile::WholeWrite1}));
        for (std::size_t i = 0u; i < payload->bindings.size(); ++i) {
            auto& b = payload->bindings[i];
            auto savedBinding = b;
            b.write.resource = {};
            assert(identity());
            b = savedBinding;
            b.buffer.reset();
            b.textureView.reset();
            b.sampler.reset();
            assert(identity());
            b = savedBinding;
            assert(!identity());
        }
        const auto group = payload->group;
        payload->group = nullptr;
        assert(identity());
        payload->group = group;
        assert(!identity());
        if (version == 2u) {
            const auto count = payload->dynamicOffsetCount;
            ++payload->dynamicOffsetCount;
            assert(identity());
            payload->dynamicOffsetCount = count;
            if (count) {
                const auto binding = payload->dynamicOffsetBindings[0];
                ++payload->dynamicOffsetBindings[0];
                assert(identity());
                payload->dynamicOffsetBindings[0] = binding;
                assert(!identity());
            }
            auto& range = payload->bindings[0].write.bufferBindingRange;
            const auto saved = range;
            ++range.offsetBytes;
            assert(identity());
            range = saved;
            ++range.sizeBytes;
            assert(identity());
            range = saved;
            assert(!identity());
        }
        // Preserve an accepted group when a genuine native candidate fails.
        // clang-format off
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpGeneralCreateGroup = d.createBindGroup;
                d.createBindGroup = function(desc) {
                    return Module.arpGeneralCreateGroup.call(this, {...desc, entries : []});
                };
            // clang-format on
        });
        assert(!begin(elements));
        drain();
        assert(object.finishOriginalGeneralWrite());
        assert(object.contentRevision() == revision + 1u
            && rhi_webgpu::webGpuNativePayloadFor(object) == payload && identity());
        assert(!object.cancelOriginalGeneralWrite());
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createBindGroup = Module.arpGeneralCreateGroup;
                delete Module.arpGeneralCreateGroup;
            // clang-format on
        });
        // clang-format on
        assert(!identity() && !object.originalScopedContentDiagnostic());
        assert(!object.write(elements[0].write).success);
        for (const auto& e : elements) {
            const char* receiptId = nullptr;
            auto receipt = object.observeSuccessfulBindingWrite(e.write.binding, 0u, receiptId);
            assert(receipt && !receiptId && !receipt->diagnostic());
        }
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return identity();
            }));
        });
        std::puts(
            "BINDLESS_RHI_GENERAL_DESCRIPTOR_WRITE_OK actual-group scopes3 atomic-revision canonical-owners no-Sources-cycle quotas3 allocation-budget failed-replacement-preserved hot0-query0 execution-closed");
        if (version == 2u)
            std::puts(
                "BINDLESS_RHI_GENERAL_FINITE_WRITE2_OK mixed-group genuine-scopes atomic-revision profile-isolated ranges-offsets-frozen failed-replacement-preserved quotas3 hot0-query0 execution-closed");
    }
#endif
};
} // namespace arp::test
