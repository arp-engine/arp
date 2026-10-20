#pragma once

#include "original_command_resource_states_fixture.h"
#include "rhi/frontend/resource_texture_state_control.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/command_lowering.h"
#include "versioned_mutation_array_fixture.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalTextureStateFixture final {
    static constexpr std::uint64_t bytes = 65536u;
    using S = rhi::ResourceState;
    using R = rhi::TextureSubresourceRange;
    using A = rhi::TextureAspect;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verifyControl()
    {
        VersionedMutationArrayFixture::verify();
        rhi::TextureDesc desc;
        desc.extent = {4u, 4u, 1u};
        desc.mipLevelCount = 2u;
        desc.arrayLayerCount = 2u;
        desc.format = rhi::TextureFormat::Depth24Stencil8;
        auto cpu = quota();
        {
            core::CpuMemoryQuotaScope scope(cpu);
            rhi::common::RecordedTextureStateControl control(desc, S::Undefined);
            const R depth{0u, 2u, 0u, 2u, A::Depth}, stencil{0u, 2u, 0u, 2u, A::Stencil},
                single{1u, 1u, 1u, 1u, A::Depth};
            const auto d = control.observe(depth), s = control.observe(stencil);
            assert(d && s && control.geometry().cellCount() == 8u);
            core::MutationJournal noWrite;
            assert(control.prepare(noWrite));
            const auto prepared = control.observePending(depth, noWrite);
            assert(prepared);
            noWrite.commit();
            assert(!control.validate(*d) && control.prepare(noWrite));
            assert(!control.matchesPending(*prepared, noWrite));
            noWrite.commit();
            assert(!control.write(single, S::Undefined, S::CopyDst, nullptr));
            assert(control.uniformState(stencil) == S::Undefined
                && control.uniformState(single) == S::CopyDst);
            is(control.validate(*d), "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM");
            is(control.validate(*s), "RHI-ORIGINAL-TEXTURE-STATE-STALE");
            core::MutationJournal journal;
            assert(!control.write(single, S::CopyDst, S::CopySrc, &journal));
            control.invalidate();
            journal.rollback();
            assert(!control.active() && !control.observe(single));
            assert(control.uniformState(single) == S::CopyDst);
            is(control.write(stencil, S::Undefined, S::ColorAttachment, nullptr),
                "RHI-RESOURCE-STATE-INVALID");
        }
        assert(!cpu->liveBytes());
    }

    // normalized is chosen by the explicit fixture invocation, never backend
    // name inference. Native creation must supply the selected control shape.
    template <class Factory, class Transition>
    static void verify(Factory&& create, Transition&& transition, bool normalized)
    {
        static_assert(!std::is_default_constructible_v<rhi::GpuTexture::CommittedStateObservation>);
        verifyControl();
        auto domain = quota(), registry = quota(), resources = quota();
        {
            auto device = create();
            assert(device);
            rhi::TextureDesc desc;
            desc.extent = {4u, 4u, 1u};
            desc.mipLevelCount = 2u;
            desc.arrayLayerCount = 2u;
            desc.format = rhi::TextureFormat::Rgba8Unorm;
            desc.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
            desc.initialState = S::CopySrc;
            const auto whole = rhi::defaultTextureSubresourceRange(desc);
            const R part{0u, 1u, 0u, 1u, A::Color}, other{1u, 1u, 1u, 1u, A::Color};
            auto ordinary = device->createTexture(desc);
            assert(ordinary && !ordinary->observeOriginalCommittedState(whole));
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 0u, 2u, 0u, 7u, bytes}, registry));
            ordinary.reset();
            const auto make = [&]() {
                try {
                    core::CpuMemoryQuotaScope scope(resources);
                    return device->createTexture(desc);
                } catch (const std::bad_alloc&) {
                    return core::UniquePtr<rhi::GpuTexture>{};
                }
            };
            CpuQuotaFactoryFixture pressure(resources);
            const auto peak = pressure.measure(make);
            pressure.verify(peak, make);
            const auto memory = device->memoryStats().allocatedBytes;
            {
                core::CpuMemoryQuotaLease full(resources, bytes - resources->liveBytes());
                assert(full.charged() && !make());
            }
            assert(device->memoryStats().allocatedBytes == memory);
            auto a = make(), b = make();
            assert(a && b);
            auto first = a->observeOriginalCommittedState(whole),
                 equal = b->observeOriginalCommittedState(whole);
            assert(first && equal);
            assert(first->state() == equal->state() && first->revision() == equal->revision()
                && first->resourceGeneration() != equal->resourceGeneration());
            assert(!a->validateOriginalCommittedState(*first));
            is(b->validateOriginalCommittedState(*first),
                "RHI-ORIGINAL-TEXTURE-STATE-SOURCE-MISMATCH");
            assert(!a->observeOriginalCommittedState({UINT32_MAX, 2u, 0u, 1u, A::Color}));
            is(a->originalCommittedStateDiagnostic({0u, 1u, 0u, 1u, A::Depth}),
                "RHI-ORIGINAL-TEXTURE-RANGE-ASPECT");
            auto& mutableDesc = const_cast<rhi::TextureDesc&>(a->desc());
            ++mutableDesc.arrayLayerCount;
            is(a->originalCommittedStateDiagnostic(whole), "RHI-ORIGINAL-TEXTURE-STATE-LAYOUT");
            --mutableDesc.arrayLayerCount;
            if (normalized) {
                auto untouched = a->observeOriginalCommittedState(other);
                assert(untouched);
                assert(transition(*a, part, S::CopySrc, S::CopyDst, nullptr).success);
                assert(a->subresourceState(part) == S::CopyDst
                    && a->subresourceState(other) == S::CopySrc);
                assert(!a->observeOriginalCommittedState(whole));
                is(first->diagnostic(), "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM");
                is(untouched->diagnostic(), "RHI-ORIGINAL-TEXTURE-STATE-STALE");
                const auto beforeMixed = a->observeOriginalCommittedState(part);
                assert(beforeMixed);
                assert(!transition(*a, whole, S::Undefined, S::CopyDst, nullptr).success);
                assert(!a->validateOriginalCommittedState(*beforeMixed));
                assert(transition(*a, part, S::CopyDst, S::CopySrc, nullptr).success);
                is(first->diagnostic(), "RHI-ORIGINAL-TEXTURE-STATE-STALE");
                const R vertical{0u, 2u, 0u, 1u, A::Color}, intersection{1u, 1u, 0u, 1u, A::Color},
                    horizontal{1u, 1u, 0u, 2u, A::Color};
                assert(transition(*a, vertical, S::CopySrc, S::CopyDst, nullptr).success);
                assert(a->subresourceState(intersection) == S::CopyDst
                    && a->subresourceState(other) == S::CopySrc);
                is(a->originalCommittedStateDiagnostic(horizontal),
                    "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM");
                assert(transition(*a, intersection, S::CopyDst, S::ShaderRead, nullptr).success);
                is(a->originalCommittedStateDiagnostic(vertical),
                    "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM");
                assert(transition(*a, intersection, S::ShaderRead, S::CopyDst, nullptr).success);
                assert(transition(*a, vertical, S::CopyDst, S::CopySrc, nullptr).success);
            } else {
                assert(!a->observeOriginalCommittedState(part));
                is(a->originalCommittedStateDiagnostic(part),
                    "RHI-ORIGINAL-TEXTURE-STATE-PARTIAL-CONTROL-UNPROVEN");
                assert(!transition(*a, part, S::CopySrc, S::CopyDst, nullptr).success
                    && !a->validateOriginalCommittedState(*first));
            }
            auto current = a->observeOriginalCommittedState(whole);
            assert(current);
            core::MutationJournal journal, foreign;
            assert(transition(*a, whole, S::CopySrc, S::CopyDst, &journal).success);
            assert(!a->observeOriginalCommittedState(whole));
            is(current->diagnostic(), "RHI-ORIGINAL-TEXTURE-STATE-PENDING");
            assert(!transition(*a, whole, S::CopyDst, S::CopySrc, &foreign).success
                && foreign.empty());
            assert(!transition(*a, whole, S::CopyDst, S::CopySrc, nullptr).success);
            journal.rollback();
            assert(a->subresourceState(whole) == S::CopySrc);
            is(current->diagnostic(), "RHI-ORIGINAL-TEXTURE-STATE-STALE");
            auto restored = a->observeOriginalCommittedState(whole);
            assert(restored);
            assert(transition(*a, whole, S::CopySrc, S::CopyDst, &journal).success);
            journal.commit();
            is(restored->diagnostic(), "RHI-ORIGINAL-TEXTURE-STATE-STALE");
            const auto committed = a->observeOriginalCommittedState(whole);
            assert(committed && committed->state() == S::CopyDst);
            assert(!transition(*a, whole, S::CopyDst, static_cast<S>(UINT32_MAX), nullptr).success
                && !committed->diagnostic());
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{domain, registry, resources}, [&]() noexcept {
                    auto copy = *committed;
                    if (copy.diagnostic() || a->validateOriginalCommittedState(copy))
                        return "TEST-TEXTURE-COPY";
                    if (!transition(*a, whole, S::CopyDst, S::CopySrc, &journal).success)
                        return "TEST-TEXTURE-WRITE";
                    if (a->observeOriginalCommittedState(whole))
                        return "TEST-TEXTURE-PENDING";
                    journal.rollback();
                    return copy.diagnostic() && a->subresourceState(whole) == S::CopyDst
                        ? nullptr
                        : "TEST-TEXTURE-ABA";
                }));
            auto dying = b->observeOriginalCommittedState(whole);
            assert(dying);
            assert(transition(*b, whole, S::CopySrc, S::CopyDst, &journal).success);
            b.reset();
            assert(journal.empty());
            is(dying->diagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
            auto terminal = a->observeOriginalCommittedState(whole);
            assert(terminal);
            assert(transition(*a, whole, S::CopyDst, S::CopySrc, &journal).success);
            device->retireOriginalDeviceLifetime();
            // Legacy barrier results own PMR diagnostic strings. Their error
            // construction is tested without exhausted quotas; control entry
            // points and rollback have separate hot allocation0 coverage.
            assert(!transition(*a, whole, S::CopySrc, S::CopyDst, nullptr).success);
            assert(!transition(*a, whole, S::CopySrc, S::CopyDst, &foreign).success
                && foreign.empty());
            journal.rollback();
            assert(a->subresourceState(whole) == S::CopyDst
                && !a->observeOriginalCommittedState(whole));
            assert(!transition(*a, whole, S::CopyDst, S::CopySrc, nullptr).success);
            is(terminal->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
            a.reset();
            is(terminal->diagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
            device.reset();
        }
        assert(!domain->liveBytes() && !registry->liveBytes());
        {
            auto device = create();
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 0u, 1u, 0u, 5u, bytes}, registry));
            rhi::TextureDesc desc;
            desc.extent = {2u, 2u, 1u};
            desc.format = rhi::TextureFormat::Rgba8Unorm;
            desc.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
            desc.initialState = S::CopySrc;
            core::CpuMemoryQuotaScope scope(resources);
            auto texture = device->createTexture(desc);
            assert(texture);
            const auto range = rhi::defaultTextureSubresourceRange(desc);
            core::MutationJournal journal, foreign;
            assert(transition(*texture, range, S::CopySrc, S::CopyDst, &journal).success);
            device.reset();
            assert(!transition(*texture, range, S::CopyDst, S::CopySrc, &foreign).success
                && foreign.empty());
            journal.rollback();
            assert(texture->subresourceState(range) == S::CopySrc
                && !texture->observeOriginalCommittedState(range));
            assert(!transition(*texture, range, S::CopySrc, S::CopyDst, nullptr).success);
            is(texture->originalCommittedStateDiagnostic(range),
                "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        }
        assert(!domain->liveBytes() && !registry->liveBytes() && !resources->liveBytes());
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
            assert(!arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
                       .originalIssuerAvailable
                && !arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
                    .executionAvailable);
        OriginalCommandResourceStatesFixture::verify(create, transition, normalized);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_TEXTURE_CONTROL_OK actual-registration original-control exact-range-revision normalized-or-uniform mixed-partial-reject rollback-ABA commit weak-retirement hot-allocation0 issuers4-closed");
    }
};
} // namespace arp::test
