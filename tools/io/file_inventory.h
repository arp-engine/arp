#pragma once

#include "core/memory/cpu_allocator.h"

#include <string_view>

namespace arp::tools::io {

struct ToolFileInventoryLimits {
    std::size_t requestBytes = 0;
    std::size_t responseBytes = 0;
};

// Trusted compile-time wire identity supplied by the owning tool. The shared
// implementation has no knowledge of game-specific schemas or diagnostic names.
struct ToolFileInventoryProtocol {
    const char* requestType = "arp.tool-file-inventory-request";
    const char* responseType = "arp.tool-file-inventory";
    const char* diagnosticPrefix = "TOOL-FILE-INVENTORY-";
};

// Offline, ordered raw-file receipts. Both protocols retain their explicit
// identities; v1 emits hashes and v2 additionally returns bounded owned bytes.
class ToolFileInventory final {
public:
    static core::pmr::String prepare(std::string_view request,
        ToolFileInventoryLimits limits,
        ToolFileInventoryProtocol protocol = {});
};

// Process framing only; filesystem requests always use ColdToolFiles/FileService.
int runFileInventoryCli(int argc, char** argv, ToolFileInventoryProtocol protocol);

} // namespace arp::tools::io
