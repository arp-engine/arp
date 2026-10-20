#pragma once

// ArpScript network module binding.
//
// Exposes runtime-owned TCP, UDP, HTTP client and WebSocket client objects:
//   import arp_network
//   var sock = arp_network.network.create_tcp_socket()
//   await sock.connect("127.0.0.1", 8080)
//   await sock.send("hello")
//   var r = await sock.recv(1024)
//   sock.close()
//
// Async methods (connect/send/recv, sendto/recvfrom, http_get/http_post,
// ws_connect/ws_send_text/ws_recv/ws_close) return a ScriptAwaitable adapter
// bound to a real NetworkOperation object. The caller drives the owning
// io_context (dispatch loop) and calls Vm::resume() to obtain the
// NetworkResult; the operation remains available for reflected status/result
// inspection until the script explicitly destroys it.
//
// Sync methods (UDP bind and close) return immediately.
//
// All operations share a single NetworkResult result type; per-operation
// semantics map to different fields (see NetworkResult field comments).
//
// Status: Contract Complete + Prototype Complete.
// HTTPS and WSS use the network TLS backend when ARP_NETWORK_TLS_BACKEND is
// OpenSSL; a None build fails deterministically without plaintext fallback.

#include "core/cpu_allocator.h"
#include "network/endpoint.h"
#include "network/http/client.h"
#include "network/http/server.h"
#include "network/io_context.h"
#include "network/socket.h"
#include "network/transport.h"
#include "network/websocket/client.h"
#include "reflection/object.h"
#include "reflection/reflection_macros.h"
#include "scriptvm/runtime/cpp_plugin_api.h"
#include "scriptvm/runtime/module_registry.h"
#include "scriptvm/runtime/value.h"

#include <memory>
#include <mutex>

namespace arp::scriptvm {

class NetworkRuntime;
class NetworkOperation;
class NetworkConnection;
class NetworkRequest;
class NetworkMessage;
class HttpScriptServer;
class HttpScriptIncomingRequest;

inline constexpr const char* kNetworkIoContextService = "arp.network.io-context";
inline constexpr std::uint32_t kNetworkIoContextServiceVersion = 1u;

int registerNetworkScriptPlugin(ArpScriptPluginHostV2* host);

network::io_context* currentNetworkIoContext() noexcept;

// Unified result type for every network operation. Fields are reused across
// operations with the following semantics:
//   success  - operation outcome
//   code     - errno (TCP/UDP), HTTP status code (HTTP), WS opcode (ws_recv)
//   bytes    - transferred byte count (send/recv) or 0
//   message  - error message / HTTP reason phrase / status text
//   data     - received payload (tcp_recv/udp_recvfrom/http body/ws payload)
//   peer     - UDP recvfrom sender address ("ip:port")
//   headers  - HTTP response headers serialized as "Name: Value\n" lines
ASTRUCT(Name = "NetworkResult", Construct = Default, Ownership = Value, Lifecycle = Value)

struct NetworkResult {
    APROP()
    bool success = false;
    APROP()
    int code = 0;
    APROP()
    int bytes = 0;
    APROP()
    core::pmr::String message{&core::defaultCpuMemoryResource()};
    APROP()
    core::pmr::String data{&core::defaultCpuMemoryResource()};
    APROP()
    core::pmr::String peer{&core::defaultCpuMemoryResource()};
    APROP()
    core::pmr::String headers{&core::defaultCpuMemoryResource()};
};

AENUM(Name = "NetworkOperationState")
enum class NetworkOperationState {
    Pending,
    Ready,
    Failed,
    Destroyed,
};

AENUM(Name = "NetworkConnectionKind")
enum class NetworkConnectionKind {
    Tcp,
    Udp,
    Http,
    WebSocket,
};

AENUM(Name = "NetworkRequestMethod")
enum class NetworkRequestMethod {
    Get,
    Post,
    Put,
    Patch,
    Delete,
};

// Engine-owned connection identity. Native sockets/clients retain their
// backend state privately; scripts observe the stable domain connection
// object rather than a socket or protocol handle.
ACLASS(Name = "NetworkConnection", Ownership = Engine, Lifecycle = Explicit)

class NetworkConnection {
public:
    NetworkConnection(
        NetworkRuntime* owner, NetworkConnectionKind kind, const core::pmr::String& endpoint)
        : owner_(owner), kind_(kind), endpoint_(endpoint),
          reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    ~NetworkConnection();

