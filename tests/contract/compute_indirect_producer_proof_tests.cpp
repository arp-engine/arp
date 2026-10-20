#include "../fixtures/compute_fixed_words_wgsl_fixture.h"
#include "../fixtures/spirv_binary_mutation_fixture.h"
#include "core/file_io.h"
#include "rhi/frontend/command_draw_validation.h"
#include "shader/compute_indirect_producer_proof.h"
#include "shader/fragment_atomic_write_proof.h"
#include "shader/scalar_compute_cross_target_proof.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef NDEBUG
#error Compute producer proof tests require assertions
#endif

namespace {
using namespace arp;
using Proof = shader::ComputeIndirectProducerProof;

class Fixture final : public test::SpirvBinaryMutationFixture {
public:
    using SpirvBinaryMutationFixture::SpirvBinaryMutationFixture;

    Proof::Facts valid() const
    {
        Proof::Facts facts;
        const auto* id = Proof::verify(bytes().data(), bytes().size(), "computeMain", facts);
        if (id)
            std::fprintf(stderr, "Unexpected producer proof rejection: %s\n", id);
        assert(!id && facts.contractVersion == 1u && facts.byteCount == 20u && !facts.byteOffset);
        return facts;
    }

    void reject(const char* expected) const
    {
        Proof::Facts facts;
        facts.contractVersion = 99u;
        facts.fixedWords.fill(99u);
        const auto* id = Proof::verify(bytes().data(), bytes().size(), "computeMain", facts);
        if (!id || std::strcmp(id, expected))
            std::fprintf(stderr, "Expected %s, got %s\n", expected, id ? id : "success");
        assert(id && !std::strcmp(id, expected));
        assert(!facts.contractVersion && !facts.byteCount && !facts.set && !facts.binding);
        for (auto value : facts.fixedWords)
            assert(!value);
    }
};

void run(Fixture& f)
{
    static_assert(
        Proof::wordCount * sizeof(std::uint32_t) == sizeof(rhi::DrawIndexedIndirectCommand));
    const auto facts = f.valid();
    assert(facts.set == 0u && facts.binding == 3u && facts.storageClass == 12u);
    assert((facts.fixedWords == std::array<std::uint32_t, 5>{3u, 2u, 1u, 0xfffffffeu, 1u}));
    assert(!Proof::validateDispatch(1u, 1u, 1u));
    for (const auto value : {0u, 2u, UINT32_MAX}) {
        assert(Proof::validateDispatch(value, 1u, 1u));
        assert(Proof::validateDispatch(1u, value, 1u));
        assert(Proof::validateDispatch(1u, 1u, value));
    }
    // Names do not provide proof. Literal changes produce different facts.
    f.patch(5u, 1u, 0x78787878u);
    assert(f.valid().fixedWords == facts.fixedWords);
    f.reset();
    const auto lastStore = f.locate(62u, -1, 0u, 4u), lastAddress = f.locate(65u, -1, 0u, 4u);
    const auto firstAddress = f.locate(65u);
    const auto signedWordId = f.operand(f.locate(62u, -1, 0u, 3u), 1u);
    f.patch(43u, 2u, 0x80000000u, 1, signedWordId);
    assert(f.valid().fixedWords[3] == 0x80000000u);
    f.reset();
    for (auto axis : {2u, 3u, 4u}) {
        f.patch(16u, axis, 2u);
        f.reject("SHADER-GRAPHICS-WRITE-INSTRUCTION-DOMAIN");
        f.reset();
    }
    f.patch(15u, 0u, 4u);
    f.reject("SHADER-GRAPHICS-WRITE-INSTRUCTION-DOMAIN");
    f.reset();
    f.patch(10u, 0u, 0u);
    f.reject("SHADER-GRAPHICS-WRITE-INSTRUCTION-DOMAIN");
    f.reset();
    f.patch(71u, 2u, 8u, 1, 6u);
    f.reject("SHADER-GRAPHICS-WRITE-DECORATION");
    f.reset();
    const auto array = f.operand(f.locate(29u), 0u), structure = f.operand(f.locate(30u), 0u);
    f.patch(71u, 2u, 8u, 0, array);
    f.reject("SHADER-GRAPHICS-WRITE-STORAGE-ABI");
    f.reset();
    f.patch(72u, 3u, 4u, 0, structure);
    f.reject("SHADER-GRAPHICS-WRITE-STORAGE-ABI");
    f.reset();
    f.duplicateDecoration(34u);
    f.reject("SHADER-GRAPHICS-WRITE-STORAGE-ABI");
    f.reset();
    f.remove(71u, 1, 34u);
    f.reject("SHADER-GRAPHICS-WRITE-STORAGE-ABI");
    f.reset();
    const auto firstIndexId = f.operand(firstAddress, 4u), lastIndexId = f.operand(lastAddress, 4u);
    f.word(lastAddress + 5u, firstIndexId);
    f.reject("SHADER-COMPUTE-INDIRECT-PRODUCER-ADDRESS");
    f.reset();
    f.word(firstAddress + 5u, lastIndexId);
    f.reject("SHADER-COMPUTE-INDIRECT-PRODUCER-ADDRESS");
    f.reset();
    f.patch(43u, 2u, 5u, 1, lastIndexId);
    f.reject("SHADER-COMPUTE-INDIRECT-PRODUCER-ADDRESS");
    f.reset();
    f.word(lastStore + 1u, f.operand(firstAddress, 1u));
    f.reject("SHADER-COMPUTE-INDIRECT-PRODUCER-VALUE");
    f.reset();
    f.word(lastStore + 2u, lastIndexId);
    f.reject("SHADER-COMPUTE-INDIRECT-PRODUCER-VALUE");
    f.reset();
    f.word(lastAddress + 2u, f.operand(firstAddress, 1u));
    f.reject("SHADER-GRAPHICS-WRITE-ID");
    f.reset();
    f.remove(62u, 0, f.operand(lastAddress, 1u));
    f.reject("SHADER-GRAPHICS-WRITE-MODULE-DOMAIN");
    f.reset();
    f.word(lastStore, (3u << 16u) | 253u);
    f.reject("SHADER-GRAPHICS-WRITE-BODY");
    f.reset();
    f.word(lastAddress, 65u);
    f.reject("SHADER-SPIRV-INSTRUCTION");
    f.reset();
    f.word(0u, 0u);
    f.reject("SHADER-SPIRV-BINARY");
    f.reset();
    Proof::Facts out;
    assert(Proof::verify(f.bytes().data(), f.bytes().size(), "otherMain", out));
    assert(Proof::verify(f.bytes().data(), f.bytes().size() - 1u, "computeMain", out));
    assert(Proof::verify(nullptr, f.bytes().size(), "computeMain", out));
    core::ByteBuffer unaligned{&core::defaultCpuMemoryResource()};
    unaligned.push_back(0u);
    unaligned.insert(unaligned.end(), f.bytes().begin(), f.bytes().end());
    assert(!Proof::verify(unaligned.data() + 1u, f.bytes().size(), "computeMain", out));
    shader::FragmentAtomicWriteProof::Facts scalar;
    assert(shader::FragmentAtomicWriteProof::verify(
        f.bytes().data(), f.bytes().size(), "computeMain", scalar));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    core::CpuMemoryQuotaScope scope(quota);
    core::CpuMemoryQuotaLease exhausted(quota, 1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    for (unsigned i = 0u; i < 100u; ++i)
        f.valid();
    std::pmr::set_default_resource(previous);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 9);
    auto producer = arp::core::readBinaryFile(argv[1], std::filesystem::file_size(argv[1]));
    assert(producer.success);
    Fixture fixture(std::move(producer.bytes));
    run(fixture);
    auto wgslPath = std::filesystem::path(argv[1]);
    wgslPath.replace_extension(".wgsl");
    auto wgsl = arp::core::readTextFile(wgslPath, std::filesystem::file_size(wgslPath));
    assert(wgsl.success);
    arp::test::ComputeFixedWordsWgslFixture::verify(wgsl.text, fixture.valid());
    arp::shader::ScalarComputeCrossTargetProof::Facts correlated;
    auto correlate = [&] {
        return arp::shader::ScalarComputeCrossTargetProof::verify(
            fixture.bytes().data(), fixture.bytes().size(), wgsl.text, "computeMain", correlated);
    };
    assert(
        !correlate() && correlated.binary.byteCount == 20u && correlated.target.byteCount == 20u);
    const auto index = wgsl.text.find("u32(4294967294)");
    assert(index != wgsl.text.npos);
    wgsl.text.replace(index, 15u, "u32(4294967295)");
    assert(std::string_view(correlate()) == "SHADER-SCALAR-COMPUTE-TARGET-MISMATCH");
    assert(!correlated.binary.contractVersion && !correlated.target.contractVersion);
    wgsl.text.replace(index, 15u, "u32(4294967294)");
    assert(!correlate());
    auto fragment = arp::core::readBinaryFile(argv[2], std::filesystem::file_size(argv[2]));
    assert(fragment.success);
    Proof::Facts output;
    assert(Proof::verify(fragment.bytes.data(), fragment.bytes.size(), "fragmentMain", output));
    for (unsigned variant = 0u; variant < 2u; ++variant) {
        auto binary = arp::core::readBinaryFile(
            argv[3u + variant], std::filesystem::file_size(argv[3u + variant]));
        assert(binary.success);
        const auto* entry = variant ? "noopInstanceMain" : "noopIndexMain";
        assert(!Proof::verify(binary.bytes.data(), binary.bytes.size(), entry, output));
        const std::array<std::uint32_t, 5u> expected{variant ? UINT32_MAX : 0u,
            variant ? 0u : UINT32_MAX,
            UINT32_MAX,
            0x80000000u,
            UINT32_MAX};
        assert(output.contractVersion == 1u && output.byteCount == 20u
            && output.fixedWords == expected);
        // A different selected entry must not inherit the no-op facts.
        assert(Proof::verify(binary.bytes.data(),
            binary.bytes.size(),
            variant ? "noopIndexMain" : "noopInstanceMain",
            output));
        assert((!output.contractVersion && output.fixedWords == std::array<std::uint32_t, 5u>{}));
    }
    const std::array<const char*, 4u> entries{"positiveBaseMain",
        "zeroInstanceBaseMain",
        "outOfRangeIndexMain",
        "outOfRangeInstanceMain"};
    const std::array<std::array<std::uint32_t, 5u>, 4u> expected{{{3u, 1u, 1u, 1u, 2u},
        {3u, 1u, 1u, 0xfffffffeu, 0u},
        {4u, 2u, 1u, 0xfffffffeu, 1u},
        {3u, UINT32_MAX, 1u, 0xfffffffeu, 1u}}};
    for (std::size_t i = 0u; i < entries.size(); ++i) {
        auto binary =
            arp::core::readBinaryFile(argv[5u + i], std::filesystem::file_size(argv[5u + i]));
        assert(binary.success);
        assert(!Proof::verify(binary.bytes.data(), binary.bytes.size(), entries[i], output));
        assert(output.contractVersion == 1u && output.byteCount == 20u
            && output.fixedWords == expected[i]);
        assert(Proof::verify(
            binary.bytes.data(), binary.bytes.size(), entries[(i + 1u) % entries.size()], output));
        assert((!output.contractVersion && output.fixedWords == std::array<std::uint32_t, 5u>{}));
    }
    std::puts(
        "ComputeFixedIndirectProducer/1: actual full20-byte ordered stores/type/literal/single invocation, hostile mutations and allocation0 PASS; no native execution authority");
}
