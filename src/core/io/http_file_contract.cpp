#include "core/io/http_file_contract.h"

#include "core/io/filesystem_path.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace arp::core::io {
namespace {

bool number(std::string_view text, std::uint64_t& value) noexcept
{
    if (text.empty())
        return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool contentRange(
    std::string_view text, std::uint64_t& first, std::uint64_t& last, std::uint64_t& total) noexcept
{
    constexpr std::string_view prefix = "bytes ";
    if (text.substr(0, prefix.size()) != prefix)
        return false;
    text.remove_prefix(prefix.size());
    const auto dash = text.find('-');
    const auto slash = text.find('/');
    return dash != text.npos && slash != text.npos && dash < slash
        && number(text.substr(0, dash), first)
        && number(text.substr(dash + 1, slash - dash - 1), last)
        && number(text.substr(slash + 1), total) && first <= last && last < total;
}

bool urlByte(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
        || c == '_' || c == '.' || c == '~' || c == '/';
}

bool headerName(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto c = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + ('a' - 'A') : a[i];
        if (c != b[i])
            return false;
    }
    return true;
}

} // namespace

FileReadValidation HttpFileContract::validate(const HttpFileBackendDesc& desc) noexcept
{
    if (desc.contractVersion != 1 || !desc.maxInFlight || !desc.maxBytesInFlight
        || !desc.maxPathBytes || !desc.maxHeaderBytes || !desc.ioChunkBytes || !desc.timeoutMs
        || !desc.cancellationPollMs || desc.cancellationPollMs > desc.timeoutMs
        || desc.ioChunkBytes > INT32_MAX || desc.maxHeaderBytes > INT32_MAX
        || desc.timeoutMs > INT32_MAX || desc.baseUrl.size() >= INT32_MAX
        || desc.maxPathBytes > (INT32_MAX - desc.baseUrl.size() - 1) / 3)
        return {FileIoStatus::InvalidRequest, "IO-HTTP-DESCRIPTOR"};
    const std::string_view url = desc.baseUrl;
    const auto prefix = url.substr(0, 7) == "http://" ? 7u
        : url.substr(0, 8) == "https://"              ? 8u
                                                      : 0u;
    if (!prefix || url.back() != '/' || url.size() > desc.maxPathBytes)
        return {FileIoStatus::InvalidRequest, "IO-HTTP-BASE-URL"};
    const auto slash = url.find('/', prefix);
    const auto authority = url.substr(prefix, slash - prefix);
    if (authority.empty() || authority.find_first_of("@?#%\\") != authority.npos)
        return {FileIoStatus::InvalidRequest, "IO-HTTP-AUTHORITY"};
    // Version1 accepts ASCII DNS/IPv4 authorities and an optional decimal port.
    // IPv6 and credentials need an explicit subsequent contract, never guessing.
    const auto colon = authority.find(':');
    const auto host = authority.substr(0, colon);
    if (host.empty())
        return {FileIoStatus::InvalidRequest, "IO-HTTP-HOST"};
    for (const char c : host)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
                || c == '.'))
            return {FileIoStatus::InvalidRequest, "IO-HTTP-HOST"};
    std::uint64_t port = 0;
    if (colon != authority.npos
        && (!number(authority.substr(colon + 1), port) || !port || port > UINT16_MAX))
        return {FileIoStatus::InvalidRequest, "IO-HTTP-PORT"};
    const auto path = url.substr(slash + 1, url.size() - slash - 2);
    if ((url.size() != slash + 1 && path.empty())
        || (!path.empty() && !core::FilePath::isSafeRelativePath(path, desc.maxPathBytes)))
        return {FileIoStatus::InvalidRequest, "IO-HTTP-BASE-PATH"};
    for (const char c : path)
        if (!urlByte(c))
            return {FileIoStatus::InvalidRequest, "IO-HTTP-BASE-PATH-ENCODING"};
    return {};
}

FileReadValidation HttpFileContract::validate(
    const HttpFileBackendDesc& desc, const FileReadRequest& request) noexcept
{
    if (const auto descriptor = validate(desc); descriptor.diagnostic)
        return descriptor;
    if (const auto base = FileReadContract::validate(request); base.diagnostic)
        return base;
    if (request.contractVersion != 1 || request.pathScope != FilePathScope::MountedResource
        || !core::FilePath::isSafeRelativePath(request.path, desc.maxPathBytes))
        return {FileIoStatus::InvalidRequest, "IO-HTTP-BOUNDED-RELATIVE-READ-REQUIRED"};
    if (request.maxBytes > SIZE_MAX)
        return {FileIoStatus::BudgetExceeded, "IO-HTTP-ADDRESS-BUDGET"};
    return {};
}

