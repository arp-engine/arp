#include "codegen_io.h"
#include "native_plugins.h"
#include "scriptvm/aot/aot_codegen_v5.h"
#include "scriptvm/plugin/plugin_runtime_scope.h"
#include "scriptvm/runtime/script_session.h"

#include <charconv>
#include <cstdio>

namespace {
class CodegenApplication final {
public:
#if defined(_WIN32)
    int run(int argc, wchar_t** argv) const
#else
    int run(int argc, char** argv) const
#endif
    {
        using namespace arp;
        if (argc < 3)
            return 2;
        const auto argument = [&](int index) {
            const auto bytes = std::filesystem::path(argv[index]).u8string();
            return core::pmr::String(reinterpret_cast<const char*>(bytes.data()),
                bytes.size(),
                &core::defaultCpuMemoryResource());
        };
        bool check = false;
        auto source = examples::NativeStartupSource::Directory;
        core::pmr::String baseUrl(&core::defaultCpuMemoryResource());
        std::uint64_t maxOutputBytes = survivors::CodegenIoLimits::maxOutputBytes;
        for (int i = 3; i < argc; ++i) {
            const auto option = argument(i);
            if (option == "--check")
                check = true;
            else if (option == "--source-base-url" && i + 1 < argc) {
                baseUrl = argument(++i);
                source = examples::NativeStartupSource::Http;
                if (baseUrl.empty()) {
                    std::fputs("SURVIVORS-CODEGEN-SOURCE-CONFIG\n", stderr);
                    return 2;
                }
            } else if (option == "--max-output-bytes" && i + 1 < argc) {
                const auto number = argument(++i);
                const auto parsed =
                    std::from_chars(number.data(), number.data() + number.size(), maxOutputBytes);
                if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()
                    || !maxOutputBytes
                    || maxOutputBytes > survivors::CodegenIoLimits::maxOutputBytes) {
                    std::fputs("SURVIVORS-CODEGEN-OUTPUT-OPTIONS\n", stderr);
                    return 2;
                }
            } else {
                std::fputs("SURVIVORS-CODEGEN-OPTIONS\n", stderr);
                return 2;
            }
        }
        core::pmr::String diagnostic(&core::defaultCpuMemoryResource());
        survivors::CodegenIo io;
        const auto input =
            io.prepareInputs(std::filesystem::path(argv[1]), source, baseUrl, diagnostic);
        if (!input.gameplay || !input.content) {
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
            return 1;
        }
        const auto& config = input.gameplay->config;
        const auto& rules = input.gameplay->rules;
        physics2d::PhysicsWorld2D world(config.physics);
        scene::Scene scene;
        scene::MainSceneRegistry scenes;
        scriptvm::ScriptPluginRuntimeScope scope;
        if (!survivors::installNativePlugins(scope, scene, scenes, world))
            return 1;
        auto& modules = scope.modules();
        scriptvm::ScriptComponentModule module;
        if (!module.load(rules.source(),
                "survivors",
                "20000000-0000-4000-8000-000000009001",
                1,
                modules,
                {"arp_scene", "arp_scene2d"},
                config.executionBudget,
                config.maxScriptInstances)) {
            std::fprintf(stderr, "%s\n", module.diagnostic().c_str());
            return 1;
        }
        if (check) {
            std::puts("PASS canonical component module compilation");
            return 0;
        }
        scriptvm::DirectNativeCodegenOptionsV5 options;
        options.packageId = "survivors";
        options.packageIdentity = "survivors@1";
        options.sourceHash = core::Sha256::hexDigest(
            core::StringView(scriptvm::ScriptComponentModule::compose(rules.source())));
        options.dependencyGraphHash =
            core::Sha256::hexDigest(core::StringView("arp_scene/1;arp_scene2d/1"));
        core::pmr::String contracts(&core::defaultCpuMemoryResource());
        for (const char* name : {"arp_scene", "arp_scene2d"}) {
            auto exports = modules.loadModule(name);
            contracts += exports->reflectionRegistryHash + exports->exportContractHash;
            scriptvm::AotPluginDependencyManifestV5 dependency;
            dependency.moduleId = name;
            dependency.moduleVersion = 1;
            dependency.exportContractHash = exports->exportContractHash;
            dependency.reflectionRegistryHash = exports->reflectionRegistryHash;
            options.pluginDependencies.push_back(std::move(dependency));
        }
        options.reflectionRegistryHash = core::Sha256::hexDigest(core::StringView(contracts));
        options.hostContractHash = options.reflectionRegistryHash;
        options.registrationSymbol = "arp_register_survivors_v5";
        options.platforms = scriptvm::currentAotPlatform();
        options.budget = config.executionBudget;
        auto generated =
            scriptvm::generateDirectNativeModuleV5(module.session().typedIr(), options);
        if (!generated.ok()) {
            std::fprintf(stderr, "%s\n", generated.error.message.c_str());
            for (const auto& f : module.session().typedIr().functions)
                for (const auto& block : f.blocks)
                    for (const auto& op : block.operations)
                        if (op.opcode == scriptvm::TypedIrOpcode::CallValue
                            && op.callableBindingId.empty() && op.closureTargetFunctionId.empty())
                            std::fprintf(stderr,
                                "unbound %s line=%d target=%s\n",
                                f.id.c_str(),
                                op.source.line,
                                op.targetId.c_str());
            return 1;
        }
        if (!io.saveOutputs(std::filesystem::path(argv[2]),
                {generated.header, generated.source, generated.manifestYaml},
                maxOutputBytes,
                diagnostic)) {
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
            return 1;
        }
        std::puts("Generated ArpScript DirectNative package (no handwritten gameplay C++)");
        return 0;
    }
};
} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    try {
        return CodegenApplication{}.run(argc, argv);
    } catch (const std::bad_alloc&) {
        std::fputs("SURVIVORS-CODEGEN-ALLOCATION\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "SURVIVORS-CODEGEN-FAILURE: %s\n", error.what());
    }
    return 1;
}