    const arp::reflection::object& nativeReflectionObject() const { return reflectionObject_; }

    AFUNC(Name = "kind")

    NetworkConnectionKind kind() const { return kind_; }
    AFUNC(Name = "endpoint")

    core::pmr::String endpoint() const { return endpoint_; }
    AFUNC(Name = "is_open")

    bool isOpen() const { return open_; }
    AFUNC(Name = "close")
    bool close();
    AFUNC(Name = "destroy")
    bool destroy();

private:
    friend class NetworkRuntime;
    friend class TCPScriptSocket;
    friend class UDPScriptSocket;
    friend class HttpScriptClient;
    friend class WebSocketScriptClient;

    void setEndpointNative(const core::pmr::String& endpoint) { endpoint_ = endpoint; }

    void setOpenNative(bool open) { open_ = open; }

    NetworkRuntime* owner_ = nullptr;
    NetworkConnectionKind kind_ = NetworkConnectionKind::Tcp;
    core::pmr::String endpoint_{&core::defaultCpuMemoryResource()};
    bool open_ = false;
    arp::reflection::object reflectionObject_;
};

// Engine-owned request description. HTTP and WebSocket operations retain a
// request object so scripts can inspect the exact method/URL/body submitted.
ACLASS(Name = "NetworkRequest", Ownership = Engine, Lifecycle = Explicit)

class NetworkRequest {
public:
    NetworkRequest(NetworkRuntime* owner, NetworkRequestMethod method, const core::pmr::String& url)
        : owner_(owner), method_(method), url_(url),
          reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    ~NetworkRequest();

    const arp::reflection::object& nativeReflectionObject() const { return reflectionObject_; }

    AFUNC(Name = "method")

    NetworkRequestMethod method() const { return method_; }
    AFUNC(Name = "url")

    core::pmr::String url() const { return url_; }
    // Preserve the existing callable getter API rather than synthesizing a property.
    AFUNC(Name = "content_type", MethodOnly)

    core::pmr::String contentType() const { return contentType_; }
    AFUNC(Name = "body", MethodOnly)

    core::pmr::String body() const { return body_; }
    AFUNC(Name = "set_content_type")

    void setContentType(const core::pmr::String& value) { contentType_ = value; }
    AFUNC(Name = "set_body")

    void setBody(const core::pmr::String& value) { body_ = value; }
    AFUNC(Name = "destroy")
    bool destroy();

private:
    friend class NetworkRuntime;
    NetworkRuntime* owner_ = nullptr;
    NetworkRequestMethod method_ = NetworkRequestMethod::Get;
    core::pmr::String url_{&core::defaultCpuMemoryResource()};
    core::pmr::String contentType_{&core::defaultCpuMemoryResource()};
    core::pmr::String body_{&core::defaultCpuMemoryResource()};
    arp::reflection::object reflectionObject_;
};

// Engine-owned message/result object. It is a stable object even when the
// network operation is pending; the operation fills it at completion.
ACLASS(Name = "NetworkMessage", Ownership = Engine, Lifecycle = Explicit)

class NetworkMessage {
public:
    explicit NetworkMessage(NetworkRuntime* owner)
        : owner_(owner), reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    ~NetworkMessage();

    const arp::reflection::object& nativeReflectionObject() const { return reflectionObject_; }

    AFUNC(Name = "code")

    int code() const { return code_; }
    AFUNC(Name = "bytes")

    int bytes() const { return bytes_; }
    AFUNC(Name = "data")

    core::pmr::String data() const { return data_; }
    AFUNC(Name = "peer")

    core::pmr::String peer() const { return peer_; }
    AFUNC(Name = "headers")

    core::pmr::String headers() const { return headers_; }
    AFUNC(Name = "destroy")
    bool destroy();

public:
    friend class NetworkRuntime;
    friend class NetworkOperation;

