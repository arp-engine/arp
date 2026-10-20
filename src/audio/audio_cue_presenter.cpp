#include "audio/audio_cue_presenter.h"

namespace arp::audio {

bool AudioCuePresenter::registerCue(std::uint32_t cueId, AudioPlayRequest request) noexcept
{
    if (cueId == 0u || !request.clip.valid())
        return false;
    for (auto& entry : entries_) {
        if (entry.cueId == cueId || entry.cueId == 0u) {
            entry = {cueId, request};
            return true;
        }
    }
    return false;
}

VoiceId AudioCuePresenter::present(std::uint32_t cueId) noexcept
{
    for (const auto& entry : entries_)
        if (entry.cueId == cueId)
            return system_.play(entry.request);
    return {};
}

} // namespace arp::audio
