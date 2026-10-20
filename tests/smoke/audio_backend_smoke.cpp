#include "audio/audio_system.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

bool waitForCallback(arp::audio::AudioSystem& system)
{
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (system.telemetry().renderedFrames > 0u) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

} // namespace

int main()
{
    constexpr std::array<float, 2> silence{0.0f, 0.0f};
    arp::audio::AudioSystem system({2u, true});
    if (!system.telemetry().deviceRunning) {
        std::fputs("audio backend smoke failed: output device did not start\n", stderr);
        return 1;
    }
    if (!system.play({silence.data(), 1u, 48000u, 2u}, 0.0f, true).valid()
        || !waitForCallback(system)) {
        std::fputs("audio backend smoke failed: device callback did not advance\n", stderr);
        return 1;
    }

    system.stopDevice();
    if (system.telemetry().deviceRunning || !system.startDevice() || !waitForCallback(system)) {
        std::fputs("audio backend smoke failed: device stop/restart contract failed\n", stderr);
        return 1;
    }
    return 0;
}