    // Native completion bridge; intentionally not marked AFUNC/APROP.
    void applyResultNative(const NetworkResult& result)
    {
        code_ = result.code;
        bytes_ = result.bytes;
        data_ = result.data;
        peer_ = result.peer;
        headers_ = result.headers;
    }

private:
    NetworkRuntime* owner_ = nullptr;
    int code_ = 0;
    int bytes_ = 0;
    core::pmr::String data_{&core::defaultCpuMemoryResource()};
    core::pmr::String peer_{&core::defaultCpuMemoryResource()};
    core::pmr::String headers_{&core::defaultCpuMemoryResource()};
    arp::reflection::object reflectionObject_;
};

// Shared completion state outlives the engine object when an in-flight native
// callback races with explicit operation destruction. It is not reflected or
// exposed to scripts; scripts see NetworkOperation's real native object.
struct NetworkOperationData {
    mutable std::mutex mutex;
    NetworkOperationState state = NetworkOperationState::Pending;
    NetworkResult result;
    bool awaitConsumed = false;
    NetworkMessage* message = nullptr;
};

ACLASS(Name = "NetworkOperation", Ownership = Engine, Lifecycle = Explicit)

class NetworkOperation {
public:
    explicit NetworkOperation(NetworkRuntime* owner = nullptr)
        : owner_(owner), data_(core::makeShared<NetworkOperationData>()),
          reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    ~NetworkOperation();

    const arp::reflection::object& nativeReflectionObject() const { return reflectionObject_; }

    // These methods take a snapshot under the operation lock. Async callbacks
    // never mutate reflected fields directly from the IO thread.
    AFUNC(Name = "state")
    NetworkOperationState state();
    AFUNC(Name = "is_complete")
    bool isComplete();
    AFUNC(Name = "is_success")
    bool isSuccess();
    AFUNC(Name = "result")
    NetworkResult result();
    AFUNC(Name = "error")
    core::pmr::String error();
    AFUNC(Name = "connection")

    NetworkConnection* connection() const { return connection_; }
    AFUNC(Name = "request")

    NetworkRequest* request() const { return request_; }
    AFUNC(Name = "message")

    NetworkMessage* message() const { return message_; }
    AFUNC(Name = "destroy")
    bool destroy();

    // Native-only bridge used by NetworkRuntime and async adapters. This is
    // deliberately not marked AFUNC/APROP and cannot enter the script ABI.
    std::shared_ptr<NetworkOperationData> sharedData() const { return data_; }

    void completeNative(const NetworkResult& result);
    Value makeAwaitableNative();

    void attachConnectionNative(NetworkConnection* value) { connection_ = value; }

    void attachRequestNative(NetworkRequest* value) { request_ = value; }

    void attachMessageNative(NetworkMessage* value)
    {
        message_ = value;
        if (data_ != nullptr)
            data_->message = value;
    }

private:
    friend class NetworkRuntime;
    NetworkRuntime* owner_ = nullptr;
    std::shared_ptr<NetworkOperationData> data_;
    arp::reflection::object reflectionObject_;
    NetworkConnection* connection_ = nullptr;
    NetworkRequest* request_ = nullptr;
    NetworkMessage* message_ = nullptr;
};

using TcpNativeSocket = network::tcp::socket<network::ipv4>;
using UdpNativeSocket = network::udp::socket<network::ipv4>;

struct TCPScriptSocketState {
    explicit TCPScriptSocketState(network::io_context* ctx)
        : socket(core::makeUnique<TcpNativeSocket>(ctx))
    {}

    core::UniquePtr<TcpNativeSocket> socket;
    bool open = false;
};

struct UDPScriptSocketState {
    explicit UDPScriptSocketState(network::io_context* ctx)
        : socket(core::makeUnique<UdpNativeSocket>(ctx))
    {}

    core::UniquePtr<UdpNativeSocket> socket;
    bool open = false;
};

struct WebSocketScriptClientState {
    explicit WebSocketScriptClientState(network::io_context* ctx)
        : client(core::makeUnique<network::websocket::Client>(ctx))
    {}

