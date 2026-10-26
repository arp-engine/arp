#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/allocation_lifetime.h"
#include "rhi/frontend/resource_registration.h"
#include "rhi/frontend/resource_state_control.h"
#include "rhi/frontend/resource_texture_content_control.h"
#include "rhi/frontend/resource_texture_state_control.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_resource_types.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

namespace arp::rhi {

class BindlessSnapshot;
class OriginalCommandStateTransaction;
class GpuMemoryHeap;

class GpuBufferWriteSealState {
public:
    bool transferWritesSealed() const noexcept
    {
        return (flags_.load(std::memory_order_acquire) & 1u) != 0u;
    }

    bool writableDescriptorSealed() const noexcept
    {
        return (flags_.load(std::memory_order_acquire) & 2u) != 0u;
    }

private:
    friend class GpuBuffer;

    void sealTransferWrites() noexcept { flags_.fetch_or(1u, std::memory_order_release); }

    void sealAllGpuWrites() noexcept { flags_.fetch_or(3u, std::memory_order_release); }

    std::atomic<std::uint8_t> flags_{0};
};

class ARP_API GpuBuffer : public core::SharedObjectIdentity {
public:
    class CommittedStateObservation final {
        friend class GpuBuffer;
        friend class OriginalCommandStateTransaction;

    public:
        CommittedStateObservation(const CommittedStateObservation&) = default;

        ResourceState state() const noexcept { return observed_.value; }

        std::uint64_t revision() const noexcept { return observed_.revision; }

        std::uint64_t resourceGeneration() const noexcept { return generation_; }

        bool nativeExecutionProven() const noexcept { return false; }

        const char* diagnostic() const noexcept
        {
            if (const auto* id = registration_.diagnostic())
                return id;
            if (registration_.generation() != generation_)
                return "RHI-ORIGINAL-BUFFER-STATE-GENERATION";
            if (!control_ || !control_->active())
                return "RHI-ORIGINAL-BUFFER-STATE-INACTIVE";
            if (!semantics::isBufferResourceState(control_->state()))
                return "RHI-ORIGINAL-BUFFER-STATE-VALUE";
            if (control_->pending())
                return "RHI-ORIGINAL-BUFFER-STATE-PENDING";
            return control_->matches(observed_) ? nullptr : "RHI-ORIGINAL-BUFFER-STATE-STALE";
        }

    private:
        CommittedStateObservation(OriginalResourceRegistrationTicket registration,
            const common::RecordedResourceStateControl* control,
            common::RecordedResourceStateControl::Observation observed) noexcept
            : registration_(std::move(registration)), control_(control), observed_(observed),
              generation_(registration_.generation())
        {}

        const OriginalResourceRegistrationTicket registration_;
        const common::RecordedResourceStateControl* const control_;
        const common::RecordedResourceStateControl::Observation observed_;
        const std::uint64_t generation_;
    };

    virtual ~GpuBuffer() = default;
    virtual GpuResourceId id() const = 0;
    virtual const BufferDesc& desc() const = 0;
    virtual const ResourceAllocation& allocation() const = 0;
    virtual ResourceState state() const = 0;

    bool originalPoolAllocationCharged() const noexcept
    {
        return originalAllocationLifetime_ && originalAllocationLifetime_->hasPoolCharge();
    }

    OriginalResourceRegistrationTicket originalResourceRegistration() const noexcept
    {
        return originalRegistration_.ticket();
    }

    const char* originalCommittedStateDiagnostic() const noexcept
    {
        const auto registration = originalResourceRegistration();
        if (const auto* diagnostic = registration.diagnostic())
            return diagnostic;
        if (!registration.matchesIdentity(this, id().value, OriginalRegisteredResourceKind::Buffer))
            return "RHI-ORIGINAL-BUFFER-STATE-REGISTRATION";
        if (originalStateBindingInvalid_)
            return "RHI-ORIGINAL-BUFFER-STATE-CONTROL-BINDING";
        if (!originalStateControl_)
            return "RHI-ORIGINAL-BUFFER-STATE-SOURCE-MISSING";
        if (!originalStateControl_->active())
            return "RHI-ORIGINAL-BUFFER-STATE-INACTIVE";
        if (!semantics::isBufferResourceState(originalStateControl_->state()))
            return "RHI-ORIGINAL-BUFFER-STATE-VALUE";
        return originalStateControl_->pending() ? "RHI-ORIGINAL-BUFFER-STATE-PENDING" : nullptr;
    }

