// ArpScript network module unit tests.
//
// Covers module registration/export table, TCP loopback echo, UDP loopback
// echo, HTTP loopback GET, and WebSocket connect. All network servers run on
// loopback with a real io_context.

#include "network/http/session.h"
#include "network/io_service.h"
#include "network/network_module.h"
#include "network/socket_runtime.h"
#include "scriptvm/interpreter/eval.h"
#include "scriptvm/interpreter/vm.h"
#include "scriptvm/runtime/module_registry.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using namespace arp::scriptvm;

namespace {

int g_failures = 0;

void require(bool cond, const char* label)
{
    if (!cond) {
        std::printf("FAIL: %s\n", label);
        ++g_failures;
    }
}

arp::core::pmr::String text(const char* value)
{
    return {value, &arp::core::defaultCpuMemoryResource()};
}

arp::network::byte_buffer byteBufferFrom(const std::uint8_t* data, std::size_t size)
{
    arp::network::byte_buffer buf{&arp::core::defaultCpuMemoryResource()};
    buf.resize(size);
    if (size > 0) {
        std::memcpy(buf.data(), data, size);
    }
    return buf;
}

void installNetworkPlugin(ModuleRegistry& modules, arp::network::io_context& ioContext)
{
    ScriptPluginServiceRegistry services;
    assert(services.provide(arp::core::pmr::String(arp::scriptvm::kNetworkIoContextService,
                                &arp::core::defaultCpuMemoryResource()),
        1u,
        &ioContext));
    ScriptPluginRegistrar registrar(text("network"), &services);
    ArpScriptPluginHostV2 host{
        ARP_SCRIPT_PLUGIN_ABI_VERSION_V2,
        &registrar,
        &services,
        "test",
        "debug",
        nullptr,
    };
    assert(registerNetworkScriptPlugin(&host) == 1);
    auto exports = registrar.build();
    assert(exports != nullptr);
    assert(modules.installPluginModule(registrar.name(), std::move(exports)));
}

Program compileScript(const char* source)
{
    auto compiled =
        arp::scriptvm::test::buildInterpreter(source, "network-module.arp", "network-module");
    require(compiled.ok(), "compile ok");
    if (!compiled.ok()) {
        std::fprintf(stderr,
            "COMPILE ERROR: %.*s\n",
            (int)compiled.error.message.size(),
            compiled.error.message.data());
        return {};
    }
    return std::move(compiled.program);
}

// Run a script function and drive io_context until the VM completes or times out.
// Returns the final result value.
Value runWithIo(Vm& vm,
    const Program& program,
    const char* funcName,
    arp::network::io_context& ioContext,
    arp::network::io_service* service)
{
    arp::scriptvm::VmError err;
    auto result = vm.runFunctionByName(program, text(funcName), {}, &err);
    for (int i = 0; i < 500 && vm.suspended(); ++i) {
        ioContext.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (vm.suspended()) {
            arp::scriptvm::VmError resumeErr;
            auto pending = vm.resume(program, &resumeErr);
            if (resumeErr.code == arp::scriptvm::ScriptDiagnosticCode::None) {
                result = std::move(pending);
                break;
            }
            require(resumeErr.code == arp::scriptvm::ScriptDiagnosticCode::Suspended,
                "resume should be suspended or success");
        }
    }
    require(!vm.suspended(), "VM should not remain suspended");
    return result;
}

// ---------------------------------------------------------------------------
// Test 1: Module registration and export table
// ---------------------------------------------------------------------------

void testModuleRegistration()
{
    arp::network::io_context ioContext;
    ModuleRegistry modules;
    installNetworkPlugin(modules, ioContext);

    // The four classes must be exported.
    require(modules.hasExport(text("network"), text("TCPSocket")), "network.TCPSocket exported");
    require(modules.hasExport(text("network"), text("UDPSocket")), "network.UDPSocket exported");
    require(modules.hasExport(text("network"), text("HttpClient")), "network.HttpClient exported");
    require(modules.hasExport(text("network"), text("WebSocketClient")),
        "network.WebSocketClient exported");
}

// ---------------------------------------------------------------------------
// Test 2: TCP loopback echo
// ---------------------------------------------------------------------------

void testTcpEcho()
{
    using namespace arp::network;
    SocketRuntime runtime;
    require(runtime.valid(), "SocketRuntime valid");

    io_service service;
    io_context ioContext(&service);
    ModuleRegistry modules;
    installNetworkPlugin(modules, ioContext);

    // Set up a loopback TCP echo server.
    tcp::socket<ipv4> acceptor(&ioContext);
    tcp::socket<ipv4> serverClient(&ioContext);
    error_code ec;
    require(acceptor.open(ec) == 0, "acceptor open");
    require(acceptor.set_reuseaddr(ec, true) == 0, "acceptor reuseaddr");
    require(acceptor.bind(ec, endpoint::localhost<ipv4>(0)) == 0, "acceptor bind");
    require(acceptor.listen(ec, 4) == 0, "acceptor listen");
    const auto listenEndpoint = acceptor.local_endpoint();
    require(listenEndpoint.port() != 0, "acceptor port assigned");

    bool echoed = false;
    acceptor.async_accept(
        serverClient, [&serverClient, &echoed, &ioContext](const error_code& acceptEc) {
            require(!acceptEc, "accept ok");
            auto recvBuf = arp::core::makeShared<byte_buffer>(1024, 0);
            serverClient.async_recv(*recvBuf,
                [&serverClient, &echoed, recvBuf, &ioContext](
                    const error_code& recvEc, std::size_t bytes) {
                    require(!recvEc && bytes > 0, "server recv ok");
                    serverClient.async_send(byteBufferFrom(recvBuf->data(), bytes),
                        [&echoed, &ioContext](const error_code&, std::size_t) {
                            echoed = true;
                            ioContext.shutdown();
                        });
                });
        });

    const std::string portStr = std::to_string(listenEndpoint.port());
    const std::string source = std::string("import network\n")
        + "def echo(host: string, port: int) -> any:\n"
        + "    var sock: any = network.TCPSocket()\n"
        + "    var conn: any = await sock.connect(host, port)\n" + "    if not conn.success:\n"
        + "        sock.close()\n" + "        return conn\n"
        + "    var sent: any = await sock.send(\"hello tcp\")\n" + "    if not sent.success:\n"
        + "        sock.close()\n" + "        return sent\n"
        + "    var recv: any = await sock.recv(64)\n" + "    sock.close()\n" + "    return recv\n";
    std::printf("  compiling script...\n");
    std::fflush(stdout);
    auto program = compileScript(source.c_str());
    std::printf("  script compiled, running VM...\n");
    std::fflush(stdout);

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(makeString(text("127.0.0.1")));
    args.push_back(makeInt(std::stoi(portStr)));

    Vm vm;
    vm.setModuleRegistry(&modules);
    std::printf("  starting reactor thread...\n");
    std::fflush(stdout);
    std::thread reactor([&]() {
        service.run();
    });

    std::printf("  calling runFunction...\n");
    std::fflush(stdout);
    arp::scriptvm::VmError err;
    auto result = vm.runFunction(program, 0, args, &err);
    std::printf("  runFunction returned, driving io...\n");
    std::fflush(stdout);
    for (int i = 0; i < 500 && vm.suspended(); ++i) {
        ioContext.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (vm.suspended()) {
            arp::scriptvm::VmError resumeErr;
            auto pending = vm.resume(program, &resumeErr);
            if (resumeErr.code == arp::scriptvm::ScriptDiagnosticCode::None) {
                result = std::move(pending);
                break;
            }
        }
    }

    if (reactor.joinable()) {
        service.close();
        reactor.join();
    }

    require(!vm.suspended(), "tcp echo: VM not suspended");
    require(echoed, "tcp echo: server echoed");
}

// ---------------------------------------------------------------------------
// Test 3: UDP loopback echo
// ---------------------------------------------------------------------------

void testUdpEcho()
{
    using namespace arp::network;
    SocketRuntime runtime;
    require(runtime.valid(), "SocketRuntime valid (udp)");

    io_service service;
    io_context ioContext(&service);
    ModuleRegistry modules;
    installNetworkPlugin(modules, ioContext);

    // Set up a UDP echo receiver.
    udp::socket<ipv4> receiver(&ioContext);
    error_code ec;
    require(receiver.open(ec) == 0, "udp receiver open");
    require(receiver.bind(ec, endpoint::localhost<ipv4>(0)) == 0, "udp receiver bind");
    const auto recvEndpoint = receiver.local_endpoint();
    require(recvEndpoint.port() != 0, "udp receiver port assigned");

    bool echoed = false;
    auto recvBuf = arp::core::makeShared<byte_buffer>(256, 0);
    receiver.async_recvfrom(*recvBuf,
        [&receiver, &echoed, &ioContext, recvBuf](
            const error_code& recvEc, const endpoint& peer, std::size_t bytes) {
            require(!recvEc && bytes > 0, "udp server recv ok");
            receiver.async_sendto(peer,
                byteBufferFrom(recvBuf->data(), bytes),
                [&echoed, &ioContext](const error_code&, std::size_t) {
                    echoed = true;
                    ioContext.shutdown();
                });
        });

    const std::string portStr = std::to_string(recvEndpoint.port());
    const std::string source = std::string("import network\n")
        + "def echo(host: string, port: int) -> any:\n"
        + "    var sock: any = network.UDPSocket()\n"
        + "    var bound: any = sock.bind(\"0.0.0.0\", 0)\n" + "    if not bound.success:\n"
        + "        sock.close()\n" + "        return bound\n"
        + "    var sent: any = await sock.sendto(host, port, \"hello udp\")\n"
        + "    if not sent.success:\n" + "        sock.close()\n" + "        return sent\n"
        + "    var recv: any = await sock.recvfrom(64)\n" + "    sock.close()\n"
        + "    return recv\n";
    auto program = compileScript(source.c_str());

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(makeString(text("127.0.0.1")));
    args.push_back(makeInt(std::stoi(portStr)));

    Vm vm;
    vm.setModuleRegistry(&modules);
    std::thread reactor([&]() {
        service.run();
    });

    arp::scriptvm::VmError err;
    auto result = vm.runFunction(program, 0, args, &err);
    for (int i = 0; i < 500 && vm.suspended(); ++i) {
        ioContext.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (vm.suspended()) {
            arp::scriptvm::VmError resumeErr;
            auto pending = vm.resume(program, &resumeErr);
            if (resumeErr.code == arp::scriptvm::ScriptDiagnosticCode::None) {
                result = std::move(pending);
                break;
            }
        }
    }

    if (reactor.joinable()) {
        service.close();
        reactor.join();
    }

    require(!vm.suspended(), "udp echo: VM not suspended");
    require(echoed, "udp echo: server echoed");
}

// ---------------------------------------------------------------------------
// Test 4: HTTP loopback GET
// ---------------------------------------------------------------------------

void testHttpGet()
{
    using namespace arp::network;
    SocketRuntime runtime;
    require(runtime.valid(), "SocketRuntime valid (http)");

    io_service service;
    io_context ioContext(&service);
    ModuleRegistry modules;
    installNetworkPlugin(modules, ioContext);

    // Set up a minimal HTTP server that responds with 200 OK.
    tcp::socket<ipv4> acceptor(&ioContext);
    tcp::socket<ipv4> serverClient(&ioContext);
    error_code ec;
    require(acceptor.open(ec) == 0, "http acceptor open");
    require(acceptor.set_reuseaddr(ec, true) == 0, "http acceptor reuseaddr");
    require(acceptor.bind(ec, endpoint::localhost<ipv4>(0)) == 0, "http acceptor bind");
    require(acceptor.listen(ec, 4) == 0, "http acceptor listen");
    const auto listenEndpoint = acceptor.local_endpoint();
    require(listenEndpoint.port() != 0, "http acceptor port assigned");

    bool responded = false;
    http::Session serverSession;
    acceptor.async_accept(serverClient,
        [&serverClient, &serverSession, &responded, &ioContext](const error_code& acceptEc) {
            require(!acceptEc, "http accept ok");
            serverSession.async_read_request(serverClient,
                [&serverClient, &serverSession, &responded, &ioContext](
                    const http::SessionStatus&, const http::Request&) {
                    http::Response response;
                    response.set_status(http::StatusCode(200));
                    response.set_reason("OK");
                    response.headers().add("Content-Type", "text/plain");
                    response.set_body_text("hello http");
                    serverSession.async_write_response(serverClient,
                        response,
                        [&responded, &ioContext](const http::SessionStatus&) {
                            responded = true;
                            ioContext.shutdown();
                        });
                });
        });

    const std::string portStr = std::to_string(listenEndpoint.port());
    const std::string url = "http://127.0.0.1:" + portStr + "/test";
    const std::string source = std::string("import network\n") + "def fetch(url: string) -> any:\n"
        + "    var client: any = network.HttpClient()\n" + "    return await client.get(url)\n";
    auto program = compileScript(source.c_str());

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(makeString(text(url.c_str())));

    Vm vm;
    vm.setModuleRegistry(&modules);
    std::thread reactor([&]() {
        service.run();
    });

    arp::scriptvm::VmError err;
    auto result = vm.runFunction(program, 0, args, &err);
    for (int i = 0; i < 500 && vm.suspended(); ++i) {
        ioContext.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (vm.suspended()) {
            arp::scriptvm::VmError resumeErr;
            auto pending = vm.resume(program, &resumeErr);
            if (resumeErr.code == arp::scriptvm::ScriptDiagnosticCode::None) {
                result = std::move(pending);
                break;
            }
        }
    }

    if (reactor.joinable()) {
        service.close();
        reactor.join();
    }

    require(!vm.suspended(), "http get: VM not suspended");
    require(responded, "http get: server responded");
}

// ---------------------------------------------------------------------------
// Test 5: WebSocket connect (graceful failure without full WS server)
// ---------------------------------------------------------------------------

void testWsConnect()
{
    using namespace arp::network;
    SocketRuntime runtime;
    require(runtime.valid(), "SocketRuntime valid (ws)");

    io_service service;
    io_context ioContext(&service);
    ModuleRegistry modules;
    installNetworkPlugin(modules, ioContext);

    // The current cleartext WebSocket facade validates and resolves ws://
    // without opening a transport. Keep this test focused on the script
    // binding and do not create an unserviced native accept operation.
#if defined(ARP_NETWORK_TLS_BACKEND_OPENSSL)
    // OpenSSL builds exercise the deterministic malformed-URI path here;
    // encrypted loopback exchange is covered by the dedicated network smoke.
    const std::string url = "wss://";
#else
    const std::string url = "wss://127.0.0.1:1/chat";
#endif
    const std::string source = std::string("import network\n")
        + "def connect(url: string) -> any:\n" + "    var client: any = network.WebSocketClient()\n"
        + "    return await client.connect(url)\n";
    auto program = compileScript(source.c_str());

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(makeString(text(url.c_str())));

    Vm vm;
    vm.setModuleRegistry(&modules);

    arp::scriptvm::VmError err;
    auto result = vm.runFunction(program, 0, args, &err);
    for (int i = 0; i < 300 && vm.suspended(); ++i) {
        ioContext.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (vm.suspended()) {
            arp::scriptvm::VmError resumeErr;
            auto pending = vm.resume(program, &resumeErr);
            if (resumeErr.code == arp::scriptvm::ScriptDiagnosticCode::None) {
                result = std::move(pending);
                break;
            }
            // HostError is acceptable (WS handshake fails without a real server).
            if (resumeErr.code == arp::scriptvm::ScriptDiagnosticCode::HostError) {
                break;
            }
        }
    }

    require(!vm.suspended(), "ws connect: VM not suspended (should resolve or fail)");
}

} // namespace

int main()
{
    std::printf("[1/5] testModuleRegistration...\n");
    std::fflush(stdout);
    testModuleRegistration();
    std::printf("[2/5] testTcpEcho...\n");
    std::fflush(stdout);
    testTcpEcho();
    std::printf("[3/5] testUdpEcho...\n");
    std::fflush(stdout);
    testUdpEcho();
    std::printf("[4/5] testHttpGet...\n");
    std::fflush(stdout);
    testHttpGet();
    std::printf("[5/5] testWsConnect...\n");
    std::fflush(stdout);
    testWsConnect();

    if (g_failures == 0) {
        std::printf("All ScriptVM network module tests passed\n");
        return 0;
    }
    std::printf("%d test(s) failed\n", g_failures);
    return 1;
}