    core::UniquePtr<network::websocket::Client> client;
    bool connected = false;
};

// Engine-owned TCP object. It is created by NetworkRuntime and returned to
// scripts as the same canonical reflection::object retained by the runtime.
ACLASS(Name = "TcpSocket", Ownership = Engine, Lifecycle = Explicit)

class TCPScriptSocket {
public:
    TCPScriptSocket() : TCPScriptSocket(currentNetworkIoContext()) {}

    explicit TCPScriptSocket(network::io_context* ctx)
        : ctx_(ctx), state_(core::makeShared<TCPScriptSocketState>(ctx)),
          reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    TCPScriptSocket(network::io_context* ctx, NetworkRuntime* owner) : TCPScriptSocket(ctx)
    {
        owner_ = owner;
    }

    ~TCPScriptSocket();

    const arp::reflection::object& nativeReflectionObject() const { return reflectionObject_; }

    // async: await network.TCPSocket.connect(host, port) -> NetworkResult
    AFUNC(Async)
    Value connect(const core::pmr::String& host, int port);
    // async: await sock.send(data) -> NetworkResult{ bytes }
    AFUNC(Async)
    Value send(const core::pmr::String& data);
    // async: await sock.recv(maxBytes) -> NetworkResult{ bytes, data }
    AFUNC(Async)
    Value recv(int maxBytes);
    // sync: sock.close() -> NetworkResult
    AFUNC()
    NetworkResult close();

    AFUNC(Name = "is_open")

    bool isOpen() { return state_ != nullptr && state_->open; }
    AFUNC(Name = "connection")

    NetworkConnection* connection() const { return connection_; }

    AFUNC(Name = "destroy")
    bool destroy();

private:
    friend class NetworkRuntime;
    network::io_context* ctx_ = nullptr;
    std::shared_ptr<TCPScriptSocketState> state_;
    NetworkRuntime* owner_ = nullptr;
    NetworkConnection* connection_ = nullptr;
    arp::reflection::object reflectionObject_;
};

// Engine-owned UDP object.
ACLASS(Name = "UdpSocket", Ownership = Engine, Lifecycle = Explicit)

class UDPScriptSocket {
public:
    UDPScriptSocket() : UDPScriptSocket(currentNetworkIoContext()) {}

    explicit UDPScriptSocket(network::io_context* ctx)
        : ctx_(ctx), state_(core::makeShared<UDPScriptSocketState>(ctx)),
          reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    UDPScriptSocket(network::io_context* ctx, NetworkRuntime* owner) : UDPScriptSocket(ctx)
    {
        owner_ = owner;
    }

    ~UDPScriptSocket();

    const arp::reflection::object& nativeReflectionObject() const { return reflectionObject_; }

    // sync: sock.bind(addr, port) -> NetworkResult{ success }
    AFUNC()
    NetworkResult bind(const core::pmr::String& addr, int port);
    // async: await sock.sendto(host, port, data) -> NetworkResult{ bytes }
    AFUNC(Async)
    Value sendto(const core::pmr::String& host, int port, const core::pmr::String& data);
    // async: await sock.recvfrom(maxBytes) -> NetworkResult{ bytes, data, peer }
    AFUNC(Async)
    Value recvfrom(int maxBytes);
    // sync: sock.close() -> NetworkResult
    AFUNC()
    NetworkResult close();

    AFUNC(Name = "is_open")

    bool isOpen() { return state_ != nullptr && state_->open; }
    AFUNC(Name = "connection")

    NetworkConnection* connection() const { return connection_; }

    AFUNC(Name = "destroy")
    bool destroy();

private:
    friend class NetworkRuntime;
    network::io_context* ctx_ = nullptr;
    std::shared_ptr<UDPScriptSocketState> state_;
    NetworkRuntime* owner_ = nullptr;
    NetworkConnection* connection_ = nullptr;
    arp::reflection::object reflectionObject_;
};

// Engine-owned HTTP client object.
ACLASS(Name = "HttpClient", Ownership = Engine, Lifecycle = Explicit)

class HttpScriptClient {
public:
    HttpScriptClient() : HttpScriptClient(currentNetworkIoContext()) {}

