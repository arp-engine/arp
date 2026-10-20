#pragma once
#include "core/cpu_allocator.h"
#include "shader/compute_fixed_words_wgsl_proof.h"

#include <cassert>

namespace arp::test {
class ComputeFixedWordsWgslFixture final {
public:
    static void verify(
        core::StringView source, const shader::ComputeIndirectProducerProof::Facts& binary)
    {
        using Proof = shader::ComputeFixedWordsWgslProof;
        Proof::Facts facts;
        assert(!Proof::verify(source, "computeMain", facts));
        assert(facts.contractVersion == 1u && facts.byteOffset == binary.byteOffset
            && facts.byteCount == binary.byteCount);
        assert(facts.words == binary.fixedWords);
        auto rejected = [&](core::StringView input, core::StringView entry = "computeMain") {
            facts = {99u, 99u, 99u, 99u, 99u, {99u, 99u, 99u, 99u, 99u}};
            assert(Proof::verify(input, entry, facts));
            assert(!facts.contractVersion && !facts.group && !facts.binding && !facts.byteOffset
                && !facts.byteCount);
            for (auto value : facts.words)
                assert(!value);
        };
        auto replaced = [&](core::StringView before, core::StringView after) {
            core::pmr::String copy(source);
            const auto at = copy.find(before);
            assert(at != copy.npos);
            copy.replace(at, before.size(), after);
            return copy;
        };
        rejected({});
        rejected(source, "otherEntry");
        rejected(source.substr(0u, source.find("return")));
        for (const auto mutation : {"read", "uniform", "read_writeX"})
            rejected(replaced("read_write", mutation));
        rejected(replaced("array<u32>", "array<f32>"));
        rejected(replaced("workgroup_size(1, 1, 1)", "workgroup_size(2, 1, 1)"));
        rejected(replaced("i32(4)", "i32(5)"));
        rejected(replaced("i32(4)", "i32(3)"));
        rejected(replaced("i32(4)", "i32(index)"));
        rejected(replaced("i32(4)", "i32(04)"));
        rejected(replaced("u32(4294967294)", "u32(4294967296)"));
        rejected(replaced("u32(4294967294)", "u32(-2)"));
        rejected(replaced("u32(4294967294)", "u32(2u)"));
        rejected(replaced("return;", "return; Output_0[i32(5)] = u32(9);"));
        auto extra = core::pmr::String(source);
        extra += "fn other() {}";
        rejected(extra);
        extra = source;
        extra += "/* unterminated";
        rejected(extra);
        extra = source;
        extra.push_back('\0');
        rejected(extra);
        extra = source;
        extra += "\xc3\xa9";
        rejected(extra);
        extra = source;
        extra += "// trailing comment\n/* nested /*inner*/ outer*/";
        assert(!Proof::verify(extra, "computeMain", facts));
        auto comments = replaced("read_write", "/*a /*b*/ c*/read_write//line\n");
        assert(!Proof::verify(comments, "computeMain", facts));
        auto different = replaced("u32(4294967294)", "u32(4294967295)");
        assert(!Proof::verify(different, "computeMain", facts) && facts.words != binary.fixedWords);
        auto renamed = core::pmr::String(source);
        for (auto at = renamed.find("Output_0"); at != renamed.npos;
            at = renamed.find("Output_0", at + 1u))
            renamed.replace(at, 8u, "Buffer_9");
        assert(!Proof::verify(renamed, "computeMain", facts) && facts.words == binary.fixedWords);
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, 1u);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!Proof::verify(source, "computeMain", facts));
            rejected(different, "wrongEntry");
        }
        std::pmr::set_default_resource(previous);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
};
} // namespace arp::test
