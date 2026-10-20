# miniaudio

- Version: `0.11.23`
- Source: `https://github.com/mackron/miniaudio/blob/0.11.23/miniaudio.h`
- SHA-256 (`miniaudio.h`): `7E4F3F13C8FE66DF2080AC3DD12A89193E3C2463CB7F067C798ABD7331CD8EE6`
- License: public domain or MIT-0; the complete license text is included in
  `miniaudio.h`.

`src/audio/miniaudio_backend.cpp` is the sole translation unit that defines
`MINIAUDIO_IMPLEMENTATION`. Audio decoding is disabled for Audio v1; clips are
supplied to the runtime as validated, pre-decoded PCM.