    explicit HttpScriptClient(network::io_context* ctx)
        : ctx_(ctx), reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    HttpScriptClient(network::io_context* ctx, NetworkRuntime* owner) : HttpScriptClient(ctx)
    {
        owner_ = owner;
    }

    ~HttpScriptClient();

    const arp::reflection::object& nativeReflectionObject() const { return reflectionObject_; }

    // async: await client.get(url) -> NetworkResult{ code, data(body), headers, message(reason) }
    AFUNC(Async)
    Value get(const core::pmr::String& url);
    // async: await client.post(url, contentType, body) -> NetworkResult{ code, data, headers, message }
    AFUNC(Async)
    Value post(const core::pmr::String& url,
        const core::pmr::String& contentType,
        const core::pmr::String& body);
    AFUNC(Name = "set_header")
    NetworkResult setHeader(const core::pmr::String& name, const core::pmr::String& value);
    // TLS defaults to system trust. The non-default modes are explicit so
    // scripts cannot silently downgrade certificate verification.
    AFUNC(Name = "set_tls_verification")
    NetworkResult setTlsVerification(const core::pmr::String& mode);
    AFUNC(Name = "set_trusted_certificate")
    NetworkResult setTrustedCertificate(const core::pmr::String& pem);
    AFUNC(Name = "has_context")

    bool hasContext() { return ctx_ != nullptr; }
    AFUNC(Name = "connection")

    NetworkConnection* connection() const { return connection_; }

    AFUNC(Name = "destroy")
    bool destroy();

private:
    friend class NetworkRuntime;
    network::io_context* ctx_ = nullptr;
    NetworkRuntime* owner_ = nullptr;
    NetworkConnection* connection_ = nullptr;
    arp::reflection::object reflectionObject_;
    network::http::HeaderList headers_;
    network::tls::CertificateVerification tlsVerification_ =
        network::tls::CertificateVerification::system_trust;
    core::pmr::String trustedCertificate_{&core::defaultCpuMemoryResource()};
};

// Engine-owned WebSocket client object.
ACLASS(Name = "WebSocketClient", Ownership = Engine, Lifecycle = Explicit)

class WebSocketScriptClient {
public:
    WebSocketScriptClient() : WebSocketScriptClient(currentNetworkIoContext()) {}

    explicit WebSocketScriptClient(network::io_context* ctx)
        : ctx_(ctx), state_(core::makeShared<WebSocketScriptClientState>(ctx)),
          reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    WebSocketScriptClient(network::io_context* ctx, NetworkRuntime* owner)
        : WebSocketScriptClient(ctx)
    {
        owner_ = owner;
    }

    ~WebSocketScriptClient();

    const arp::reflection::object& nativeReflectionObject() const { return reflectionObject_; }

    // async: await client.connect(url) -> NetworkResult{ success }
    AFUNC(Async)
    Value connect(const core::pmr::String& url);
    // async: await client.send_text(text) -> NetworkResult{ bytes }
    AFUNC(Name = "send_text", Async)
    Value sendText(const core::pmr::String& text);
    // async: await client.recv() -> NetworkResult{ code(opcode), data(payload), bytes }
    AFUNC(Async)
    Value recv();
    // async: await client.close(code, reason) -> NetworkResult
    AFUNC(Async)
    Value close(int closeCode, const core::pmr::String& reason);
    AFUNC(Name = "is_connected")

    bool isConnected() { return state_ != nullptr && state_->connected; }
    AFUNC(Name = "connection")

    NetworkConnection* connection() const { return connection_; }
    AFUNC(Name = "set_tls_verification")
    NetworkResult setTlsVerification(const core::pmr::String& mode);
    AFUNC(Name = "set_trusted_certificate")
    NetworkResult setTrustedCertificate(const core::pmr::String& pem);

