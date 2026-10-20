#include "shader/shader_compiler.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <type_traits>

#ifdef NDEBUG
#error Shader target contract tests require assertions
#endif

namespace {

using namespace arp::shader;

class ShaderCompileTargetFixture {
public:
    ShaderCompileTargetFixture()
    {
        request_.sourcePath = "scene.slang";
        // Existing compiler metadata fixture; not a new executable shader.
        request_.sourceCode = "cbuffer SceneConstants;"
                              "StructuredBuffer<float4> ClusterLightList;"
                              "Texture2D BaseColorTexture;"
                              "SamplerState LinearSampler;";
        request_.moduleName = "SceneStylizedPbr";
        request_.entryPoints = {
            {"vertexMain", ShaderStage::Vertex}, {"fragmentMain", ShaderStage::Fragment}};
    }

    void verifyUnknownTargets()
    {
        constexpr int unknowns[] = {
            -1, 3, 4, 5, 255, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
        for (const int value : unknowns) {
            for (unsigned missing = 0; missing != 8; ++missing) {
                auto request = request_;
                request.target = static_cast<TargetBackend>(value);
                if (missing & 1u)
                    request.moduleName.clear();
                if (missing & 2u)
                    request.sourceCode.clear();
                if (missing & 4u)
                    request.entryPoints.clear();
                const auto before = request;
                const auto result = compiler_.compile(request);
                assert(!result.success);
                assert(result.diagnostics == "SHADER-COMPILE-TARGET-UNKNOWN");
                assert(result.target == request.target);
                assert(result.bytecode.empty() && result.bindings.empty());
                assert(result.reflection.entryPoints.empty());
                assert(result.reflection.bindings.empty());
                assert(result.reflection.vertexAttributes.empty());
                assert(result.reflection.pushConstantMembers.empty());
                assert(result.reflection.pushConstantSizeBytes == 0u);
                assert(request.target == before.target);
                assert(request.moduleName == before.moduleName);
                assert(request.sourceCode == before.sourceCode);
                assert(request.sourcePath == before.sourcePath);
                assert(request.entryPoints.size() == before.entryPoints.size());
                for (std::size_t i = 0; i < request.entryPoints.size(); ++i) {
                    assert(request.entryPoints[i].name == before.entryPoints[i].name);
                    assert(request.entryPoints[i].stage == before.entryPoints[i].stage);
                }
            }
        }
    }

    void verifyKnownTargets()
    {
        constexpr TargetBackend targets[] = {
            TargetBackend::SpirV, TargetBackend::Msl, TargetBackend::Wgsl};
        constexpr const char* names[] = {"spirv", "msl", "wgsl"};
        for (std::size_t i = 0; i < 3; ++i) {
            auto request = request_;
            request.target = targets[i];
            const auto result = compiler_.compile(request);
            assert(result.success && result.diagnostics.empty());
            assert(result.target == request.target);
            arp::core::pmr::String expected = "stub-compiled:SceneStylizedPbr:";
            expected += names[i];
            assert(result.bytecode.size() == expected.size());
            for (std::size_t j = 0; j < expected.size(); ++j)
                assert(result.bytecode[j] == static_cast<std::uint8_t>(expected[j]));
            assert(result.bindings.size() == 4u);
            assert(result.reflection.bindings.size() == 4u);
            assert(result.reflection.entryPoints.size() == 2u);

            request.moduleName.clear();
            assert(compiler_.compile(request).diagnostics == "moduleName is required");
            request.moduleName = request_.moduleName;
            request.sourceCode.clear();
            assert(compiler_.compile(request).diagnostics == "sourceCode is empty");
            request.sourceCode = request_.sourceCode;
            request.entryPoints.clear();
            assert(
                compiler_.compile(request).diagnostics == "at least one entry point is required");
        }
    }

private:
    ShaderCompiler compiler_;
    ShaderCompileRequest request_;
};

} // namespace

int main()
{
    static_assert(std::is_same_v<std::underlying_type_t<TargetBackend>, int>);
    static_assert(static_cast<int>(TargetBackend::SpirV) == 0);
    static_assert(static_cast<int>(TargetBackend::Msl) == 1);
    static_assert(static_cast<int>(TargetBackend::Wgsl) == 2);
    ShaderCompileTargetFixture fixture;
    fixture.verifyUnknownTargets();
    fixture.verifyKnownTargets(); // A rejected request cannot poison the next request.
    fixture.verifyUnknownTargets();
    std::puts(
        "ShaderCompileTargetAdmission/1: 56 invalid combinations, three legacy targets passed");
}
