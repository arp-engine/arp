#include "../fixtures/graphics_shader_module_artifact_fixture.h"

#include <type_traits>
#ifdef NDEBUG
#error Module artifact tests require assertions
#endif
int main(int argc, char** argv)
{
    using namespace arp;
    using Artifact = shader::GraphicsShaderModuleArtifact;
    static_assert(
        !std::is_default_constructible_v<Artifact> && !std::is_copy_constructible_v<Artifact>);
    assert(argc == 3);
    if (core::StringView(argv[2]) == "valid") {
        test::GraphicsShaderModuleArtifactFixture::verify(argv[1]);
        return 0;
    }
    const shader::GraphicsShaderModuleLoadBudget budget{
        1024u * 1024u, 16u * 1024u * 1024u, 4u * 1024u * 1024u, 4u, 4u, 256u, 2u};
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    core::pmr::String diagnostic;
    auto value = Artifact::load(argv[1], budget, quota, diagnostic);
    if (value || diagnostic.find(argv[2]) == diagnostic.npos)
        std::printf("MODULE_EXPECT %s GOT %s\n", argv[2], diagnostic.c_str());
    assert(!value && diagnostic.find(argv[2]) != diagnostic.npos && !quota->liveBytes());
}
