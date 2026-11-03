#pragma once

#include "core/container/array_view.h"
#include "core/io/file_request.h"
#include "core/io/file_result.h"

#include <string_view>

namespace arp::build_a_queen {

// Prepared inputs for character-manifest/v1. IO belongs to the caller's
// FileService; this owner validates the declared bindings and retains bytes.
// Versions are per-file observations, not a cross-file atomic snapshot.
class CharacterSources final {
public:
    static constexpr std::string_view manifestFile = "character_manifest.yaml";
    static constexpr std::size_t characterCount = 2;
    static constexpr std::size_t maxManifestBytes = 64 * 1024;
    static constexpr std::size_t maxCharacterBytes = 2 * 1024 * 1024;

    CharacterSources();
    ~CharacterSources();
    CharacterSources(const CharacterSources&) = delete;
    CharacterSources& operator=(const CharacterSources&) = delete;

    bool setManifest(const core::io::FileReadRequest&, core::io::FileReadResult);
    bool addCharacter(const core::io::FileReadRequest&, core::io::FileReadResult);
    // All declared bytes are retained; SceneContent still validates the assets.
    bool ready() const noexcept;

    const char* diagnostic() const noexcept { return diagnostic_; }

    std::string_view manifestText() const noexcept;
    core::pmr::String characterPath(std::size_t index) const;
    core::ArrayView<const std::uint8_t> characterBytes(std::string_view name) const noexcept;
    const core::io::FileReadResult* manifestSource() const noexcept;
    const core::io::FileReadResult* characterSource(std::size_t index) const noexcept;

private:
    struct State;
    bool reject(const char* diagnostic) noexcept;
    core::UniquePtr<State> state_;
    const char* diagnostic_ = nullptr;
};

} // namespace arp::build_a_queen