    std::optional<CommittedStateObservation> observeOriginalCommittedState() const noexcept
    {
        if (originalCommittedStateDiagnostic())
            return {};
        const auto state = originalStateControl_->observe();
        return state ? std::optional<CommittedStateObservation>(CommittedStateObservation(
                           originalResourceRegistration(), originalStateControl_, *state))
                     : std::nullopt;
    }

    const char* validateOriginalCommittedState(
        const CommittedStateObservation& observation) const noexcept
    {
        if (const auto* diagnostic = originalCommittedStateDiagnostic())
            return diagnostic;
        if (observation.control_ != originalStateControl_
            || !observation.registration_.matchesIdentity(
                this, id().value, OriginalRegisteredResourceKind::Buffer))
            return "RHI-ORIGINAL-BUFFER-STATE-SOURCE-MISMATCH";
        return observation.diagnostic();
    }

    bool hostWritesSealed() const noexcept
    {
        return hostWritesSealed_.load(std::memory_order_acquire);
    }

    bool gpuWritesSealed() const noexcept { return gpuWriteSeal_->writableDescriptorSealed(); }

    bool gpuTransferWritesSealed() const noexcept { return gpuWriteSeal_->transferWritesSealed(); }

    std::weak_ptr<const GpuBufferWriteSealState> gpuWriteSealToken() const noexcept
    {
        return gpuWriteSeal_;
    }

    // Monotonic ownership restriction across all backends. This grants no
    // privileged writer and cannot undo a snapshot/packet's existing seals.
    void sealWrites() noexcept
    {
        sealHostWrites();
        sealGpuWrites();
    }

    // HostBufferWriteSeal/1: blocks host uploads only, monotonically. Shader
    // and transfer execution still require their own existing authorization.
    void sealHostWrites() noexcept { hostWritesSealed_.store(true, std::memory_order_release); }

protected:
    void bindOriginalAllocationLifetime(OriginalAllocationLifetime& lifetime) noexcept
    {
        originalAllocationLifetime_ = originalAllocationLifetimeBound_ ? nullptr : &lifetime;
        originalAllocationLifetimeBound_ = true;
    }

    // Backend creation binds its one genuine original logical control. The
    // pointer remains private to L2 observations; no mutable/native state leaks.
    void bindOriginalRecordedState(common::RecordedResourceStateControl& control) noexcept
    {
        if (originalStateControl_ || originalStateBindingInvalid_) {
            originalStateControl_ = nullptr;
            originalStateBindingInvalid_ = true;
            return;
        }
        originalStateControl_ = &control;
    }

    template <class Source> void bindOriginalRecordedStateSource(Source& source) noexcept
    {
        bindOriginalRecordedState(source.originalRecordedStateForBinding());
    }

private:
    friend class GpuDevice;
    friend class OriginalCommandStateTransaction;
    OriginalResourceRegistrationBinding originalRegistration_;
    friend class OriginalSubmittedBufferReadPins;
    OriginalAllocationLifetime* originalAllocationLifetime_ = nullptr;
    bool originalAllocationLifetimeBound_ = false;
    common::RecordedResourceStateControl* originalStateControl_ = nullptr;
    bool originalStateBindingInvalid_ = false;
    friend class BindlessSnapshot;
    friend class BindlessIndirectPacketPool;
    friend class BindlessOwnedFaultDomain;

    void sealGpuWrites() noexcept { gpuWriteSeal_->sealAllGpuWrites(); }

    void sealGpuTransferWrites() noexcept { gpuWriteSeal_->sealTransferWrites(); }

    std::atomic<bool> hostWritesSealed_{false};
    std::shared_ptr<GpuBufferWriteSealState> gpuWriteSeal_ =
        core::makeShared<GpuBufferWriteSealState>();
};

class ARP_API GpuTexture : public core::SharedObjectIdentity {
public:
    class ContentExpectation final {
        friend class GpuTexture;
        friend class OriginalCommandStateTransaction;

        ContentExpectation(OriginalResourceRegistrationTicket registration,
            const common::OriginalTextureContentControl* control,
            common::OriginalTextureContentControl::Observation value) noexcept
            : registration_(std::move(registration)), control_(control), value_(value)
        {}

