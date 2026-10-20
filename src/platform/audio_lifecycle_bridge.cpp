#include "platform/audio_lifecycle_bridge.h"

#include "audio/audio_system.h"

namespace arp::platform {

void applyAudioLifecycle(audio::AudioSystem& audio, PlatformLifecycleEvent event) noexcept
{
    switch (event) {
    case PlatformLifecycleEvent::Pause:
    case PlatformLifecycleEvent::Stop:
    case PlatformLifecycleEvent::EnterBackground:
        audio.suspend();
        break;
    case PlatformLifecycleEvent::Resume:
    case PlatformLifecycleEvent::Start:
    case PlatformLifecycleEvent::EnterForeground:
        audio.resume();
        break;
    default:
        break;
    }
}

} // namespace arp::platform
