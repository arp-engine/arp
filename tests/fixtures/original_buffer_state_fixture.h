#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_command_buffer_states_fixture.h"
#include "original_texture_state_range_fixture.h"
#include "rhi/frontend/command_model.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalBufferStateFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    // Transition is an actual backend's logical state writer. This fixture
    // proves original CPU control identity, never native barrier/completion.
    template <class Factory, class Transition>
    static void verify(Factory&& create, Transition&& transition)
    {
        using S = rhi::ResourceState;
        static_assert(!std::is_default_constructible_v<rhi::GpuBuffer::CommittedStateObservation>);
        auto domain = quota(), metadata = quota(), resources = quota();
        {
            auto device = create();
            assert(device);
            rhi::BufferDesc desc;
            desc.sizeBytes = 64u;
            desc.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
            desc.initialState = S::CopySrc;
            auto ordinary = device->createBuffer(desc);
            assert(ordinary && !ordinary->observeOriginalCommittedState());
            is(ordinary->originalCommittedStateDiagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 2u, 0u, 0u, 7u, bytes}, metadata));
            assert(!ordinary->observeOriginalCommittedState());
            ordinary.reset();
            const auto make = [&] {
                core::CpuMemoryQuotaScope scope(resources);
                return device->createBuffer(desc);
            };
            auto a = make(), b = make();
            assert(a && b);
            auto first = a->observeOriginalCommittedState(),
                 equal = b->observeOriginalCommittedState();
            assert(first && equal);
            assert(first->state() == equal->state() && first->revision() == equal->revision());
            assert(first->resourceGeneration() != equal->resourceGeneration()
                && !first->nativeExecutionProven());
            assert(!a->validateOriginalCommittedState(*first));
            is(b->validateOriginalCommittedState(*first),
                "RHI-ORIGINAL-BUFFER-STATE-SOURCE-MISMATCH");
            const auto generation = first->resourceGeneration();
            core::MutationJournal journal, foreign;
            assert(transition(*a, S::CopySrc, S::CopyDst, &journal).success);
            assert(!a->observeOriginalCommittedState());
            is(first->diagnostic(), "RHI-ORIGINAL-BUFFER-STATE-PENDING");
            is(a->validateOriginalCommittedState(*first), "RHI-ORIGINAL-BUFFER-STATE-PENDING");
            assert(!transition(*a, S::CopyDst, S::CopySrc, &foreign).success && foreign.empty());
            assert(!transition(*a, S::CopyDst, S::CopySrc, nullptr).success);
            journal.rollback();
            assert(a->state() == S::CopySrc && journal.empty());
            is(first->diagnostic(), "RHI-ORIGINAL-BUFFER-STATE-STALE");
            auto restored = a->observeOriginalCommittedState();
            assert(restored && restored->revision() != first->revision());
            assert(transition(*a, S::CopySrc, S::CopyDst, &journal).success);
            journal.commit();
            auto committed = a->observeOriginalCommittedState();
            assert(committed && committed->state() == S::CopyDst);
            is(restored->diagnostic(), "RHI-ORIGINAL-BUFFER-STATE-STALE");
            assert(transition(*a, S::CopyDst, S::CopySrc, nullptr).success);
            assert(transition(*a, S::CopySrc, S::CopyDst, nullptr).success);
            is(committed->diagnostic(), "RHI-ORIGINAL-BUFFER-STATE-STALE");
            auto current = a->observeOriginalCommittedState();
            assert(current && current->state() == committed->state()
                && current->resourceGeneration() == generation);
            assert(!transition(*a, S::CopyDst, S::ColorAttachment, nullptr).success
                && !current->diagnostic());
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{domain, metadata, resources}, [&]() noexcept {
                    auto copy = *current;
                    if (copy.diagnostic() || a->validateOriginalCommittedState(copy))
                        return "TEST-BUFFER-STATE-COPY";
                    if (!transition(*a, S::CopyDst, S::CopySrc, &journal).success)
                        return "TEST-BUFFER-STATE-WRITE";
                    if (a->observeOriginalCommittedState())
                        return "TEST-PENDING-COMMITTED-STATE";
                    journal.rollback();
                    auto after = a->observeOriginalCommittedState();
                    return after && after->state() == S::CopyDst
                            && after->resourceGeneration() == generation
                            && std::string_view(copy.diagnostic())
                                == "RHI-ORIGINAL-BUFFER-STATE-STALE"
                        ? nullptr
                        : "TEST-BUFFER-STATE-ROLLBACK";
                }));
            auto dying = b->observeOriginalCommittedState();
            assert(dying);
            assert(transition(*b, S::CopySrc, S::CopyDst, &journal).success);
            b.reset();
            assert(journal.empty());
            is(dying->diagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
            auto terminal = a->observeOriginalCommittedState();
            assert(terminal);
            assert(transition(*a, S::CopyDst, S::CopySrc, &journal).success);
            device->retireOriginalDeviceLifetime();
            assert(!a->observeOriginalCommittedState());
            is(terminal->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
            // Legacy barrier results own PMR diagnostic strings. Their error
            // construction is tested without exhausted quotas; control entry
            // points and rollback have separate hot allocation0 coverage.
            assert(!transition(*a, S::CopySrc, S::CopyDst, nullptr).success);
            assert(!transition(*a, S::CopySrc, S::CopyDst, &foreign).success && foreign.empty());
            journal.rollback();
            assert(a->state() == S::CopyDst && !a->observeOriginalCommittedState());
            assert(!transition(*a, S::CopyDst, S::CopySrc, nullptr).success);
            a.reset();
            is(terminal->diagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
            device.reset();
        }
        assert(!domain->liveBytes() && !metadata->liveBytes());
        {
            auto device = create();
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 1u, 0u, 0u, 5u, bytes}, metadata));
            rhi::BufferDesc desc;
            desc.sizeBytes = 64u;
            desc.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
            desc.initialState = S::CopySrc;
            core::CpuMemoryQuotaScope scope(resources);
            auto buffer = device->createBuffer(desc);
            assert(buffer);
            core::MutationJournal journal, foreign;
            assert(transition(*buffer, S::CopySrc, S::CopyDst, &journal).success);
            device.reset();
            assert(
                !transition(*buffer, S::CopyDst, S::CopySrc, &foreign).success && foreign.empty());
            journal.rollback();
            assert(buffer->state() == S::CopySrc && !buffer->observeOriginalCommittedState());
            assert(!transition(*buffer, S::CopySrc, S::CopyDst, nullptr).success);
            is(buffer->originalCommittedStateDiagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        }
        assert(!domain->liveBytes() && !metadata->liveBytes() && !resources->liveBytes());
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
            assert(!arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
                       .originalIssuerAvailable
                && !arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
                    .executionAvailable);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_BUFFER_STATE_OK original-control factory-registration exact-source-generation-revision pending-foreign-write-reject rollback-ABA commit weak-destruction-retirement hot-allocation0 full-state-native-gates4-closed");
        OriginalCommandBufferStatesFixture::verify(create, transition);
        OriginalTextureStateRangeFixture::verify();
    }
};
} // namespace arp::test
