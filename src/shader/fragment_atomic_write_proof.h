#pragma once
#include "shader/scalar_storage_binary_proof.h"

namespace arp::shader {
class FragmentAtomicWriteProof final {
public:
    using Facts = ScalarStorageBinaryProof::Facts;

    static const char* verify(
        const void* bytes, std::size_t count, core::StringView entry, Facts& output) noexcept
    {
        return ScalarStorageBinaryProof::verify(
            bytes, count, entry, output, ScalarStorageBinaryProof::Operation::AtomicWrite);
    }
};
} // namespace arp::shader