    public:
        ContentExpectation(const ContentExpectation&) = default;

        auto state() const noexcept { return value_.value.state; }

        std::uint64_t contentGeneration() const noexcept { return value_.value.generation; }

        std::uint32_t controlVersion() const noexcept
        {
            return !registration_.diagnostic() && control_ ? control_->contractVersion() : 0u;
        }

        std::uint64_t revision() const noexcept { return value_.revision; }

        std::uint64_t resourceGeneration() const noexcept { return registration_.generation(); }

        bool nativeExecutionProven() const noexcept { return false; }

        const char* diagnostic() const noexcept
        {
            if (const auto* id = registration_.diagnostic())
                return id;
            if (!control_)
                return "RHI-CANONICAL-CONTENT-EXPECTATION-UNISSUED";
            if (const auto* id = control_->diagnostic())
                return id;
            return control_->value_.matches(value_) ? nullptr
                                                    : "RHI-CANONICAL-CONTENT-EXPECTATION-STALE";
        }

    private:
        const OriginalResourceRegistrationTicket registration_;
        const common::OriginalTextureContentControl* const control_;
        const common::OriginalTextureContentControl::Observation value_;
    };

    const char* originalContentDiagnostic() const noexcept
    {
        const auto registration = originalResourceRegistration();
        if (const auto* id = registration.diagnostic())
            return id;
        if (!registration.matchesIdentity(
                this, id().value, OriginalRegisteredResourceKind::Texture))
            return "RHI-CANONICAL-CONTENT-RESOURCE-REGISTRATION";
        return originalContent_ ? originalContent_->diagnostic()
                                : "RHI-CANONICAL-CONTENT-EXPECTATION-UNISSUED";
    }

    std::optional<ContentExpectation> observeOriginalContent() const noexcept
    {
        if (originalContentDiagnostic()
            || (originalContent_->contractVersion() != 1u
                && originalContent_->contractVersion() != 3u))
            return {};
        const auto value = originalContent_->value_.observe();
        return value ? std::optional<ContentExpectation>{ContentExpectation{
                           originalResourceRegistration(), originalContent_.get(), *value}}
                     : std::nullopt;
    }

    const char* validateOriginalContent(const ContentExpectation& expectation) const noexcept
    {
        if (const auto* id = originalContentDiagnostic())
            return id;
        if (expectation.control_ != originalContent_.get()
            || !expectation.registration_.matchesIdentity(
                this, id().value, OriginalRegisteredResourceKind::Texture))
            return "RHI-CANONICAL-CONTENT-EXPECTATION-FOREIGN";
        return expectation.diagnostic();
    }

    bool originalContentExclusive() const noexcept { return bool(originalContent_); }

    // ContentCreation2 observations prove original CPU metadata only. They do
    // not expose mutation or completion authority to resource consumers.
    class ContentRangeExpectation final {
        friend class GpuTexture;

        ContentRangeExpectation(OriginalResourceRegistrationTicket registration,
            const common::OriginalTextureContentControl* control,
            common::OriginalTextureContentControl::RangeObservation value) noexcept
            : registration_(std::move(registration)), control_(control), value_(value)
        {}

    public:
        ContentRangeExpectation(const ContentRangeExpectation&) = default;

        const auto& range() const noexcept { return value_.range; }

        auto state() const noexcept { return value_.value.state; }

        std::uint64_t contentGeneration() const noexcept { return value_.value.generation; }

        std::uint32_t controlVersion() const noexcept
        {
            return !registration_.diagnostic() && control_ ? control_->contractVersion() : 0u;
        }

        std::uint64_t revision() const noexcept { return value_.stamp.revision; }

        std::uint64_t resourceGeneration() const noexcept { return registration_.generation(); }

        bool nativeExecutionProven() const noexcept { return false; }

        const char* diagnostic() const noexcept
        {
            if (const auto* id = registration_.diagnostic())
                return id;
            if (!control_
                || !OriginalTextureContentCreationContract::isCellCreation(
                    control_->contractVersion()))
                return "RHI-CANONICAL-CONTENT-RANGE-UNISSUED";
            if (const auto* id = control_->diagnostic())
                return id;
            return control_->matchesRange(value_) ? nullptr : "RHI-CANONICAL-CONTENT-RANGE-STALE";
        }

