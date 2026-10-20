#include "../fixtures/graphics_shader_module_artifact_fixture.h"
#include "../fixtures/original_graphics_pipeline_input_fixture.h"

#ifdef NDEBUG
#error Finite Graphics artifact input contracts require assertions
#endif

int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc == 2);
    test::GraphicsShaderModuleArtifactFixture::verify(argv[1], [](const auto& artifact) {
        auto input = test::OriginalGraphicsPipelineInputFixture::verify(artifact);
        assert(input && !input->nativeExecutionProven());
        const auto& package = input->package();
        assert(package.reflection.descriptorBindings.size() == 4u);
        for (const auto& binding : package.reflection.descriptorBindings)
            assert(binding.stages == rhi::ShaderStage::Fragment && binding.arrayCount == 1u);
        std::puts(
            "BINDLESS_RHI_GENERAL_FINITE_GRAPHICS_INPUT_OK original-module-custody entries2 Fragment-bindings4 four-target-selection hot0 native-execution-closed");
    });
}
