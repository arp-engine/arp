#include "resource/audio_package.h"

#include <cassert>
#include <filesystem>
#include <fstream>

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "arp_audio_package_fixture.arpaudio";
    arp::resource::AudioPackageData package;
    package.samples = {0.25f, -0.25f, 0.5f, -0.5f};
    package.channels = 2u;
    package.playback = {true, 0u, 2u};
    assert(arp::resource::writeAudioPackage(path, package).empty());
    auto read = arp::resource::readAudioPackage(path, 1024u);
    assert(read.success && read.data.samples == package.samples && read.data.playback.loop);

    assert(!arp::resource::readAudioPackage(path, 1u).success);
    {
        std::fstream corrupt(path, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(0);
        corrupt.put('X');
    }
    assert(!arp::resource::readAudioPackage(path, 1024u).success);

    assert(arp::resource::writeAudioPackage(path, package).empty());
    {
        std::fstream corrupt(path, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(-1, std::ios::end);
        corrupt.put('\0');
    }
    assert(!arp::resource::readAudioPackage(path, 1024u).success);
    std::filesystem::remove(path);

    package.playback = {true, 2u, 2u};
    assert(!arp::resource::writeAudioPackage(path, package).empty());
}
