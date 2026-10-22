#pragma once
#include "core/text/ascii_token_cursor.h"
#include "shader/target_buffer_memory_declarations.h"

namespace arp::shader {
// Cold operation correspondence, never whole-program/native/original authority.
class ARP_API RuntimeArrayLengthOperations final {
public:
    struct Budget {
        std::size_t maxOperations = 0u, maxTokens = 0u;
    };

    struct Facts {
        std::uint32_t set = 0u, binding = 0u, group = 0u, targetBinding = 0u;
        std::uint32_t sourceResultId = 0u, sourceMember = 0u;
        std::uint64_t runtimeOffsetBytes = 0u, runtimeStrideBytes = 0u;
        std::size_t targetByteOffset = 0u;
    };

    static const char* verify(const core::ByteBuffer& binary,
        core::StringView target,
        const ShaderReflection&,
        std::uint32_t maxBindings,
        const TargetBufferMemoryLoadRequest&,
        Budget,
        core::pmr::Vector<Facts>& output);

private:
    using I = SpirvInstructionView::Instruction;
    static int resultOperand(const I&) noexcept;
};

// Explicit cold-load policy. Borrowed for loading; never retained as authority.
struct RuntimeLengthOperationLoadRequest {
    std::uint32_t contractVersion = 0u;
    RuntimeArrayLengthOperations::Budget operations;
};
} // namespace arp::shader
