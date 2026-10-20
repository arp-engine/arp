#include "audio/audio_system.h"

#include <array>
#include <cassert>

int main()
{
    arp::audio::AudioSystem system({4u, false});
    const std::array<float, 4> samples{0.25f, -0.5f, 0.5f, -0.25f};
    const auto voice = system.play(arp::audio::AudioClipView{samples.data(), 2u, 48000u, 2u}, 0.5f);
    assert(voice.valid());
    std::array<float, 4> output{};
    system.render(output.data(), 2u);
    assert(
        output[0] == 0.125f && output[1] == -0.25f && output[2] == 0.25f && output[3] == -0.125f);
    system.render(output.data(), 1u);
    assert(system.telemetry().activeVoices == 0u);
    assert(system.stop({}) == arp::audio::AudioStatus::InvalidConfiguration);
    assert(!system.play(arp::audio::AudioClipView{samples.data(), 2u, 44100u, 2u}).valid());
    const std::array<float, 2> mono{1.0f, 1.0f};
    arp::audio::AudioPlayRequest request{
        {mono.data(), 2u, 48000u, 2u}, 1.0f, false, arp::audio::AudioBus::Music};
    assert(
        system.setBusVolume(arp::audio::AudioBus::Music, 0.5f) == arp::audio::AudioStatus::Ready);
    assert(system.play(request).valid());
    system.render(output.data(), 1u);
    assert(output[0] == 0.5f && output[1] == 0.5f);
    system.suspend();
    system.render(output.data(), 1u);
    assert(output[0] == 0.0f && output[1] == 0.0f);
    system.resume();
    const std::array<float, 8> looped{0.1f, 0.1f, 0.2f, 0.2f, 0.3f, 0.3f, 0.4f, 0.4f};
    const arp::audio::AudioClipView loopedClip{looped.data(), 4u, 48000u, 2u, true, 1u, 3u};
    assert(system.play(loopedClip).valid());
    std::array<float, 10> loopOutput{};
    system.render(loopOutput.data(), 5u);
    assert(loopOutput[0] == 0.1f && loopOutput[2] == 0.2f && loopOutput[4] == 0.3f
        && loopOutput[6] == 0.2f && loopOutput[8] == 0.3f);
}
