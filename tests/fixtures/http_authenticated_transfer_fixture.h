#pragma once

#include "network/http/byte_read_transport.h"

#include <cassert>

namespace arp::network::http::tests {

inline constexpr const char* transferToken = "transfer-012345678901234567890123456789";
inline constexpr const char* transferBody = u8"{\"message\":\"雪\"}";

inline ByteReadDesc transferDescriptor()
{
    return {4096, 256, 2000, 5, 0};
}

inline ByteReadRequest transferRequest(std::string_view origin, std::string_view route = "/echo")
{
    ByteReadRequest request;
    request.contractVersion = 2;
    request.replayProtected = true; // Echo fixture has no persistent side effect.
    request.operation = ByteReadRequest::Operation::BearerJsonPost;
    request.url = core::pmr::String(origin) + core::pmr::String(route);
    request.credentialOrigin = origin;
    request.bearerToken = transferToken;
    request.requestBody = transferBody;
    request.maxRequestBytes = 1024;
    request.maxBodyBytes = 1024;
    return request;
}

// The application owns the bounded response; transport failures never turn a
// partially received body into a successful RPC response.
class TransferProbe final : public ByteReadObserver {
public:
    explicit TransferProbe(std::size_t budget = 1024) : budget_(budget) {}

    bool onHeaders(unsigned status, std::string_view) noexcept override
    {
        httpStatus = status;
        ++headerCalls;
        if (status >= 300 && status < 400) {
            onFailure(ByteReadStatus::ProtocolError, "TEST-REDIRECT-REFUSED");
            return false;
        }
        return true;
    }

    bool onBody(const std::uint8_t* bytes, std::size_t count) noexcept override
    {
        if (failed || count > budget_ - body.size()) {
            onFailure(ByteReadStatus::BudgetExceeded, "TEST-RESPONSE-BUDGET");
            return false;
        }
        try {
            body.append(reinterpret_cast<const char*>(bytes), count);
            return true;
        } catch (...) {
            onFailure(ByteReadStatus::BudgetExceeded, "TEST-ALLOCATION");
            return false;
        }
    }

    void onEnd() noexcept override { ended = !failed; }

    void onFailure(ByteReadStatus status, const char*) noexcept override
    {
        if (!failed)
            failure = status;
        failed = true;
        body.clear();
    }

    core::pmr::String body;
    unsigned httpStatus = 0, headerCalls = 0;
    ByteReadStatus failure = ByteReadStatus::Success;
    bool failed = false, ended = false;

private:
    std::size_t budget_;
};

} // namespace arp::network::http::tests
