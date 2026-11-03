#pragma once

#include "core/container/array_view.h"
#include "core/io/file_save.h"

#include <filesystem>

namespace arp::tools::io {

enum class NativeToolOutputEncoding { ExactBytes, LegacyNativeText };
enum class NativeToolOutputFailure { None, Options, Budget, Directory, Baseline, Save, Allocation };

struct NativeToolOutputFile {
    std::filesystem::path path;
    core::StringView bytes;
};

struct NativeToolOutputDesc {
    core::ArrayView<NativeToolOutputFile> files;
    std::uint32_t maxFiles = 0;
    std::uint64_t maxTotalBytes = 0;
    NativeToolOutputEncoding encoding = NativeToolOutputEncoding::ExactBytes;
};

struct NativeToolOutputResult {
    NativeToolOutputFailure failure = NativeToolOutputFailure::None;
    const char* diagnosticId = nullptr;
    std::size_t committedFiles = 0;
    core::io::FileSaveResult lastSave;

    bool success() const noexcept { return failure == NativeToolOutputFailure::None; }
};

// Offline tool policy, not a cross-file transaction: preflight all candidates
// and old baselines, then verify each FileService save without retry/fallback.
class NativeToolOutputWriter final {
public:
    NativeToolOutputResult save(const NativeToolOutputDesc&) const noexcept;
};
} // namespace arp::tools::io