core::pmr::String HttpFileContract::url(const HttpFileBackendDesc& desc, std::string_view path)
{
    core::pmr::String result = desc.baseUrl;
    result.reserve(desc.baseUrl.size() + 3 * path.size());
    constexpr char digits[] = "0123456789ABCDEF";
    for (const unsigned char c : path) {
        if (urlByte(static_cast<char>(c)))
            result += static_cast<char>(c);
        else {
            result += '%';
            result += digits[c >> 4];
            result += digits[c & 15];
        }
    }
    return result;
}

bool HttpFileContract::usesWireRange(const FileReadRequest& request) noexcept
{
    return request.range.mode == FileReadRangeMode::ExactRange
        && request.expectedVersion.kind == FileVersionKind::None;
}

std::uint64_t HttpFileContract::wireBudget(const FileReadRequest& request) noexcept
{
    return request.expectedVersion.kind != FileVersionKind::None ? request.maxVersionBytes
                                                                 : request.maxBytes;
}

FileReadValidation HttpFileContract::responseHeaders(
    unsigned status, std::string_view block, HttpFileResponseHeaders& output) noexcept
{
    output = {status, {}, {}, {}};
    bool seenLength = false, seenRange = false, seenEncoding = false;
    while (!block.empty()) {
        const auto end = block.find('\n');
        auto line = block.substr(0, end);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        block = end == block.npos ? std::string_view{} : block.substr(end + 1);
        if (line.empty())
            continue;
        const auto colon = line.find(':');
        if (colon == line.npos || !colon || line.front() == ' ' || line.front() == '\t')
            return {FileIoStatus::IntegrityMismatch, "IO-HTTP-HEADER-SYNTAX"};
        const auto name = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.remove_suffix(1);
        std::string_view* target = nullptr;
        bool* seen = nullptr;
        if (headerName(name, "content-length")) {
            target = &output.contentLength;
            seen = &seenLength;
        } else if (headerName(name, "content-range")) {
            target = &output.contentRange;
            seen = &seenRange;
        } else if (headerName(name, "content-encoding")) {
            target = &output.contentEncoding;
            seen = &seenEncoding;
        }
        if (target) {
            if (*seen || value.empty())
                return {FileIoStatus::IntegrityMismatch, "IO-HTTP-DUPLICATE-OR-EMPTY-HEADER"};
            *seen = true;
            *target = value;
        }
    }
    return {};
}

HttpFileBodyReceiver::HttpFileBodyReceiver(const FileReadRequest& request) : request_(request)
{
    result_.bytes.reserve(static_cast<std::size_t>(request.maxBytes));
}

