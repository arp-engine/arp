#include "tools/io/file_inventory.h"
#include "tools/survivors/detail/inventory_protocol.h"

int main(int argc, char** argv)
{
    return arp::tools::io::runFileInventoryCli(
        argc, argv, arp::tools::survivors::detail::inventoryProtocol);
}
