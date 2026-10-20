// ArpScript network module smoke test.
//
// Minimal TCP loopback echo through the script VM, proving the class-based
// network binding integrates end-to-end.

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

} // namespace

int main()
{
    using namespace arp::network;
    SocketRuntime runtime;
    assert(runtime.valid());

    io_service service;
    io_context ioContext(&service);
    ModuleRegistry modules;
    ScriptPluginServiceRegistry services;
    if (!services.provide(arp::core::pmr::String(
                              kNetworkIoContextService, &arp::core::defaultCpuMemoryResource()),
            1u,
            &ioContext))
        return 1;
    ScriptPluginRegistrar registrar(
        arp::core::pmr::String("network", &arp::core::defaultCpuMemoryResource()), &services);
    ArpScriptPluginHostV2 host{
        ARP_SCRIPT_PLUGIN_ABI_VERSION_V2, &registrar, &services, "test", "debug", nullptr};
    if (registerNetworkScriptPlugin(&host) != 1)
        return 1;
    auto exports = registrar.build();
    if (!exports || !modules.installPluginModule(registrar.name(), std::move(exports)))
        return 1;

    // Minimal TCP echo server.
    tcp::socket<ipv4> acceptor(&ioContext);
    tcp::socket<ipv4> serverClient(&ioContext);
    error_code ec;
    assert(acceptor.open(ec) == 0);
    assert(acceptor.set_reuseaddr(ec, true) == 0);
    assert(acceptor.bind(ec, endpoint::localhost<ipv4>(0)) == 0);
    assert(acceptor.listen(ec, 4) == 0);
    const auto listenEndpoint = acceptor.local_endpoint();
    assert(listenEndpoint.port() != 0);

    bool echoed = false;
    acceptor.async_accept(
        serverClient, [&serverClient, &echoed, &ioContext](const error_code& acceptEc) {
            assert(!acceptEc);
            auto recvBuf = arp::core::makeShared<byte_buffer>(64, 0);
            serverClient.async_recv(*recvBuf,
                [&serverClient, &echoed, recvBuf, &ioContext](
                    const error_code& recvEc, std::size_t bytes) {
                    assert(!recvEc && bytes > 0);
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
        + "    var sent: any = await sock.send(\"smoke\")\n"
        + "    var recv: any = await sock.recv(32)\n" + "    sock.close()\n" + "    return recv\n";

    auto compiled = arp::scriptvm::test::buildInterpreter(
        source.c_str(), "network-smoke.arp", "network-smoke", &modules);
    if (!compiled.ok()) {
        std::fprintf(stderr,
            "COMPILE ERROR: %.*s\n",
            (int)compiled.error.message.size(),
            compiled.error.message.data());
        return 1;
    }
    auto program = std::move(compiled.program);

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(makeString(text("127.0.0.1")));
    args.push_back(makeInt(std::stoi(portStr)));

    Vm vm;
    vm.setModuleRegistry(&modules);
    std::thread reactor([&]() {
        service.run();
    });

    VmError err;
    auto result = vm.runFunction(program, 0, args, &err);
    for (int i = 0; i < 500 && vm.suspended(); ++i) {
        ioContext.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (vm.suspended()) {
            VmError resumeErr;
            auto pending = vm.resume(program, &resumeErr);
            if (resumeErr.code == ScriptDiagnosticCode::None) {
                result = std::move(pending);
                break;
            }
        }
    }

    if (reactor.joinable()) {
        service.close();
        reactor.join();
    }

    assert(!vm.suspended());
    assert(echoed);
    std::printf("ScriptVM network module smoke test passed\n");
    return 0;
}
