#include "audio/audio_components.h"

#include <array>
#include <cassert>

int main()
{
    arp::audio::AudioSystem system({2u, false});
    const std::array<float, 2> samples{0.5f, -0.5f};
    arp::audio::AudioSourceComponent source({&system, {samples.data(), 1u, 48000u, 2u}});
    arp::scene::SceneUpdateContext context;
    source.onEnable(context);
    assert(source.playing());
    std::array<float, 2> output{};
    system.render(output.data(), 1u);
    assert(output[0] == 0.5f && output[1] == -0.5f);
    source.onDisable(context);
    system.render(output.data(), 1u);
    assert(system.telemetry().activeVoices == 0u);
}
