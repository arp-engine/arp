#include "audio/audio_cue_presenter.h"
#include "platform/audio_lifecycle_bridge.h"

#include <array>
#include <cassert>

int main()
{
    arp::audio::AudioSystem system({2u, false});
    arp::audio::AudioCuePresenter presenter(system);
    const std::array<float, 2> samples{0.5f, 0.25f};
    assert(presenter.registerCue(7u, {{samples.data(), 1u, 48000u, 2u}}));
    assert(presenter.present(7u).valid());
    std::array<float, 2> output{};
    arp::platform::applyAudioLifecycle(
        system, arp::platform::PlatformLifecycleEvent::EnterBackground);
    system.render(output.data(), 1u);
    assert(output[0] == 0.0f && output[1] == 0.0f);
    arp::platform::applyAudioLifecycle(
        system, arp::platform::PlatformLifecycleEvent::EnterForeground);
    system.render(output.data(), 1u);
    assert(output[0] == 0.5f && output[1] == 0.25f);
    assert(!presenter.present(99u).valid());
}
