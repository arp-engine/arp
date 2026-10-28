#pragma once

#include "rhi/frontend/command_sources.h"
#include "rhi/semantics/ordinary_sampled_color.h"

namespace arp::rhi {

// Captured scalar observations borrow the existing view/parent owner closure.
// They grant no upload, state mutation, submission or completion authority.
class OriginalOrdinarySampledColorRead final {
public:
    struct Snapshot {
        std::optional<GpuTexture::ContentExpectation> content;
        std::optional<GpuTexture::CommittedStateObservation> state;
    };

    static const char* capture(
        const OriginalCommandTextureViewSource& view, Snapshot& output) noexcept
    {
        const auto& texture = *view.originalParentLease()->originalObject();
        if (const auto* id = semantics::OriginalOrdinarySampledColorContract::validateView(
                texture.desc(), view.originalObject()->desc()))
            return id;
        Snapshot next{texture.observeOriginalContent(),
            texture.observeOriginalCommittedState(defaultTextureSubresourceRange(texture.desc()))};
        if (const auto* id = validate(view, next))
            return id;
        output.content.emplace(*next.content);
        output.state.emplace(*next.state);
        return nullptr;
    }

    static const char* validate(
        const OriginalCommandTextureViewSource& view, const Snapshot& snapshot) noexcept
    {
        const auto& texture = *view.originalParentLease()->originalObject();
        if (!snapshot.content || !snapshot.state || snapshot.content->controlVersion() != 3u
            || snapshot.content->state() != common::OriginalTextureContentControl::State::Completed
            || !snapshot.content->contentGeneration() || !snapshot.content->revision()
            || snapshot.state->state() != ResourceState::ShaderRead)
            return "RHI-ORDINARY-SAMPLED-COLOR-NOT-COMPLETED";
        if (const auto* id = view.originalObject()->originalResourceRegistration().diagnostic())
            return id;
        if (const auto* id = texture.originalResourceRegistration().diagnostic())
            return id;
        if (const auto* id = texture.validateOriginalContent(*snapshot.content))
            return id;
        return texture.validateOriginalCommittedState(*snapshot.state);
    }
};

} // namespace arp::rhi
