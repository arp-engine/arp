#pragma once

#include "core/encoding/base64.h"
#include "core/io/staged_directory.h"
#include "editor/project/project_manifest.h"

namespace arp::editor {
// Editor-owned YAML archive. All paths are relative, all bytes are explicit
// base64; importing never overwrites an existing workspace or guesses a scene.
class BrowserWorkspace final {
public:
    static constexpr std::size_t maxFiles = 256;
    static constexpr std::size_t maxDecodedBytes = 32 * 1024 * 1024;
    static constexpr std::size_t maxArchiveBytes = 48 * 1024 * 1024;

    struct File {
        core::pmr::String path;
        core::ByteBuffer bytes;
    };

    struct Archive {
        core::pmr::String scene;
        core::pmr::Vector<File> files;
    };

    struct Staged {
        std::filesystem::path root;
        core::pmr::String scene;
        core::UniquePtr<core::io::StagedDirectory> directory;
        Staged() = default;
        Staged(const Staged&) = delete;
        Staged& operator=(const Staged&) = delete;
        Staged(Staged&&) noexcept = default;
        Staged& operator=(Staged&&) = delete;
        ~Staged() = default;
    };

    static Archive decode(core::StringView);
    static Staged stage(core::StringView, std::uint64_t serial);
    static core::pmr::String encode(const std::filesystem::path&, core::StringView scene);
};
} // namespace arp::editor