    private:
        const OriginalResourceRegistrationTicket registration_;
        const common::OriginalTextureContentControl* const control_;
        const common::OriginalTextureContentControl::RangeObservation value_;
    };

    std::optional<ContentRangeExpectation> observeOriginalContent(
        const TextureSubresourceRange& range) const noexcept
    {
        if (originalContentDiagnostic()
            || !OriginalTextureContentCreationContract::isCellCreation(
                originalContent_->contractVersion()))
            return {};
        const auto value = originalContent_->observeRange(range);
        return value ? std::optional<ContentRangeExpectation>{{originalResourceRegistration(),
                           originalContent_.get(),
                           *value}}
                     : std::nullopt;
    }

    const char* validateOriginalContent(const ContentRangeExpectation& expectation) const noexcept
    {
        if (const auto* id = originalContentDiagnostic())
            return id;
        if (expectation.control_ != originalContent_.get()
            || !expectation.registration_.matchesIdentity(
                this, id().value, OriginalRegisteredResourceKind::Texture))
            return "RHI-CANONICAL-CONTENT-RANGE-FOREIGN";
        return expectation.diagnostic();
    }

    // Descriptor metadata can retain an immutable range identity across a
    // proposal/rollback. Do not use this in place of content validation when
    // reading, initializing, proposing or completing any resource contents.
    const char* validateOriginalContentIdentity(
        const ContentRangeExpectation& expectation) const noexcept
    {
        if (const auto* diagnostic = expectation.registration_.diagnostic())
            return diagnostic;
        if (!originalContent_ || expectation.control_ != originalContent_.get()
            || !expectation.registration_.matchesIdentity(
                this, id().value, OriginalRegisteredResourceKind::Texture))
            return "RHI-CANONICAL-CONTENT-RANGE-FOREIGN";
        if (const auto* diagnostic = originalContent_->identityDiagnostic())
            return diagnostic;
        return originalContent_->geometry_
            ? originalContent_->geometry_->validate(expectation.range())
            : "RHI-CANONICAL-CONTENT-RANGE-UNISSUED";
    }

    std::shared_ptr<core::CpuMemoryQuota> originalContentCpuQuota() const noexcept
    {
        return originalContent_ ? originalContent_->quota_
                                : std::shared_ptr<core::CpuMemoryQuota>{};
    }

    class CommittedStateObservation final {
        friend class GpuTexture;
        friend class OriginalCommandStateTransaction;

    public:
        CommittedStateObservation(const CommittedStateObservation&) = default;

        const TextureSubresourceRange& range() const noexcept { return range_; }

        ResourceState state() const noexcept { return state_; }

        std::uint64_t revision() const noexcept { return revision_; }

        std::uint64_t resourceGeneration() const noexcept { return generation_; }

        bool nativeExecutionProven() const noexcept { return false; }

        const char* diagnostic() const noexcept
        {
            if (const auto* id = registration_.diagnostic())
                return id;
            if (registration_.generation() != generation_)
                return "RHI-ORIGINAL-TEXTURE-STATE-GENERATION";
            if (normalized_)
                return normalized_->validate({range_, state_, revision_});
            if (!uniform_ || !uniform_->active())
                return "RHI-ORIGINAL-TEXTURE-STATE-INACTIVE";
            if (uniform_->pending())
                return "RHI-ORIGINAL-TEXTURE-STATE-PENDING";
            return uniform_->matches({state_, revision_}) ? nullptr
                                                          : "RHI-ORIGINAL-TEXTURE-STATE-STALE";
        }

    private:
        CommittedStateObservation(OriginalResourceRegistrationTicket registration,
            const common::RecordedTextureStateControl* normalized,
            const common::RecordedResourceStateControl* uniform,
            TextureSubresourceRange range,
            ResourceState state,
            std::uint64_t revision) noexcept
            : registration_(std::move(registration)), normalized_(normalized), uniform_(uniform),
              range_(range), state_(state), revision_(revision),
              generation_(registration_.generation())
        {}

        const OriginalResourceRegistrationTicket registration_;
        const common::RecordedTextureStateControl* const normalized_;
        const common::RecordedResourceStateControl* const uniform_;
        const TextureSubresourceRange range_;
        const ResourceState state_;
        const std::uint64_t revision_, generation_;
    };

