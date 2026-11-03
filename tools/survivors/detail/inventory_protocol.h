#pragma once

#include "tools/io/file_inventory.h"

namespace arp::tools::survivors::detail {

// Only the Survivors adapter owns this historical wire identity.
inline constexpr io::ToolFileInventoryProtocol inventoryProtocol{
    "arp.survivors-package-inventory-request",
    "arp.survivors-package-inventory",
    "SURVIVORS-PACKAGE-INVENTORY-"};

} // namespace arp::tools::survivors::detail
