#pragma once

#include "network/http/message.h"

namespace arp::network::http {

enum class BodyTransferMode {
    content_length,
    chunked,
};

struct SerializationOptions {
    BodyTransferMode transfer_mode = BodyTransferMode::content_length;
    bool head_response = false;
    bool close_connection = false;
};

struct SerializeResult {
    core::pmr::String text{&core::defaultCpuMemoryResource()};
    DiagnosticCode diagnostic = DiagnosticCode::none;

    bool success() const noexcept;
};

class ARP_API Serializer {
public:
    static SerializeResult serializeRequest(
        const Request& request, const SerializationOptions& options = {});
    static SerializeResult serializeResponse(
        const Response& response, const SerializationOptions& options = {});
};

} // namespace arp::network::http