    AFUNC(Name = "destroy")
    bool destroy();

private:
    friend class NetworkRuntime;
    network::io_context* ctx_ = nullptr;
    std::shared_ptr<WebSocketScriptClientState> state_;
    NetworkRuntime* owner_ = nullptr;
    NetworkConnection* connection_ = nullptr;
    arp::reflection::object reflectionObject_;
    network::tls::CertificateVerification tlsVerification_ =
        network::tls::CertificateVerification::system_trust;
    core::pmr::String trustedCertificate_{&core::defaultCpuMemoryResource()};
};

// Runtime-owned object registry. The registry itself is the only module
// object bound during Plugin registration; all network object methods are
// generated direct calls on the returned canonical native references.
ASTRUCT(Name = "HttpServerConfig", Construct = Default, Ownership = Value, Lifecycle = Value)

struct HttpScriptServerConfig {
    APROP()
    int max_connections = 16;
    APROP()
    int max_pending_requests = 16;
    APROP()
    int max_routes = 64;
    APROP()
    int read_timeout_ms = 10000;
    APROP()
    int response_timeout_ms = 30000;
    APROP()
    int write_timeout_ms = 10000;
    APROP()
    int read_buffer_size = 4096;
    APROP()
    int max_header_bytes = 8192;
    APROP()
    int max_header_count = 64;
    APROP()
    int max_body_bytes = 1048576;
    APROP()
    int max_buffer_bytes = 2097152;
};

ASTRUCT(Name = "HttpResponse", Construct = Default, Ownership = Value, Lifecycle = Value)

struct HttpScriptResponse {
    APROP()
    int status = 200;
    APROP()
    core::pmr::String reason{"OK", &core::defaultCpuMemoryResource()};
    APROP()
    core::pmr::String body{&core::defaultCpuMemoryResource()};
    APROP()
    core::pmr::String content_type{"text/plain; charset=utf-8", &core::defaultCpuMemoryResource()};
    AFUNC(Name = "add_header")
    NetworkResult addHeader(const core::pmr::String& name, const core::pmr::String& value);
    network::http::HeaderList headers;
};

struct HttpServerScriptState;

ACLASS(Name = "HttpServerRequest", Ownership = Engine, Lifecycle = Explicit)

class HttpScriptIncomingRequest {
public:
    HttpScriptIncomingRequest(NetworkRuntime* owner,
        core::SharedPtr<HttpServerScriptState> state,
        network::http::IncomingRequestPtr request);
    ~HttpScriptIncomingRequest();

    const reflection::object& nativeReflectionObject() const { return reflectionObject_; }
    AFUNC(Name = "method")
    core::pmr::String method() const;
    AFUNC(Name = "target")
    core::pmr::String target() const;
    AFUNC(Name = "path")
    core::pmr::String path() const;
    AFUNC(Name = "query")
    core::pmr::String query() const;
    AFUNC(Name = "headers")
    core::pmr::String headers() const;
    AFUNC(Name = "header")
    core::pmr::String header(const core::pmr::String& name) const;
    AFUNC(Name = "body")
    core::pmr::String body() const;
    AFUNC(Name = "route_pattern")
    core::pmr::String routePattern() const;
    AFUNC(Name = "param")
    core::pmr::String param(const core::pmr::String& name) const;
    AFUNC(Name = "is_active")
    bool isActive() const;
    // Calling returns a ScriptAwaitable synchronously; only await suspends.
    AFUNC(Name = "respond")
    Value respond(const HttpScriptResponse& response);
    AFUNC(Name = "destroy")
    bool destroy();

private:
    NetworkRuntime* owner_;
    core::SharedPtr<HttpServerScriptState> state_;
    network::http::IncomingRequestPtr request_;
    reflection::object reflectionObject_;
};

ACLASS(Name = "HttpServer", Ownership = Engine, Lifecycle = Explicit)

class HttpScriptServer {
public:
    HttpScriptServer(
        network::io_context* context, NetworkRuntime* owner, const HttpScriptServerConfig& config);
    ~HttpScriptServer();

