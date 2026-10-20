# Audio v1 Design And Acceptance

## Status

Current status: **Backend Validated (Windows)**.

The Windows configuration has been compiled, exercised through deterministic
offline tests, and passed the explicit miniaudio device smoke. That does not
constitute **Platform Validated** or **Production Ready**. Those labels require
the evidence listed in the acceptance matrix below.

## Runtime Contract

- `arp_audio` owns the backend-neutral mixer and private miniaudio device
  backend. SDL remains unrelated to audio output.
- Audio v1 output is fixed at 48,000 Hz, interleaved stereo `float` PCM.
- The callback consumes a fixed-capacity command ring and voice pool. It does
  not allocate, lock, read files, or decode source codecs.
- Buses are `Master`, `Music`, `Sfx`, and `Ui`. Voices expose volume, loop,
  listener position, and basic spatial attenuation/panning.
- `AudioSourceComponent` and `AudioListenerComponent` provide scene-facing
  control. `AudioCuePresenter` maps stable numeric gameplay cue ids to play
  requests.
- `applyAudioLifecycle` maps pause/stop/background to suspend, and
  start/resume/foreground to resume. Native platform event dispatch must call
  this bridge for the platform integration to be considered validated.

## Asset Contract

- Authoring import uses miniaudio once, decodes and resamples to the runtime
  PCM contract, then persists `.arpaudio`.
- `.arpaudio` is `ARPAUD01`, version 1: fixed 64-byte little-endian header,
  48 kHz stereo float PCM payload, optional loop interval, and deterministic
  payload hash.
- The reader rejects a wrong magic/version/header size, non-stereo/non-48 kHz
  contract, malformed loop bounds, byte-budget overrun, inconsistent payload
  size, and hash mismatch.
- `AudioClipResource` loads package-owned PCM through `ResourceManager` and
  preserves the package loop interval. Playback reaches the loop end and then
  returns to its declared loop start; clips without loop metadata retain
  whole-clip loop behavior when the caller requests looping.
- `AudioPlaybackRegistry` observes resource lifetime. Unload first queues a
  stop and returns a deterministic deferred result; it succeeds only after
  audio callback completion is observed.

## Acceptance Matrix

| Requirement | State | Required authoritative evidence |
| --- | --- | --- |
| PCM mixer, buses, spatialization, cue/component contracts | Contract Complete | `arp_audio_system_tests`, `arp_audio_component_tests`, `arp_audio_cue_lifecycle_tests` |
| Import, package validation, clip loading and unload safety | Contract Complete | `arp_audio_importer_tests`, `arp_audio_package_tests`, `arp_audio_clip_resource_tests`, `arp_audio_resource_lifecycle_tests` |
| Windows miniaudio device output | Backend Validated (Windows) | `arp_audio_backend_smoke`: `startDevice`, real callback frame progress, stop and restart all pass on this Windows target |
| Android AAudio/OpenSL ES lifecycle and audio focus | Not Platform Validated | Android build plus device logs for focus loss/gain, pause/resume, background/foreground and device recreation |
| iOS CoreAudio lifecycle/interruption | Not Platform Validated | iOS build plus device logs for interruption, route change, background/foreground and restart |
| Web Audio unlock and lifecycle | Not Platform Validated | Emscripten browser smoke proving user-gesture unlock, suspend/resume and tab lifecycle behavior |
| Streaming music package | Deferred, not part of `audio.clip/v1` | Separate versioned `audio.stream/v1` contract with bounded decode/ring-buffer and platform evidence |
| Production release | Not Production Ready | All backend/platform rows above, budget/telemetry evidence and target-device smoke artifacts |

## Reproducible Local Verification

```powershell
python build.py configure --platform win --backend vulkan --config debug
cmake --build build/win-vulkan-debug --target arp_audio_system_tests arp_audio_clip_resource_tests arp_audio_component_tests arp_audio_importer_tests arp_audio_cue_lifecycle_tests arp_audio_package_tests arp_audio_resource_lifecycle_tests arp_engine -j 4
ctest --test-dir build/win-vulkan-debug -R '^arp_audio_.*_tests$' --output-on-failure
cmake --build build/win-vulkan-debug --target arp_audio_backend_smoke -j 4
.\build\win-vulkan-debug\arp_audio_backend_smoke.exe
```

The CMake files are generated artifacts. Audio targets and dependencies are
declared in `build.py` and `scripts/cmake_gen.py`; regenerate them with the
configure command rather than editing `CMakeLists.txt` manually.
