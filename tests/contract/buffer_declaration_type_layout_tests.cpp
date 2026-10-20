#include "../fixtures/buffer_declaration_type_layout_fixture.h"
#include "core/ascii_token_cursor.h"

#include <array>
#include <string_view>

#ifdef NDEBUG
#error Buffer declaration type layout tests require assertions
#endif
namespace {
using Graph = arp::shader::BufferDeclarationTypeLayouts;

class TypeLayoutApplication final {
public:
    int run(int argc, char** argv) const
    {
        if (argc == 1) {
            arp::test::BufferDeclarationTypeLayoutFixture::verify();
            return 0;
        }
        if (argc == 3) {
            arp::test::BufferDeclarationTypeLayoutFixture::real(argv[1], argv[2]);
            return 0;
        }
        if (argc != 7
            || (std::string_view(argv[1]) != "--spirv" && std::string_view(argv[1]) != "--wgsl"))
            return fail("SHADER-BUFFER-TYPE-TOOL-ARGUMENTS");
        std::array<std::uint32_t, 4> limits{};
        for (std::size_t i = 0u; i < limits.size(); ++i) {
            arp::core::AsciiTokenCursor cursor(argv[i + 3u]);
            if (!cursor.decimalUint32(limits[i]) || !cursor.atEnd())
                return fail("SHADER-BUFFER-TYPE-TOOL-BUDGET");
        }
        const auto file = arp::core::readBinaryFile(argv[2], limits[0]);
        if (!file.success)
            return fail("SHADER-BUFFER-TYPE-TOOL-READ");
        Graph graph;
        const Graph::Budget budget{limits[1], limits[2], limits[3]};
        if (std::string_view(argv[1]) == "--spirv") {
            arp::core::pmr::Vector<arp::shader::SpirvBufferDeclarationLayout> facts{
                &arp::core::defaultCpuMemoryResource()};
            if (const auto* id = arp::shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(
                    file.bytes.data(), file.bytes.size(), limits[3], budget, facts, graph))
                return fail(id);
        } else {
            arp::core::pmr::Vector<arp::shader::WgslBufferDeclarationLayout> facts{
                &arp::core::defaultCpuMemoryResource()};
            const std::string_view source(
                reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size());
            if (const auto* id = arp::shader::WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
                    source, limits[3], budget, facts, graph))
                return fail(id);
        }
        dump(graph);
        return 0;
    }

    static void dump(const Graph& graph)
    {
        std::puts(
            "type: BufferDeclarationTypeLayouts\ncontractVersion: 1\nstatus: DeclarationOnly");
        std::printf("nodes:%s\n", graph.nodes().empty() ? " []" : "");
        for (const auto& n : graph.nodes()) {
            std::printf(
                "  - kind: %u\n    scalar: %u\n    scalarBits: %u\n    matrixEncoding: %u\n    rows: %u\n    columns: %u\n",
                static_cast<unsigned>(n.kind),
                static_cast<unsigned>(n.scalar),
                n.scalarBits,
                static_cast<unsigned>(n.matrixEncoding),
                n.rows,
                n.columns);
            optionalIndex("child", n.child);
            std::printf(
                "    memberBegin: %llu\n    memberCount: %llu\n    count: %llu\n    strideBytes: %llu\n    occupiedExtentBytes: %llu\n"
                "    alignmentKnown: %s\n    alignmentBytes: %llu\n    targetSizeKnown: %s\n    targetSizeBytes: %llu\n",
                value(n.memberBegin),
                value(n.memberCount),
                value(n.count),
                value(n.strideBytes),
                value(n.occupiedExtentBytes),
                n.alignmentKnown ? "true" : "false",
                value(n.alignmentBytes),
                n.targetSizeKnown ? "true" : "false",
                value(n.targetSizeBytes));
        }
        std::printf("members:%s\n", graph.members().empty() ? " []" : "");
        for (const auto& m : graph.members())
            std::printf(
                "  - type: %llu\n    offsetBytes: %llu\n    alignmentKnown: %s\n    alignmentBytes: %llu\n    reservedSizeKnown: %s\n    reservedSizeBytes: %llu\n",
                value(m.type),
                value(m.offsetBytes),
                m.alignmentKnown ? "true" : "false",
                value(m.alignmentBytes),
                m.reservedSizeKnown ? "true" : "false",
                value(m.reservedSizeBytes));
        std::printf("roots:%s\n", graph.roots().empty() ? " []" : "");
        for (const auto& r : graph.roots()) {
            std::printf(
                "  - set: %u\n    binding: %u\n    storage: %u\n    type: %llu\n    descriptorCount: %llu\n",
                r.set,
                r.binding,
                static_cast<unsigned>(r.storage),
                value(r.type),
                value(r.descriptorCount));
            optionalIndex("descriptorType", r.descriptorType);
        }
    }

private:
    static unsigned long long value(std::uint64_t v) { return static_cast<unsigned long long>(v); }

    static void optionalIndex(const char* name, std::size_t index)
    {
        if (index == Graph::absent)
            std::printf("    %s: null\n", name);
        else
            std::printf("    %s: %llu\n", name, value(index));
    }

    static int fail(const char* id)
    {
        std::fprintf(stderr, "%s\n", id);
        return 1;
    }
};
} // namespace

int main(int argc, char** argv)
{
    try {
        return TypeLayoutApplication{}.run(argc, argv);
    } catch (const std::bad_alloc&) {
        std::fputs("SHADER-BUFFER-TYPE-TOOL-ALLOCATION\n", stderr);
        return 1;
    }
}
