#pragma once

#include "core/io/file_result.h"

namespace arp::tools::shader {

struct SpirvVerifierSourceDesc {
    core::StringView path;
    core::StringView httpBase;
    std::uint64_t maxBytes = 0;
};

// Cold CLI acquisition only; binary verification stays in the shader module.
class SpirvVerifierSource final {
public:
    static core::io::FileReadResult acquire(const SpirvVerifierSourceDesc&);
};

} // namespace arp::tools::shader
