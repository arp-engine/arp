#pragma once
#include "capability_override_device.h"
#include "cpu_quota_factory_fixture.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
// Logical publication negatives use a test proxy and original controls only;
// actual backend factories and real native loss are verified separately.
class OriginalStateDomainFixture final {
    using S = rhi::ResourceState;
    using Scalar = rhi::common::RecordedResourceStateControl;
    using Texture = rhi::common::RecordedTextureStateControl;
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

    class Buffer final : public rhi::GpuBuffer {
    public:
        explicit Buffer(Scalar* state) : state_(state)
        {
            if (state)
                bindOriginalRecordedState(*state);
        }

        rhi::GpuResourceId id() const override { return id_; }

        const rhi::BufferDesc& desc() const override { return desc_; }

        const rhi::ResourceAllocation& allocation() const override { return allocation_; }

        S state() const override { return state_ ? state_->state() : S::CopySrc; }

    private:
        Scalar* state_;
        rhi::GpuResourceId id_ = rhi::allocateGpuResourceId();
        rhi::BufferDesc desc_;
        rhi::ResourceAllocation allocation_;
    };

    class Image final : public rhi::GpuTexture {
    public:
        Image(const rhi::TextureDesc& desc, Texture& state) : desc_(desc), state_(state)
        {
            bindOriginalRecordedState(state);
        }

        rhi::GpuResourceId id() const override { return id_; }

        const rhi::TextureDesc& desc() const override { return desc_; }

        const rhi::ResourceAllocation& allocation() const override { return allocation_; }

        S state() const override
        {
            return *state_.uniformState(rhi::defaultTextureSubresourceRange(desc_));
        }

        S subresourceState(const rhi::TextureSubresourceRange& range) const override
        {
            return state_.uniformState(range).value_or(S::Undefined);
        }

    private:
        rhi::GpuResourceId id_ = rhi::allocateGpuResourceId();
        rhi::TextureDesc desc_;
        Texture& state_;
        rhi::ResourceAllocation allocation_;
    };

public:
    template <class Factory> static void verify(Factory&& create)
    {
        static_assert(!std::is_default_constructible_v<Texture::PendingObservation>);
        for (const bool shutdown : {false, true}) {
            auto inner = create();
            assert(inner);
            auto domain = quota(), metadata = quota(), resource = quota();
            Scalar scalar(S::CopySrc), unbound(S::CopySrc);
            rhi::TextureDesc desc;
            desc.extent = {4u, 4u, 1u};
            desc.mipLevelCount = 2u;
            desc.arrayLayerCount = 2u;
            desc.format = rhi::TextureFormat::Rgba8Unorm;
            desc.initialState = S::CopySrc;
            core::CpuMemoryQuotaScope scope(resource);
            Texture texture(desc, S::CopySrc), peer(desc, S::CopySrc);
            const auto range = rhi::defaultTextureSubresourceRange(desc);
            auto device = core::makeUnique<tests::CapabilityOverrideDevice>(*inner, nullptr);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 2u, 1u, 0u, 9u, bytes}, metadata));
            core::MutationJournal journal, foreign;
            assert(unbound.prepare(journal));
            assert(!device->completeOriginalFixtureCandidate(core::makeUnique<Buffer>(&unbound)));
            is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-RESOURCE-STATE-DOMAIN-PENDING");
            assert(!unbound.originalDomainDiagnostic());
            journal.rollback();
            auto buffer =
                device->completeOriginalFixtureCandidate(core::makeUnique<Buffer>(&scalar));
            assert(buffer);
            const auto committed = scalar.observe();
            assert(committed);
            assert(!device->completeOriginalFixtureCandidate(core::makeUnique<Buffer>(&scalar)));
            is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-RESOURCE-STATE-DOMAIN-ALREADY-BOUND");
            auto missing =
                device->completeOriginalFixtureCandidate(core::makeUnique<Buffer>(nullptr));
            assert(missing);
            is(missing->originalCommittedStateDiagnostic(),
                "RHI-ORIGINAL-BUFFER-STATE-SOURCE-MISSING");
            missing.reset();
            auto image =
                device->completeOriginalFixtureCandidate(core::makeUnique<Image>(desc, texture));
            assert(image);
            const auto textureCommitted = texture.observe(range);
            assert(textureCommitted);
            assert(!scalar.writeBuffer(S::CopyDst, &journal));
            assert(!texture.write(range, S::CopySrc, S::CopyDst, &journal));
            const auto pending = scalar.observePending(journal);
            const auto texturePending = texture.observePending(range, journal);
            assert(pending && texturePending && scalar.matchesPending(*pending, journal)
                && texture.matchesPending(*texturePending, journal));
            assert(!texture.observePending(range, foreign)
                && !texture.matchesPending(*texturePending, foreign)
                && !peer.matchesPending(*texturePending, journal));
            const auto oldPending = *texturePending;
            const rhi::TextureSubresourceRange part{0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
            assert(!texture.write(part, S::CopyDst, S::CopySrc, &journal));
            assert(!texture.matchesPending(oldPending, journal)
                && !texture.observePending(range, journal));
            const auto partPending = texture.observePending(part, journal);
            assert(partPending);
            const char* terminal = shutdown ? "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN"
                                            : "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED";
            if (shutdown)
                device.reset();
            else
                device->retireOriginalDeviceLifetime();
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{domain, metadata, resource}, [&]() noexcept {
                    is(scalar.originalDomainDiagnostic(), terminal);
                    is(texture.originalDomainDiagnostic(), terminal);
                    if (scalar.active() || texture.active() || scalar.canPrepare(foreign)
                        || texture.canPrepare(foreign) || scalar.prepare(foreign)
                        || texture.prepare(foreign) || !foreign.empty())
                        return "TEST-DOMAIN-PREPARE";
                    if (scalar.observe() || scalar.matches(*committed)
                        || scalar.observePending(journal)
                        || scalar.matchesPending(*pending, journal) || texture.observe(range)
                        || texture.observePending(part, journal)
                        || texture.matchesPending(*partPending, journal))
                        return "TEST-DOMAIN-OBSERVATION";
                    is(texture.validate(*textureCommitted), terminal);
                    is(scalar.writeBuffer(S::CopySrc, &journal), terminal);
                    is(texture.write(part, S::CopySrc, S::CopyDst, &journal), terminal);
                    journal.rollback();
                    is(scalar.writeBuffer(S::CopyDst, nullptr), terminal);
                    is(texture.write(range, S::CopySrc, S::CopyDst, nullptr), terminal);
                    return scalar.state() == S::CopySrc && texture.uniformState(range) == S::CopySrc
                            && !scalar.active() && !texture.active()
                            && !scalar.observePending(journal)
                            && !texture.observePending(range, journal)
                        ? nullptr
                        : "TEST-DOMAIN-ROLLBACK";
                }));
            buffer.reset();
            image.reset();
            device.reset();
            assert(!metadata->liveBytes());
        }
        std::puts(
            "BINDLESS_RHI_ORIGINAL_STATE_DOMAIN_OK sealed-one-shot pending-duplicate-missing-negative scalar-normalized exact-control-journal-range terminal-retire-shutdown rollback-no-revival hot-allocation0 full-gates-closed");
    }
};
} // namespace arp::test
