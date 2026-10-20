#pragma once

#include "core/api.h"
#include "renderer/frame/render_frame_payload_adapter.h"

namespace arp::renderer {

class ARP_API SceneRuntimePayloadAdapter final : public IRenderFramePayloadAdapter {
public:
    RenderFramePayloadAdapterKey key() const override;
    RenderFramePayloadAdapterResult adapt(const CustomWorkloadPayloadRef& payload) const override;
};

} // namespace arp::renderer
