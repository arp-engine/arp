#pragma once
#include "shader/compute_fixed_words_wgsl_proof.h"

namespace arp::shader {
// Independent complete-program proofs. Physical/logical slot correlation is
// owned by the versioned lowering consumer, never inferred from matching IDs.
class ScalarComputeCrossTargetProof final {
public:
    struct Facts {
        ComputeIndirectProducerProof::Facts binary;
        ComputeFixedWordsWgslProof::Facts target;
    };

    static const char* verify(const void* spirv,
        std::size_t bytes,
        core::StringView wgsl,
        core::StringView entry,
        Facts& output) noexcept
    {
        output = {};
        Facts facts;
        if (const auto* id =
                ComputeIndirectProducerProof::verify(spirv, bytes, entry, facts.binary))
            return id;
        if (const auto* id = ComputeFixedWordsWgslProof::verify(wgsl, entry, facts.target))
            return id;
        if (facts.binary.fixedWords != facts.target.words
            || facts.binary.byteOffset != facts.target.byteOffset
            || facts.binary.byteCount != facts.target.byteCount)
            return "SHADER-SCALAR-COMPUTE-TARGET-MISMATCH";
        output = facts;
        return nullptr;
    }
};
} // namespace arp::shader
