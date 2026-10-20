#include "scriptvm/aot/aot_codegen_v5.h"
#include "tests/fixtures/script_component_fixture.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv)
{
    using namespace arp;
    using namespace scriptvm;
    if (argc != 3 && argc != 4)
        return 2;
    const bool domains = argc == 4;
    tests::ScriptComponentFixture fixture;
    if (!(domains ? fixture.installDomains() : fixture.install()))
        return 3;
    auto& modules = fixture.scope.modules();
    std::ifstream stream(argv[1]);
    const std::string bytes((std::istreambuf_iterator<char>(stream)), {});
    core::pmr::String source(bytes.c_str());
    auto budget = tests::ScriptComponentFixture::budget();
    ScriptComponentModule module;
    if (!module.load(source,
            "components",
            "test-script",
            1,
            modules,
            domains ? core::pmr::Vector<core::pmr::String>{"arp_scene", "arp_scene2d"}
                    : core::pmr::Vector<core::pmr::String>{"arp_scene"},
            budget,
            32)) {
        std::fprintf(stderr, "%s\n", module.diagnostic().c_str());
        return 5;
    }
    DirectNativeCodegenOptionsV5 options;
    options.packageId = "components";
    options.packageIdentity = "components@1";
    options.sourceHash =
        core::Sha256::hexDigest(core::StringView(ScriptComponentModule::compose(source)));
    options.dependencyGraphHash = core::Sha256::hexDigest(core::StringView("arp_scene/1"));
    auto exports = modules.loadModule("arp_scene");
    options.reflectionRegistryHash = exports->reflectionRegistryHash;
    options.hostContractHash = exports->exportContractHash;
    AotPluginDependencyManifestV5 dependency;
    dependency.moduleId = "arp_scene";
    dependency.moduleVersion = 1;
    dependency.exportContractHash = exports->exportContractHash;
    dependency.reflectionRegistryHash = exports->reflectionRegistryHash;
    options.pluginDependencies.push_back(std::move(dependency));
    if (domains) {
        auto second = modules.loadModule("arp_scene2d");
        AotPluginDependencyManifestV5 d;
        d.moduleId = "arp_scene2d";
        d.moduleVersion = 1;
        d.exportContractHash = second->exportContractHash;
        d.reflectionRegistryHash = second->reflectionRegistryHash;
        options.pluginDependencies.push_back(std::move(d));
    }
    options.registrationSymbol = "arp_register_components_v5";
    options.platforms = currentAotPlatform();
    options.budget = budget;
    auto result = generateDirectNativeModuleV5(module.session().typedIr(), options);
    if (!result.ok()) {
        std::fprintf(stderr, "%s\n", result.error.message.c_str());
        for (const auto& f : module.session().typedIr().functions)
            for (const auto& b : f.blocks)
                for (const auto& op : b.operations)
                    if (op.opcode == TypedIrOpcode::CallValue && op.callableBindingId.empty()
                        && op.closureTargetFunctionId.empty())
                        std::fprintf(stderr,
                            "unbound %s line=%d target=%s\n",
                            f.id.c_str(),
                            op.source.line,
                            op.targetId.c_str());
        for (const auto& binding : module.session().typedIr().callableBindings)
            if (binding.targetId.find("PhysicsWorld") != core::pmr::String::npos)
                std::fprintf(
                    stderr, "contract=%s arity=%d\n", binding.targetId.c_str(), binding.fixedArity);
        return 6;
    }
    std::filesystem::path folder(argv[2]);
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "script_aot_v5.gen.h") << result.header;
    std::ofstream(folder / "components.gen.cpp") << result.source;
    std::ofstream(folder / "components.manifest.yaml") << result.manifestYaml;
}
