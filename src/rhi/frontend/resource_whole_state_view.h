#pragma once
#include "rhi/frontend/resource_state_control.h"
#include "rhi/frontend/resource_texture_state_control.h"

#include <variant>

namespace arp::rhi::common {
// Borrowed, read-only projection of a scalar buffer or a normalized whole
// texture. The resource owner must outlive this view; it owns no state/storage.
class RecordedWholeResourceStateView final {
public:
    struct Observation {
        ResourceState value;
        std::uint64_t revision;
    };

    class PendingObservation final {
        friend class RecordedWholeResourceStateView;
        using Stamp = std::variant<RecordedResourceStateControl::PendingObservation,
            RecordedTextureStateControl::PendingObservation>;

        PendingObservation(ResourceState value, std::uint64_t revision, Stamp stamp) noexcept
            : value_(value), revision_(revision), stamp_(std::move(stamp))
        {}

    public:
        ResourceState value() const noexcept { return value_; }

        std::uint64_t revision() const noexcept { return revision_; }

    private:
        ResourceState value_;
        std::uint64_t revision_;
        Stamp stamp_;
    };

    RecordedWholeResourceStateView() = default;

    explicit RecordedWholeResourceStateView(const RecordedResourceStateControl& scalar) noexcept
        : scalar_(&scalar)
    {}

    explicit RecordedWholeResourceStateView(const RecordedTextureStateControl& texture) noexcept
        : texture_(&texture)
    {}

    explicit operator bool() const noexcept { return scalar_ || texture_; }

    const void* identity() const noexcept
    {
        return scalar_ ? static_cast<const void*>(scalar_) : texture_;
    }

    const auto* scalarControl() const noexcept { return scalar_; }

    const auto* textureControl() const noexcept { return texture_; }

    bool operator==(const RecordedWholeResourceStateView& rhs) const noexcept
    {
        return scalar_ == rhs.scalar_ && texture_ == rhs.texture_;
    }

    bool operator!=(const RecordedWholeResourceStateView& rhs) const noexcept
    {
        return !(*this == rhs);
    }

    std::optional<ResourceState> uniformState() const noexcept
    {
        return scalar_ ? std::optional<ResourceState>{scalar_->state()}
            : texture_ ? texture_->uniformWholeState()
                       : std::nullopt;
    }

    // Invalid enum is an explicit failed query for legacy value-returning
    // consumers; every observation/execution gate rejects it as unknown.
    ResourceState state() const noexcept
    {
        return uniformState().value_or(static_cast<ResourceState>(UINT32_MAX));
    }

    bool active() const noexcept
    {
        return scalar_ ? scalar_->active() : texture_ && texture_->active();
    }

    bool pending() const noexcept
    {
        return scalar_ ? scalar_->pending() : texture_ && texture_->pending();
    }

    std::uint64_t revision() const noexcept
    {
        return scalar_ ? scalar_->revision() : texture_ ? texture_->revision() : 0u;
    }

    std::optional<Observation> observe() const noexcept
    {
        if (!active() || pending())
            return {};
        const auto value = uniformState();
        return value && semantics::isKnownResourceState(*value)
            ? std::optional<Observation>{{*value, revision()}}
            : std::nullopt;
    }

    bool matches(const Observation& observation) const noexcept
    {
        const auto now = observe();
        return now && now->value == observation.value && now->revision == observation.revision;
    }

    std::optional<PendingObservation> observePending(
        const core::MutationJournal& journal) const noexcept
    {
        if (!active())
            return {};
        const auto value = uniformState();
        if (!value || !semantics::isKnownResourceState(*value))
            return {};
        if (scalar_) {
            const auto stamp = scalar_->observePending(journal);
            return stamp ? std::optional<PendingObservation>{PendingObservation{
                               *value, stamp->revision(), *stamp}}
                         : std::nullopt;
        }
        const auto stamp = texture_->observePending(texture_->wholePrimaryAspectRange(), journal);
        return stamp ? std::optional<PendingObservation>{PendingObservation{
                           *value, stamp->revision(), *stamp}}
                     : std::nullopt;
    }

    bool matchesPending(
        const PendingObservation& observation, const core::MutationJournal& journal) const noexcept
    {
        if (!active() || uniformState() != observation.value_)
            return false;
        if (scalar_) {
            const auto* stamp =
                std::get_if<RecordedResourceStateControl::PendingObservation>(&observation.stamp_);
            return stamp && scalar_->matchesPending(*stamp, journal);
        }
        const auto* stamp =
            std::get_if<RecordedTextureStateControl::PendingObservation>(&observation.stamp_);
        return stamp && texture_->matchesPending(*stamp, journal);
    }

private:
    const RecordedResourceStateControl* scalar_ = nullptr;
    const RecordedTextureStateControl* texture_ = nullptr;
};
} // namespace arp::rhi::common
