#pragma once

#include "core/text/number_format.h"
#include "scriptvm/runtime/diagnostics.h"

namespace arp::tools::asset_importer::detail {
inline core::pmr::String diagnosticText(const scriptvm::ScriptDiagnostic& diagnostic)
{
    core::pmr::String result{&core::defaultCpuMemoryResource()};
    if (!diagnostic.file.empty()) {
        result += diagnostic.file;
        result += ":";
    }
    if (diagnostic.line > 0) {
        core::appendUnsigned(result, diagnostic.line);
        result += ":";
        core::appendUnsigned(result, diagnostic.column);
        result += " ";
    }
    result += diagnostic.message;
    return result;
}
} // namespace arp::tools::asset_importer::detail
