#include "../fixtures/graphics_triangle_storage_proof_fixture.h"
#include "../fixtures/spirv_binary_mutation_fixture.h"
#include "core/io/file_io.h"

#include <cstring>
#ifdef NDEBUG
#error Graphics proof tests require assertions
#endif
namespace {
using namespace arp;
using Proof = shader::VertexIndexTriangleProof;

class Fixture final : public test::SpirvBinaryMutationFixture {
public:
    using SpirvBinaryMutationFixture::SpirvBinaryMutationFixture;

    Proof::Facts valid() const
    {
        Proof::Facts facts;
        const auto* id = Proof::verify(bytes().data(), bytes().size(), "vertexMain", facts);
        if (id)
            std::fprintf(stderr, "VERTEX_TRIANGLE_FAILURE %s\n", id);
        assert(!id && facts.contractVersion == 1u && facts.requiresDrawParameters);
        return facts;
    }

    void reject() const
    {
        static unsigned mutation = 0u;
        ++mutation;
        Proof::Facts facts{99u,
            Proof::InputSemantics::SubtractBaseVertex,
            true,
            shader::TrianglePositionBits::fullscreen()};
        const auto* id = Proof::verify(bytes().data(), bytes().size(), "vertexMain", facts);
        if (!id)
            std::fprintf(stderr, "Accepted mutation %u\n", mutation);
        assert(id);
        assert(!facts.contractVersion && !facts.requiresDrawParameters);
        for (const auto& p : facts.positions)
            for (const auto bit : p)
                assert(!bit);
    }
};

void run(Fixture& f)
{
    assert(f.valid().positions == shader::TrianglePositionBits::fullscreen());
    const auto vertex = f.operand(f.locate(71u, 2, 42u), 0u),
               base = f.operand(f.locate(71u, 2, 4424u), 0u);
    const auto local = f.operand(f.locate(59u, 2, 7u), 1u);
    const auto vertexLoad = f.operand(f.locate(61u, 2, vertex), 1u),
               baseLoad = f.operand(f.locate(61u, 2, base), 1u);
    for (unsigned ordering = 0u; ordering < 4u; ++ordering) {
        f.reset();
        for (unsigned branch = 0u; branch < 2u; ++branch)
            if (ordering & (1u << branch)) {
                const auto first =
                    std::min(f.locate(61u, 2, vertex, branch), f.locate(61u, 2, base, branch));
                f.swapAdjacentInstructions(first);
            }
        assert(f.valid().positions == shader::TrianglePositionBits::fullscreen());
        for (unsigned branch = 0u; branch < 2u; ++branch) {
            const auto subtraction = f.locate(130u, -1, 0u, branch);
            const auto lhs = f.operand(subtraction, 2u), rhs = f.operand(subtraction, 3u);
            f.word(subtraction + 3u, rhs);
            f.word(subtraction + 4u, lhs);
            f.reject();
            f.word(subtraction + 3u, lhs);
            f.word(subtraction + 4u, rhs);
        }
    }
    f.reset();
    for (const auto pair : {std::pair<std::uint32_t, std::uint32_t>{17u, 0u},
             {14u, 1u},
             {15u, 0u},
             {21u, 1u},
             {22u, 1u},
             {23u, 2u},
             {32u, 1u},
             {54u, 2u},
             {247u, 1u}}) {
        f.reset();
        f.patch(pair.first, pair.second, 99u);
        f.reject();
    }
    for (const auto pair : {std::pair<std::uint32_t, std::uint32_t>{130u, 2u},
             {130u, 3u},
             {124u, 2u},
             {170u, 2u},
             {170u, 3u},
             {250u, 0u},
             {250u, 1u},
             {250u, 2u},
             {247u, 0u},
             {249u, 0u},
             {62u, 0u},
             {62u, 1u},
             {80u, 0u},
             {80u, 2u},
             {80u, 3u},
             {80u, 4u}}) {
        f.reset();
        f.patch(pair.first, pair.second, vertex);
        f.reject();
    }
    f.reset();
    f.patch(130u, 2u, baseLoad);
    f.patch(130u, 3u, vertexLoad);
    f.reject();
    // Every nested control/data edge, including the second condition and all
    // merge/store/load blocks, is independently hostile-tested.
    for (const auto opcode : {61u, 130u, 124u, 170u, 247u, 250u, 249u, 62u}) {
        f.reset();
        shader::SpirvInstructionView original(f.bytes().data(), f.bytes().size());
        for (auto offset = shader::SpirvInstructionView::headerWords;
            offset < original.sizeWords();) {
            shader::SpirvInstructionView::Instruction instruction;
            assert(original.read(offset, instruction));
            const auto next = instruction.next();
            if (instruction.opcode() == opcode) {
                const auto first =
                    (opcode == 61u || opcode == 130u || opcode == 124u || opcode == 170u) ? 2u : 0u;
                for (auto operand = first; operand < instruction.operands(); ++operand) {
                    f.reset();
                    f.word(offset + 1u + operand, UINT32_MAX);
                    f.reject();
                }
                f.reset();
            }
            offset = next;
        }
    }
    f.reset();
    f.patch(61u, 2u, local, 2, vertex);
    f.reject();
    f.reset();
    f.patch(71u, 2u, 43u, 0, vertex);
    f.reject();
    f.reset();
    f.patch(71u, 2u, 42u, 0, base);
    f.reject();
    f.reset();
    f.duplicateDecoration(11u);
    f.reject();
    f.reset();
    f.remove(17u, 0, 4427u);
    f.reject();
    f.reset();
    f.remove(253u);
    f.reject();
    f.reset();
    f.word(f.locate(130u), (5u << 16u) | 128u);
    f.reject();
    f.reset();
    f.patch(43u, 1u, vertex);
    f.reject();
    f.reset();
    f.word(3u, vertex);
    f.reject();
    f.reset();
    f.word(f.locate(15u) + 3u, 0u);
    f.reject();
    // Literal-string framing: valid debug string, bad padding/no terminator.
    f.reset();
    auto at = f.locate(5u);
    shader::SpirvInstructionView view(f.bytes().data(), f.bytes().size());
    shader::SpirvInstructionView::Instruction i;
    assert(view.read(at, i) && i.hasLiteralString(1u));
    const auto last = i.next() - 1u;
    f.word(last, 0xff000000u);
    f.reject();
    f.reset();
    f.word(last, 0xffffffffu);
    f.reject();
    f.reset();
    for (std::size_t ordinal = 0u;; ++ordinal) {
        const auto name = f.locate(5u, -1, 0u, ordinal);
        assert(view.read(name, i));
        if (i.operands() > 2u) {
            f.word(name + 2u, 0u);
            break;
        }
    }
    f.reject(); // terminator before the final literal word
    f.reset();
    f.word(f.locate(54u), 54u);
    f.reject(); // zero-word malformed framing
    f.reset();
    f.patch(43u, 2u, shader::TrianglePositionBits::one, 2, shader::TrianglePositionBits::three);
    assert(f.valid().positions != shader::TrianglePositionBits::fullscreen());
    f.reset();
    core::ByteBuffer unaligned(f.bytes().size() + 1u);
    std::copy(f.bytes().begin(), f.bytes().end(), unaligned.begin() + 1u);
    Proof::Facts facts;
    assert(!Proof::verify(unaligned.data() + 1u, f.bytes().size(), "vertexMain", facts));
    assert(Proof::verify(f.bytes().data(), f.bytes().size(), "otherMain", facts)
        && !facts.contractVersion);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 4);
    auto vertex = core::readBinaryFile(argv[1]);
    auto fragment = core::readBinaryFile(argv[2]);
    auto source = core::readTextFile(argv[3]);
    assert(vertex.success && fragment.success && source.success);
    Fixture f(std::move(vertex.bytes));
    run(f);
    test::GraphicsTriangleStorageProofFixture::verify(f.bytes(), fragment.bytes, source.text);
    std::puts(
        "ClosedGraphicsTriangleStorageRead/1: actual complete Vertex/Fragment binary + combined WGSL, CFG/reference/literal/interface negatives, zero-base draw gate and allocation0 PASS");
}
