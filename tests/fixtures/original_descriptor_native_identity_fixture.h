#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/descriptor_validation.h"

namespace arp::test {
class OriginalDescriptorNativeIdentityFixture final {
    using Contract = rhi::OriginalDescriptorNativeIdentityContract;
    using Request = Contract::Request;
    static constexpr Request finite{1u, true, Request::Profile::FiniteWrite1};
    static constexpr Request whole{1u, true, Request::Profile::WholeWrite1};

    static void is(const char* actual, const char* expected)
    {
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void verifyContract()
    {
        assert(!Contract::validateIntent(finite) && !Contract::validateIntent(whole));
        auto invalid = finite;
        invalid.contractVersion = 2u;
        is(Contract::validateIntent(invalid), "RHI-ORIGINAL-GROUP-IDENTITY-VERSION");
        invalid = finite;
        invalid.prototypeOptIn = false;
        is(Contract::validateIntent(invalid), "RHI-ORIGINAL-GROUP-IDENTITY-OPT-IN");
        invalid = finite;
        invalid.profile = Request::Profile::None;
        is(Contract::validateIntent(invalid), "RHI-ORIGINAL-GROUP-IDENTITY-PROFILE");
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto f = Contract::lowering(backend, finite.profile),
                       w = Contract::lowering(backend, whole.profile);
            assert(!f.executionAvailable && !w.executionAvailable);
            assert(w.identityAvailable == (backend == rhi::Backend::WebGPU)
                && w.validatorOnly == (backend == rhi::Backend::Null));
            assert(w.mode
                == (backend == rhi::Backend::WebGPU ? rhi::CapabilityMode::Equivalent
                                                    : rhi::CapabilityMode::DiagnosticFailure));
            assert(bool(w.diagnostic) != w.identityAvailable);
            assert(f.identityAvailable == (backend == rhi::Backend::WebGPU)
                && f.validatorOnly == (backend == rhi::Backend::Null));
        }
        rhi_webgpu::WebGpuNativeDescriptorGroupPayload empty;
        const auto count = core::defaultCpuAllocator().stats().allocationCount;
        assert(empty.diagnoseOriginalNativeIdentity(finite));
        is(empty.diagnoseOriginalNativeIdentity(whole), "RHI-ORIGINAL-FINITE-WRITE-UNPREPARED");
        is(empty.diagnoseOriginalNativeIdentity({}), "RHI-ORIGINAL-GROUP-IDENTITY-VERSION");
        is(empty.diagnoseOriginalNativeIdentity({1u, false, Request::Profile::WholeWrite1}),
            "RHI-ORIGINAL-GROUP-IDENTITY-OPT-IN");
        is(empty.diagnoseOriginalNativeIdentity(invalid), "RHI-ORIGINAL-GROUP-IDENTITY-PROFILE");
        assert(count == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_NATIVE_IDENTITY_CONTRACT_OK version1 explicit-finite-whole profiles backends4 no-execution-authority whole-committed-scopes-required empty-reject allocation0");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyWholeIdentity(
        const std::shared_ptr<rhi_webgpu::WebGpuNativeDescriptorGroupPayload>& payload,
        const char* expected = nullptr)
    {
        assert(payload);
        const auto* diagnostic = payload->diagnoseOriginalNativeIdentity(whole);
        if (expected)
            is(diagnostic, expected);
        else
            assert(!diagnostic);
        is(payload->diagnoseOriginalNativeIdentity(finite), "RHI-ORIGINAL-SCOPED-WRITE-PROFILE");
    }

    // Shared diagnostic-only mutation driver. Restore every original field
    // before releasing any genuine native owner; never issue GPU work here.
    template <class Reject>
    static void mutateActual(
        const std::shared_ptr<rhi_webgpu::WebGpuNativeDescriptorGroupPayload>& payload,
        Reject&& reject)
    {
        assert(payload && payload->group && payload->layout && !payload->bindings.empty());
        const auto g = payload->group;
        const auto l = payload->layout;
        payload->group = nullptr;
        reject();
        payload->group = g;
        payload->layout = nullptr;
        reject();
        payload->layout = l;
        // Backend handles are opaque. The existing real group/layout
        // handles suffice as hostile non-null replacements; no native
        // API sees these diagnostic-only corruptions, and restore is
        // mandatory before resource release.
        payload->group = reinterpret_cast<WGPUBindGroup>(l);
        reject();
        payload->group = g;
        payload->layout = reinterpret_cast<WGPUBindGroupLayout>(g);
        reject();
        payload->layout = l;
        payload->tableBacked = true;
        reject();
        payload->tableBacked = false;
        payload->containsLoweredInputAttachment = true;
        reject();
        payload->containsLoweredInputAttachment = false;
        ++payload->shaderPackageHash;
        reject();
        --payload->shaderPackageHash;
        ++payload->dynamicOffsetCount;
        reject();
        --payload->dynamicOffsetCount;
        if (!payload->dynamicOffsetBindings.empty()) {
            ++payload->dynamicOffsetBindings[0];
            reject();
            --payload->dynamicOffsetBindings[0];
        }
        auto& b = payload->bindings[0];
        const auto resource = b.write.resource;
        b.write.resource = {};
        reject();
        b.write.resource = resource;
        const auto state = b.write.resourceState;
        b.write.resourceState = state == rhi::ResourceState::Undefined
            ? rhi::ResourceState::ShaderRead
            : rhi::ResourceState::Undefined;
        reject();
        b.write.resourceState = state;
        const auto size = b.write.bufferBindingRange.sizeBytes;
        ++b.write.bufferBindingRange.sizeBytes;
        reject();
        b.write.bufferBindingRange.sizeBytes = size;
        ++b.arrayElement;
        reject();
        --b.arrayElement;
        const auto stages = b.binding.stages;
        b.binding.stages = rhi::ShaderStage::None;
        reject();
        b.binding.stages = stages;
        auto buffer = b.buffer;
        b.buffer.reset();
        reject();
        b.buffer = buffer;
        b.buffer = std::shared_ptr<rhi_webgpu::WebGpuNativeBufferPayload>(payload, buffer.get());
        reject();
        b.buffer = buffer;
    }

    template <class Diagnose, std::size_t Count>
    static void verifyActual(
        const std::shared_ptr<rhi_webgpu::WebGpuNativeDescriptorGroupPayload>& payload,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, Count>& quotas,
        Diagnose&& diagnose,
        bool report = true)
    {
        assert(payload && payload->group && payload->layout && !payload->bindings.empty());
        assert(!payload->diagnoseOriginalNativeIdentity(finite) && !diagnose());
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    const auto reject = [&] {
                        is(payload->diagnoseOriginalNativeIdentity(finite),
                            "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY");
                        is(diagnose(), "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY");
                        std::uint32_t group = 99u, count = 99u;
                        const std::uint32_t* offsets = nullptr;
                        is(payload->diagnoseOriginalFiniteGroup(group, offsets, count),
                            "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY");
                        assert(!group && !count && !offsets);
                    };
                    mutateActual(payload, reject);
                    assert(!payload->diagnoseOriginalNativeIdentity(finite) && !diagnose());
                    is(payload->diagnoseOriginalNativeIdentity(whole),
                        "RHI-ORIGINAL-SCOPED-WRITE-PROFILE");
                    return nullptr;
                }));
        });
        if (report)
            std::puts(
                "BINDLESS_RHI_DESCRIPTOR_NATIVE_IDENTITY_OK genuine-finite-group original-handles frozen-entries binding-owner-shape-write dynamic-mapping mutations-reject restore-output0 hot0 query0 profiles-disjoint native-recording-closed");
    }
#endif
};
} // namespace arp::test
