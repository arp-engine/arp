#include "../fixtures/spirv_binary_mutation_fixture.h"
#include "core/io/file_io.h"
#include "shader/compute_indirect_producer_proof.h"
#include "shader/vertex_location_input_proof.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef NDEBUG
#error Vertex ABI proof tests require assertions
#endif
namespace {
using namespace arp;
using Proof = shader::VertexLocationInputProof;

class Fixture final : public test::SpirvBinaryMutationFixture {
public:
    using SpirvBinaryMutationFixture::SpirvBinaryMutationFixture;

    core::pmr::Vector<shader::ShaderVertexAttribute> expected() const
    {
        core::pmr::Vector<shader::ShaderVertexAttribute> result;
        for (const auto pair : {std::pair<std::uint32_t, shader::VertexAttributeFormat>{
                                    0u, shader::VertexAttributeFormat::Float2},
                 {1u, shader::VertexAttributeFormat::Float2},
                 {2u, shader::VertexAttributeFormat::Float4}}) {
            shader::ShaderVertexAttribute attribute;
            attribute.location = pair.first;
            attribute.format = pair.second;
            result.push_back(std::move(attribute));
        }
        return result;
    }

    auto valid() const
    {
        Proof::Facts facts;
        auto inputs = expected();
        const auto* id = Proof::verify(bytes().data(), bytes().size(), "vertexMain", inputs, facts);
        if (id)
            std::fprintf(stderr, "Unexpected ABI proof rejection: %s\n", id);
        assert(!id && facts.contractVersion == 1u && facts.inputCount == 3u
            && facts.outputCount == 2u && facts.entryFunction
            && facts.binaryBytes == bytes().size());
        return facts;
    }

    void reject() const
    {
        Proof::Facts facts{99u, 99u, 99u, 99u, 99u};
        auto inputs = expected();
        assert(Proof::verify(bytes().data(), bytes().size(), "vertexMain", inputs, facts));
        assert(!facts.contractVersion && !facts.inputCount && !facts.outputCount
            && !facts.entryFunction && !facts.binaryBytes);
    }
};

void run(Fixture& f)
{
    f.valid();
    const auto input = f.operand(f.locate(59u, 2, 1u), 1u);
    const auto inputDecoration = f.locate(71u, 0, input);
    const auto secondInput = f.operand(f.locate(59u, 2, 1u, 1u), 1u);
    const auto entry = f.locate(15u);
    for (const auto builtin : {0u, 5u, 6u, 42u, 43u, 4424u, 4425u, 4426u, UINT32_MAX}) {
        f.reset();
        f.word(inputDecoration + 2u, 11u);
        f.word(inputDecoration + 3u, builtin);
        f.reject();
    }
    f.reset();
    f.patch(15u, 0u, 4u);
    f.reject();
    f.reset();
    f.word(entry + 3u, 0u);
    f.reject();
    f.reset();
    f.patch(22u, 1u, 64u);
    f.reject();
    f.reset();
    f.patch(23u, 2u, 6u);
    f.reject();
    f.reset();
    f.patch(32u, 1u, 2u);
    f.reject();
    f.reset();
    f.patch(59u, 2u, 3u, 1, input);
    f.reject();
    f.reset();
    f.patch(71u, 2u, 0u, 0, secondInput);
    f.reject();
    f.reset();
    f.patch(71u, 2u, 8u, 0, input);
    f.reject();
    f.reset();
    f.remove(71u, 0, input);
    f.reject();
    f.reset();
    f.duplicateDecoration(30u);
    f.reject();
    f.reset();
    f.word(inputDecoration, (4u << 16u) | 73u);
    f.reject();
    f.reset();
    f.word(entry + 9u, input);
    f.reject(); // Duplicate interface ID.
    f.reset();
    f.remove(59u, 1, input);
    f.reject();
    f.reset();
    f.patch(43u, 1u, input);
    f.reject(); // Duplicate definition despite a plausible decoration.
    f.reset();
    f.word(3u, input);
    f.reject(); // Result IDs outside the declared bound.
    f.reset();
    f.word(f.locate(61u), (4u << 16u) | 57u);
    f.reject(); // Unknown/helper call language.
    f.reset();
    f.patch(129u, 2u, input);
    f.reject();
    f.reset();
    f.patch(62u, 0u, input);
    f.reject();
    f.reset();
    f.patch(54u, 3u, input);
    f.reject();
    f.reset();
    f.remove(253u);
    f.reject();
    f.reset();
    f.valid();
    auto inputs = f.expected();
    Proof::Facts facts;
    inputs[0].format = shader::VertexAttributeFormat::Unknown;
    assert(Proof::verify(f.bytes().data(), f.bytes().size(), "vertexMain", inputs, facts)
        && !facts.contractVersion);
    inputs = f.expected();
    inputs[0].format = shader::VertexAttributeFormat::Float3;
    assert(Proof::verify(f.bytes().data(), f.bytes().size(), "vertexMain", inputs, facts));
    inputs = f.expected();
    inputs[1].location = 0u;
    assert(Proof::verify(f.bytes().data(), f.bytes().size(), "vertexMain", inputs, facts));
    inputs = f.expected();
    inputs.pop_back();
    assert(Proof::verify(f.bytes().data(), f.bytes().size(), "vertexMain", inputs, facts));
    inputs = f.expected();
    core::ByteBuffer unaligned(f.bytes().size() + 1u);
    std::copy(f.bytes().begin(), f.bytes().end(), unaligned.begin() + 1u);
    assert(!Proof::verify(unaligned.data() + 1u, f.bytes().size(), "vertexMain", inputs, facts));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    core::CpuMemoryQuotaScope scope(quota);
    core::CpuMemoryQuotaLease pressure(quota, 1u);
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 100u; ++i)
        assert(!Proof::verify(f.bytes().data(), f.bytes().size(), "vertexMain", inputs, facts));
    std::pmr::set_default_resource(previous);
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    auto source = core::readBinaryFile(argv[1]);
    assert(source.success);
    Fixture f(std::move(source.bytes));
    run(f);
    auto unsupported = core::readBinaryFile(argv[2]);
    assert(unsupported.success);
    auto inputs = f.expected();
    Proof::Facts facts;
    assert(Proof::verify(
               unsupported.bytes.data(), unsupported.bytes.size(), "vertexMain", inputs, facts)
        && !facts.contractVersion);
    shader::ComputeIndirectProducerProof::Facts compute;
    assert(shader::ComputeIndirectProducerProof::verify(
        f.bytes().data(), f.bytes().size(), "vertexMain", compute));
    std::puts(
        "VertexLocationFloatInput/1: actual selected binary input/type/location/interface proof, builtin/alias/ID/domain mutations, cleared facts/unaligned/allocation0 PASS; no native instance authority");
}
