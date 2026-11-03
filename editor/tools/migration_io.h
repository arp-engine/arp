#pragma once

#include "core/io/file_save.h"
#include "core/io/file_text.h"

#include <filesystem>

namespace arp::editor {

// Offline converter policy. Schema conversion stays with the Project/Scene
// codecs; directory ownership/publication stays with core::io::StagedDirectory.
class EditorMigrationIo final {
public:
    static core::io::FileTextReadResult readSource(
        const std::filesystem::path& path, std::uint64_t maxBytes);
    static core::io::FileSaveResult writeNewFile(const std::filesystem::path& directory,
        core::StringView relativePath,
        core::StringView bytes,
        std::uint64_t maxBytes);
};

} // namespace arp::editor
