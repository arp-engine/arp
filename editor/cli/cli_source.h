#pragma once

#include "core/io/file_result.h"

#include <filesystem>

namespace arp::editor {

// Native CLI bootstrap only. ScriptVM owns source decoding and language diagnostics.
class EditorCliSource final {
public:
    static constexpr std::uint64_t maxSourceBytes = 2 * 1024 * 1024;
    static core::io::FileReadResult read(
        const std::filesystem::path& path, std::uint64_t maxBytes = maxSourceBytes) noexcept;
};

} // namespace arp::editor
