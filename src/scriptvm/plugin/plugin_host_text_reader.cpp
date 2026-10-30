#include "scriptvm/plugin/plugin_host_text_reader.h"

#include "core/io/blocking_file_reader.h"
#include "core/io/filesystem_path.h"
#include "core/text/text_decoder.h"

#include <algorithm>
#include <limits>

namespace arp::scriptvm {
namespace {

core::io::FileReadRequest requestFor(const std::filesystem::path& path, std::uint64_t maxBytes)
{
    core::io::FileReadRequest request;
    request.contractVersion = 1;
    request.pathScope = core::io::FilePathScope::HostFilesystem;
    request.path = core::FilePath::utf8(path);
    request.maxBytes = maxBytes;
    return request;
}

const char* readDiagnostic(const core::io::FileReadResult& source) noexcept
{
    if (source.diagnosticId)
        return source.diagnosticId;
    return source.status == core::io::FileIoStatus::BudgetExceeded ? "ARP-PLUGIN-HOST-TEXT-BUDGET"
                                                                   : "ARP-PLUGIN-HOST-TEXT-READ";
}

} // namespace

bool PluginHostTextReader::validBudget(const PluginHostTextBudget& budget) noexcept
{
    constexpr auto max = std::numeric_limits<std::size_t>::max();
    return budget.contractVersion == 1 && budget.maxMetadataBytes > 0
        && budget.maxMetadataBytes < max && budget.maxAuditBytes > 0 && budget.maxAuditBytes < max
        && budget.maxDiagnosticBytes > 0 && budget.maxDiagnosticBytes <= (max - 1u) / 4u;
}

PluginHostTextResult PluginHostTextReader::read(
    const std::filesystem::path& path, PluginHostTextKind kind) const noexcept
{
    PluginHostTextResult result;
    if (!validBudget(budget_)) {
        result.diagnosticId = "ARP-PLUGIN-HOST-TEXT-CONFIG";
        return result;
    }
    if (kind != PluginHostTextKind::Metadata && kind != PluginHostTextKind::Audit) {
        result.diagnosticId = "ARP-PLUGIN-HOST-TEXT-KIND";
        return result;
    }
    try {
        const auto limit =
            kind == PluginHostTextKind::Metadata ? budget_.maxMetadataBytes : budget_.maxAuditBytes;
        const auto source = core::io::BlockingFileReader().read(requestFor(path, limit));
        if (source.status != core::io::FileIoStatus::Success) {
            result.diagnosticId = readDiagnostic(source);
            return result;
        }
        core::TextDecodeDesc decode;
        decode.bom = core::TextBomPolicy::Preserve;
        decode.nul = core::TextNulPolicy::Reject;
        decode.maxOutputBytes = limit;
        const auto* bytes =
            source.bytes.empty() ? "" : reinterpret_cast<const char*>(source.bytes.data());
        auto text = core::TextDecoder::decode({bytes, source.bytes.size()}, decode);
        result.success = text.status == core::TextDecodeStatus::Success;
        result.diagnosticId = text.diagnosticId;
        if (result.success)
            result.text = std::move(text.text);
    } catch (...) {
        result.success = false;
        result.text.clear();
        result.diagnosticId = "ARP-PLUGIN-HOST-TEXT-ALLOCATION";
    }
    return result;
}

PluginHostTextResult PluginHostTextReader::diagnostic(
    const std::filesystem::path& path) const noexcept
{
    PluginHostTextResult result;
    if (!validBudget(budget_)) {
        result.diagnosticId = "ARP-PLUGIN-HOST-TEXT-CONFIG";
        return result;
    }
    try {
        auto request = requestFor(path, budget_.maxDiagnosticBytes + 1u);
        request.range = core::io::FileReadRange::exactRange(0, request.maxBytes);
        request.partialPolicy = core::io::FilePartialReadPolicy::AllowPartial;
        const auto source = core::io::BlockingFileReader().read(request);
        if (source.status != core::io::FileIoStatus::Success) {
            result.diagnosticId = readDiagnostic(source);
            return result;
        }
        const auto count = std::min<std::size_t>(source.bytes.size(), budget_.maxDiagnosticBytes);
        result.truncated = source.bytes.size() > count;
        core::pmr::String text{&core::defaultCpuMemoryResource()};
        text.reserve(count * 4u);
        constexpr char digits[] = "0123456789abcdef";
        for (std::size_t index = 0; index < count; ++index) {
            const auto byte = source.bytes[index];
            if (byte == '\\')
                text += "\\\\";
            else if ((byte >= 32u && byte <= 126u) || byte == '\r' || byte == '\n' || byte == '\t')
                text.push_back(static_cast<char>(byte));
            else {
                text += "\\x";
                text.push_back(digits[byte >> 4u]);
                text.push_back(digits[byte & 15u]);
            }
        }
        result.text = std::move(text);
        result.success = true;
    } catch (...) {
        result.diagnosticId = "ARP-PLUGIN-HOST-TEXT-ALLOCATION";
    }
    return result;
}

} // namespace arp::scriptvm
