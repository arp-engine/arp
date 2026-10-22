#include "../fixtures/spirv_msl_buffer_memory_lowering_fixture.h"
#include "../fixtures/spirv_wgsl_buffer_memory_lowering_fixture.h"
#include "../fixtures/webgpu_raw_physical_metadata_fixture.h"
#include "core/text/ascii_token_cursor.h"
#ifdef NDEBUG
#error Buffer lowering contract tests require assertions
#endif
int main(int argc, char** argv)
{
    if (argc >= 8 && (argc - 8) % 3 == 0 && std::string_view(argv[1]) == "--declared-memory") {
        using namespace arp;
        using Graph = shader::BufferDeclarationTypeLayouts;
        using Lowering = shader::SpirvWgslBufferMemoryLowering;
        const auto number = [](const char* text) {
            core::AsciiTokenCursor cursor(text);
            std::uint32_t n = 0u;
            assert(cursor.decimalUint32(n) && cursor.atEnd());
            return n;
        };
        // Tool-only cold declaration budgets, never runtime device limits.
        const auto wgsl = core::readBinaryFile(argv[2], 32u * 1024u * 1024u);
        const auto binary = core::readBinaryFile(argv[3], 32u * 1024u * 1024u);
        assert(wgsl.success && binary.success);
        Graph source, target;
        core::pmr::Vector<shader::SpirvBufferDeclarationLayout> sourceFacts;
        core::pmr::Vector<shader::WgslBufferDeclarationLayout> targetFacts;
        assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(binary.bytes.data(),
            binary.bytes.size(),
            64u,
            {4096u, 4096u, 64u},
            sourceFacts,
            source));
        assert(!shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
            {reinterpret_cast<const char*>(wgsl.bytes.data()), wgsl.bytes.size()},
            64u,
            {4096u, 4096u, 64u},
            targetFacts,
            target));
        core::pmr::Vector<Lowering::ArrayValueWrapper> arrays;
        core::pmr::Vector<Lowering::MatrixColumnValue> matrices;
        for (int i = 8; i < argc; i += 3) {
            const auto a = number(argv[i + 1]), b = number(argv[i + 2]);
            if (std::string_view(argv[i]) == "array")
                arrays.push_back({a, b});
            else {
                const auto tag = std::string_view(argv[i]);
                assert(tag == "matrix" || tag == "matrix-array");
                matrices.push_back({a,
                    b,
                    tag == "matrix" ? Lowering::MatrixTargetForm::Matrix
                                    : Lowering::MatrixTargetForm::ColumnArrayStruct});
            }
        }
        Lowering::Facts facts;
        const auto* id = Lowering::compare(source,
            number(argv[4]),
            target,
            number(argv[5]),
            static_cast<Lowering::RootForm>(number(argv[6])),
            8192u,
            facts,
            {arrays.data(), arrays.size(), matrices.data(), matrices.size(), 2u});
        if (std::string_view(argv[7]) == "valid") {
            if (id)
                std::fprintf(stderr, "Declared memory: %s\n", id);
            assert(!id && facts.comparedPairs && facts.matrixColumnValues == matrices.size()
                && facts.arrayValueWrappers == arrays.size());
        } else {
            if (!id || std::string_view(id) != argv[7])
                std::fprintf(stderr, "Expected %s, got %s\n", argv[7], id ? id : "success");
            assert(id && std::string_view(id) == argv[7] && !facts.comparedPairs
                && !facts.sourceOccupiedBytes && !facts.targetDeclarationBytes
                && !facts.matrixColumnValues && !facts.arrayValueWrappers);
        }
        std::printf(
            "BUFFER_DECLARED_MEMORY2_OK pairs=%zu matrices=%zu arrays=%zu sourceOccupied=%llu targetBytes=%llu declaration-only\n",
            facts.comparedPairs,
            facts.matrixColumnValues,
            facts.arrayValueWrappers,
            static_cast<unsigned long long>(facts.sourceOccupiedBytes),
            static_cast<unsigned long long>(facts.targetDeclarationBytes));
        return 0;
    }
    if (argc == 1) {
        arp::test::SpirvWgslBufferMemoryLoweringFixture::verify();
        arp::test::SpirvMslBufferMemoryLoweringFixture::verify();
        arp::test::WebGpuRawPhysicalMetadataFixture::verify();
        return 0;
    }
    if (argc == 3) {
        arp::test::SpirvWgslBufferMemoryLoweringFixture::real(argv[1], argv[2]);
        return 0;
    }
    return 1;
}
