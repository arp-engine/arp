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
    const std::filesystem::path sourcePath(argv[1]);
    const std::filesystem::path generatedDirectory(argv[2]);
    const auto source = read(sourcePath);
    assert(source.find("float4x4 viewProjection;") != std::string::npos);
    assert(source.find("float4x4 skinMatrices[kMaxQueenSkinningJoints];") != std::string::npos);
    assert(source.find("row_major") == std::string::npos);
    assert(source.find("[[vk::binding(0, 0)]]") != std::string::npos);
    for (const auto* extension : {"spv", "metal", "wgsl", "reflection.yaml"})
        assert(std::filesystem::is_regular_file(
            generatedDirectory / (std::string("queen_skinned.") + extension)));
    const auto reflection = read(generatedDirectory / "queen_skinned.reflection.yaml");
    assert(reflection.find("pushConstantSizeBytes: 160") != std::string::npos);
    assert(reflection.find("name: lightViewProjection") != std::string::npos);
    assert(reflection.find("name: gSkinning") != std::string::npos);
    return 0;
}
