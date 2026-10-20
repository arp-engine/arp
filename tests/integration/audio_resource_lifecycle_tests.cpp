#include "resource/audio_playback_registry.h"

#include <cassert>

int main()
{
    arp::core::pmr::Vector<float> samples{&arp::core::defaultCpuMemoryResource()};
    samples = {0.25f, -0.25f, 0.25f, -0.25f};
    constexpr const char* id = "a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11";
    arp::resource::ResourceManager manager;
    assert(manager.load<arp::resource::AudioClipResource>({}, id, std::move(samples), 2u).success);
    arp::audio::AudioSystem audio({2u, false});
    arp::resource::AudioPlaybackRegistry registry(manager, audio);
    assert(registry.play(manager.find<arp::resource::AudioClipResource>(id), {}).valid());
    assert(!manager.unload(id).success);
    float output[2]{};
    audio.render(output, 1u);
    assert(manager.unload(id).success);
}
