#pragma once

#include "core/diagnostics/diagnostic_error.h"
#include "tools/io/file_inventory.h"

namespace arp::tools::io::detail {

struct FileInventoryWireProfile {
    const char* requestType;
    const char* responseType;
    const char* diagnosticPrefix;

    core::pmr::String diagnostic(std::string_view suffix) const
    {
        core::pmr::String result(diagnosticPrefix);
        result += suffix;
        return result;
    }
};

inline FileInventoryWireProfile fileInventoryWireProfile(ToolFileInventoryProtocol protocol)
{
    if (!protocol.requestType || !*protocol.requestType || !protocol.responseType
        || !*protocol.responseType || !protocol.diagnosticPrefix || !*protocol.diagnosticPrefix)
        throw core::DiagnosticError("TOOL-FILE-INVENTORY-PROTOCOL");
    return {protocol.requestType, protocol.responseType, protocol.diagnosticPrefix};
}

} // namespace arp::tools::io::detail
