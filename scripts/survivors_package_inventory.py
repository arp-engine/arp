"""Explicit Survivors v1/v2 compatibility projection over the shared IO client."""
from tool_file_inventory import FileInputReceipt, ToolFileInventoryClient


class PackageInventoryClient(ToolFileInventoryClient):
    request_type = "arp.survivors-package-inventory-request"
    response_type = "arp.survivors-package-inventory"
    diagnostic_prefix = "SURVIVORS-PACKAGE-INVENTORY-"
