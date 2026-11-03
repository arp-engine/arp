#include "tools/survivors/package_file_inventory.h"

#include "tools/io/file_inventory.h"
#include "tools/survivors/detail/inventory_protocol.h"

namespace arp::tools::survivors {

core::pmr::String PackageFileInventory::prepare(
    std::string_view request, PackageInventoryLimits limits)
{
    return io::ToolFileInventory::prepare(
        request, {limits.requestBytes, limits.responseBytes}, detail::inventoryProtocol);
}

} // namespace arp::tools::survivors
