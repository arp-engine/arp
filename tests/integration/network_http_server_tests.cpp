#include "tests/fixtures/http_server_fixture.h"

using namespace arp::network;
using namespace arp::network::http;

int main()
{
    test::ServerFixture f;
    auto& server = f.make_server();
    assert(server.route(Method::Get, "/") == DiagnosticCode::none);
    assert(server.start(endpoint::localhost<ipv4>(0)).success());
    assert(server.route(Method::Get, "/late") == DiagnosticCode::routes_frozen);
    auto& missing = f.client(server, "GET /missing HTTP/1.1\r\nHost: x\r\n\r\n");
    auto& wrong = f.client(server, "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    auto& malformed = f.client(server, "GET / HTTP/1.1\r\n\r\n");
    f.until([&] {
        return missing.done && wrong.done && malformed.done;
    });
    assert(missing.response.find("404 Not Found") != missing.response.npos);
    assert(wrong.response.find("405 Method Not Allowed") != wrong.response.npos
        && wrong.response.find("Allow: GET") != wrong.response.npos);
    assert(malformed.response.find("400 Bad Request") != malformed.response.npos);

    IncomingRequestPtr incoming;
    auto& client = f.client(server, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    server.async_next_request([&](const SessionStatus& status, IncomingRequestPtr value) {
        assert(status.success());
        incoming = std::move(value);
    });
    bool busy = false;
    server.async_next_request([&](const SessionStatus& status, IncomingRequestPtr) {
        busy = status.diagnostic == DiagnosticCode::server_busy;
    });
    assert(busy);
    f.until([&] {
        return bool(incoming);
    });
    Response response;
    response.set_reason("OK");
    response.set_body_text("one");
    bool written = false, duplicate = false;
    incoming->async_respond(response, [&](const SessionStatus& status) {
        written = status.success();
    });
    incoming->async_respond(response, [&](const SessionStatus& status) {
        duplicate = status.diagnostic == DiagnosticCode::response_already_sent;
    });
    assert(duplicate);
    f.until([&] {
        return written && client.done;
    });
    bool expired = false;
    incoming->async_respond(response, [&](const SessionStatus& status) {
        expired = !status.success();
    });
    assert(expired);

    bool stopped_wait = false;
    server.async_next_request([&](const SessionStatus& status, IncomingRequestPtr) {
        stopped_wait = status.diagnostic == DiagnosticCode::server_stopped;
    });
    server.stop();
    server.stop();
    assert(stopped_wait);
    assert(server.start(endpoint::localhost<ipv4>(0)).diagnostic == DiagnosticCode::server_busy);
    f.until([&] {
        return server.stopped();
    });
    assert(server.start(endpoint::localhost<ipv4>(0)).success());

    // Read cancellation and an independently running server on the same IO loop.
    auto& hanging = f.client(server, "GET / HTTP/1.1\r\nHost:");
    auto& second = f.make_server();
    assert(second.route(Method::Get, "/") == DiagnosticCode::none);
    assert(second.start(endpoint::localhost<ipv4>(0)).success());
    auto& independent = f.client(second, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    f.until([&] {
        return hanging.sent;
    });
    server.stop();
    second.async_next_request([&](const SessionStatus& status, IncomingRequestPtr request) {
        assert(status.success());
        request->async_respond(response, [](const SessionStatus& write) {
            assert(write.success());
        });
    });
    f.until([&] {
        return independent.done && hanging.done && server.stopped();
    });
    assert(independent.response.find("one") != independent.response.npos);

    ServerConfig bounded;
    bounded.max_connections = 4;
    bounded.max_pending_requests = 1;
    bounded.read_timeout = std::chrono::milliseconds(120);
    bounded.response_timeout = std::chrono::seconds(3);
    auto& limited = f.make_server(bounded);
    assert(limited.route(Method::Get, "/") == DiagnosticCode::none);
    assert(limited.start(endpoint::localhost<ipv4>(0)).success());
    auto& first = f.client(limited, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    f.until([&] {
        return first.sent;
    });
    // Wait for first request to reach the queue without consuming it.
    const auto until = clock::now() + std::chrono::milliseconds(25);
    while (clock::now() < until) {
        f.context.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto& full = f.client(limited, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    f.until([&] {
        return full.done;
    });
    assert(full.response.find("503 Service Unavailable") != full.response.npos);
    IncomingRequestPtr queued_request;
    limited.async_next_request([&](const SessionStatus& status, IncomingRequestPtr request) {
        assert(status.success());
        queued_request = std::move(request);
    });
    assert(queued_request);
    queued_request->async_respond(response, [](const SessionStatus& status) {
        assert(status.success());
    });
    f.until([&] {
        return first.done;
    });

    ServerConfig deadlines;
    deadlines.response_timeout = std::chrono::milliseconds(250);
    auto& timeoutServer = f.make_server(deadlines);
    assert(timeoutServer.route(Method::Get, "/") == DiagnosticCode::none);
    assert(timeoutServer.start(endpoint::localhost<ipv4>(0)).success());
    IncomingRequestPtr timeout_request;
    timeoutServer.async_next_request([&](const SessionStatus& status, IncomingRequestPtr request) {
        assert(status.success());
        timeout_request = std::move(request);
    });
    auto& expiring = f.client(timeoutServer, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    f.until([&] {
        return bool(timeout_request);
    });
    f.until([&] {
        return expiring.done && !timeout_request->active();
    });
    timeout_request->async_respond(response, [&](const SessionStatus& status) {
        assert(status.diagnostic == DiagnosticCode::request_expired);
        assert(status.transport_error == inet_errors::timed_out);
    });
    auto& partial = f.client(limited, "GET / HTTP/1.1\r\n");
    f.until([&] {
        return partial.done;
    });

    auto& disconnect = f.client(limited, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    IncomingRequestPtr disconnected;
    limited.async_next_request([&](const SessionStatus& status, IncomingRequestPtr request) {
        assert(status.success());
        disconnected = std::move(request);
    });
    f.until([&] {
        return bool(disconnected);
    });
    disconnect.close();
    f.until([&] {
        return !disconnected->active();
    });
    bool peer_closed = false;
    disconnected->async_respond(response, [&](const SessionStatus& status) {
        peer_closed = !status.success();
    });
    assert(peer_closed);

    // Destroy the facade while accept/read completions still own its state.
    auto disposable = arp::core::makeUnique<Server>(&f.context);
    assert(disposable->start(endpoint::localhost<ipv4>(0)).success());
    auto& abandoned = f.client(*disposable, "GET / HTTP/1.1\r\n");
    f.until([&] {
        return abandoned.sent;
    });
    disposable.reset();
    f.until([&] {
        return abandoned.done;
    });

    // One connection pauses accept; retiring it resumes accept without polling
    // another business request into the queue early.
    ServerConfig oneConnection;
    oneConnection.max_connections = 1;
    auto& serial = f.make_server(oneConnection);
    assert(serial.route(Method::Get, "/") == DiagnosticCode::none);
    assert(serial.start(endpoint::localhost<ipv4>(0)).success());
    IncomingRequestPtr held;
    serial.async_next_request([&](const SessionStatus& status, IncomingRequestPtr request) {
        assert(status.success());
        held = std::move(request);
    });
    auto& serialFirst = f.client(serial, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    f.until([&] {
        return bool(held);
    });
    int delivered = 0;
    serial.async_next_request([&](const SessionStatus& status, IncomingRequestPtr request) {
        assert(status.success());
        ++delivered;
        request->async_respond(response, [](const SessionStatus& status) {
            assert(status.success());
        });
    });
    auto& serialNext = f.client(serial, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    f.until([&] {
        return serialNext.sent;
    });
    assert(delivered == 0);
    held->async_respond(response, [](const SessionStatus& status) {
        assert(status.success());
    });
    f.until([&] {
        return serialFirst.done && serialNext.done;
    });
    assert(delivered == 1);

    // Stop after submitting a large transfer, before its dispatch completion.
    // A reactor completion already queued must not notify the business twice.
    ServerConfig large;
    large.session.parser_limits.max_body_bytes = 16u * 1024u * 1024u;
    auto& writingServer = f.make_server(large);
    assert(writingServer.route(Method::Get, "/") == DiagnosticCode::none);
    assert(writingServer.start(endpoint::localhost<ipv4>(0)).success());
    auto& stalled = f.client(writingServer, "GET / HTTP/1.1\r\nHost: x\r\n\r\n", false);
    error_code bufferError;
    assert(socketops::setbuffersize(stalled.socket.native_handle(), SO_RCVBUF, 1024, bufferError));
    IncomingRequestPtr writingRequest;
    writingServer.async_next_request([&](const SessionStatus& status, IncomingRequestPtr request) {
        assert(status.success());
        writingRequest = std::move(request);
    });
    f.until([&] {
        return bool(writingRequest);
    });
    arp::core::pmr::String payload(large.session.parser_limits.max_body_bytes, 'w');
    Response bigResponse;
    bigResponse.set_body_text(payload);
    int completions = 0;
    writingRequest->async_respond(bigResponse, [&](const SessionStatus& status) {
        ++completions;
        assert(status.diagnostic == DiagnosticCode::server_stopped);
    });
    assert(completions == 0);
    writingServer.stop();
    f.until([&] {
        return writingServer.stopped();
    });
    assert(completions == 1);
    stalled.close();

    // Exercise reactor cancellation of an actually blocked transport write.
    // Explicit sender buffer capacity makes this independent of OS defaults.
    tcp::socket<ipv4> listener(&f.context), peer(&f.context);
    error_code ec;
    assert(listener.open(ec) == 0 && listener.bind(ec, endpoint::localhost<ipv4>(0)) == 0
        && listener.listen(ec, 1) == 0);
    bool accepted = false;
    listener.async_accept(peer, [&](const error_code& error) {
        assert(!error);
        accepted = true;
    });
    auto writerClient = arp::core::makeUnique<test::RawClient>(&f.context);
    auto* writerRaw = writerClient.get();
    f.clients.push_back(std::move(writerClient));
    writerRaw->connect(listener.local_endpoint(), "x", false);
    f.until([&] {
        return accepted && writerRaw->sent;
    });
    assert(socketops::setbuffersize(peer.native_handle(), SO_SNDBUF, 4096, ec));
    // Winsock may admit one oversized send even with a small SO_SNDBUF.
    // Fill using bounded small writes until the explicit would-block boundary.
    bool backpressure = false;
    for (std::size_t filled = 0; filled < payload.size();) {
        const auto bytes = peer.send(ec, payload.data(), 4096);
        if (bytes < 0) {
            assert(ec == inet_errors::would_block || ec == inet_errors::try_again
                || ec == inet_errors::no_buffer_space);
            backpressure = true;
            break;
        }
        assert(bytes > 0);
        filled += static_cast<std::size_t>(bytes);
    }
    assert(backpressure);
    int cancelledWrites = 0;
    peer.async_send(
        payload.data(), payload.size(), [&](const error_code& error, std::size_t bytes) {
            assert(error == inet_errors::operation_aborted && bytes < payload.size());
            ++cancelledWrites;
        });
    bool writePending = false;
    f.context.run_after(std::chrono::milliseconds(25), [&] {
        writePending = true;
    });
    f.until([&] {
        return writePending;
    });
    assert(cancelledWrites == 0);
    bool peerClosed = false, listenerClosed = false;
    peer.async_close([&](const error_code&) {
        peerClosed = true;
    });
    listener.async_close([&](const error_code&) {
        listenerClosed = true;
    });
    f.until([&] {
        return cancelledWrites == 1 && peerClosed && listenerClosed;
    });
    writerRaw->close();
}
