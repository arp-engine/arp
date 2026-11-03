#include "core/diagnostics/diagnostic_error.h"
#include "core/encoding/yaml_contract.h"
#include "tools/io/cold_tool_files.h"
#include "tools/io/detail/file_inventory_protocol.h"
#include "tools/io/file_inventory.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {
std::size_t number(
    const char* value, const arp::tools::io::detail::FileInventoryWireProfile& profile)
{
    const auto text = std::string_view(value);
    std::size_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()
        || !arp::tools::io::ColdToolFiles::validBudget(result))
        throw arp::core::DiagnosticError(profile.diagnostic("ARGUMENTS"));
    return result;
}
} // namespace

int arp::tools::io::runFileInventoryCli(int argc, char** argv, ToolFileInventoryProtocol protocol)
{
    using namespace arp;
    const auto profile = detail::fileInventoryWireProfile(protocol);
    try {
#if defined(_WIN32)
        if (_setmode(_fileno(stdin), _O_BINARY) < 0 || _setmode(_fileno(stdout), _O_BINARY) < 0)
            throw core::DiagnosticError(profile.diagnostic("STDIO"));
#endif
        if (argc != 7 || std::string_view(argv[1]) != "--request-limit-bytes"
            || std::string_view(argv[3]) != "--response-limit-bytes"
            || std::string_view(argv[5]) != "--preparation-bytes")
            throw core::DiagnosticError(profile.diagnostic("ARGUMENTS"));
        const ToolFileInventoryLimits limits{number(argv[2], profile), number(argv[4], profile)};
        const auto preparationBytes = number(argv[6], profile);
        core::CpuMemoryQuotaScope quota(core::makeShared<core::CpuMemoryQuota>(preparationBytes));
        core::pmr::String request;
        std::array<char, tools::io::ColdToolFileProfile::ioChunkBytes> chunk;
        // stdin/stdout are the process protocol, not filesystem resource inputs.
        while (true) {
            const auto count = std::fread(chunk.data(),
                1,
                std::min(chunk.size(), limits.requestBytes - request.size() + 1),
                stdin);
            if (count > limits.requestBytes - request.size())
                throw core::DiagnosticError(profile.diagnostic("CONTROL-BUDGET"));
            request.append(chunk.data(), count);
            if (count == 0) {
                if (std::ferror(stdin))
                    throw core::DiagnosticError(profile.diagnostic("STDIN"));
                break;
            }
        }
        const auto result = ToolFileInventory::prepare(request, limits, protocol);
        if (std::fwrite(result.data(), 1, result.size(), stdout) != result.size()
            || std::fflush(stdout) != 0)
            throw core::DiagnosticError(profile.diagnostic("STDOUT"));
        return 0;
    } catch (const core::YamlContractError& error) {
        std::fprintf(stderr, "%s\n", error.diagnostic);
    } catch (const std::bad_alloc&) {
        std::fprintf(stderr, "%sALLOCATION\n", profile.diagnosticPrefix);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
    }
    return 1;
}
