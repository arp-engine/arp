#include "content.h"
#include "native_plugins.h"
#include "scriptvm/aot/aot_codegen_v5.h"
#include "scriptvm/plugin/plugin_runtime_scope.h"
#include "scriptvm/runtime/script_session.h"

#include <cstdio>
#include <fstream>

int main(int argc, char** argv)
{
    using namespace arp;
    if (argc != 3 && argc != 4)
        return 2;
    core::pmr::String root(argv[1]), diagnostic;
    survivors::GameConfig config;
    survivors::Rules rules;
    if (!config.load(root, diagnostic) || !rules.load(root, config.maxSourceBytes, diagnostic)) {
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
        return 1;
    }
    resource::ResourceManager resources;
    survivors::Content content(resources);
    if (!content.load(root, config, diagnostic)) {
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
        return 1;
    }
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
    if (argc == 4) {
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
    core::pmr::String contracts;
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
    auto generated = scriptvm::generateDirectNativeModuleV5(module.session().typedIr(), options);
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
    std::filesystem::path out(argv[2]);
    std::filesystem::create_directories(out);
    std::ofstream(out / "script_aot_v5.gen.h") << generated.header;
    std::ofstream(out / "survivors.gen.cpp") << generated.source;
    std::ofstream(out / "survivors.manifest.yaml") << generated.manifestYaml;
    std::puts("Generated ArpScript DirectNative package (no handwritten gameplay C++)");
}
