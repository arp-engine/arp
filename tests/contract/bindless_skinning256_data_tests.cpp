#include "../fixtures/original_graphics_skinning256_data_fixture.h"
#include "core/file_io.h"
#include "shader/spirv_buffer_declaration_layout.h"

#ifdef NDEBUG
#error Skinning256 tests require assertions
#endif

int main(int argc, char** argv)
{
    using Data = arp::test::OriginalGraphicsSkinning256DataFixture;
    const auto values = Data::expected();
    constexpr std::array<float, 4u> vertex{-8274.5f, -8360.5f, 8460.5f, 2135.125f};
    constexpr std::array<std::array<float, 4u>, 4u> previous{
        {{1732.75f, -1704.75f, -1692.75f, 1776.75f},
            {1903.625f, -1877.625f, -1867.625f, 1953.625f},
            {2074.5f, -2050.5f, -2042.5f, 2130.5f},
            {2245.375f, -2223.375f, -2217.375f, 2307.375f}}};
    for (std::size_t pixel = 0u; pixel < previous.size(); ++pixel)
        for (std::size_t lane = 0u; lane < vertex.size(); ++lane) {
            assert(values[pixel * 16u + lane] == vertex[lane]);
            assert(values[pixel * 16u + 4u + lane] == previous[pixel][lane]);
        }
    // Independent closed row sums cancel translation, with a 180-degree Z
    // quaternion. Tangent's zero coordinate sum cancels the joint base.
    const auto normal = arp::core::normalize(arp::core::Vec3{-1880.25f, -1900.25f, 1920.25f});
    const auto tangent = arp::core::normalize(arp::core::Vec3{-2.f, -2.f, 2.f});
    for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
        const auto offset = pixel * 16u;
        assert(std::abs(values[offset + 8u] - normal.x) < 1e-6f);
        assert(std::abs(values[offset + 9u] - normal.y) < 1e-6f);
        assert(std::abs(values[offset + 10u] - normal.z) < 1e-6f);
        assert(std::abs(values[offset + 12u] - tangent.x) < 1e-6f);
        assert(std::abs(values[offset + 13u] - tangent.y) < 1e-6f);
        assert(std::abs(values[offset + 14u] - tangent.z) < 1e-6f);
        assert(values[offset + 11u] == 0.f && values[offset + 15u] == -1.f);
    }
    auto matrices = Data::matrices();
    const auto current = Data::skin(matrices, {2, 3, 5, 1}, false);
    const auto old = Data::skin(matrices, {2, 3, 5, 1}, true);
    matrices.current.skinMatrices[255].fill(0.f);
    assert(Data::skin(matrices, {2, 3, 5, 1}, false).x != current.x);
    assert(Data::skin(matrices, {2, 3, 5, 1}, true).x == old.x);
    matrices.previous.skinMatrices[255].fill(0.f);
    assert(Data::skin(matrices, {2, 3, 5, 1}, true).x == old.x);
    matrices.previous.skinMatrices[219].fill(0.f);
    assert(Data::skin(matrices, {2, 3, 5, 1}, true).x != old.x);
    std::puts(
        "BINDLESS_SKINNING256_DATA_OK renderer32864 influence32 independent-exact-oracle high-index previous-clamp");
    assert(argc == 1 || argc == 2);
    if (argc == 2) {
        const auto binary = arp::core::readBinaryFile(argv[1], 1024u * 1024u);
        assert(binary.success);
        using Graph = arp::shader::BufferDeclarationTypeLayouts;
        Graph graph;
        arp::core::pmr::Vector<arp::shader::SpirvBufferDeclarationLayout> facts;
        assert(!arp::shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
            binary.bytes.data(), binary.bytes.size(), 16u, {512u, 512u, 16u}, facts, graph));
        for (std::size_t i = 0u; i < graph.nodes().size(); ++i) {
            const auto& node = graph.nodes()[i];
            if (node.kind == Graph::Kind::Matrix)
                std::printf("SKINNING256_SOURCE_MATRIX type=%zu rows=%u columns=%u stride=%llu\n",
                    i,
                    node.rows,
                    node.columns,
                    static_cast<unsigned long long>(node.strideBytes));
        }
        for (std::size_t i = 0u; i < graph.roots().size(); ++i) {
            const auto& root = graph.roots()[i];
            std::printf(
                "SKINNING256_SOURCE_ROOT index=%zu set=%u binding=%u type=%zu extent=%llu\n",
                i,
                root.set,
                root.binding,
                root.type,
                static_cast<unsigned long long>(graph.nodes()[root.type].occupiedExtentBytes));
        }
    }
}
