#pragma once
#include "shader/scalar_storage_binary_proof.h"

namespace arp::shader {
// replacement is the exact compared word, shared with writer facts. Literal
// green/red output and uint32 element-zero read are mandatory proof semantics.
class FragmentStorageReadProof final {
public:
    using Facts = ScalarStorageBinaryProof::Facts;

    static const char* verify(
        const void* bytes, std::size_t count, core::StringView entry, Facts& output) noexcept
    {
        return ScalarStorageBinaryProof::verify(
            bytes, count, entry, output, ScalarStorageBinaryProof::Operation::EqualityRead);
    }
};
} // namespace arp::shader
