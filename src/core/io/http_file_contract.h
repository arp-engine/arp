#pragma once

#include "core/io/file_read_contract.h"

#include <string_view>

namespace arp::core::io {

struct HttpFileBackendDesc {
    core::pmr::String baseUrl{&core::defaultCpuMemoryResource()};
    std::uint32_t contractVersion = 1;
    std::uint32_t maxInFlight = 0;
    std::uint64_t maxBytesInFlight = 0;
    std::size_t maxPathBytes = 0;
    std::size_t maxHeaderBytes = 0;
    std::size_t ioChunkBytes = 0;
    std::uint32_t timeoutMs = 0;
    std::uint32_t cancellationPollMs = 0;
    // Native DNS result capacity; zero explicitly selects numeric endpoints.
    std::uint32_t maxDnsAddresses = 0;
};

// Views are consumed during header admission, never retained by the receiver.
// Adapters reject duplicate singleton headers before constructing this view.
struct HttpFileResponseHeaders {
    unsigned status = 0;
    std::string_view contentLength;
    std::string_view contentRange;
    std::string_view contentEncoding;
};

class ARP_API HttpFileContract final {
public:
    static FileReadValidation validate(const HttpFileBackendDesc& desc) noexcept;
    static FileReadValidation validate(
        const HttpFileBackendDesc& desc, const FileReadRequest& request) noexcept;
    static core::pmr::String url(const HttpFileBackendDesc& desc, std::string_view path);
    static bool usesWireRange(const FileReadRequest& request) noexcept;
    static std::uint64_t wireBudget(const FileReadRequest& request) noexcept;
    static FileReadValidation responseHeaders(
        unsigned status, std::string_view block, HttpFileResponseHeaders& output) noexcept;
};

// Shared byte contract for engine HTTP Client and Fetch. No transport, callbacks, ETag
// cache or domain parsing. A content-conditioned range hashes the whole stream
// and copies only the requested slice, within two explicit byte budgets.
class ARP_API HttpFileBodyReceiver final {
public:
    explicit HttpFileBodyReceiver(const FileReadRequest& request);
    bool headers(const HttpFileResponseHeaders& headers);
    bool append(const std::uint8_t* bytes, std::size_t count);
    void finish();
    void fail(FileIoStatus status, const char* diagnostic) noexcept;
    FileReadResult take() noexcept;

    FileIoStatus status() const noexcept { return result_.status; }

    std::uint64_t receivedBytes() const noexcept { return received_; }

private:
    const FileReadRequest& request_;
    FileReadResult result_;
    core::Sha256 hash_;
    std::uint64_t received_ = 0;
    std::uint64_t expectedBytes_ = 0;
    bool lengthKnown_ = false;
    bool admitted_ = false;
    bool finished_ = false;
};

} // namespace arp::core::io
