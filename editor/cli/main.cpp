#include "core/file_io.h"
#include "network/io_context.h"
#include "network/io_service.h"
#include "network/network_module.h"
#include "network/socket_runtime.h"
#include "scriptvm/interpreter/pipeline.h"
#include "scriptvm/interpreter/vm.h"
#include "scriptvm/runtime/module_registry.h"
#include "scriptvm/runtime/value.h"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>

extern bool arp_add_arp_network_static_plugin(arp::scriptvm::StaticPluginCatalog&);

namespace {
arp::core::pmr::String environmentValue(const char* name, const char* fallback)
{
    const auto* value = std::getenv(name);
    return arp::core::pmr::String(value ? value : fallback, &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String diagnosticText(const arp::scriptvm::ScriptDiagnostic& error)
{
    arp::core::pmr::String message(&arp::core::defaultCpuMemoryResource());
    if (!error.file.empty()) {
        message += error.file;
        message += ":";
    }
    if (error.line > 0) {
        message += std::to_string(error.line);
        message += ":";
        message += std::to_string(error.column);
        message += " ";
    }
    message += error.message;
    return message;
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    network::SocketRuntime socketRuntime;
    if (!socketRuntime.valid()) {
        std::fprintf(stderr, "arp_editor_cli: network runtime initialization failed\n");
        return 10;
    }

    network::io_service service;
    network::io_context context(&service);
    scriptvm::ScriptPluginServiceRegistry services;
    if (!services.provide(core::pmr::String(scriptvm::kNetworkIoContextService,
                              &core::defaultCpuMemoryResource()),
            scriptvm::kNetworkIoContextServiceVersion,
            &context)) {
        std::fprintf(stderr, "arp_editor_cli: network service registration failed\n");
        return 10;
    }
    scriptvm::StaticPluginCatalog catalog;
    if (!arp_add_arp_network_static_plugin(catalog)) {
        std::fprintf(stderr, "arp_editor_cli: network plugin registration failed\n");
        return 10;
    }
    scriptvm::ModuleRegistry modules;
    modules.setStaticPluginCatalog(&catalog, &services);

    const auto source =
        core::readTextFile(std::filesystem::path(ARP_SOURCE_DIR) / "editor/cli/arp_editor_cli.arp");
    if (!source.success) {
        std::fprintf(stderr, "arp_editor_cli: %s\n", source.diagnostics.c_str());
        return 10;
    }
    scriptvm::ScriptInterpreterBuildRequest request;
    request.source = source.text;
    request.filename = "editor/cli/arp_editor_cli.arp";
    request.moduleId = "arp_editor_cli";
    request.moduleVersion = 1;
    request.dependencyGraphHash =
        "sha256:0000000000000000000000000000000000000000000000000000000000000000";
    request.reflectionRegistryHash =
        "sha256:0000000000000000000000000000000000000000000000000000000000000000";
    request.moduleRegistry = &modules;
    const auto built = scriptvm::ScriptInterpreterPipeline::build(request);
    if (!built.ok()) {
        const auto message = diagnosticText(built.error);
        std::fprintf(stderr, "arp_editor_cli: %s\n", message.c_str());
        return 10;
    }

    reflection::hlist scriptArguments = reflection::makeEmptyHList();
    scriptArguments.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index)
        scriptArguments.push_back(scriptvm::makeString(std::string_view(argv[index])));
    core::pmr::Vector<scriptvm::Value> arguments(&core::defaultCpuMemoryResource());
    arguments.push_back(scriptvm::Value(std::move(scriptArguments)));
    arguments.push_back(
        scriptvm::makeString(environmentValue("ARP_EDITOR_REMOTE_URL", "http://127.0.0.1:8765")));
    arguments.push_back(scriptvm::makeString(environmentValue("ARP_EDITOR_REMOTE_TOKEN", "")));
    scriptvm::Vm vm;
    vm.setModuleRegistry(&modules);
    scriptvm::VmError error;
    auto result = vm.runFunctionByName(built.program, "main", arguments, &error);
    std::thread reactor;
    if (vm.suspended())
        reactor = std::thread([&service] {
            service.run();
        });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (vm.suspended() && std::chrono::steady_clock::now() < deadline) {
        context.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (vm.suspended())
            result = vm.resume(built.program, &error);
    }
    service.close();
    if (reactor.joinable())
        reactor.join();
    if (vm.suspended()) {
        std::fprintf(stderr, "arp_editor_cli: request timed out\n");
        return 10;
    }
    if (error.code != scriptvm::ScriptDiagnosticCode::None) {
        std::fprintf(stderr, "arp_editor_cli: %s\n", error.message.c_str());
        if (!error.traceback.empty())
            std::fprintf(stderr, "%s\n", error.traceback.c_str());
        return 10;
    }
    core::pmr::String output(&core::defaultCpuMemoryResource());
    if (!scriptvm::tryGetString(result, output)) {
        std::fprintf(stderr, "arp_editor_cli: script returned an invalid CLI result\n");
        return 10;
    }
    const std::string_view resultView(output.data(), output.size());
    const auto separator = resultView.find('\n');
    if (separator == std::string_view::npos) {
        std::fprintf(stderr, "arp_editor_cli: script result is missing its exit code\n");
        return 10;
    }
    int exitCode = 0;
    const auto parsedCode =
        std::from_chars(resultView.data(), resultView.data() + separator, exitCode);
    if (parsedCode.ec != std::errc{} || parsedCode.ptr != resultView.data() + separator
        || exitCode < 0 || exitCode > 255) {
        std::fprintf(stderr, "arp_editor_cli: script returned an invalid exit code\n");
        return 10;
    }
    const auto message = resultView.substr(separator + 1);
    if (!message.empty())
        std::fwrite(message.data(), 1, message.size(), exitCode == 0 ? stdout : stderr);
    return exitCode;
}
