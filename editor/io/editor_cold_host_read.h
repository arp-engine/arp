#pragma once

#include "core/io/file_io.h"
#include "core/io/file_request.h"
#include "core/io/file_result.h"

namespace arp::editor {

// Editor cold host policy, never a frame/network loader. Inline WorkService
// preserves synchronous native/MEMFS reads through the real FileService gate.
class EditorColdHostRead final {
public:
    static core::io::FileReadResult read(const core::io::FileReadRequest&) noexcept;
    // Zero is an exact-empty logical budget; the FileService request stays positive.
    static core::BinaryFileReadResult readBinaryBytes(
        const std::filesystem::path&, std::uint64_t maxBytes);
    // Byte-exact legacy source projection; Text/1 decoding remains with the caller.
    static core::TextFileReadResult readTextBytes(
        const std::filesystem::path&, std::uint64_t maxBytes);
};

} // namespace arp::editor