    const reflection::object& nativeReflectionObject() const { return reflectionObject_; }
    AFUNC(Name = "route")
    NetworkResult route(const core::pmr::String& method, const core::pmr::String& pattern);
    AFUNC(Name = "start")
    NetworkResult start(const core::pmr::String& host, int port);
    AFUNC(Name = "local_endpoint")
    core::pmr::String localEndpoint() const;
    AFUNC(Name = "local_port")
    int localPort() const;
    AFUNC(Name = "is_running")
    bool isRunning() const;
    AFUNC(Name = "is_stopped")
    bool isStopped() const;
    // Calling returns a ScriptAwaitable synchronously; only await suspends.
    AFUNC(Name = "next_request")
    Value nextRequest();
    AFUNC(Name = "stop")
    NetworkResult stop();
    AFUNC(Name = "destroy")
    bool destroy();

private:
    NetworkRuntime* owner_;
    core::SharedPtr<HttpServerScriptState> state_;
    reflection::object reflectionObject_;
};

ACLASS(Name = "NetworkRuntime", Export = "network", Ownership = Engine, Lifecycle = Explicit)

class NetworkRuntime {
public:
    explicit NetworkRuntime(network::io_context* context)
        : context_(context), reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    ~NetworkRuntime();

    const arp::reflection::object& nativeReflectionObject() const { return reflectionObject_; }

    AFUNC(Name = "has_context")

    bool hasContext() { return context_ != nullptr; }
    AFUNC(Name = "create_tcp_socket")
    TCPScriptSocket* createTcpSocket();
    AFUNC(Name = "create_udp_socket")
    UDPScriptSocket* createUdpSocket();
    AFUNC(Name = "create_http_client")
    HttpScriptClient* createHttpClient();
    AFUNC(Name = "create_http_server")
    HttpScriptServer* createHttpServer();
    AFUNC(Name = "create_http_server_with_config")
    HttpScriptServer* createHttpServerWithConfig(const HttpScriptServerConfig& config);
    AFUNC(Name = "create_http_response")
    HttpScriptResponse createHttpResponse();
    AFUNC(Name = "create_websocket_client")
    WebSocketScriptClient* createWebSocketClient();
    AFUNC(Name = "create_connection")
    NetworkConnection* createConnection(
        NetworkConnectionKind kind, const core::pmr::String& endpoint);
    AFUNC(Name = "create_request")
    NetworkRequest* createRequest(NetworkRequestMethod method, const core::pmr::String& url);
    AFUNC(Name = "create_message")
    NetworkMessage* createMessage();

    // Native-only operation factory used by async domain objects. It is not
    // an additional script service or a script-visible factory method.
    NetworkOperation* beginOperation();
    HttpScriptIncomingRequest* createHttpServerRequest(
        core::SharedPtr<HttpServerScriptState> state, network::http::IncomingRequestPtr request);

private:
    friend class NetworkOperation;
    friend class NetworkConnection;
    friend class NetworkRequest;
    friend class NetworkMessage;
    friend class TCPScriptSocket;
    friend class UDPScriptSocket;
    friend class HttpScriptClient;
    friend class WebSocketScriptClient;
    friend class HttpScriptServer;
    friend class HttpScriptIncomingRequest;

    bool destroyTcpSocket(TCPScriptSocket* value);
    bool destroyUdpSocket(UDPScriptSocket* value);
    bool destroyHttpClient(HttpScriptClient* value);
    bool destroyHttpServer(HttpScriptServer* value);
    bool destroyHttpServerRequest(HttpScriptIncomingRequest* value);
    bool destroyWebSocketClient(WebSocketScriptClient* value);
    bool destroyOperation(NetworkOperation* value);
    bool destroyConnection(NetworkConnection* value);
    bool destroyRequest(NetworkRequest* value);
    bool destroyMessage(NetworkMessage* value);
    bool operationReferences(const void* value) const;

    network::io_context* context_ = nullptr;
    core::pmr::Vector<core::UniquePtr<TCPScriptSocket>> tcpSockets_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<UDPScriptSocket>> udpSockets_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<HttpScriptClient>> httpClients_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<HttpScriptServer>> httpServers_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<HttpScriptIncomingRequest>> httpServerRequests_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<WebSocketScriptClient>> webSocketClients_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<NetworkOperation>> operations_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<NetworkConnection>> connections_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<NetworkRequest>> requests_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<NetworkMessage>> messages_{&core::defaultCpuMemoryResource()};
    arp::reflection::object reflectionObject_;
};

} // namespace arp::scriptvm
