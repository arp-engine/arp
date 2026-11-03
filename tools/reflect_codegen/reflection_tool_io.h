#pragma once

#include "tools/io/cold_tool_files.h"
#include "tools/io/native_tool_outputs.h"

namespace arp::tools::reflect_codegen {

struct ReflectionIoLimits {
    static constexpr std::uint64_t headerBytes = 2u * 1024u * 1024u;
    static constexpr std::uint64_t inputBytes = 16u * 1024u * 1024u;
    static constexpr std::uint64_t outputBytes = 16u * 1024u * 1024u;
    static constexpr std::uint32_t headerCount = 128;
    std::uint64_t maxHeaderBytes = headerBytes, maxInputBytes = inputBytes,
                  maxOutputBytes = outputBytes;
    std::uint32_t maxHeaders = headerCount;

    bool valid() const noexcept;
};

// Header identity and parser semantics stay in the generator. This session
// owns only bounded acquisition and retirement of the explicit file source.
class ReflectionToolIo final {
public:
    ReflectionToolIo(const ReflectionIoLimits&, bool http, core::StringView baseUrl);
    core::io::FileTextReadResult readHeader(const std::filesystem::path&);

    void retire() { files_->retire(); }

private:
    ReflectionIoLimits limits_;
    core::UniquePtr<io::ColdToolFiles> files_;
    core::PoolHandle mount_;
    std::uint64_t inputBytes_ = 0;
    std::uint32_t reads_ = 0;
    bool http_;
};
} // namespace arp::tools::reflect_codegen
