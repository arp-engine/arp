#include "../fixtures/spirv_binary_mutation_fixture.h"
#include "core/file_io.h"
#include "shader/fragment_atomic_write_proof.h"
#include "shader/fragment_storage_read_proof.h"
#include "shader/fragment_uniform_read_proof.h"

#include <cstdio>
#include <filesystem>

#ifdef NDEBUG
#error Fragment storage read proof requires assertions
#endif
namespace {
using namespace arp;
using Proof = shader::FragmentStorageReadProof;

class ReadProofFixture final : public test::SpirvBinaryMutationFixture {
public:
    using SpirvBinaryMutationFixture::SpirvBinaryMutationFixture;

    Proof::Facts valid() const
    {
        Proof::Facts facts;
        const auto* id = Proof::verify(bytes().data(), bytes().size(), "fragmentMain", facts);
        if (id)
            std::fprintf(stderr, "Read proof: %s\n", id);
        assert(!id && facts.contractVersion == 1u && !facts.byteOffset && facts.byteCount == 4u);
        return facts;
    }

    void reject() const
    {
        Proof::Facts facts{99u, 99u, 99u, 99u, 99u, 99u};
        assert(Proof::verify(bytes().data(), bytes().size(), "fragmentMain", facts));
        assert(!facts.contractVersion && !facts.set && !facts.binding && !facts.replacement
            && !facts.byteOffset && !facts.byteCount);
    }
};
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 4);
    auto file = core::readBinaryFile(argv[1], std::filesystem::file_size(argv[1]));
    assert(file.success);
    ReadProofFixture f(std::move(file.bytes));
    const auto facts = f.valid();
    assert(!facts.set && facts.binding == 3u && facts.replacement == 305419896u);
    f.patch(71u, 2u, 8u, 1, 33u);
    assert(f.valid().binding == 8u);
    f.reset();
    f.patch(71u, 2u, 2u, 1, 34u);
    assert(f.valid().set == 2u);
    f.reset();
    const auto expected = f.operand(f.locate(170u), 3u);
    f.patch(43u, 2u, 0xffffffffu, 1, expected);
    assert(f.valid().replacement == 0xffffffffu);
    f.reset();
    f.patch(5u, 1u, 0x78787878u);
    f.valid();
    f.reset();
    f.remove(71u, 1, 24u);
    f.reject();
    f.reset();
    f.duplicateDecoration(24u);
    f.reject();
    f.reset();
    f.duplicateDecoration(34u);
    f.reject();
    f.reset();
    const auto zero = f.operand(f.locate(65u), 4u);
    f.patch(43u, 2u, 1u, 1, zero);
    f.reject();
    f.reset();
    f.patch(72u, 3u, 4u);
    f.reject();
    f.reset();
    f.patch(71u, 2u, 8u, 1, 6u);
    f.reject();
    f.reset();
    f.patch(61u, 2u, expected);
    f.reject();
    f.reset();
    f.patch(170u, 2u, expected);
    f.reject();
    f.reset();
    f.patch(170u, 3u, f.operand(f.locate(61u), 1u));
    f.reject();
    f.reset();
    const auto select = f.locate(169u);
    const auto yes = f.operand(select, 3u), no = f.operand(select, 4u);
    f.patch(169u, 3u, no);
    f.patch(169u, 4u, yes);
    f.reject();
    f.reset();
    f.patch(169u, 2u, expected);
    f.reject();
    f.reset();
    f.patch(62u, 1u, yes);
    f.reject();
    f.reset();
    f.word(f.locate(61u), (4u << 16u) | 229u);
    f.reject();
    f.reset();
    f.patch(61u, 1u, f.operand(f.locate(65u), 1u));
    f.reject();
    f.reset();
    f.patch(21u, 1u, 64u);
    f.reject();
    f.reset();
    f.patch(15u, 0u, 5u);
    f.reject();
    f.reset();
    f.word(f.locate(65u), 65u);
    f.reject();
    f.reset();
    Proof::Facts output;
    assert(Proof::verify(f.bytes().data(), f.bytes().size(), "other", output));
    assert(shader::FragmentAtomicWriteProof::verify(
        f.bytes().data(), f.bytes().size(), "fragmentMain", output));
    auto writer = core::readBinaryFile(argv[2], std::filesystem::file_size(argv[2]));
    assert(writer.success);
    assert(Proof::verify(writer.bytes.data(), writer.bytes.size(), "fragmentMain", output));
    auto uniform = core::readBinaryFile(argv[3], std::filesystem::file_size(argv[3]));
    assert(uniform.success);
    test::SpirvBinaryMutationFixture u(std::move(uniform.bytes));
    using Uniform = shader::FragmentUniformReadProof;
    const auto validUniform = [&] {
        const auto* id =
            Uniform::verify(u.bytes().data(), u.bytes().size(), "fragmentMain", output);
        if (id)
            std::fprintf(stderr, "Uniform read proof: %s\n", id);
        assert(!id && output.storageClass == shader::ScalarStorageBinaryProof::uniformStorageClass
            && output.replacement == facts.replacement && output.byteCount == 4u
            && !output.byteOffset);
    };
    validUniform();
    assert(Proof::verify(u.bytes().data(), u.bytes().size(), "fragmentMain", output));
    const auto rejectUniform = [&] {
        assert(Uniform::verify(u.bytes().data(), u.bytes().size(), "fragmentMain", output));
    };
    u.patch(72u, 3u, 4u);
    rejectUniform();
    u.reset();
    u.duplicateDecoration(34u);
    rejectUniform();
    u.reset();
    u.patch(59u, 2u, 12u, 2, 2u);
    rejectUniform();
    u.reset();
    const auto member = u.operand(u.locate(65u), 3u);
    u.patch(43u, 2u, 1u, 1, member);
    rejectUniform();
    u.reset();
    u.patch(61u, 2u, u.operand(u.locate(170u), 3u));
    rejectUniform();
    u.reset();
    validUniform();
    core::ByteBuffer unaligned{&core::defaultCpuMemoryResource()};
    unaligned.push_back(0u);
    unaligned.insert(unaligned.end(), f.bytes().begin(), f.bytes().end());
    assert(!Proof::verify(unaligned.data() + 1u, f.bytes().size(), "fragmentMain", output));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    core::CpuMemoryQuotaScope scope(quota);
    core::CpuMemoryQuotaLease pressure(quota, 1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    for (unsigned i = 0u; i < 100u; ++i) {
        f.valid();
        validUniform();
    }
    std::pmr::set_default_resource(previous);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    std::puts(
        "FragmentStorageReadProof/1: actual uint32 element-zero load/full-word equality/green-red output, hostile mutations and allocation0 PASS; no execution authority");
}