bool HttpFileBodyReceiver::headers(const HttpFileResponseHeaders& headers)
{
    if (admitted_ || finished_ || status() != FileIoStatus::Success) {
        fail(FileIoStatus::IntegrityMismatch, "IO-HTTP-HEADER-STATE");
        return false;
    }
    if (headers.status != 200 && headers.status != 206) {
        const auto status = headers.status == 404            ? FileIoStatus::MissingFile
            : headers.status == 401 || headers.status == 403 ? FileIoStatus::PermissionDenied
            : headers.status == 412                          ? FileIoStatus::VersionMismatch
            : headers.status == 416                          ? FileIoStatus::InvalidRange
                                                             : FileIoStatus::Unsupported;
        fail(status,
            headers.status >= 300 && headers.status < 400 ? "IO-HTTP-REDIRECT-DENIED"
                                                          : "IO-HTTP-STATUS");
        return false;
    }
    if (!headers.contentEncoding.empty() && headers.contentEncoding != "identity") {
        fail(FileIoStatus::Unsupported, "IO-HTTP-CONTENT-ENCODING");
        return false;
    }
    const bool range = HttpFileContract::usesWireRange(request_);
    if (headers.status != (range ? 206u : 200u)) {
        fail(FileIoStatus::IntegrityMismatch, "IO-HTTP-RANGE-STATUS");
        return false;
    }
    lengthKnown_ = !headers.contentLength.empty();
    if (lengthKnown_ && !number(headers.contentLength, expectedBytes_)) {
        fail(FileIoStatus::IntegrityMismatch, "IO-HTTP-CONTENT-LENGTH");
        return false;
    }
    if (range) {
        std::uint64_t first = 0, last = 0, total = 0;
        if (!contentRange(headers.contentRange, first, last, total)) {
            fail(FileIoStatus::IntegrityMismatch, "IO-HTTP-CONTENT-RANGE");
            return false;
        }
        const auto resolved = FileReadContract::resolveRange(request_, total);
        if (resolved.diagnostic) {
            fail(resolved.status, resolved.diagnostic);
            return false;
        }
        if (first != resolved.offset || last - first + 1 != resolved.size
            || (lengthKnown_ && expectedBytes_ != resolved.size)) {
            fail(FileIoStatus::IntegrityMismatch, "IO-HTTP-RANGE-MISMATCH");
            return false;
        }
        expectedBytes_ = resolved.size;
        lengthKnown_ = true;
    } else if (!headers.contentRange.empty()) {
        fail(FileIoStatus::IntegrityMismatch, "IO-HTTP-UNEXPECTED-RANGE");
        return false;
    }
    if (lengthKnown_ && expectedBytes_ > HttpFileContract::wireBudget(request_)) {
        fail(FileIoStatus::BudgetExceeded, "IO-HTTP-WIRE-BUDGET");
        return false;
    }
    admitted_ = true;
    return true;
}

bool HttpFileBodyReceiver::append(const std::uint8_t* bytes, std::size_t count)
{
    if (!admitted_ || finished_ || status() != FileIoStatus::Success) {
        fail(FileIoStatus::IntegrityMismatch, "IO-HTTP-BODY-STATE");
        return false;
    }
    if (count > HttpFileContract::wireBudget(request_) - received_) {
        fail(FileIoStatus::BudgetExceeded, "IO-HTTP-WIRE-BUDGET");
        return false;
    }
    if (lengthKnown_ && count > expectedBytes_ - received_) {
        fail(FileIoStatus::IntegrityMismatch, "IO-HTTP-EXCESS-BODY");
        return false;
    }
    if (!HttpFileContract::usesWireRange(request_))
        hash_.update({reinterpret_cast<const char*>(bytes), count});
    const auto first = request_.expectedVersion.kind != FileVersionKind::None
            && request_.range.mode == FileReadRangeMode::ExactRange
        ? request_.range.offset
        : 0;
    const auto size = request_.range.mode == FileReadRangeMode::ExactRange ? request_.range.size
                                                                           : request_.maxBytes;
    const auto begin = std::max(first, received_);
    const auto end = std::min(first + size, received_ + count);
    if (begin < end)
        result_.bytes.insert(
            result_.bytes.end(), bytes + (begin - received_), bytes + (end - received_));
    received_ += count;
    return true;
}

void HttpFileBodyReceiver::finish()
{
    if (status() != FileIoStatus::Success)
        return;
    if (!admitted_ || finished_ || (lengthKnown_ && received_ != expectedBytes_)) {
        fail(FileIoStatus::IntegrityMismatch, "IO-HTTP-TRUNCATED-BODY");
        return;
    }
    finished_ = true;
    if (!HttpFileContract::usesWireRange(request_)) {
        const auto resolved = FileReadContract::resolveRange(request_, received_);
        if (resolved.diagnostic) {
            fail(resolved.status, resolved.diagnostic);
            return;
        }
        result_.version = FileVersionEncoding::finish(hash_, FileVersionKind::ContentSha256);
        if (request_.expectedVersion.kind != FileVersionKind::None
            && result_.version != request_.expectedVersion)
            fail(FileIoStatus::VersionMismatch, "IO-HTTP-CONTENT-VERSION-MISMATCH");
    }
}

void HttpFileBodyReceiver::fail(FileIoStatus status, const char* diagnostic) noexcept
{
    if (result_.status != FileIoStatus::Success)
        return;
    result_.status = status;
    result_.diagnosticId = diagnostic;
    result_.bytes.clear();
    result_.version = {};
}

FileReadResult HttpFileBodyReceiver::take() noexcept
{
    return std::move(result_);
}

} // namespace arp::core::io
