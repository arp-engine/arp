#pragma once

#include "../common/native_startup_files.h"
#include "content_source_task.h"
#include "gameplay_source_task.h"

#include <array>

namespace arp::survivors {

struct CodegenIoLimits {
    static constexpr std::uint64_t maxOutputBytes = 16u * 1024u * 1024u;
};

struct PreparedCodegenInputs {
    core::SharedPtr<const PreparedGameplay> gameplay;
    core::SharedPtr<PreparedContent> content;
};

// Offline example coordinator. Domain source tasks own decoding, while
// FileService owns byte acquisition, preconditions and individual saves.
class CodegenIo final {
public:
    PreparedCodegenInputs prepareInputs(const std::filesystem::path& directory,
        examples::NativeStartupSource source,
        core::StringView baseUrl,
        core::pmr::String& diagnostic) const;
    bool saveOutputs(const std::filesystem::path& directory,
        const std::array<core::StringView, 3>& contents,
        std::uint64_t maxOutputBytes,
        core::pmr::String& diagnostic) const;
};
} // namespace arp::survivors
