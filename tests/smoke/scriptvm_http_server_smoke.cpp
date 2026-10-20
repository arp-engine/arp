#include "network/network_aot.h"
#include "network/network_module.h"
#include "scriptvm/aot/aot_codegen_v5.h"
#include "scriptvm/interpreter/vm.h"
#include "tests/fixtures/script_http_server_fixture.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef ARP_HTTP_SERVER_AOT
extern "C" int arp_register_http_server_test_v5(arp::scriptvm::AotRegistrationHostV5* host);
#endif

using namespace arp::scriptvm;

namespace {
using arp::core::pmr::String;
using arp::scriptvm::test::ScriptServerFixture;

void fail(const VmError& error)
{
    std::fprintf(stderr,
        "script error %d: %.*s\n",
        int(error.code),
        int(error.message.size()),
        error.message.data());
    assert(false);
}

#ifdef ARP_HTTP_SERVER_AOT
class Runner {
public:
    explicit Runner(ScriptServerFixture& fixture)
    {
        AotRegistrationHostV5 host{kAotContractVersion,
            [](const AotPackageDescriptorV5* package, void* user) {
                *static_cast<const AotPackageDescriptorV5**>(user) = package;
                return true;
            },
            &package_};
        assert(arp_register_http_server_test_v5(&host) == 1);
        module_ = &package_->modules[0];
        context_->lifetimeOwner = context_;
        context_->memoryQuota =
            arp::core::makeShared<arp::core::CpuMemoryQuota>(module_->budget.maxMemoryBytes);
        context_->valueMemoryDescriptor = &describeNetworkScriptValue;
        context_->packageDescriptor = package_;
        context_->moduleDescriptor = module_;
        context_->moduleId = module_->moduleId;
        context_->moduleVersion = module_->moduleVersion;
        context_->artifactIdentity = module_->artifactIdentity;
        context_->resetBudget(module_->budget);
        context_->boundCallUserData = &fixture.modules;
        context_->boundCall = [](void* user,
                                  const char* binding,
                                  const char* target,
                                  const Value* receiver,
                                  const Value* arguments,
                                  std::size_t count,
                                  Value* result,
                                  VmError* error) {
            auto& registry = *static_cast<ModuleRegistry*>(user);
            for (const auto& contract :
                registry.queryReflectionCallableContracts(String("arp_network"))) {
                if (contract.bindingId != binding || contract.targetId != target
                    || !contract.valid() || contract.fixedArity != static_cast<int>(count))
                    continue;
                if (receiver && receiver->class_()) {
                    for (const auto& field : receiver->class_()->get_all_fields()) {
                        if (!field.second.is_function())
                            continue;
                        auto fn = field.second.cast<arp::reflection::function>();
                        if (fn.identity() != contract.functionIdentity)
                            continue;
                        arp::core::pmr::Vector<Value> args(arguments, arguments + count);
                        *result = fn.call_v(*receiver, args);
                        return true;
                    }
                }
            }
            error->code = ScriptDiagnosticCode::AotMissingEntry;
            error->message = "HTTP fixture reflection binding identity or arity mismatch";
            return false;
        };
        context_->moduleExportUserData = &fixture.modules;
        context_->moduleExport =
            [](void* user, const char* module, const char* name, Value* result, VmError* error) {
                const auto* value =
                    static_cast<ModuleRegistry*>(user)->findExport(String(module), String(name));
                if (!value) {
                    error->code = ScriptDiagnosticCode::AotModuleUnavailable;
                    error->message = "HTTP server plugin export is unavailable";
                    return false;
                }
                *result = *value;
                return true;
            };
        VmError error;
        assert(context_->ensureGlobals(module_->globalCount, &error));
        for (std::size_t i = 0; i < module_->functionCount; ++i) {
            if (std::strcmp(module_->functions[i].functionId, module_->moduleInitFunctionId) == 0)
                module_->functions[i].entry(*context_, nullptr, 0, nullptr, &error);
        }
        if (error.code != ScriptDiagnosticCode::None)
            fail(error);
        context_->moduleInitialized = true;
    }

    Value run(const char* name, const arp::core::pmr::Vector<Value>& args = {})
    {
        entry_ = nullptr;
        for (std::size_t i = 0; i < module_->functionCount; ++i)
            if (String(module_->functions[i].functionId) == String("arp-fn:http_server/") + name)
                entry_ = &module_->functions[i];
        assert(entry_);
        assert(entry_->executionKind == AotFunctionExecutionKind::DirectNative);
        context_->resetBudget(module_->budget);
        generator_ = makeNil();
        auto value = invoke(args.data(), args.size());
        if (asDirectNativeGeneratorV5(value)) {
            generator_ = value;
            return resume();
        }
        return value;
    }

    Value resume()
    {
        VmError error;
        auto result = resumeDirectNativeGeneratorV5(*asDirectNativeGeneratorV5(generator_), &error);
        suspended_ = error.code == ScriptDiagnosticCode::Suspended;
        if (!suspended_ && error.code != ScriptDiagnosticCode::None)
            fail(error);
        return result;
    }

    bool suspended() const { return suspended_; }

private:
    Value invoke(const Value* args, std::size_t count)
    {
        arp::core::CpuMemoryQuotaScope quota(context_->memoryQuota);
        VmError error;
        Value result = entry_->entry(*context_, args, count, nullptr, &error);
        suspended_ = error.code == ScriptDiagnosticCode::Suspended;
        if (!suspended_ && error.code != ScriptDiagnosticCode::None)
            fail(error);
        return result;
    }