    virtual ~GpuTexture() = default;
    virtual GpuResourceId id() const = 0;
    virtual const TextureDesc& desc() const = 0;
    virtual const ResourceAllocation& allocation() const = 0;
    virtual ResourceState state() const = 0;
    virtual ResourceState subresourceState(const TextureSubresourceRange& range) const = 0;

    bool originalPoolAllocationCharged() const noexcept
    {
        return originalAllocationLifetime_ && originalAllocationLifetime_->hasPoolCharge();
    }

    OriginalResourceRegistrationTicket originalResourceRegistration() const noexcept
    {
        return originalRegistration_.ticket();
    }

    const char* originalCommittedStateDiagnostic(
        const TextureSubresourceRange& range) const noexcept
    {
        const auto registration = originalResourceRegistration();
        if (const auto* id = registration.diagnostic())
            return id;
        if (!registration.matchesIdentity(
                this, id().value, OriginalRegisteredResourceKind::Texture))
            return "RHI-ORIGINAL-TEXTURE-STATE-REGISTRATION";
        if (originalStateBindingInvalid_)
            return "RHI-ORIGINAL-TEXTURE-STATE-CONTROL-BINDING";
        if (originalNormalizedState_) {
            if (!originalNormalizedState_->geometry().matches(desc()))
                return "RHI-ORIGINAL-TEXTURE-STATE-LAYOUT";
            return originalNormalizedState_->committedDiagnostic(range);
        }
        if (!originalUniformState_ || !originalUniformGeometry_)
            return "RHI-ORIGINAL-TEXTURE-STATE-SOURCE-MISSING";
        if (!originalUniformGeometry_->matches(desc()))
            return "RHI-ORIGINAL-TEXTURE-STATE-LAYOUT";
        if (const auto* id = originalUniformGeometry_->validate(range))
            return id;
        if (desc().format == TextureFormat::Depth24Stencil8)
            return "RHI-ORIGINAL-TEXTURE-STATE-ASPECT-CONTROL-UNPROVEN";
        if (range != defaultTextureSubresourceRange(desc()))
            return "RHI-ORIGINAL-TEXTURE-STATE-PARTIAL-CONTROL-UNPROVEN";
        if (!originalUniformState_->active())
            return "RHI-ORIGINAL-TEXTURE-STATE-INACTIVE";
        if (!semantics::isTextureResourceState(desc().format, originalUniformState_->state()))
            return "RHI-RESOURCE-STATE-INVALID";
        return originalUniformState_->pending() ? "RHI-ORIGINAL-TEXTURE-STATE-PENDING" : nullptr;
    }

    std::optional<CommittedStateObservation> observeOriginalCommittedState(
        const TextureSubresourceRange& range) const noexcept
    {
        if (originalCommittedStateDiagnostic(range))
            return {};
        if (originalNormalizedState_) {
            const auto observation = originalNormalizedState_->observe(range);
            if (!observation)
                return {};
            return CommittedStateObservation(originalResourceRegistration(),
                originalNormalizedState_,
                nullptr,
                range,
                observation->state,
                observation->revision);
        }
        const auto observation = originalUniformState_->observe();
        if (!observation)
            return {};
        return CommittedStateObservation(originalResourceRegistration(),
            nullptr,
            originalUniformState_,
            range,
            observation->value,
            observation->revision);
    }

    const char* validateOriginalCommittedState(
        const CommittedStateObservation& observation) const noexcept
    {
        if (const auto* id = originalCommittedStateDiagnostic(observation.range_))
            return id;
        if (observation.normalized_ != originalNormalizedState_
            || observation.uniform_ != originalUniformState_
            || !observation.registration_.matchesIdentity(
                this, id().value, OriginalRegisteredResourceKind::Texture))
            return "RHI-ORIGINAL-TEXTURE-STATE-SOURCE-MISMATCH";
        return observation.diagnostic();
    }

protected:
    void bindOriginalAllocationLifetime(OriginalAllocationLifetime& lifetime) noexcept
    {
        originalAllocationLifetime_ = originalAllocationLifetimeBound_ ? nullptr : &lifetime;
        originalAllocationLifetimeBound_ = true;
    }

