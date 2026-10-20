#include "audio/audio_importer.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace {
void writeU16(std::ofstream& file, std::uint16_t value)
{
    file.put(static_cast<char>(value & 0xFFu));
    file.put(static_cast<char>((value >> 8u) & 0xFFu));
}

void writeU32(std::ofstream& file, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        file.put(static_cast<char>((value >> (i * 8)) & 0xFFu));
}
} // namespace

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "arp_audio_import_fixture.wav";
    {
        std::ofstream file(path, std::ios::binary);
        file.write("RIFF", 4);
        writeU32(file, 40u);
        file.write("WAVEfmt ", 8);
        writeU32(file, 16u);
        writeU16(file, 1u);
        writeU16(file, 1u);
        writeU32(file, 48000u);
        writeU32(file, 96000u);
        writeU16(file, 2u);
        writeU16(file, 16u);
        file.write("data", 4);
        writeU32(file, 4u);
        writeU16(file, 16384u);
        writeU16(file, static_cast<std::uint16_t>(-16384));
    }
    const auto decoded = arp::audio::importAudioFile(path.string().c_str(), 1024u);
    assert(decoded.success() && decoded.channels == 2u && decoded.samples.size() == 4u);
    assert(!arp::audio::importAudioFile(path.string().c_str(), 1u).success());
    std::filesystem::remove(path);
    assert(!arp::audio::importAudioFile("missing.wav", 1024u).success());
}