    const AotPackageDescriptorV5* package_ = nullptr;
    const AotModuleDescriptorV5* module_ = nullptr;
    const AotFunctionDescriptorV5* entry_ = nullptr;
    arp::core::SharedPtr<DirectNativeExecutionContext> context_ =
        arp::core::makeShared<DirectNativeExecutionContext>();
    Value generator_;
    bool suspended_ = false;
};
#else
class Runner {
public:
    explicit Runner(ScriptServerFixture& fixture)
    {
        std::ifstream file(
            std::filesystem::path(ARP_SOURCE_DIR) / "examples/network/http_server.arp");
        String source;
        for (String line; std::getline(file, line);) {
            source += line;
            source += '\n';
        }
        built_ = arp::scriptvm::test::buildInterpreter(
            std::move(source), String("http_server.arp"), String("http_server"), &fixture.modules);
        if (!built_.ok()) {
            std::fprintf(
                stderr, "%.*s\n", int(built_.error.message.size()), built_.error.message.data());
            assert(false);
        }
        vm_.setModuleRegistry(&fixture.modules);
    }

    Value run(const char* name, const arp::core::pmr::Vector<Value>& args = {})
    {
        VmError error;
        generator_ = makeNil();
        auto value = vm_.runFunctionByName(built_.program, String(name), args, &error);
        if (asScriptGenerator(value)) {
            generator_ = value;
            return resume();
        }
        suspended_ = vm_.suspended();
        if (error.code != ScriptDiagnosticCode::None
            && error.code != ScriptDiagnosticCode::Suspended)
            fail(error);
        return value;
    }

    Value resume()
    {
        VmError error;
        auto* generator = asScriptGenerator(generator_);
        auto result = generator ? vm_.resumeGeneratorValue(*generator, error)
                                : vm_.resume(built_.program, &error);
        suspended_ = generator ? error.code == ScriptDiagnosticCode::Suspended : vm_.suspended();
        if (error.code != ScriptDiagnosticCode::None
            && error.code != ScriptDiagnosticCode::Suspended)
            fail(error);
        return result;
    }

    bool suspended() const { return suspended_; }

    void generate(const std::filesystem::path& directory)
    {
        DirectNativeCodegenOptionsV5 options;
        options.packageId = "http_server_test";
        options.packageIdentity = "http_server_test@1";
        options.sourceHash = arp::scriptvm::test::fixtureHash('2');
        options.dependencyGraphHash = arp::scriptvm::test::fixtureHash('0');
        options.reflectionRegistryHash = arp::scriptvm::test::fixtureHash('1');
        options.hostContractHash = arp::scriptvm::test::fixtureHash('3');
        options.registrationSymbol = "arp_register_http_server_test_v5";
        options.platforms = currentAotPlatform();
        options.budget = {1000000, 64, 4096, 4096, 128, 64, 32u * 1024u * 1024u};
        auto generated = generateDirectNativeModuleV5(built_.typedIr, options);
        if (!generated.ok()) {
            std::fprintf(stderr,
                "%.*s\n",
                int(generated.error.message.size()),
                generated.error.message.data());
            assert(false);
        }
        std::filesystem::create_directories(directory);
        std::ofstream(directory / "script_aot_v5.gen.h") << generated.header;
        std::ofstream(directory / "http_server.gen.cpp") << generated.source;
        std::ofstream(directory / "http_server.manifest.yaml") << generated.manifestYaml;
    }

private:
    ScriptInterpreterBuildResult built_;
    Vm vm_;
    Value generator_;
    bool suspended_ = false;
};
#endif
} // namespace

int main(int argc, char** argv)
{
    ScriptServerFixture f;
    Runner runner(f);
#ifndef ARP_HTTP_SERVER_AOT
    if (argc == 3 && std::strcmp(argv[1], "--generate-aot") == 0) {
        runner.generate(argv[2]);
        return 0;
    }
#endif
    auto server = runner.run("setup");
    assert(server.is_native_reference() && server.native_reference_alive());
    assert(server.call("is_running").cast<bool>());
    const auto address = arp::network::endpoint(
        "127.0.0.1", static_cast<arp::network::uint16>(server.call("local_port").cast<int>()));
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(server);
    for (int i = 0; i < 3; ++i) {
        auto result = runner.run("serve_one", args);
        assert(runner.suspended());
        auto client = arp::core::makeUnique<arp::network::http::test::RawClient>(&f.context);
        auto* raw = client.get();
        f.clients.push_back(std::move(client));
        raw->connect(address,
            i == 0 ? "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n"
                : i == 1
                ? "GET /users/42?q=ok HTTP/1.1\r\nHost: localhost\r\n\r\n"
                : "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\necho");
        f.until([&] {
            if (runner.suspended())
                result = runner.resume();
            return !runner.suspended() && raw->done;
        });
        int bytes = 0;
        assert(tryGetInt(result, bytes) && bytes > 0);
        assert(raw->response.find("Connection: close") != raw->response.npos);
        assert(raw->response.find("X-ARP: script-server") != raw->response.npos);
        assert(raw->response.find(i == 0 ? "healthy"
                       : i == 1          ? "42"
                                         : "echo")
            != raw->response.npos);
    }
    assert(runner.run("shutdown", args).cast<bool>());
    assert(!server.native_reference_alive());
    // Drain the reactor-owned listener close before the module/context dies.
    bool drained = false;
    f.context.run_after(std::chrono::milliseconds(40), [&] {
        drained = true;
    });
    f.until([&] {
        return drained;
    });
}
