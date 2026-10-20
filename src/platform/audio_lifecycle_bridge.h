#pragma once

#include "core/api.h"
#include "platform/platform_lifecycle_state.h"

namespace arp::audio {
class AudioSystem;
}

namespace arp::platform {

// Maps the platform lifecycle contract onto the backend-neutral audio system.
ARP_API void applyAudioLifecycle(audio::AudioSystem& audio, PlatformLifecycleEvent event) noexcept;

} // namespace arp::platform
