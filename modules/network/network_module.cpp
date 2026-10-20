#include "network/network_module.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace arp::reflection::generated {
bool register_arp_network_reflection(
    arp::scriptvm::ModuleRegistration*, const arp::reflection::object*);
} // namespace arp::reflection::generated

namespace arp::scriptvm {

// ---------------------------------------------------------------------------
// Async state + awaitable bridge (shared by all async methods)
// ---------------------------------------------------------------------------

namespace {

// Reflection class metadata is process-global, while a script module binds to
// one host-owned io_context. Constructors therefore resolve the most recently
// registered context instead of capturing the first registration forever.
std::atomic<network::io_context*> activeNetworkIoContext{nullptr};

void completeOperation(
    const std::shared_ptr<NetworkOperationData>& data, const NetworkResult& result)
{
    std::lock_guard lock(data->mutex);
    if (data->state == NetworkOperationState::Destroyed)
        return;
    data->result = result;
    if (data->message != nullptr)
        data->message->applyResultNative(result);
    data->state = result.success ? NetworkOperationState::Ready : NetworkOperationState::Failed;
}

Value makeNetworkAwaitable(NetworkOperation& operation)
{
    auto state = operation.sharedData();
    ScriptAwaitable awaitable;
    awaitable.operation = operation.nativeReflectionObject();
    awaitable.ready = [state] {
        std::lock_guard lock(state->mutex);
        return state->state != NetworkOperationState::Pending;
    };
    awaitable.poll = [state](Value& value, core::pmr::String& diagnostics) {
        std::lock_guard lock(state->mutex);
        if (state->state == NetworkOperationState::Pending)
            return ScriptAwaitStatus::Pending;
        if (state->state == NetworkOperationState::Destroyed) {
            diagnostics = "network operation was destroyed";
            return ScriptAwaitStatus::Failed;
        }
        if (state->state == NetworkOperationState::Failed) {
            diagnostics = state->result.message;
            state->awaitConsumed = true;
            return ScriptAwaitStatus::Failed;
        }
        if (!state->awaitConsumed) {
            value = Value{state->result};
            state->awaitConsumed = true;
        }
        return ScriptAwaitStatus::Ready;
    };
    return Value{std::move(awaitable)};
}

Value immediateFailureValue(const char* message)
{
    NetworkResult r;
    r.success = false;
    r.message = core::pmr::String(message, &core::defaultCpuMemoryResource());
    return Value{std::move(r)};
}

Value failedNetworkAwaitable(NetworkRuntime* runtime, const char* message)
{
    if (runtime == nullptr)
        return immediateFailureValue(message);
    auto* operation = runtime->beginOperation();
    if (operation == nullptr)
        return immediateFailureValue(message);
    NetworkResult result;
    result.success = false;
    result.message = core::pmr::String(message, &core::defaultCpuMemoryResource());
    operation->attachMessageNative(runtime->createMessage());
    completeOperation(operation->sharedData(), result);
    return makeNetworkAwaitable(*operation);
}

void attachOperationMetadata(NetworkRuntime* runtime,
    NetworkOperation* operation,
    NetworkConnection* connection = nullptr,
    NetworkRequest* request = nullptr)
{
    if (runtime == nullptr || operation == nullptr)
        return;
    operation->attachConnectionNative(connection);
    operation->attachRequestNative(request);
    operation->attachMessageNative(runtime->createMessage());
}

Value immediateOk()
{
    NetworkResult r;
    r.success = true;
    r.message = core::pmr::String("ok", &core::defaultCpuMemoryResource());
    return Value{std::move(r)};
}

core::pmr::String serializeHeaders(const network::http::HeaderList& headers)
{
    core::pmr::String out{&core::defaultCpuMemoryResource()};
    for (std::size_t i = 0; i < headers.size(); ++i) {
        const auto& h = headers.at(i);
        out.append(h.name().data(), h.name().size());
        out.append(": ");
        out.append(h.value().data(), h.value().size());
        out.append("\n");
    }
    return out;
}

core::pmr::String byteBufferToString(const network::byte_buffer& buf)
{
    return core::pmr::String(
        reinterpret_cast<const char*>(buf.data()), buf.size(), &core::defaultCpuMemoryResource());
}

core::pmr::String errorMessage(const network::error_code& ec)
{
    const auto& msg = ec.message();
    return core::pmr::String(msg.data(), msg.size(), &core::defaultCpuMemoryResource());
}

bool getStringArg(const Value& v, core::pmr::String& out)
{
    return tryGetString(v, out);
}

bool getIntArg(const Value& v, int& out)
{
    return tryGetInt(v, out);
}

bool parseTlsVerification(
    const core::pmr::String& mode, network::tls::CertificateVerification& verification)
{
    if (mode == "system_trust") {
        verification = network::tls::CertificateVerification::system_trust;
        return true;
    }
    if (mode == "explicit_trust") {
        verification = network::tls::CertificateVerification::explicit_trust;
        return true;
    }
    if (mode == "disabled_for_test") {
        verification = network::tls::CertificateVerification::disabled_for_test;
        return true;
    }
    return false;
}

NetworkResult tlsConfigOk()
{
    NetworkResult result;
    result.success = true;
    result.message = core::pmr::String("ok", &core::defaultCpuMemoryResource());
    return result;
}

NetworkResult tlsConfigFailure(const char* message)
{
    NetworkResult result;
    result.message = core::pmr::String(message, &core::defaultCpuMemoryResource());
    return result;
}

} // namespace

network::io_context* currentNetworkIoContext() noexcept
{
    return activeNetworkIoContext.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// NetworkOperation
// ---------------------------------------------------------------------------

NetworkOperation::~NetworkOperation()
{
    if (data_ != nullptr) {
        std::lock_guard lock(data_->mutex);
        data_->state = NetworkOperationState::Destroyed;
    }
    reflectionObject_.invalidate_native_reference();
}

NetworkConnection::~NetworkConnection()
{
    reflectionObject_.invalidate_native_reference();
}

bool NetworkConnection::close()
{
    open_ = false;
    return true;
}

bool NetworkConnection::destroy()
{
    return owner_ != nullptr && owner_->destroyConnection(this);
}

NetworkRequest::~NetworkRequest()
{
    reflectionObject_.invalidate_native_reference();
}

bool NetworkRequest::destroy()
{
    return owner_ != nullptr && owner_->destroyRequest(this);
}

NetworkMessage::~NetworkMessage()
{
    reflectionObject_.invalidate_native_reference();
}

bool NetworkMessage::destroy()
{
    return owner_ != nullptr && owner_->destroyMessage(this);
}

void NetworkOperation::completeNative(const NetworkResult& result)
{
    if (message_ != nullptr)
        message_->applyResultNative(result);
    completeOperation(data_, result);
}

Value NetworkOperation::makeAwaitableNative()
{
    return makeNetworkAwaitable(*this);
}

NetworkOperationState NetworkOperation::state()
{
    std::lock_guard lock(data_->mutex);
    return data_->state;
}

bool NetworkOperation::isComplete()
{
    std::lock_guard lock(data_->mutex);
    return data_->state != NetworkOperationState::Pending;
}

bool NetworkOperation::isSuccess()
{
    std::lock_guard lock(data_->mutex);
    return data_->state == NetworkOperationState::Ready && data_->result.success;
}

NetworkResult NetworkOperation::result()
{
    std::lock_guard lock(data_->mutex);
    return data_->result;
}

core::pmr::String NetworkOperation::error()
{
    std::lock_guard lock(data_->mutex);
    return data_->result.message;
}

bool NetworkOperation::destroy()
{
    return owner_ != nullptr && owner_->destroyOperation(this);
}

// ---------------------------------------------------------------------------
// TCPScriptSocket
// ---------------------------------------------------------------------------

TCPScriptSocket::~TCPScriptSocket()
{
    network::error_code ignored;
    if (state_ != nullptr && state_->socket != nullptr && state_->open) {
        state_->socket->close(ignored);
        state_->open = false;
    }
    reflectionObject_.invalidate_native_reference();
}

UDPScriptSocket::~UDPScriptSocket()
{
    network::error_code ignored;
    if (state_ != nullptr && state_->socket != nullptr && state_->open) {
        state_->socket->close(ignored);
        state_->open = false;
    }
    reflectionObject_.invalidate_native_reference();
}

HttpScriptClient::~HttpScriptClient()
{
    reflectionObject_.invalidate_native_reference();
}

WebSocketScriptClient::~WebSocketScriptClient()
{
    if (state_ != nullptr) {
        state_->connected = false;
        state_->client.reset();
    }
    reflectionObject_.invalidate_native_reference();
}

bool TCPScriptSocket::destroy()
{
    return owner_ != nullptr && owner_->destroyTcpSocket(this);
}

bool UDPScriptSocket::destroy()
{
    return owner_ != nullptr && owner_->destroyUdpSocket(this);
}

bool HttpScriptClient::destroy()
{
    return owner_ != nullptr && owner_->destroyHttpClient(this);
}

bool WebSocketScriptClient::destroy()
{
    return owner_ != nullptr && owner_->destroyWebSocketClient(this);
}

NetworkRuntime::~NetworkRuntime()
{
    httpServers_.clear();
    httpServerRequests_.clear();
    operations_.clear();
    messages_.clear();
    requests_.clear();
    connections_.clear();
    webSocketClients_.clear();
    httpClients_.clear();
    udpSockets_.clear();
    tcpSockets_.clear();
    reflectionObject_.invalidate_native_reference();
}

NetworkOperation* NetworkRuntime::beginOperation()
{
    if (context_ == nullptr)
        return nullptr;
    auto value = core::makeUnique<NetworkOperation>(this);
    auto* result = value.get();
    operations_.push_back(std::move(value));
    return result;
}

NetworkConnection* NetworkRuntime::createConnection(
    NetworkConnectionKind kind, const core::pmr::String& endpoint)
{
    if (context_ == nullptr)
        return nullptr;
    auto value = core::makeUnique<NetworkConnection>(this, kind, endpoint);
    auto* result = value.get();
    connections_.push_back(std::move(value));
    return result;
}

NetworkRequest* NetworkRuntime::createRequest(
    NetworkRequestMethod method, const core::pmr::String& url)
{
    if (context_ == nullptr)
        return nullptr;
    auto value = core::makeUnique<NetworkRequest>(this, method, url);
    auto* result = value.get();
    requests_.push_back(std::move(value));
    return result;
}

NetworkMessage* NetworkRuntime::createMessage()
{
    if (context_ == nullptr)
        return nullptr;
    auto value = core::makeUnique<NetworkMessage>(this);
    auto* result = value.get();
    messages_.push_back(std::move(value));
    return result;
}

TCPScriptSocket* NetworkRuntime::createTcpSocket()
{
    if (context_ == nullptr)
        return nullptr;
    auto value = core::makeUnique<TCPScriptSocket>(context_, this);
    auto* result = value.get();
    tcpSockets_.push_back(std::move(value));
    result->connection_ = createConnection(
        NetworkConnectionKind::Tcp, core::pmr::String("", &core::defaultCpuMemoryResource()));
    return result;
}

UDPScriptSocket* NetworkRuntime::createUdpSocket()
{
    if (context_ == nullptr)
        return nullptr;
    auto value = core::makeUnique<UDPScriptSocket>(context_, this);
    auto* result = value.get();
    udpSockets_.push_back(std::move(value));
    result->connection_ = createConnection(
        NetworkConnectionKind::Udp, core::pmr::String("", &core::defaultCpuMemoryResource()));
    return result;
}

HttpScriptClient* NetworkRuntime::createHttpClient()
{
    if (context_ == nullptr)
        return nullptr;
    auto value = core::makeUnique<HttpScriptClient>(context_, this);
    auto* result = value.get();
    httpClients_.push_back(std::move(value));
    result->connection_ = createConnection(
        NetworkConnectionKind::Http, core::pmr::String("", &core::defaultCpuMemoryResource()));
    return result;
}

WebSocketScriptClient* NetworkRuntime::createWebSocketClient()
{
    if (context_ == nullptr)
        return nullptr;
    auto value = core::makeUnique<WebSocketScriptClient>(context_, this);
    auto* result = value.get();
    webSocketClients_.push_back(std::move(value));
    result->connection_ = createConnection(
        NetworkConnectionKind::WebSocket, core::pmr::String("", &core::defaultCpuMemoryResource()));
    return result;
}

bool NetworkRuntime::destroyTcpSocket(TCPScriptSocket* value)
{
    const auto it =
        std::find_if(tcpSockets_.begin(), tcpSockets_.end(), [value](const auto& candidate) {
            return candidate.get() == value;
        });
    if (it == tcpSockets_.end())
        return false;
    tcpSockets_.erase(it);
    return true;
}

bool NetworkRuntime::destroyUdpSocket(UDPScriptSocket* value)
{
    const auto it =
        std::find_if(udpSockets_.begin(), udpSockets_.end(), [value](const auto& candidate) {
            return candidate.get() == value;
        });
    if (it == udpSockets_.end())
        return false;
    udpSockets_.erase(it);
    return true;
}

bool NetworkRuntime::destroyHttpClient(HttpScriptClient* value)
{
    const auto it =
        std::find_if(httpClients_.begin(), httpClients_.end(), [value](const auto& candidate) {
            return candidate.get() == value;
        });
    if (it == httpClients_.end())
        return false;
    httpClients_.erase(it);
    return true;
}

bool NetworkRuntime::destroyWebSocketClient(WebSocketScriptClient* value)
{
    const auto it = std::find_if(
        webSocketClients_.begin(), webSocketClients_.end(), [value](const auto& candidate) {
            return candidate.get() == value;
        });
    if (it == webSocketClients_.end())
        return false;
    webSocketClients_.erase(it);
    return true;
}

bool NetworkRuntime::destroyOperation(NetworkOperation* value)
{
    const auto it =
        std::find_if(operations_.begin(), operations_.end(), [value](const auto& candidate) {
            return candidate.get() == value;
        });
    if (it == operations_.end())
        return false;
    operations_.erase(it);
    return true;
}

bool NetworkRuntime::operationReferences(const void* value) const
{
    return std::any_of(operations_.begin(), operations_.end(), [value](const auto& candidate) {
        return candidate != nullptr
            && (candidate->connection_ == value || candidate->request_ == value
                || candidate->message_ == value);
    });
}

bool NetworkRuntime::destroyConnection(NetworkConnection* value)
{
    if (value == nullptr || operationReferences(value))
        return false;
    const auto usedBySocket =
        std::any_of(tcpSockets_.begin(),
            tcpSockets_.end(),
            [value](const auto& candidate) {
                return candidate != nullptr && candidate->connection_ == value;
            })
        || std::any_of(udpSockets_.begin(),
            udpSockets_.end(),
            [value](const auto& candidate) {
                return candidate != nullptr && candidate->connection_ == value;
            })
        || std::any_of(httpClients_.begin(),
            httpClients_.end(),
            [value](const auto& candidate) {
                return candidate != nullptr && candidate->connection_ == value;
            })
        || std::any_of(
            webSocketClients_.begin(), webSocketClients_.end(), [value](const auto& candidate) {
                return candidate != nullptr && candidate->connection_ == value;
            });
    if (usedBySocket)
        return false;
    const auto it =
        std::find_if(connections_.begin(), connections_.end(), [value](const auto& candidate) {
            return candidate.get() == value;
        });
    if (it == connections_.end())
        return false;
    connections_.erase(it);
    return true;
}

bool NetworkRuntime::destroyRequest(NetworkRequest* value)
{
    if (value == nullptr || operationReferences(value))
        return false;
    const auto it =
        std::find_if(requests_.begin(), requests_.end(), [value](const auto& candidate) {
            return candidate.get() == value;
        });
    if (it == requests_.end())
        return false;
    requests_.erase(it);
    return true;
}

bool NetworkRuntime::destroyMessage(NetworkMessage* value)
{
    if (value == nullptr || operationReferences(value))
        return false;
    const auto it =
        std::find_if(messages_.begin(), messages_.end(), [value](const auto& candidate) {
            return candidate.get() == value;
        });
    if (it == messages_.end())
        return false;
    messages_.erase(it);
    return true;
}

Value TCPScriptSocket::connect(const core::pmr::String& host, int port)
{
    if (ctx_ == nullptr || state_ == nullptr || state_->socket == nullptr) {
        return failedNetworkAwaitable(owner_, "TCPSocket: no io_context");
    }
    if (state_->open) {
        return failedNetworkAwaitable(owner_, "TCPSocket: already open");
    }

    auto* operation = owner_ != nullptr ? owner_->beginOperation() : nullptr;
    if (operation == nullptr)
        return immediateFailureValue("TCPSocket: no operation owner");
    attachOperationMetadata(owner_, operation, connection_);
    auto state = operation->sharedData();
    auto socketState = state_;
    auto& socket = *socketState->socket;
    if (connection_ != nullptr) {
        core::pmr::String endpoint(host, &core::defaultCpuMemoryResource());
        endpoint += ":";
        endpoint += std::to_string(port).c_str();
        connection_->setEndpointNative(endpoint);
    }
    network::error_code ec;
    if (socket.open(ec) != 0) {
        NetworkResult r;
        r.code = ec.value();
        r.message = errorMessage(ec);
        completeOperation(state, r);
        return makeNetworkAwaitable(*operation);
    }
    if (socket.bind(ec, network::endpoint::anyaddr<network::ipv4>(0)) != 0) {
        const network::error_code bindEc = ec;
        network::error_code ignored;
        socket.close(ignored);
        NetworkResult r;
        r.code = bindEc.value();
        r.message = errorMessage(bindEc);
        completeOperation(state, r);
        return makeNetworkAwaitable(*operation);
    }

    socketState->open = true;
    network::endpoint remote(host, static_cast<network::uint16>(port));
    auto* connection = connection_;
    socket.async_connect(
        remote, [state, socketState, connection](const network::error_code& connectEc) {
            NetworkResult r;
            r.success = !connectEc;
            r.code = connectEc ? connectEc.value() : 0;
            r.message = connectEc ? errorMessage(connectEc)
                                  : core::pmr::String("ok", &core::defaultCpuMemoryResource());
            if (connectEc) {
                network::error_code ignored;
                socketState->socket->close(ignored);
                socketState->open = false;
            }
            if (connection != nullptr)
                connection->setOpenNative(!connectEc);
            completeOperation(state, r);
        });
    return makeNetworkAwaitable(*operation);
}

Value TCPScriptSocket::send(const core::pmr::String& data)
{
    if (state_ == nullptr || state_->socket == nullptr || !state_->open) {
        return failedNetworkAwaitable(owner_, "TCPSocket: not connected");
    }

    auto* operation = owner_ != nullptr ? owner_->beginOperation() : nullptr;
    if (operation == nullptr)
        return immediateFailureValue("TCPSocket: no operation owner");
    attachOperationMetadata(owner_, operation, connection_);
    auto state = operation->sharedData();
    auto socketState = state_;
    network::byte_buffer buffer{&core::defaultCpuMemoryResource()};
    buffer.resize(data.size());
    if (!data.empty()) {
        std::memcpy(buffer.data(), data.data(), data.size());
    }
    socketState->socket->async_send(
        buffer, [state, socketState](const network::error_code& ec, std::size_t bytes) {
            NetworkResult r;
            r.success = !ec;
            r.code = ec ? ec.value() : 0;
            r.bytes = static_cast<int>(bytes);
            r.message =
                ec ? errorMessage(ec) : core::pmr::String("ok", &core::defaultCpuMemoryResource());
            completeOperation(state, r);
        });
    return makeNetworkAwaitable(*operation);
}

Value TCPScriptSocket::recv(int maxBytes)
{
    if (state_ == nullptr || state_->socket == nullptr || !state_->open) {
        return failedNetworkAwaitable(owner_, "TCPSocket: not connected");
    }
    if (maxBytes <= 0) {
        return failedNetworkAwaitable(owner_, "TCPSocket.recv: maxBytes must be positive");
    }

    auto* operation = owner_ != nullptr ? owner_->beginOperation() : nullptr;
    if (operation == nullptr)
        return immediateFailureValue("TCPSocket: no operation owner");
    attachOperationMetadata(owner_, operation, connection_);
    auto state = operation->sharedData();
    auto socketState = state_;
    auto buffer = core::makeShared<network::byte_buffer>(
        static_cast<std::size_t>(maxBytes), static_cast<std::uint8_t>(0));
    socketState->socket->async_recv(
        *buffer, [state, buffer, socketState](const network::error_code& ec, std::size_t bytes) {
            NetworkResult r;
            r.success = !ec && bytes > 0;
            r.code = ec ? ec.value() : 0;
            r.bytes = static_cast<int>(bytes);
            r.message =
                ec ? errorMessage(ec) : core::pmr::String("ok", &core::defaultCpuMemoryResource());
            if (bytes > 0) {
                r.data = core::pmr::String(reinterpret_cast<const char*>(buffer->data()),
                    bytes,
                    &core::defaultCpuMemoryResource());
            }
            completeOperation(state, r);
        });
    return makeNetworkAwaitable(*operation);
}

NetworkResult TCPScriptSocket::close()
{
    network::error_code ec;
    if (state_ != nullptr && state_->socket != nullptr && state_->open) {
        state_->socket->close(ec);
        state_->open = false;
    }
    if (connection_ != nullptr)
        connection_->setOpenNative(false);
    NetworkResult r;
    r.success = true;
    r.message = core::pmr::String("ok", &core::defaultCpuMemoryResource());
    return r;
}

// ---------------------------------------------------------------------------
// UDPScriptSocket
// ---------------------------------------------------------------------------

NetworkResult UDPScriptSocket::bind(const core::pmr::String& addr, int port)
{
    if (ctx_ == nullptr || state_ == nullptr || state_->socket == nullptr) {
        NetworkResult r;
        r.message =
            core::pmr::String("UDPSocket: no io_context", &core::defaultCpuMemoryResource());
        return r;
    }
    if (state_->open) {
        NetworkResult r;
        r.message = core::pmr::String("UDPSocket: already open", &core::defaultCpuMemoryResource());
        return r;
    }

    network::error_code ec;
    if (state_->socket->open(ec) != 0) {
        NetworkResult r;
        r.code = ec.value();
        r.message = errorMessage(ec);
        return r;
    }
    if (state_->socket->bind(ec, network::endpoint(addr, static_cast<network::uint16>(port)))
        != 0) {
        const network::error_code bindEc = ec;
        network::error_code ignored;
        state_->socket->close(ignored);
        NetworkResult r;
        r.code = bindEc.value();
        r.message = errorMessage(bindEc);
        return r;
    }
    state_->open = true;
    if (connection_ != nullptr) {
        core::pmr::String endpoint(addr, &core::defaultCpuMemoryResource());
        endpoint += ":";
        endpoint += std::to_string(port).c_str();
        connection_->setEndpointNative(endpoint);
        connection_->setOpenNative(true);
    }
    NetworkResult r;
    r.success = true;
    r.message = core::pmr::String("ok", &core::defaultCpuMemoryResource());
    return r;
}

Value UDPScriptSocket::sendto(
    const core::pmr::String& host, int port, const core::pmr::String& data)
{
    if (state_ == nullptr || state_->socket == nullptr || !state_->open) {
        return failedNetworkAwaitable(owner_, "UDPSocket: not bound");
    }

    auto* operation = owner_ != nullptr ? owner_->beginOperation() : nullptr;
    if (operation == nullptr)
        return immediateFailureValue("UDPSocket: no operation owner");
    attachOperationMetadata(owner_, operation, connection_);
    auto state = operation->sharedData();
    auto socketState = state_;
    network::byte_buffer buffer{&core::defaultCpuMemoryResource()};
    buffer.resize(data.size());
    if (!data.empty()) {
        std::memcpy(buffer.data(), data.data(), data.size());
    }
    network::endpoint remote(host, static_cast<network::uint16>(port));
    socketState->socket->async_sendto(
        remote, buffer, [state, socketState](const network::error_code& ec, std::size_t bytes) {
            NetworkResult r;
            r.success = !ec;
            r.code = ec ? ec.value() : 0;
            r.bytes = static_cast<int>(bytes);
            r.message =
                ec ? errorMessage(ec) : core::pmr::String("ok", &core::defaultCpuMemoryResource());
            completeOperation(state, r);
        });
    return makeNetworkAwaitable(*operation);
}

Value UDPScriptSocket::recvfrom(int maxBytes)
{
    if (state_ == nullptr || state_->socket == nullptr || !state_->open) {
        return failedNetworkAwaitable(owner_, "UDPSocket: not bound");
    }
    if (maxBytes <= 0) {
        return failedNetworkAwaitable(owner_, "UDPSocket.recvfrom: maxBytes must be positive");
    }

    auto* operation = owner_ != nullptr ? owner_->beginOperation() : nullptr;
    if (operation == nullptr)
        return immediateFailureValue("UDPSocket: no operation owner");
    attachOperationMetadata(owner_, operation, connection_);
    auto state = operation->sharedData();
    auto socketState = state_;
    auto buffer = core::makeShared<network::byte_buffer>(
        static_cast<std::size_t>(maxBytes), static_cast<std::uint8_t>(0));
    socketState->socket->async_recvfrom(*buffer,
        [state, buffer, socketState](
            const network::error_code& ec, const network::endpoint& peer, std::size_t bytes) {
            NetworkResult r;
            r.success = !ec && bytes > 0;
            r.code = ec ? ec.value() : 0;
            r.bytes = static_cast<int>(bytes);
            r.message =
                ec ? errorMessage(ec) : core::pmr::String("ok", &core::defaultCpuMemoryResource());
            if (bytes > 0) {
                r.data = core::pmr::String(reinterpret_cast<const char*>(buffer->data()),
                    bytes,
                    &core::defaultCpuMemoryResource());
            }
            r.peer = peer.to_string();
            completeOperation(state, r);
        });
    return makeNetworkAwaitable(*operation);
}

NetworkResult UDPScriptSocket::close()
{
    network::error_code ec;
    if (state_ != nullptr && state_->socket != nullptr && state_->open) {
        state_->socket->close(ec);
        state_->open = false;
    }
    if (connection_ != nullptr)
        connection_->setOpenNative(false);
    NetworkResult r;
    r.success = true;
    r.message = core::pmr::String("ok", &core::defaultCpuMemoryResource());
    return r;
}

// ---------------------------------------------------------------------------
// HttpScriptClient
// ---------------------------------------------------------------------------

Value HttpScriptClient::get(const core::pmr::String& url)
{
    if (ctx_ == nullptr) {
        return failedNetworkAwaitable(owner_, "HttpClient: no io_context");
    }

    auto* operation = owner_ != nullptr ? owner_->beginOperation() : nullptr;
    if (operation == nullptr)
        return immediateFailureValue("HttpClient: no operation owner");
    auto* requestObject = owner_->createRequest(NetworkRequestMethod::Get, url);
    attachOperationMetadata(owner_, operation, connection_, requestObject);
    if (connection_ != nullptr)
        connection_->setEndpointNative(url);
    if (connection_ != nullptr)
        connection_->setEndpointNative(url);
    auto state = operation->sharedData();
    network::http::Client client(ctx_);
    auto request = network::http::ClientRequest::get(network::StringView(url.data(), url.size()));
    for (std::size_t index = 0; index < headers_.size(); ++index) {
        const auto& header = headers_.at(index);
        if (request.add_header(header.name(), header.value())
            != network::http::DiagnosticCode::none) {
            return immediateFailureValue("HttpClient: invalid request header");
        }
    }
    request.tls_config().set_certificate_verification(tlsVerification_);
    request.tls_config().set_trusted_certificate_pem(
        network::StringView(trustedCertificate_.data(), trustedCertificate_.size()));
    client.async_send(request,
        [state](
            const network::http::ClientStatus& status, const network::http::Response& response) {
            NetworkResult r;
            r.success = status.success();
            r.code = static_cast<int>(response.status().value());
            r.message = core::pmr::String(response.reason().data(),
                response.reason().size(),
                &core::defaultCpuMemoryResource());
            r.data = byteBufferToString(response.body());
            r.headers = serializeHeaders(response.headers());
            if (!r.success) {
                r.message =
                    core::pmr::String("http request failed", &core::defaultCpuMemoryResource());
            }
            completeOperation(state, r);
        });
    return makeNetworkAwaitable(*operation);
}

Value HttpScriptClient::post(const core::pmr::String& url,
    const core::pmr::String& contentType,
    const core::pmr::String& body)
{
    if (ctx_ == nullptr) {
        return failedNetworkAwaitable(owner_, "HttpClient: no io_context");
    }

    auto* operation = owner_ != nullptr ? owner_->beginOperation() : nullptr;
    if (operation == nullptr)
        return immediateFailureValue("HttpClient: no operation owner");
    auto* requestObject = owner_->createRequest(NetworkRequestMethod::Post, url);
    if (requestObject != nullptr) {
        requestObject->setContentType(contentType);
        requestObject->setBody(body);
    }
    attachOperationMetadata(owner_, operation, connection_, requestObject);
    if (connection_ != nullptr)
        connection_->setEndpointNative(url);
    auto state = operation->sharedData();
    network::http::Client client(ctx_);
    auto request = network::http::ClientRequest::post(network::StringView(url.data(), url.size()));
    for (std::size_t index = 0; index < headers_.size(); ++index) {
        const auto& header = headers_.at(index);
        if (request.add_header(header.name(), header.value())
            != network::http::DiagnosticCode::none) {
            return immediateFailureValue("HttpClient: invalid request header");
        }
    }
    request.tls_config().set_certificate_verification(tlsVerification_);
    request.tls_config().set_trusted_certificate_pem(
        network::StringView(trustedCertificate_.data(), trustedCertificate_.size()));
    request.set_body_text(network::StringView(body.data(), body.size()));
    if (contentType == "application/json") {
        request.set_content_type(network::http::ContentType::Json);
    } else if (contentType == "text/html") {
        request.set_content_type(network::http::ContentType::Html);
    } else if (contentType == "application/xml") {
        request.set_content_type(network::http::ContentType::Xml);
    } else {
        request.set_content_type(network::http::ContentType::Plain);
    }
    client.async_send(request,
        [state](
            const network::http::ClientStatus& status, const network::http::Response& response) {
            NetworkResult r;
            r.success = status.success();
            r.code = static_cast<int>(response.status().value());
            r.message = core::pmr::String(response.reason().data(),
                response.reason().size(),
                &core::defaultCpuMemoryResource());
            r.data = byteBufferToString(response.body());
            r.headers = serializeHeaders(response.headers());
            if (!r.success) {
                r.message =
                    core::pmr::String("http request failed", &core::defaultCpuMemoryResource());
            }
            completeOperation(state, r);
        });
    return makeNetworkAwaitable(*operation);
}

NetworkResult HttpScriptClient::setHeader(
    const core::pmr::String& name, const core::pmr::String& value)
{
    if (headers_.count(network::StringView(name.data(), name.size())) != 0u)
        return tlsConfigFailure("HttpClient.set_header: header name is already set");
    const auto diagnostic = headers_.add(network::StringView(name.data(), name.size()),
        network::StringView(value.data(), value.size()));
    if (diagnostic != network::http::DiagnosticCode::none)
        return tlsConfigFailure("HttpClient.set_header: invalid header name or value");
    NetworkResult result;
    result.success = true;
    result.message = core::pmr::String("ok", &core::defaultCpuMemoryResource());
    return result;
}

NetworkResult HttpScriptClient::setTlsVerification(const core::pmr::String& mode)
{
    auto verification = tlsVerification_;
    if (!parseTlsVerification(mode, verification)) {
        return tlsConfigFailure(
            "HttpClient.set_tls_verification: expected system_trust, explicit_trust, or disabled_for_test");
    }
    tlsVerification_ = verification;
    return tlsConfigOk();
}

NetworkResult HttpScriptClient::setTrustedCertificate(const core::pmr::String& pem)
{
    if (pem.empty()) {
        return tlsConfigFailure("HttpClient.set_trusted_certificate: PEM must not be empty");
    }
    trustedCertificate_ = pem;
    return tlsConfigOk();
}

// ---------------------------------------------------------------------------
// WebSocketScriptClient
// ---------------------------------------------------------------------------

Value WebSocketScriptClient::connect(const core::pmr::String& url)
{
    if (ctx_ == nullptr || state_ == nullptr) {
        return failedNetworkAwaitable(owner_, "WebSocketClient: no io_context");
    }
    if (state_->connected) {
        return failedNetworkAwaitable(owner_, "WebSocketClient: already connected");
    }

    auto* operation = owner_ != nullptr ? owner_->beginOperation() : nullptr;
    if (operation == nullptr)
        return immediateFailureValue("WebSocketClient: no operation owner");
    auto* requestObject = owner_->createRequest(NetworkRequestMethod::Get, url);
    attachOperationMetadata(owner_, operation, connection_, requestObject);
    auto state = operation->sharedData();
    if (state_->client == nullptr) {
        state_->client = core::makeUnique<network::websocket::Client>(ctx_);
    }
    auto clientState = state_;
    clientState->connected = true;
    auto* connection = connection_;
    auto request =
        network::websocket::ClientRequest::connect(network::StringView(url.data(), url.size()));
    request.tls_config().set_certificate_verification(tlsVerification_);
    request.tls_config().set_trusted_certificate_pem(
        network::StringView(trustedCertificate_.data(), trustedCertificate_.size()));
    clientState->client->async_connect(
        request, [state, clientState, connection](const network::websocket::ClientStatus& status) {
            NetworkResult r;
            r.success = status.success();
            r.code = static_cast<int>(status.transport_error.value());
            r.bytes = static_cast<int>(status.bytes);
            r.message = status.success()
                ? core::pmr::String("ok", &core::defaultCpuMemoryResource())
                : core::pmr::String("ws connect failed", &core::defaultCpuMemoryResource());
            if (!r.success) {
                clientState->connected = false;
                clientState->client.reset();
            }
            if (connection != nullptr)
                connection->setOpenNative(status.success());
            completeOperation(state, r);
        });
    return makeNetworkAwaitable(*operation);
}

Value WebSocketScriptClient::sendText(const core::pmr::String& text)
{
    if (state_ == nullptr || !state_->connected || state_->client == nullptr) {
        return failedNetworkAwaitable(owner_, "WebSocketClient: not connected");
    }

    auto* operation = owner_ != nullptr ? owner_->beginOperation() : nullptr;
    if (operation == nullptr)
        return immediateFailureValue("WebSocketClient: no operation owner");
    attachOperationMetadata(owner_, operation, connection_);
    auto state = operation->sharedData();
    auto clientState = state_;
    clientState->client->async_write_text(network::StringView(text.data(), text.size()),
        [state, clientState](const network::websocket::ClientStatus& status) {
            NetworkResult r;
            r.success = status.success();
            r.code = static_cast<int>(status.transport_error.value());
            r.bytes = static_cast<int>(status.bytes);
            r.message = status.success()
                ? core::pmr::String("ok", &core::defaultCpuMemoryResource())
                : core::pmr::String("ws send failed", &core::defaultCpuMemoryResource());
            completeOperation(state, r);
        });
    return makeNetworkAwaitable(*operation);
}

Value WebSocketScriptClient::recv()
{
    if (state_ == nullptr || !state_->connected || state_->client == nullptr) {
        return failedNetworkAwaitable(owner_, "WebSocketClient: not connected");
    }

    auto* operation = owner_ != nullptr ? owner_->beginOperation() : nullptr;
    if (operation == nullptr)
        return immediateFailureValue("WebSocketClient: no operation owner");
    attachOperationMetadata(owner_, operation, connection_);
    auto state = operation->sharedData();
    auto clientState = state_;
    clientState->client->async_read_frame(
        [state, clientState](const network::websocket::ClientStatus& status,
            const network::websocket::Frame& frame) {
            NetworkResult r;
            r.success = status.success();
            r.code = static_cast<int>(frame.opcode());
            r.bytes = static_cast<int>(status.bytes);
            r.message = status.success()
                ? core::pmr::String("ok", &core::defaultCpuMemoryResource())
                : core::pmr::String("ws recv failed", &core::defaultCpuMemoryResource());
            if (status.success()) {
                auto payload = frame.payload_text();
                r.data = core::pmr::String(
                    payload.data(), payload.size(), &core::defaultCpuMemoryResource());
            }
            completeOperation(state, r);
        });
    return makeNetworkAwaitable(*operation);
}

Value WebSocketScriptClient::close(int closeCode, const core::pmr::String& reason)
{
    if (state_ == nullptr || !state_->connected || state_->client == nullptr) {
        return failedNetworkAwaitable(owner_, "WebSocketClient: not connected");
    }

    auto* operation = owner_ != nullptr ? owner_->beginOperation() : nullptr;
    if (operation == nullptr)
        return immediateFailureValue("WebSocketClient: no operation owner");
    attachOperationMetadata(owner_, operation, connection_);
    auto state = operation->sharedData();
    auto clientState = state_;
    auto* connection = connection_;
    clientState->client->async_close(static_cast<std::uint16_t>(closeCode),
        network::StringView(reason.data(), reason.size()),
        [state, clientState, connection](const network::websocket::ClientStatus& status) {
            NetworkResult r;
            r.success = status.success();
            r.code = static_cast<int>(status.transport_error.value());
            r.message = status.success()
                ? core::pmr::String("ok", &core::defaultCpuMemoryResource())
                : core::pmr::String("ws close failed", &core::defaultCpuMemoryResource());
            clientState->connected = false;
            clientState->client.reset();
            if (connection != nullptr)
                connection->setOpenNative(false);
            completeOperation(state, r);
        });
    return makeNetworkAwaitable(*operation);
}

NetworkResult WebSocketScriptClient::setTlsVerification(const core::pmr::String& mode)
{
    auto verification = tlsVerification_;
    if (!parseTlsVerification(mode, verification)) {
        return tlsConfigFailure(
            "WebSocketClient.set_tls_verification: expected system_trust, explicit_trust, or disabled_for_test");
    }
    tlsVerification_ = verification;
    return tlsConfigOk();
}

NetworkResult WebSocketScriptClient::setTrustedCertificate(const core::pmr::String& pem)
{
    if (pem.empty()) {
        return tlsConfigFailure("WebSocketClient.set_trusted_certificate: PEM must not be empty");
    }
    trustedCertificate_ = pem;
    return tlsConfigOk();
}

namespace {

bool registerNetworkPluginExports(ScriptPluginRegistrar& builder,
    network::io_context& ioContext,
    const core::pmr::String& moduleName)
{
    activeNetworkIoContext.store(&ioContext, std::memory_order_release);
    if (builder.name() != moduleName)
        return false;
    auto runtime = core::makeShared<NetworkRuntime>(&ioContext);
    if (!builder.retainNativeLifetime(runtime))
        return false;
    return arp::reflection::generated::register_arp_network_reflection(
        builder.reflectionModule(), &runtime->nativeReflectionObject());
}

} // namespace

int registerNetworkScriptPlugin(ArpScriptPluginHostV2* host)
{
    if (host == nullptr || host->abiVersion != ARP_SCRIPT_PLUGIN_ABI_VERSION_V2
        || host->registrar == nullptr || host->services == nullptr)
        return 0;
    auto* ioContext = host->services->find<network::io_context>(
        kNetworkIoContextService, kNetworkIoContextServiceVersion);
    if (ioContext == nullptr)
        return 0;
    return registerNetworkPluginExports(*host->registrar, *ioContext, host->registrar->name()) ? 1
                                                                                               : 0;
}

} // namespace arp::scriptvm