    void bindOriginalRecordedState(common::RecordedTextureStateControl& control) noexcept
    {
        if (originalNormalizedState_ || originalUniformState_ || originalStateBindingInvalid_) {
            originalNormalizedState_ = nullptr;
            originalUniformState_ = nullptr;
            originalStateBindingInvalid_ = true;
            return;
        }
        originalNormalizedState_ = &control;
    }

    void bindOriginalRecordedState(common::RecordedResourceStateControl& control) noexcept
    {
        if (originalNormalizedState_ || originalUniformState_ || originalStateBindingInvalid_) {
            originalNormalizedState_ = nullptr;
            originalUniformState_ = nullptr;
            originalStateBindingInvalid_ = true;
            return;
        }
        const char* diagnostic = nullptr;
        const auto geometry =
            semantics::OriginalTextureStateRange::capture(desc(), UINT64_MAX, diagnostic);
        if (!geometry) {
            originalStateBindingInvalid_ = true;
            return;
        }
        originalUniformGeometry_.emplace(*geometry);
        originalUniformState_ = &control;
    }

    template <class Source> void bindOriginalRecordedStateSource(Source& source) noexcept
    {
        bindOriginalRecordedState(source.originalRecordedStateForBinding());
    }

private:
    friend class GpuDevice;
    friend class OriginalCommandStateTransaction;
    OriginalResourceRegistrationBinding originalRegistration_;
    OriginalAllocationLifetime* originalAllocationLifetime_ = nullptr;
    bool originalAllocationLifetimeBound_ = false;
    core::UniquePtr<common::OriginalTextureContentControl> originalContent_;
    common::RecordedTextureStateControl* originalNormalizedState_ = nullptr;
    common::RecordedResourceStateControl* originalUniformState_ = nullptr;
    std::optional<semantics::OriginalTextureStateRange> originalUniformGeometry_;
    bool originalStateBindingInvalid_ = false;
};

class ARP_API GpuBufferView {
public:
    virtual ~GpuBufferView() = default;
    virtual GpuResourceId id() const = 0;
    virtual GpuResourceId bufferId() const = 0;
    virtual const BufferViewDesc& desc() const = 0;
};

class ARP_API GpuTextureView {
public:
    virtual ~GpuTextureView() = default;
    virtual GpuResourceId id() const = 0;
    virtual GpuResourceId textureId() const = 0;
    virtual const TextureViewDesc& desc() const = 0;

    OriginalResourceRegistrationTicket originalResourceRegistration() const noexcept
    {
        return originalRegistration_.ticket();
    }

private:
    friend class GpuDevice;
    OriginalResourceRegistrationBinding originalRegistration_;
};

class ARP_API GpuSampler {
public:
    GpuSampler() = default;
    GpuSampler(const GpuSampler&) = delete;
    GpuSampler& operator=(const GpuSampler&) = delete;
    virtual ~GpuSampler() = default;
    virtual GpuResourceId id() const = 0;
    virtual const SamplerDesc& desc() const = 0;

    OriginalResourceRegistrationTicket originalResourceRegistration() const noexcept
    {
        return originalRegistration_.ticket();
    }

private:
    friend class GpuDevice;
    OriginalResourceRegistrationBinding originalRegistration_;
};

template <class T> struct OriginalRegisteredResourceTraits {
    using Base = std::conditional_t<std::is_base_of_v<GpuMemoryHeap, T>,
        GpuMemoryHeap,
        std::conditional_t<std::is_base_of_v<GpuBuffer, T>,
            GpuBuffer,
            std::conditional_t<std::is_base_of_v<GpuTexture, T>,
                GpuTexture,
                std::conditional_t<std::is_base_of_v<GpuTextureView, T>,
                    GpuTextureView,
                    GpuSampler>>>>;
    static_assert(std::is_base_of_v<Base, T>);
    static constexpr auto kind = std::is_same_v<Base, GpuMemoryHeap>
        ? OriginalRegisteredResourceKind::MemoryHeap
        : std::is_same_v<Base, GpuBuffer>      ? OriginalRegisteredResourceKind::Buffer
        : std::is_same_v<Base, GpuTexture>     ? OriginalRegisteredResourceKind::Texture
        : std::is_same_v<Base, GpuTextureView> ? OriginalRegisteredResourceKind::TextureView
                                               : OriginalRegisteredResourceKind::Sampler;
};

} // namespace arp::rhi
