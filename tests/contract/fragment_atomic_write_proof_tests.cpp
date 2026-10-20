#include "../fixtures/spirv_binary_mutation_fixture.h"
#include "core/file_io.h"
#include "shader/fragment_atomic_write_proof.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef NDEBUG
#error Fragment atomic proof tests require assertions
#endif

namespace {
using namespace arp;
using Proof = shader::FragmentAtomicWriteProof;

class BinaryMutationFixture final : public test::SpirvBinaryMutationFixture {
public:
    using SpirvBinaryMutationFixture::SpirvBinaryMutationFixture;

    Proof::Facts valid() const
    {
        Proof::Facts facts;
        const auto* id = Proof::verify(bytes().data(), bytes().size(), "fragmentMain", facts);
        if (id)
            std::fprintf(stderr, "Unexpected proof failure: %s\n", id);
        assert(!id && facts.contractVersion == 1u && !facts.byteOffset && facts.byteCount == 4u);
        return facts;
    }

    void reject(const char* expected) const
    {
        Proof::Facts facts{99u, 99u, 99u, 99u, 99u, 99u};
        const auto* id = Proof::verify(bytes().data(), bytes().size(), "fragmentMain", facts);
        assert(id && std::strcmp(id, expected) == 0);
        assert(!facts.contractVersion && !facts.set && !facts.binding && !facts.replacement
            && !facts.byteOffset && !facts.byteCount);
    }
};

void run(BinaryMutationFixture& fixture)
{
    const auto facts = fixture.valid();
    assert(!facts.set && facts.binding == 3u && facts.replacement == 305419896u);
    fixture.patch(71u, 2u, 8u, 1, 33u);
    assert(fixture.valid().binding == 8u);
    fixture.reset();
    fixture.patch(71u, 2u, 2u, 1, 34u);
    assert(fixture.valid().set == 2u);
    fixture.reset();
    const auto replacementId = fixture.operand(fixture.locate(229u), 5u);
    fixture.patch(43u, 2u, 0xffffffffu, 1, replacementId);
    assert(fixture.valid().replacement == 0xffffffffu);
    fixture.reset();
    fixture.patch(5u, 1u, 0x78787878u);
    fixture.valid();
    fixture.reset(); // OpName cannot mint/alter proof.
    fixture.patch(71u, 2u, 8u, 1, 6u);
    fixture.reject("SHADER-GRAPHICS-WRITE-DECORATION");
    fixture.reset();
    const auto structure = fixture.operand(fixture.locate(30u), 0u);
    const auto array = fixture.operand(fixture.locate(29u), 0u);
    fixture.patch(71u, 2u, 8u, 0, array);
    fixture.reject("SHADER-GRAPHICS-WRITE-STORAGE-ABI");
    fixture.reset();
    fixture.patch(72u, 3u, 4u, 0, structure);
    fixture.reject("SHADER-GRAPHICS-WRITE-STORAGE-ABI");
    fixture.reset();
    fixture.remove(71u, 1, 34u);
    fixture.reject("SHADER-GRAPHICS-WRITE-STORAGE-ABI");
    fixture.reset();
    fixture.duplicateDecoration(34u);
    fixture.reject("SHADER-GRAPHICS-WRITE-STORAGE-ABI");
    fixture.reset();
    fixture.duplicateDecoration(6u);
    fixture.reject("SHADER-GRAPHICS-WRITE-DECORATION");
    fixture.reset();
    const auto integerZero = fixture.operand(fixture.locate(65u), 4u);
    fixture.patch(43u, 2u, 1u, 1, integerZero);
    fixture.reject("SHADER-GRAPHICS-WRITE-ADDRESS");
    fixture.reset();
    const auto semanticsId = fixture.operand(fixture.locate(229u), 4u);
    fixture.patch(43u, 2u, 1u, 1, semanticsId);
    fixture.reject("SHADER-GRAPHICS-WRITE-ATOMIC");
    fixture.reset();
    const auto scopeId = fixture.operand(fixture.locate(229u), 3u);
    fixture.patch(43u, 2u, 2u, 1, scopeId);
    fixture.reject("SHADER-GRAPHICS-WRITE-ATOMIC");
    fixture.reset();
    const auto storageId = fixture.operand(fixture.locate(59u, 2, 12u), 1u);
    fixture.patch(62u, 0u, storageId);
    fixture.reject("SHADER-GRAPHICS-WRITE-OBSERVABLE-RESULT");
    fixture.reset();
    const auto oldValue = fixture.operand(fixture.locate(229u), 1u);
    fixture.patch(62u, 1u, oldValue);
    fixture.reject("SHADER-GRAPHICS-WRITE-OBSERVABLE-RESULT");
    fixture.reset();
    fixture.patch(44u, 2u, oldValue);
    fixture.reject("SHADER-GRAPHICS-WRITE-OBSERVABLE-RESULT");
    fixture.reset();
    fixture.patch(43u, 0u, oldValue);
    fixture.reject("SHADER-GRAPHICS-WRITE-DECLARATION");
    fixture.reset();
    fixture.patch(21u, 1u, 64u);
    fixture.reject("SHADER-GRAPHICS-WRITE-INSTRUCTION-DOMAIN");
    fixture.reset();
    fixture.patch(15u, 0u, 5u);
    fixture.reject("SHADER-GRAPHICS-WRITE-INSTRUCTION-DOMAIN");
    fixture.reset();
    fixture.patch(16u, 1u, 9u);
    fixture.reject("SHADER-GRAPHICS-WRITE-INSTRUCTION-DOMAIN");
    fixture.reset();
    fixture.word(fixture.locate(229u), (7u << 16u) | 228u);
    fixture.reject("SHADER-GRAPHICS-WRITE-BODY");
    fixture.reset();
    const auto addressId = fixture.operand(fixture.locate(65u), 1u);
    fixture.patch(229u, 1u, addressId);
    fixture.reject("SHADER-GRAPHICS-WRITE-ID");
    fixture.reset();
    fixture.word(fixture.locate(65u), 65u);
    fixture.reject("SHADER-SPIRV-INSTRUCTION");
    fixture.reset();
    fixture.word(0u, 0u);
    fixture.reject("SHADER-SPIRV-BINARY");
    fixture.reset();
    fixture.word(4u, 1u);
    fixture.reject("SHADER-SPIRV-HEADER");
    fixture.reset();
    const auto& bytes = fixture.bytes();
    Proof::Facts output;
    assert(Proof::verify(bytes.data(), bytes.size() - 1u, "fragmentMain", output));
    assert(Proof::verify(bytes.data(), bytes.size(), "otherMain", output));
    assert(Proof::verify(nullptr, bytes.size(), "fragmentMain", output));
    core::ByteBuffer unaligned{&core::defaultCpuMemoryResource()};
    unaligned.push_back(0u);
    unaligned.insert(unaligned.end(), bytes.begin(), bytes.end());
    assert(!Proof::verify(unaligned.data() + 1u, bytes.size(), "fragmentMain", output));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    core::CpuMemoryQuotaScope scope(quota);
    core::CpuMemoryQuotaLease exhausted(quota, 1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    for (unsigned i = 0u; i < 100u; ++i)
        fixture.valid();
    std::pmr::set_default_resource(previous);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    auto file = arp::core::readBinaryFile(argv[1], std::filesystem::file_size(argv[1]));
    assert(file.success);
    BinaryMutationFixture fixture(std::move(file.bytes));
    run(fixture);
    std::puts(
        "FragmentAtomicWriteProof/1: actual selected binary address/type/literal/atomic/unused result, hostile mutations, unaligned framing and allocation0 PASS; no native authority");
}
