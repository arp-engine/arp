#include "editor/viewport/editor_scene_viewport_shader_binding_contract.h"
#include "shader/shader_reflection_cache.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {

using namespace arp;

shader::ShaderReflection loadReflection()
{
    static constexpr const char* yaml = R"yaml(
moduleName: EditorViewportPbrMaterial
reflectionContractVersion: "ShaderReflection/3"
entryPoints:
  - { name: fragmentMain, stage: Fragment }
bindings:
  - { name: BaseColorTexture, type: SampledTexture, set: 4, binding: 9, arrayCount: 1, stageMask: 2 }
  - { name: NormalTexture, type: SampledTexture, set: 4, binding: 10, arrayCount: 1, stageMask: 2 }
  - { name: MetallicRoughnessTexture, type: SampledTexture, set: 4, binding: 11, arrayCount: 1, stageMask: 2 }
  - { name: EmissiveTexture, type: SampledTexture, set: 4, binding: 12, arrayCount: 1, stageMask: 2 }
  - { name: OcclusionTexture, type: SampledTexture, set: 4, binding: 13, arrayCount: 1, stageMask: 2 }
  - { name: LinearSampler, type: Sampler, set: 4, binding: 14, arrayCount: 1, stageMask: 2 }
)yaml";
    auto loaded = shader::ShaderReflectionCache::loadFromYaml(
        std::pmr::string(yaml, &core::defaultCpuMemoryResource()));
    if (!loaded.success)
        std::fprintf(stderr, "%s\n", loaded.diagnostics.c_str());
    assert(loaded.success);
    return std::move(loaded.module.reflection);
}

shader::ShaderBinding& bindingNamed(shader::ShaderReflection& reflection, const char* name)
{
    for (auto& binding : reflection.bindings) {
        if (binding.name == name)
            return binding;
    }
    assert(false);
    return reflection.bindings.front();
}

} // namespace

int main()
{
    auto reflection = loadReflection();
    const auto mapped = editor::inspectEditorSceneViewportShaderBindings(reflection);
    assert(mapped.valid());
    assert(mapped.bindings.textures[0].set == 4u);
    assert(mapped.bindings.textures[0].binding == 9u);
    assert(mapped.bindings.textures[3].binding == 13u);
    assert(mapped.bindings.textures[4].binding == 12u);
    assert(mapped.bindings.sampler.binding == 14u);
    assert(mapped.rhiReflection.descriptorBindings.size() == 6u);
    const auto rhiLayout = rhi::buildPipelineLayoutDescFromShaderReflection(mapped.rhiReflection);
    assert(rhi::validatePipelineLayoutAgainstShaderReflection(rhiLayout, mapped.rhiReflection)
            .success);
    auto invalidRhiLayout = rhiLayout;
    invalidRhiLayout.descriptorLayouts.front().bindings.front().arrayCount = 2u;
    assert(
        !rhi::validatePipelineLayoutAgainstShaderReflection(invalidRhiLayout, mapped.rhiReflection)
            .success);

    auto invalid = reflection;
    invalid.reflectionContractVersion =
        core::pmr::String("ShaderReflection/2", &core::defaultCpuMemoryResource());
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::ReflectionContractUnsupported);

    invalid = reflection;
    invalid.bindings.erase(invalid.bindings.begin());
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::MissingBinding);

    invalid = reflection;
    invalid.bindings.push_back(invalid.bindings.front());
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::DuplicateBinding);

    invalid = reflection;
    auto unrelated = invalid.bindings.front();
    unrelated.name = core::pmr::String("SceneLighting", &core::defaultCpuMemoryResource());
    unrelated.binding = 20u;
    invalid.bindings.push_back(std::move(unrelated));
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::UnexpectedBinding);

    invalid = reflection;
    bindingNamed(invalid, "NormalTexture").type = shader::BindingType::Sampler;
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::BindingTypeMismatch);

    invalid = reflection;
    bindingNamed(invalid, "NormalTexture").arrayCount = 2u;
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::ArrayCountMismatch);

    invalid = reflection;
    bindingNamed(invalid, "NormalTexture").stageMask = 1u;
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::StageMaskMismatch);

    invalid = reflection;
    bindingNamed(invalid, "LinearSampler").binding = 9u;
    const auto collision = editor::inspectEditorSceneViewportShaderBindings(invalid);
    assert(collision.diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::DescriptorAddressCollision);
    assert(std::string_view(
               editor::editorSceneViewportShaderBindingDiagnosticCode(collision.diagnostic))
        == "ARP-EDITOR-VIEWPORT-SHADER-BINDING-ADDRESS-COLLISION");
}
