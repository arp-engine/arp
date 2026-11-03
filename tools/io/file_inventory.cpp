#include "tools/io/file_inventory.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/io/file_read_contract.h"
#include "core/io/filesystem_path.h"
#include "tools/io/cold_tool_files.h"
#include "tools/io/detail/file_inventory_protocol.h"

#include <algorithm>
#include <limits>

namespace arp::tools::io {
namespace {
using Yaml = core::YamlContract;

struct Input {
    core::pmr::String name;
    core::io::FileReadRequest read;
    std::uint64_t bytes = 0;
};

bool finite(std::size_t bytes)
{
    return bytes && bytes != std::numeric_limits<std::size_t>::max();
}
} // namespace

core::pmr::String ToolFileInventory::prepare(
    core::StringView request, ToolFileInventoryLimits limits, ToolFileInventoryProtocol protocol)
{
    const auto profile = detail::fileInventoryWireProfile(protocol);
    const auto require = [&](bool condition, core::StringView suffix) {
        if (!condition)
            throw core::DiagnosticError(profile.diagnostic(suffix));
    };
    require(finite(limits.requestBytes) && finite(limits.responseBytes)
            && request.size() <= limits.requestBytes,
        "CONTROL-BUDGET");
    const auto root = Yaml::parseSingleDocument(request);
    const auto version = Yaml::uint32(root, "contractVersion");
    require(Yaml::text(root, "type") == profile.requestType && (version == 1 || version == 2),
        "CONTRACT");
    const bool includePayload = version == 2;
    if (includePayload) {
        Yaml::onlyKeys(root,
            {"type",
                "contractVersion",
                "maxFiles",
                "maxFileBytes",
                "maxTotalBytes",
                "maxPathBytes",
                "files",
                "payloadEncoding",
                "maxPayloadBytes"});
        require(Yaml::text(root, "payloadEncoding") == "base64", "PAYLOAD-ENCODING");
    } else {
        Yaml::onlyKeys(root,
            {"type",
                "contractVersion",
                "maxFiles",
                "maxFileBytes",
                "maxTotalBytes",
                "maxPathBytes",
                "files"});
    }
    const auto maxPayloadBytes =
        includePayload ? Yaml::uint64(Yaml::required(root, "maxPayloadBytes")) : 0;
    require(!includePayload || maxPayloadBytes <= limits.responseBytes, "PAYLOAD-BUDGET");
    const auto maxFiles = Yaml::uint32(root, "maxFiles");
    const auto maxFileBytes = Yaml::uint64(Yaml::required(root, "maxFileBytes"));
    const auto maxTotalBytes = Yaml::uint64(Yaml::required(root, "maxTotalBytes"));
    const auto maxPathBytes = Yaml::uint64(Yaml::required(root, "maxPathBytes"));
    const auto& entries = Yaml::sequence(root, "files");
    require(maxFiles && !entries.empty() && entries.size() <= maxFiles
            && ColdToolFiles::validBudget(maxFileBytes) && finite(maxPathBytes)
            && maxTotalBytes != UINT64_MAX,
        "BUDGET");
    core::pmr::Vector<Input> inputs;
    inputs.reserve(entries.size());
    std::uint64_t total = 0, encodedTotal = 0;
    for (const auto& entry : entries) {
        Yaml::onlyKeys(entry, {"name", "path", "bytes"});
        Input input;
        input.name = Yaml::text(entry, "name");
        input.bytes = Yaml::uint64(Yaml::required(entry, "bytes"));
        input.read.contractVersion = 1;
        input.read.pathScope = core::io::FilePathScope::HostFilesystem;
        input.read.path = Yaml::text(entry, "path");
        // Positive transport admission retains the exact observed size below.
        input.read.maxBytes = std::max<std::uint64_t>(1, input.bytes);
        const auto validation = core::io::FileReadContract::validate(input.read);
        if (validation.diagnostic)
            throw core::DiagnosticError(validation.diagnostic);
        require(core::FilePath::isSafeRelativePath(input.name, maxPathBytes)
                && input.name.find('\0') == core::pmr::String::npos
                && !core::Utf8Decoder::validate(input.name)
                && input.read.path.size() <= maxPathBytes
                && core::FilePath::isAbsoluteHostPath(input.read.path),
            "PATH");
        require(input.bytes <= maxFileBytes && input.bytes <= maxTotalBytes - total, "BYTE-BUDGET");
        require(std::none_of(inputs.begin(),
                    inputs.end(),
                    [&](const Input& other) {
                        return other.name == input.name;
                    }),
            "DUPLICATE");
        total += input.bytes;
        if (includePayload) {
            require(total <= maxPayloadBytes, "PAYLOAD-BUDGET");
            const auto encoded = ((input.bytes + 2) / 3) * 4;
            require(encoded <= limits.responseBytes - encodedTotal, "RESPONSE-BUDGET");
            encodedTotal += encoded;
        }
        inputs.push_back(std::move(input));
    }
    ColdToolFiles files(maxFileBytes, 0, ColdToolFileScope::NativeHostFilesystem);
    auto output = Yaml::Node::mapping();
    output["type"] = profile.responseType;
    output["contractVersion"] = version;
    if (includePayload)
        output["payloadEncoding"] = "base64";
    output["files"] = Yaml::Node::sequence();
    for (const auto& input : inputs) {
        auto acquired = files.read(input.read);
        if (acquired.status != core::io::FileIoStatus::Success) {
            if (acquired.diagnosticId)
                throw core::DiagnosticError(acquired.diagnosticId);
            if (!acquired.diagnostics.empty())
                throw core::DiagnosticError(acquired.diagnostics);
            throw core::DiagnosticError(profile.diagnostic("READ"));
        }
        require(acquired.bytes.size() == input.bytes, "SIZE-CHANGED");
        auto entry = Yaml::Node::mapping();
        entry["path"] = input.name;
        entry["bytes"] = static_cast<std::int64_t>(acquired.bytes.size());
        const auto digest = core::Sha256::hexDigest(acquired.bytes.empty()
                ? core::StringView{}
                : core::StringView(
                      reinterpret_cast<const char*>(acquired.bytes.data()), acquired.bytes.size()));
        // Both receipt protocols store raw lowercase hex; core's typed hash is sha256:<hex>.
        constexpr std::string_view scheme = "sha256:";
        require(digest.compare(0, scheme.size(), scheme.data(), scheme.size()) == 0
                && core::Sha256::isHexDigest(std::string_view(digest).substr(scheme.size())),
            "HASH-ENCODING");
        entry["sha256"] =
            core::pmr::String(digest.data() + scheme.size(), digest.size() - scheme.size());
        if (includePayload)
            entry["data"] = core::Base64::encode(acquired.bytes);
        output["files"].get_value_ref<Yaml::Node::sequence_type&>().push_back(std::move(entry));
    }
    files.retire();
    auto result = Yaml::serialize(output);
    require(result.size() <= limits.responseBytes, "RESPONSE-BUDGET");
    return result;
}
} // namespace arp::tools::io
