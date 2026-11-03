#include "core/diagnostics/diagnostic_error.h"
#include "tools/io/file_inventory.h"
#include "tools/survivors/package_file_inventory.h"

#include <cassert>
#include <cstdio>

namespace {
void rejectNativeProfile(bool payload = false)
{
    constexpr std::string_view request = R"YAML(type: arp.survivors-package-inventory-request
contractVersion: 1
maxFiles: 1
maxFileBytes: 1
maxTotalBytes: 0
maxPathBytes: 128
files:
  - name: empty.bin
    path: /not-opened/empty.bin
    bytes: 0
)YAML";
    bool rejected = false;
    try {
        arp::core::pmr::String input(request);
        if (payload) {
            const auto version = input.find("contractVersion: 1");
            assert(version != arp::core::pmr::String::npos);
            input[version + std::string_view("contractVersion: ").size()] = '2';
            input += "payloadEncoding: base64\nmaxPayloadBytes: 0\n";
        }
#if defined(ARP_TEST_SURVIVORS_INVENTORY)
        (void)arp::tools::survivors::PackageFileInventory::prepare(input, {4096, 4096});
#else
        const auto legacyType = input.find("arp.survivors-package-inventory-request");
        input.replace(legacyType,
            std::string_view("arp.survivors-package-inventory-request").size(),
            "arp.tool-file-inventory-request");
        (void)arp::tools::io::ToolFileInventory::prepare(input, {4096, 4096});
#endif
    } catch (const arp::core::DiagnosticError& error) {
        rejected = std::string_view(error.what()) == "TOOL-FILE-HOST-NATIVE-ONLY";
    }
    assert(rejected);
}
} // namespace

int main(int argc, char** argv)
{
    rejectNativeProfile();
    rejectNativeProfile(true);
    if (argc > 1 && std::string_view(argv[1]) == "--smoke") {
        const auto before = arp::core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i) {
            rejectNativeProfile();
            rejectNativeProfile(true);
            const auto after = arp::core::defaultCpuAllocator().stats();
            assert(after.liveBytes == before.liveBytes);
            assert(after.allocationCount - after.deallocationCount
                == before.allocationCount - before.deallocationCount);
        }
        std::puts(
            "native_profile_rejected=true ordinary_cycles=20 core_live_bytes_delta=0 core_live_count_delta=0");
    }
}
