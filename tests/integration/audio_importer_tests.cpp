#include "audio/audio_importer.h"
#include "tests/fixtures/audio_wave.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "arp_audio_import_fixture.wav";
    {
        std::ofstream file(path, std::ios::binary);
        constexpr std::array<std::int16_t, 2> values{{16384, -16384}};
        const auto bytes = audio_fixture::Wave::pcm16(1, 48000, {values.data(), values.size()});
        file.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    const auto decoded = arp::audio::importAudioFile(path.string().c_str(), 1024u);
    assert(decoded.success() && decoded.channels == 2u && decoded.samples.size() == 4u);
    assert(!arp::audio::importAudioFile(path.string().c_str(), 1u).success());
    std::filesystem::remove(path);
    assert(!arp::audio::importAudioFile("missing.wav", 1024u).success());
}
