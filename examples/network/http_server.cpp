#include "network/http/server.h"
#include "network/io_service.h"

#include <charconv>
#include <cstdio>
#include <thread>

namespace {
using namespace arp::network;
using namespace arp::network::http;

class HttpExample {
public:
    bool run(uint16 port, std::chrono::milliseconds lifetime)
    {
        if (server_.route(Method::Get, "/health") != DiagnosticCode::none
            || server_.route(Method::Get, "/users/:id") != DiagnosticCode::none
            || server_.route(Method::Post, "/echo") != DiagnosticCode::none)
            return false;
        const auto status = server_.start(endpoint("127.0.0.1", port));
        if (!status.success()) {
            std::fprintf(stderr,
                "%.*s transport=%d\n",
                int(diagnosticName(status.diagnostic).size()),
                diagnosticName(status.diagnostic).data(),
                status.transport_error.value());
            return false;
        }
        std::thread reactor([this] {
            service_.run();
        });
        std::printf("listening_port=%u\n", unsigned(server_.local_endpoint().port()));
        std::fflush(stdout);
        next();
        context_.run_after(lifetime, [this] {
            server_.stop();
        });
        while (!server_.stopped()) {
            context_.dispatch();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        context_.shutdown();
        reactor.join();
        context_.dispatch();
        return true;
    }

private:
    void next()
    {
        server_.async_next_request([this](const SessionStatus& status, IncomingRequestPtr request) {
            if (!status.success())
                return;
            Response response;
            response.set_reason("OK");
            response.set_content_type(ContentType::Plain);
            if (request->route_match().path == "/health")
                response.set_body_text("healthy");
            else if (request->route_match().pattern == "/users/:id")
                response.set_body_text(request->param("id"));
            else
                response.set_body_text(request->request().body_text());
            response.add_header("X-ARP", "cpp-server");
            request->async_respond(response, [](const SessionStatus& result) {
                if (!result.success())
                    std::fprintf(stderr,
                        "response failed: %.*s\n",
                        int(diagnosticName(result.diagnostic).size()),
                        diagnosticName(result.diagnostic).data());
            });
            next();
        });
    }

    io_service service_;
    io_context context_{&service_};
    Server server_{&context_};
};
} // namespace

int main(int argc, char** argv)
{
    int port = 0, lifetime = 10000;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc)
            return 2;
        int value = 0;
        const std::string_view argument(argv[i + 1]);
        const auto parsed =
            std::from_chars(argument.data(), argument.data() + argument.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size())
            return 2;
        if (std::string_view(argv[i]) == "--port" && value >= 0 && value <= 65535)
            port = value;
        else if (std::string_view(argv[i]) == "--duration-ms" && value > 0)
            lifetime = value;
        else
            return 2;
    }
    HttpExample app;
    return app.run(static_cast<uint16>(port), std::chrono::milliseconds(lifetime)) ? 0 : 1;
}
