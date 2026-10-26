#pragma once

#include "editor/remote/editor_remote_protocol.h"
#include "editor/workspace/editor_shell.h"

namespace arp::editor::tests {
// Actual REST/MCP parsing and owner-thread dispatch shared by settings contracts.
class EditorProtocolFixture final {
public:
    explicit EditorProtocolFixture(EditorShell& value)
        : shell(value), protocol("01234567890123456789012345678901")
    {}

    network::http::Response request(std::string_view body, bool mcp = false)
    {
        network::http::Request request;
        request.set_method(network::http::Method::Post);
        request.set_target(mcp ? "/mcp" : "/api/v1/commands");
        request.add_header("Authorization", "Bearer 01234567890123456789012345678901");
        request.add_header("Accept", "application/json");
        request.add_header("MCP-Protocol-Version", "2025-11-25");
        request.set_content_type(network::http::ContentType::Json);
        request.set_body_text(body);
        return protocol.handle(request, shell);
    }

    network::http::Response call(
        std::string_view command, std::string_view fields = {}, bool mcp = false)
    {
        core::pmr::String body;
        if (mcp) {
            body = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":")";
            body += command;
            body += R"(","arguments":{)";
            body += fields;
            body += "}}}";
        } else {
            body = R"({"schemaVersion":1,"command":")";
            body += command;
            body += '"';
            if (!fields.empty()) {
                body += ',';
                body += fields;
            }
            body += '}';
        }
        return request(body, mcp);
    }

    EditorShell& shell;
    EditorRemoteProtocol protocol;
};

} // namespace arp::editor::tests
