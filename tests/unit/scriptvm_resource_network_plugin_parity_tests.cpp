#include "core/runtime/async_runtime.h"
#include "network/io_context.h"
#include "network/network_module.h"
#include "resource/resource_manager.h"
#include "resource/resource_module.h"
#include "scriptvm/plugin/cpp_plugin_api.h"
#include "scriptvm/runtime/module_registry.h"

#include <cassert>
#include <chrono>
#include <initializer_list>
#include <thread>

extern "C" int arp_register_arp_resources(ArpScriptPluginHostV2*);
extern "C" int arp_register_arp_network(ArpScriptPluginHostV2*);

namespace {

using arp::core::pmr::String;
using arp::scriptvm::ScriptPluginRegistrar;
using arp::scriptvm::ScriptPluginServiceRegistry;

arp::core::runtime::AsyncRuntimeDesc runtimeDesc()
{
    arp::core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 8u;
    desc.workerCount = 1u;
    desc.workQueueCapacity = 4u;
    desc.completionBudget = 8u;
    return desc;
}

ArpScriptPluginHostV2 hostFor(
    ScriptPluginRegistrar& registrar, ScriptPluginServiceRegistry& services)
{
    ArpScriptPluginHostV2 host{};
    host.abiVersion = ARP_SCRIPT_PLUGIN_ABI_VERSION_V2;
    host.registrar = &registrar;
    host.services = &services;
    host.platform = "test";
    host.buildConfiguration = "debug";
    return host;
}

void assertParity(const arp::scriptvm::ModuleExports& lhs, const arp::scriptvm::ModuleExports& rhs)
{
    assert(lhs.moduleVersion == rhs.moduleVersion);
    assert(lhs.exportContractHash == rhs.exportContractHash);
    assert(lhs.symbols.size() == rhs.symbols.size());
    for (const auto& [name, value] : lhs.symbols) {
        (void)value;
        assert(rhs.symbols.find(name) != rhs.symbols.end());
    }
    assert(lhs.reflectionRegistration != nullptr);
    assert(rhs.reflectionRegistration != nullptr);
    assert(
        lhs.reflectionRegistration->types().size() == rhs.reflectionRegistration->types().size());
    assert(lhs.reflectionRegistration->functions().size()
        == rhs.reflectionRegistration->functions().size());
}

std::shared_ptr<arp::scriptvm::ModuleExports> loadStatic(const char* module,
    ArpScriptPluginEntryV2 entry,
    ScriptPluginServiceRegistry& services,
    const char* hash)
{
    arp::scriptvm::StaticPluginCatalog catalog;
    assert(catalog.add({String(module, &arp::core::defaultCpuMemoryResource()),
        2u,
        1u,
        entry,
        String(hash, &arp::core::defaultCpuMemoryResource())}));
    String diagnostics(&arp::core::defaultCpuMemoryResource());
    auto result = catalog.load(
        String(module, &arp::core::defaultCpuMemoryResource()), &services, diagnostics);
    assert(result != nullptr);
    assert(diagnostics.empty());
    return result;
}

void resourceParity()
{
    arp::core::runtime::AsyncRuntime runtime(runtimeDesc());
    arp::resource::ResourceManager manager;
    ScriptPluginServiceRegistry services;
    assert(services.provide(
        String(arp::scriptvm::kResourceManagerService, &arp::core::defaultCpuMemoryResource()),
        arp::scriptvm::kResourceManagerServiceVersion,
        &manager));
    assert(services.provide(
        String(arp::scriptvm::kAsyncRuntimeService, &arp::core::defaultCpuMemoryResource()),
        arp::scriptvm::kAsyncRuntimeServiceVersion,
        &runtime));

    ScriptPluginRegistrar dynamicRegistrar(
        String("arp_resources", &arp::core::defaultCpuMemoryResource()), &services);
    auto dynamicHost = hostFor(dynamicRegistrar, services);
    assert(arp_register_arp_resources(&dynamicHost) == 1);
    auto dynamic = dynamicRegistrar.build();
    dynamic->moduleVersion = 1u;
    dynamic->exportContractHash =
        "sha256:1db4e73a01144fa2a569261da9dd3e962c9455d8677e8885717d457dfeff47bb";
    auto statik = loadStatic("arp_resources",
        &arp_register_arp_resources,
        services,
        "sha256:1db4e73a01144fa2a569261da9dd3e962c9455d8677e8885717d457dfeff47bb");
    assertParity(*dynamic, *statik);
    for (const auto* name : {"manager",
             "ResourceManager",
             "ResourceBase",
             "ResourceLoadResult",
             "ResourceAliasResult",
             "ResourceState",
             "ResourceOperation",
             "ResourceOperationState"}) {
        const auto found =
            dynamic->symbols.find(String(name, &arp::core::defaultCpuMemoryResource()));
        assert(found != dynamic->symbols.end());
    }
    assert(dynamic->symbols.find(
               String("ArpScriptResourceResult", &arp::core::defaultCpuMemoryResource()))
        == dynamic->symbols.end());
    assert(statik->symbols.find(
               String("ArpScriptResourceResult", &arp::core::defaultCpuMemoryResource()))
        == statik->symbols.end());
    const auto managerIt =
        dynamic->symbols.find(String("manager", &arp::core::defaultCpuMemoryResource()));
    assert(managerIt != dynamic->symbols.end());
    assert(managerIt->second.is_native_reference());
    assert(managerIt->second.native_reference_alive());
    const auto missingDiagnosticsValue = managerIt->second.call(
        "diagnostics", String("missing", &arp::core::defaultCpuMemoryResource()));
    assert(missingDiagnosticsValue.is<String>());
    assert(!missingDiagnosticsValue.cast<String>().empty());
    const auto staticManagerIt =
        statik->symbols.find(String("manager", &arp::core::defaultCpuMemoryResource()));
    assert(staticManagerIt != statik->symbols.end());
    assert(staticManagerIt->second.is_native_reference());
    assert(staticManagerIt->second.native_reference_alive());
    assert(staticManagerIt->second.class_()->name() == "ResourceManager");
    assert(managerIt->second.class_()->name() == staticManagerIt->second.class_()->name());
    const auto state =
        managerIt->second.call("state", String("missing", &arp::core::defaultCpuMemoryResource()));
    assert(state.is<arp::resource::ResourceState>());
    assert(state.cast<arp::resource::ResourceState>() == arp::resource::ResourceState::Unloaded);
    const auto missingAwaitValue = managerIt->second.call("load_async",
        String("D:/arp/build/win-auto-debug/arp_missing_resource_operation.arpres",
            &arp::core::defaultCpuMemoryResource()));
    const auto* missingAwait = arp::scriptvm::asScriptAwaitable(missingAwaitValue);
    assert(missingAwait != nullptr && missingAwait->hasOperation());
    const auto missingOperation = missingAwait->operationObject();
    assert(missingOperation.is_native_reference());
    assert(missingOperation.native_reference_alive());
    assert(missingOperation.class_()->name() == "ResourceOperation");
    for (int attempt = 0; attempt < 200 && !missingAwait->isReady(); ++attempt) {
        runtime.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(missingOperation.call("is_complete").cast<bool>());
    assert(missingOperation.call("state").cast<arp::resource::ResourceOperationState>()
        == arp::resource::ResourceOperationState::Failed);
    assert(!missingOperation.call("is_success").cast<bool>());
    assert(!missingOperation.call("error").cast<String>().empty());
    assert(missingOperation.call("resource").is_undefined());
    arp::scriptvm::Value missingResult;
    String missingDiagnostics(&arp::core::defaultCpuMemoryResource());
    assert(missingAwait->pollResult(missingResult, missingDiagnostics)
        == arp::scriptvm::ScriptAwaitStatus::Failed);
    assert(!missingDiagnostics.empty());
    assert(missingOperation.call("destroy").cast<bool>());
    assert(!missingOperation.native_reference_alive());

    // Destroying a resource operation before its file completion is safe:
    // the await adapter keeps only shared completion state, never the native
    // operation address. A completion that arrives later must be ignored.
    const auto pendingResourceAwaitValue = managerIt->second.call("load_async",
        String("D:/arp/build/win-auto-debug/arp_pending_resource_operation.arpres",
            &arp::core::defaultCpuMemoryResource()));
    const auto* pendingResourceAwait = arp::scriptvm::asScriptAwaitable(pendingResourceAwaitValue);
    assert(pendingResourceAwait != nullptr && pendingResourceAwait->hasOperation());
    const auto pendingResourceOperation = pendingResourceAwait->operationObject();
    assert(pendingResourceOperation.is_native_reference());
    assert(pendingResourceOperation.native_reference_alive());
    assert(pendingResourceOperation.call("destroy").cast<bool>());
    assert(!pendingResourceOperation.native_reference_alive());
    for (int attempt = 0; attempt < 100; ++attempt) {
        runtime.dispatch();
        if (pendingResourceAwait->isReady())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    arp::scriptvm::Value pendingResourceResult;
    String pendingResourceDiagnostics(&arp::core::defaultCpuMemoryResource());
    assert(pendingResourceAwait->pollResult(pendingResourceResult, pendingResourceDiagnostics)
        == arp::scriptvm::ScriptAwaitStatus::Failed);
    assert(!pendingResourceDiagnostics.empty());
    assert(dynamic->symbols.find(String("load_async", &arp::core::defaultCpuMemoryResource()))
        == dynamic->symbols.end());
    assert(statik->symbols.find(String("load_async", &arp::core::defaultCpuMemoryResource()))
        == statik->symbols.end());
}

void networkParity()
{
    arp::network::io_context ioContext;
    ScriptPluginServiceRegistry services;
    assert(services.provide(
        String(arp::scriptvm::kNetworkIoContextService, &arp::core::defaultCpuMemoryResource()),
        arp::scriptvm::kNetworkIoContextServiceVersion,
        &ioContext));

    ScriptPluginRegistrar dynamicRegistrar(
        String("arp_network", &arp::core::defaultCpuMemoryResource()), &services);
    auto dynamicHost = hostFor(dynamicRegistrar, services);
    assert(arp_register_arp_network(&dynamicHost) == 1);
    auto dynamic = dynamicRegistrar.build();
    dynamic->moduleVersion = 1u;
    dynamic->exportContractHash =
        "sha256:3433e7df75178e644042cc414d4b128e77b0aa91e999acb88a127fd3cc9d57fd";
    auto statik = loadStatic("arp_network",
        &arp_register_arp_network,
        services,
        "sha256:3433e7df75178e644042cc414d4b128e77b0aa91e999acb88a127fd3cc9d57fd");
    assertParity(*dynamic, *statik);
    assert(dynamic->symbols.find(String("service_status", &arp::core::defaultCpuMemoryResource()))
        == dynamic->symbols.end());
    assert(statik->symbols.find(String("service_status", &arp::core::defaultCpuMemoryResource()))
        == statik->symbols.end());
    for (const auto* name : {"network",
             "TcpSocket",
             "UdpSocket",
             "HttpClient",
             "HttpServer",
             "HttpServerRequest",
             "HttpResponse",
             "HttpServerConfig",
             "WebSocketClient",
             "NetworkResult",
             "NetworkRuntime",
             "NetworkOperation",
             "NetworkOperationState",
             "NetworkConnection",
             "NetworkRequest",
             "NetworkMessage",
             "NetworkConnectionKind",
             "NetworkRequestMethod"}) {
        assert(dynamic->symbols.find(String(name, &arp::core::defaultCpuMemoryResource()))
            != dynamic->symbols.end());
        assert(statik->symbols.find(String(name, &arp::core::defaultCpuMemoryResource()))
            != statik->symbols.end());
    }
    const auto runtimeIt =
        dynamic->symbols.find(String("network", &arp::core::defaultCpuMemoryResource()));
    assert(runtimeIt != dynamic->symbols.end());
    assert(runtimeIt->second.is_native_reference());
    assert(runtimeIt->second.native_reference_alive());
    assert(dynamic->nativeLifetime != nullptr);
    assert(runtimeIt->second.class_()->name() == "NetworkRuntime");
    assert(runtimeIt->second.call("has_context").cast<bool>());

    // Protocol descriptions are real engine-owned objects. Their methods are
    // reached through generated reflection thunks, not a service facade or a
    // script-only identity object.
    const auto dynamicConnection = runtimeIt->second.call("create_connection",
        arp::scriptvm::NetworkConnectionKind::Http,
        String("https://example.test", &arp::core::defaultCpuMemoryResource()));
    assert(dynamicConnection.is_native_reference());
    assert(dynamicConnection.native_reference_alive());
    assert(dynamicConnection.class_()->name() == "NetworkConnection");
    assert(dynamicConnection.call("kind").cast<arp::scriptvm::NetworkConnectionKind>()
        == arp::scriptvm::NetworkConnectionKind::Http);
    assert(dynamicConnection.call("endpoint").cast<String>() == "https://example.test");
    assert(!dynamicConnection.call("is_open").cast<bool>());

    const auto dynamicRequest = runtimeIt->second.call("create_request",
        arp::scriptvm::NetworkRequestMethod::Post,
        String("https://example.test/upload", &arp::core::defaultCpuMemoryResource()));
    assert(dynamicRequest.is_native_reference());
    assert(dynamicRequest.native_reference_alive());
    assert(dynamicRequest.class_()->name() == "NetworkRequest");
    assert(dynamicRequest.call("method").cast<arp::scriptvm::NetworkRequestMethod>()
        == arp::scriptvm::NetworkRequestMethod::Post);
    assert(dynamicRequest.call("url").cast<String>() == "https://example.test/upload");
    dynamicRequest.call(
        "set_content_type", String("application/json", &arp::core::defaultCpuMemoryResource()));
    dynamicRequest.call(
        "set_body", String("{\"ok\":true}", &arp::core::defaultCpuMemoryResource()));
    assert(dynamicRequest.call("content_type").cast<String>() == "application/json");
    assert(dynamicRequest.call("body").cast<String>() == "{\"ok\":true}");

    const auto dynamicMessage = runtimeIt->second.call("create_message");
    assert(dynamicMessage.is_native_reference());
    assert(dynamicMessage.native_reference_alive());
    assert(dynamicMessage.class_()->name() == "NetworkMessage");
    assert(dynamicMessage.call("code").cast<int>() == 0);
    assert(dynamicMessage.call("bytes").cast<int>() == 0);

    const auto tcp = runtimeIt->second.call("create_tcp_socket");
    assert(tcp.is_native_reference());
    assert(tcp.native_reference_alive());
    assert(tcp.class_()->name() == "TcpSocket");
    assert(!tcp.call("is_open").cast<bool>());
    assert(tcp.call("destroy").cast<bool>());
    assert(!tcp.native_reference_alive());
    assert(tcp.call("is_open").is_undefined());

    const auto staticRuntimeIt =
        statik->symbols.find(String("network", &arp::core::defaultCpuMemoryResource()));
    assert(staticRuntimeIt != statik->symbols.end());
    assert(staticRuntimeIt->second.is_native_reference());

    // An async method returns the VM's await adapter, but the adapter carries
    // the same engine-owned NetworkOperation object used by the registry.
    const auto freshTcp = staticRuntimeIt->second.call("create_tcp_socket");
    const auto failedAwait = freshTcp.call(
        "connect", String("invalid.invalid", &arp::core::defaultCpuMemoryResource()), 1);
    const auto* awaitable = arp::scriptvm::asScriptAwaitable(failedAwait);
    assert(awaitable != nullptr);
    assert(awaitable->hasOperation());
    const auto failedOperation = awaitable->operationObject();
    assert(failedOperation.is_native_reference());
    assert(failedOperation.native_reference_alive());
    assert(failedOperation.class_()->name() == "NetworkOperation");
    assert(failedOperation.call("is_complete").cast<bool>());
    assert(failedOperation.call("is_success").cast<bool>() == false);
    assert(failedOperation.call("state").cast<arp::scriptvm::NetworkOperationState>()
        == arp::scriptvm::NetworkOperationState::Failed);
    const auto failedConnection = failedOperation.call("connection");
    const auto failedMessage = failedOperation.call("message");
    assert(failedConnection.is_native_reference());
    assert(failedConnection.native_reference_alive());
    assert(failedConnection == freshTcp.call("connection"));
    assert(failedMessage.is_native_reference());
    assert(failedMessage.native_reference_alive());
    assert(failedMessage.class_()->name() == "NetworkMessage");
    const auto failedResult = failedOperation.call("result");
    assert(failedResult.is<arp::scriptvm::NetworkResult>());
    assert(!failedResult.cast<arp::scriptvm::NetworkResult>().success);
    assert(!failedOperation.call("error").cast<String>().empty());
    arp::scriptvm::Value polled;
    String awaitDiagnostics(&arp::core::defaultCpuMemoryResource());
    assert(awaitable->pollResult(polled, awaitDiagnostics)
        == arp::scriptvm::ScriptAwaitStatus::Failed);
    assert(!awaitDiagnostics.empty());

    // A pending operation can be destroyed before its native callback runs.
    // The adapter remains safe because it owns only the shared completion
    // state, not the native operation address.
    arp::scriptvm::NetworkRuntime pendingRuntime(&ioContext);
    auto* pendingNative = pendingRuntime.beginOperation();
    const auto pendingAwait = pendingNative->makeAwaitableNative();
    const auto* pendingAwaitable = arp::scriptvm::asScriptAwaitable(pendingAwait);
    assert(pendingAwaitable != nullptr && pendingAwaitable->hasOperation());
    const auto pendingOperation = pendingAwaitable->operationObject();
    assert(pendingOperation.call("state").cast<arp::scriptvm::NetworkOperationState>()
        == arp::scriptvm::NetworkOperationState::Pending);
    assert(pendingOperation.call("destroy").cast<bool>());
    assert(!pendingOperation.native_reference_alive());
    arp::scriptvm::Value pendingPolled;
    String pendingDiagnostics(&arp::core::defaultCpuMemoryResource());
    assert(pendingAwaitable->pollResult(pendingPolled, pendingDiagnostics)
        == arp::scriptvm::ScriptAwaitStatus::Failed);
    assert(!pendingDiagnostics.empty());

    // A newly allocated operation is pending; native completion transitions
    // it to ready without exposing a second wrapper or service call.
    arp::scriptvm::NetworkRuntime nativeRuntime(&ioContext);
    auto* nativeOperation = nativeRuntime.beginOperation();
    assert(nativeOperation != nullptr);
    const auto pendingReference = nativeOperation->nativeReflectionObject();
    assert(pendingReference.call("state").cast<arp::scriptvm::NetworkOperationState>()
        == arp::scriptvm::NetworkOperationState::Pending);
    arp::scriptvm::NetworkResult readyResult;
    readyResult.success = true;
    readyResult.message = String("ready", &arp::core::defaultCpuMemoryResource());
    nativeOperation->completeNative(readyResult);
    assert(pendingReference.call("state").cast<arp::scriptvm::NetworkOperationState>()
        == arp::scriptvm::NetworkOperationState::Ready);
    assert(pendingReference.call("is_success").cast<bool>());
    assert(pendingReference.call("result").cast<arp::scriptvm::NetworkResult>().success);
    assert(nativeOperation->destroy());
    assert(!pendingReference.native_reference_alive());

    // A protocol object cannot be destroyed while an operation still refers
    // to it. Completion updates the same message object returned by the
    // operation accessor.
    auto* protocolConnection =
        nativeRuntime.createConnection(arp::scriptvm::NetworkConnectionKind::Http,
            String("https://native.test", &arp::core::defaultCpuMemoryResource()));
    auto* protocolRequest = nativeRuntime.createRequest(arp::scriptvm::NetworkRequestMethod::Post,
        String("https://native.test/upload", &arp::core::defaultCpuMemoryResource()));
    auto* protocolMessage = nativeRuntime.createMessage();
    auto* protocolOperation = nativeRuntime.beginOperation();
    assert(protocolConnection != nullptr && protocolRequest != nullptr && protocolMessage != nullptr
        && protocolOperation != nullptr);
    protocolRequest->setContentType(
        String("application/json", &arp::core::defaultCpuMemoryResource()));
    protocolRequest->setBody(String("{\"native\":true}", &arp::core::defaultCpuMemoryResource()));
    protocolOperation->attachConnectionNative(protocolConnection);
    protocolOperation->attachRequestNative(protocolRequest);
    protocolOperation->attachMessageNative(protocolMessage);
    const auto protocolOperationRef = protocolOperation->nativeReflectionObject();
    const auto protocolConnectionRef = protocolConnection->nativeReflectionObject();
    const auto protocolRequestRef = protocolRequest->nativeReflectionObject();
    const auto protocolMessageRef = protocolMessage->nativeReflectionObject();
    assert(protocolOperationRef.call("connection").ptr<arp::scriptvm::NetworkConnection>()
        == protocolConnection);
    assert(protocolOperationRef.call("request").ptr<arp::scriptvm::NetworkRequest>()
        == protocolRequest);
    assert(protocolOperationRef.call("message").ptr<arp::scriptvm::NetworkMessage>()
        == protocolMessage);
    assert(protocolRequestRef.call("content_type").cast<String>() == "application/json");
    assert(protocolRequestRef.call("body").cast<String>() == "{\"native\":true}");
    assert(!protocolConnection->destroy());
    assert(!protocolRequest->destroy());
    assert(!protocolMessage->destroy());
    arp::scriptvm::NetworkResult protocolResult;
    protocolResult.success = true;
    protocolResult.code = 201;
    protocolResult.bytes = 17;
    protocolResult.data = String("created", &arp::core::defaultCpuMemoryResource());
    protocolResult.peer = String("peer:443", &arp::core::defaultCpuMemoryResource());
    protocolResult.headers = String("x-test: yes\n", &arp::core::defaultCpuMemoryResource());
    protocolOperation->completeNative(protocolResult);
    assert(protocolOperationRef.call("message") == protocolMessageRef);
    assert(protocolMessageRef.call("code").cast<int>() == 201);
    assert(protocolMessageRef.call("bytes").cast<int>() == 17);
    assert(protocolMessageRef.call("data").cast<String>() == "created");
    assert(protocolMessageRef.call("peer").cast<String>() == "peer:443");
    assert(protocolMessageRef.call("headers").cast<String>() == "x-test: yes\n");
    assert(protocolOperation->destroy());
    assert(!protocolOperationRef.native_reference_alive());
    assert(protocolConnection->destroy());
    assert(protocolRequest->destroy());
    assert(protocolMessage->destroy());
    assert(!protocolConnectionRef.native_reference_alive());
    assert(!protocolRequestRef.native_reference_alive());
    assert(!protocolMessageRef.native_reference_alive());

    // Explicit operation destruction invalidates every await adapter copy;
    // the shared completion state remains safe for a late IO callback.
    assert(failedOperation.call("destroy").cast<bool>());
    assert(!failedOperation.native_reference_alive());
    assert(failedOperation.call("state").is_undefined());
    assert(!failedAwait.call("operation").native_reference_alive());

    const auto udp = staticRuntimeIt->second.call("create_udp_socket");
    assert(udp.is_native_reference());
    assert(udp.native_reference_alive());
    assert(udp.class_()->name() == "UdpSocket");
    assert(udp.call("destroy").cast<bool>());
    assert(!udp.native_reference_alive());

    const auto staticConnection = staticRuntimeIt->second.call("create_connection",
        arp::scriptvm::NetworkConnectionKind::Udp,
        String("127.0.0.1:9000", &arp::core::defaultCpuMemoryResource()));
    const auto staticRequest = staticRuntimeIt->second.call("create_request",
        arp::scriptvm::NetworkRequestMethod::Get,
        String("https://static.test", &arp::core::defaultCpuMemoryResource()));
    const auto staticMessage = staticRuntimeIt->second.call("create_message");
    assert(staticConnection.class_()->name() == "NetworkConnection");
    assert(staticRequest.class_()->name() == "NetworkRequest");
    assert(staticMessage.class_()->name() == "NetworkMessage");
    assert(staticConnection.call("kind").cast<arp::scriptvm::NetworkConnectionKind>()
        == arp::scriptvm::NetworkConnectionKind::Udp);
    assert(staticRequest.call("method").cast<arp::scriptvm::NetworkRequestMethod>()
        == arp::scriptvm::NetworkRequestMethod::Get);

    // Module exports own the runtime registry. Dropping an export table must
    // invalidate every child object reference before native memory is freed.
    auto dynamicTcp = runtimeIt->second.call("create_tcp_socket");
    assert(dynamicTcp.native_reference_alive());
    auto staticUdp = staticRuntimeIt->second.call("create_udp_socket");
    assert(staticUdp.native_reference_alive());
    dynamic.reset();
    statik.reset();
    assert(!dynamicTcp.native_reference_alive());
    assert(!staticUdp.native_reference_alive());
    assert(!failedOperation.native_reference_alive());
    assert(!dynamicConnection.native_reference_alive());
    assert(!dynamicRequest.native_reference_alive());
    assert(!dynamicMessage.native_reference_alive());
    assert(!staticConnection.native_reference_alive());
    assert(!staticRequest.native_reference_alive());
    assert(!staticMessage.native_reference_alive());
}

} // namespace

int main()
{
    resourceParity();
    networkParity();
    return 0;
}
