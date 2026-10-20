#include "resource/audio_clip_resource.h"

#include <cassert>
#include <filesystem>

int main()
{
    arp::core::pmr::Vector<float> samples{&arp::core::defaultCpuMemoryResource()};
    samples = {0.0f, 0.25f, -0.25f, 0.0f};
    arp::resource::ResourceManager manager;
    constexpr const char* clipId = "0f8fad5b-d9cb-469f-a165-70867728950e";
    auto result =
        manager.load<arp::resource::AudioClipResource>({}, clipId, std::move(samples), 2u);
    assert(result.success);
    const auto clip = manager.find<arp::resource::AudioClipResource>(clipId);
    assert(clip.ready() && clip.get()->view().valid() && clip.get()->view().frameCount == 2u);
    arp::core::pmr::Vector<float> invalid{&arp::core::defaultCpuMemoryResource()};
    invalid.push_back(0.0f);
    auto invalidResult = manager.load<arp::resource::AudioClipResource>(
        {}, "7c9e6679-7425-40de-944b-e07fc1f90ae7", std::move(invalid), 2u);
    assert(!invalidResult.success);

    const auto packagePath =
        std::filesystem::temp_directory_path() / "arp_audio_clip_resource.arpaudio";
    arp::resource::AudioPackageData package;
    package.samples = {0.1f, -0.1f, 0.2f, -0.2f};
    package.channels = 2u;
    package.playback = {true, 0u, 2u};
    assert(arp::resource::writeAudioPackage(packagePath, package).empty());
    arp::resource::ResourceManager packageManager;
    auto packageResult = packageManager.load<arp::resource::AudioClipResource>(
        {}, "de305d54-75b4-431b-adb2-eb6b9e546014", packagePath);
    std::filesystem::remove(packagePath);
    const auto packagedClip = packageManager.find<arp::resource::AudioClipResource>(
        "de305d54-75b4-431b-adb2-eb6b9e546014");
    assert(
        packageResult.success && packagedClip.ready() && packagedClip.get()->view().hasLoopPoints);
}
