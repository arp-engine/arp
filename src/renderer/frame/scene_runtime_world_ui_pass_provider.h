#pragma once

#include "core/api.h"
#include "renderer/frame/render_pass_provider.h"

namespace arp::renderer {

inline constexpr const char* kSceneRuntimeWorldUiPassProviderId =
    "renderer/SceneRuntimeWorldUiPassProvider";
inline constexpr const char* kSceneRuntimeWorldUiPassProviderCapability =
    "scene-runtime/world-ui-one-pass";
inline constexpr const char* kSceneRuntimeWorldUiPassProviderPassId = "SceneRuntimeWorldUiPass";

class ARP_API SceneRuntimeWorldUiPassProvider final : public IRenderPassProvider {
public:
    const char* providerId() const override;
    RenderFrameStage stage() const override;
    const char* capabilityLabel() const override;
    bool supports(const RenderFrameIntent& intent) const override;
    RenderPassProviderResult provide(const RenderFrameIntent& intent) const override;
};

} // namespace arp::renderer
