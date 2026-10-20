#pragma once
#include "shader/scalar_storage_binary_proof.h"

namespace arp::shader {
// ComputeFixedIndirectProducer/1: selected actual binary facts only.
// Does not authorize an output allocation, recording or GPU completion.
class ComputeIndirectProducerProof final {
public:
    using Facts = ScalarStorageBinaryProof::Facts;
    static constexpr auto wordCount = ScalarStorageBinaryProof::fixedWordCount;

    static const char* validateDispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
    {
        return x == 1u && y == 1u && z == 1u ? nullptr
                                             : "SHADER-COMPUTE-INDIRECT-PRODUCER-DISPATCH";
    }

    static const char* verify(
        const void* bytes, std::size_t count, core::StringView entry, Facts& output) noexcept
    {
        return ScalarStorageBinaryProof::verify(bytes,
            count,
            entry,
            output,
            ScalarStorageBinaryProof::Operation::ComputeFixedWordsWrite);
    }
};
} // namespace arp::shader
