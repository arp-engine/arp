#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::filesystem::path sourceRoot(argv[1]);
    const std::filesystem::path generatedDirectory(argv[2]);
    const auto surfaceSource = read(sourceRoot / "queen_surface.slang");
    const auto sceneSource = read(sourceRoot / "build_a_queen.slang") + surfaceSource;
    const auto skinnedSource = read(sourceRoot / "build_a_queen_skinned.slang") + surfaceSource;
    const auto skySource =
        read(sourceRoot / "build_a_queen_sky.slang") + read(sourceRoot / "queen_sky_color.slang");
    assert(sceneSource.find("ARP_SAMPLED_DEPTH(gQueenShadowMap)") != std::string::npos);
    assert(skinnedSource.find("ARP_SAMPLED_DEPTH(gQueenShadowMap)") != std::string::npos);
    assert(sceneSource.find("lightDirection") != std::string::npos);
    assert(sceneSource.find("float3 ambient") != std::string::npos);
    assert(skinnedSource.find("lightDirection") != std::string::npos);
    assert(skinnedSource.find("float3 ambient") != std::string::npos);
    assert(skySource.find("SV_VertexID") != std::string::npos);
    assert(skySource.find("sunCenter") != std::string::npos);

    for (const auto* extension : {"spv", "metal", "wgsl", "reflection.yaml"})
        assert(std::filesystem::is_regular_file(
            generatedDirectory / (std::string("queen_sky.") + extension)));
    const auto reflection = read(generatedDirectory / "queen_sky.reflection.yaml");
    assert(reflection.find("name: vertexMain") != std::string::npos);
    assert(reflection.find("name: fragmentMain") != std::string::npos);
    assert(reflection.find("pushConstantSizeBytes: 0") != std::string::npos);
    assert(reflection.find("bindings: []") != std::string::npos);
    for (const auto* shader : {"queen_scene", "queen_skinned"}) {
        const auto depthReflection =
            read(generatedDirectory / (std::string(shader) + ".reflection.yaml"));
        assert(depthReflection.find("name: gQueenShadowMap") != std::string::npos);
        assert(depthReflection.find("depthTexture: true") != std::string::npos);
        const auto wgsl = read(generatedDirectory / (std::string(shader) + ".wgsl"));
        assert(wgsl.find("texture_depth_2d") != std::string::npos);
    }
    return 0;
}
