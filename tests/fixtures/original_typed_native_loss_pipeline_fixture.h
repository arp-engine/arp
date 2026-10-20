#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/gpu_device.h"
#include "webgpu_foreign_device_fixture.h"

namespace arp::test {
class OriginalTypedNativeLossPipelineFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && core::StringView(actual) == expected);
    }

public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class Pipeline, class Prepare> static void verifyActual(Prepare&& prepare)
    {
        auto c = prepare();
        c->ready();
        assert(c->lossQuota && !c->device->originalDeviceNativeLossDiagnostic());
        const auto factory = [&] {
            return c->publish();
        };
        CpuQuotaFactoryFixture registry(c->quotas[1]), wrapper(c->quotas[6]);
        const auto registryPeak = registry.measure(factory), wrapperPeak = wrapper.measure(factory);
        registry.verify(registryPeak, factory);
        wrapper.verify(wrapperPeak, factory);
        for (const auto index : std::array{1u, 6u}) {
            const auto& q = c->quotas[index];
            core::CpuMemoryQuotaLease full(q, q->limitBytes() - q->liveBytes());
            assert(full.charged() && !factory());
        }
        const char* diagnostic = nullptr;
        auto pipeline =
            rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(factory(), diagnostic);
        assert(pipeline && !diagnostic);
        assert(!c->device->validateOriginalResourceRegistration(*pipeline));
        assert(pipeline->originalResourceRegistration().matchesParent(
            c->layout->originalResourceRegistration()));
        assert(core::SharedOwnerProjection::sameOwner(pipeline->originalLayoutOwner(), c->layout));
        auto native = rhi_webgpu::webGpuNativePayloadFor(*pipeline);
        assert(native && native->issuedContract());
        const auto sourceQuota = c->lossQuota;
        const std::array quotas{c->quotas[0],
            c->quotas[1],
            c->quotas[2],
            c->quotas[3],
            c->quotas[4],
            c->quotas[5],
            c->quotas[6],
            sourceQuota};
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    sourceQuota->setLimitBytes(bytes - 1u);
                    is(c->device->validateOriginalResourceRegistration(*pipeline),
                        "RHI-DEVICE-LOSS-REGISTRATION-QUOTA");
                    assert(!factory()
                        && c->device->lastOriginalResourceRegistrationDiagnostic()
                            == "RHI-DEVICE-LOSS-REGISTRATION-QUOTA");
                    sourceQuota->setLimitBytes(bytes);
                    const auto wrapperQuota = c->quotas[6];
                    c->quotas[6] = sourceQuota;
                    assert(!factory()
                        && c->device->lastOriginalResourceRegistrationDiagnostic()
                            == "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-QUOTA-DOMAIN");
                    c->quotas[6] = wrapperQuota;
                    const auto handle = native->pipeline;
                    native->pipeline = nullptr;
                    assert(
                        c->device->validateOriginalResourceRegistration(*pipeline) && !factory());
                    native->pipeline = handle;
                    const auto layout = native->layout;
                    native->layout = nullptr;
                    assert(
                        c->device->validateOriginalResourceRegistration(*pipeline) && !factory());
                    native->layout = layout;
                    return c->device->validateOriginalResourceRegistration(*pipeline);
                }));
        });
        assert(pipeline->independentAcceptedCpuQuotaDiagnostic(sourceQuota));
        auto ticket = pipeline->originalResourceRegistration();
        c->receipt.reset();
        c->layout.reset();
        assert(!c->device->validateOriginalResourceRegistration(*pipeline));
        c->device.reset();
        is(ticket.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        pipeline.reset();
        ticket = {};
        native.reset(); // Exact actual owners and weak control all retire.
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        c.reset();
        {
            auto alias = prepare(true);
            alias->ready();
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!alias->publish());
            is(alias->device->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-QUOTA-DOMAIN");
            assert(before == core::defaultCpuAllocator().stats().allocationCount);
            alias->retire();
        }
        {
            auto less = prepare(false, 10u);
            less->ready();
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!less->publish());
            is(less->device->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY");
            assert(before == core::defaultCpuAllocator().stats().allocationCount);
            less->retire();
        }
        {
            // The actor is genuinely accepted on another physical device with
            // unchanged original registered CPU layout. Publication must reject
            // the captured source join, even though all actor scopes succeeded.
            WebGpuForeignDeviceFixture foreign;
            auto other = prepare();
            {
                WebGpuForeignDeviceFixture::Scope scope(foreign);
                other->ready();
                const auto metadata = other->quotas[1]->liveBytes(),
                           wrappers = other->quotas[6]->liveBytes();
                assert(!other->publish());
                is(other->device->lastOriginalResourceRegistrationDiagnostic().data(),
                    "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-NATIVE-SOURCE");
                assert(other->quotas[1]->liveBytes() == metadata
                    && other->quotas[6]->liveBytes() == wrappers);
                other->retire();
            }
        }
        auto retained = prepare();
        retained->ready();
        auto bound =
            rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(retained->publish(), diagnostic);
        assert(bound);
        loss_<Pipeline> = core::makeUnique<Retained<Pipeline>>();
        auto& state = *loss_<Pipeline>;
        for (std::size_t i = 0u; i < retained->quotas.size(); ++i)
            state.quotas[i] = retained->quotas[i];
        state.quotas.back() = retained->lossQuota;
        state.device = std::move(retained->device);
        state.pipeline = std::move(bound);
        retained->receipt.reset();
        retained->layout.reset();
        retained.reset();
        std::printf(
            "BINDLESS_RHI_TYPED_LOSS_PIPELINE_NATIVE_OK %s registry7 accepted-scopes exact-captured-source parent-and-receipt quota-mutation native-corruption foreign-accepted-rejected caller-drop shutdown cold%llu,%llu exact-less-exhausted hot0 query0 recording-closed\n",
            std::is_same_v<Pipeline, rhi::ComputePipeline> ? "compute" : "graphics",
            static_cast<unsigned long long>(registryPeak),
            static_cast<unsigned long long>(wrapperPeak));
    }

    template <class Pipeline> static void verifyActualLoss()
    {
        assert(loss_<Pipeline>);
        auto& state = *loss_<Pipeline>;
        assert(WebGpuAsyncWaitFixture::until([&] {
            return state.pipeline->originalResourceRegistration().diagnostic() != nullptr;
        }));
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                state.quotas, [&]() noexcept -> const char* {
                    is(state.pipeline->originalResourceRegistration().diagnostic(),
                        "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                    is(state.device->validateOriginalResourceRegistration(*state.pipeline),
                        "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                    is(state.device->originalDeviceNativeLossDiagnostic(),
                        "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                    return nullptr;
                }));
        });
        const auto quotas = state.quotas;
        loss_<Pipeline>.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        std::printf(
            "BINDLESS_RHI_TYPED_LOSS_PIPELINE_PHYSICAL_LOSS_OK %s original-callback domain-native-lost registration-and-payload-rejected quotas8-zero hot0 query0 no-native-command\n",
            std::is_same_v<Pipeline, rhi::ComputePipeline> ? "compute" : "graphics");
    }

private:
    template <class Pipeline> class Retained final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 8u> quotas;
        core::UniquePtr<rhi::GpuDevice> device;
        std::shared_ptr<Pipeline> pipeline;
    };
    template <class Pipeline> inline static core::UniquePtr<Retained<Pipeline>> loss_;
#endif
};
} // namespace arp::test
