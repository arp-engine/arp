#pragma once

#include "core/memory/cpu_allocator.h"
#include "shader/buffer_declaration_type_layout.h"
#include "shader/spirv_instruction_view.h"

namespace arp::shader {
// SPIR-V occupied declaration bytes. No target minimum, access or execution
// authority is represented by this record, including a zero static prefix.
struct SpirvBufferDeclarationLayout {
    std::uint32_t set = 0u, binding = 0u, spirvId = 0u, blockTypeId = 0u, storageClass = 0u;
    std::uint64_t descriptorCount = 0u, staticOccupiedExtentBytes = 0u;
    bool hasRuntimeArray = false;
    std::uint64_t runtimeArrayOffsetBytes = 0u, runtimeArrayStrideBytes = 0u,
                  runtimeElementOccupiedExtentBytes = 0u;

    bool matches(const SpirvBufferDeclarationLayout& other) const noexcept
    {
        return set == other.set && binding == other.binding && spirvId == other.spirvId
            && blockTypeId == other.blockTypeId && storageClass == other.storageClass
            && descriptorCount == other.descriptorCount
            && staticOccupiedExtentBytes == other.staticOccupiedExtentBytes
            && hasRuntimeArray == other.hasRuntimeArray
            && runtimeArrayOffsetBytes == other.runtimeArrayOffsetBytes
            && runtimeArrayStrideBytes == other.runtimeArrayStrideBytes
            && runtimeElementOccupiedExtentBytes == other.runtimeElementOccupiedExtentBytes;
    }
};

struct SpirvPushConstantDeclarationLayout {
    bool present = false;
    std::uint32_t spirvId = 0u, blockTypeId = 0u;
    std::size_t rootIndex = BufferDeclarationTypeLayouts::absent;
    std::uint64_t occupiedExtentBytes = 0u;
};

// Cold iterative proof of declaration relationships, bounded by original load
// input/metadata quota. Reuses instruction framing and strict ID/decoration
// lookup. Unknown declaration types fail; this is not a full SPIR-V validator.
class ARP_API SpirvBufferDeclarationLayoutVerifier final {
public:
    // Access declaration only, independent of member layout and target pointer
    // permission. The caller authenticates the original selected binary facts.
    static const char* inspectReadOnlyStorage(
        const void*, std::size_t, const SpirvBufferDeclarationLayout&) noexcept;

    static const char* verify(const void*,
        std::size_t,
        std::uint32_t maxBindings,
        core::pmr::Vector<SpirvBufferDeclarationLayout>& output);
    static const char* verifyWithTypes(const void*,
        std::size_t,
        std::uint32_t maxBindings,
        BufferDeclarationTypeLayouts::Budget,
        core::pmr::Vector<SpirvBufferDeclarationLayout>& output,
        BufferDeclarationTypeLayouts& types);
    static const char* verifyWithPushConstants(const void*,
        std::size_t,
        std::uint32_t maxBindings,
        BufferDeclarationTypeLayouts::Budget,
        core::pmr::Vector<SpirvBufferDeclarationLayout>&,
        BufferDeclarationTypeLayouts&,
        SpirvPushConstantDeclarationLayout&);

private:
    using I = SpirvInstructionView::Instruction;
    using Status = SpirvInstructionView::DecorationStatus;

    enum Op : std::uint32_t {
        TypeInt = 21,
        TypeFloat = 22,
        TypeVector = 23,
        TypeMatrix = 24,
        TypeArray = 28,
        TypeRuntimeArray = 29,
        TypeStruct = 30,
        TypePointer = 32,
        Constant = 43,
        SpecConstant = 50,
        Variable = 59
    };

    struct Shape {
        std::uint64_t extent = 0u, offset = 0u, stride = 0u, elementExtent = 0u;
        bool runtime = false;
    };

    struct Key {
        std::uint32_t id = 0u, matrixStride = 0u;

        bool matches(const Key& other) const noexcept
        {
            return id == other.id && matrixStride == other.matrixStride;
        }
    };

    struct Node {
        Key key;
        Shape shape;
        bool active = true;
        std::size_t layoutIndex = BufferDeclarationTypeLayouts::absent;
    };

    struct Work {
        Key key;
        bool finish = false;
    };

    struct Rejection {
        const char* diagnostic;
    };

    explicit SpirvBufferDeclarationLayoutVerifier(const void* bytes, std::size_t count)
        : module_(bytes, count)
    {}

    static void require(bool, const char*);
    static int resultOperand(const I&) noexcept;
    I definition(std::uint32_t) const;
    I typed(std::uint32_t, Op, std::uint32_t) const;
    std::uint32_t decoration(std::uint32_t, std::uint32_t, bool marker = false) const;
    std::uint32_t memberOffset(std::uint32_t, std::uint32_t) const;
    std::uint32_t memberMatrixStride(std::uint32_t, std::uint32_t) const;
    std::uint64_t arrayLength(std::uint32_t) const;
    static std::uint64_t add(std::uint64_t, std::uint64_t);
    static std::uint64_t multiply(std::uint64_t, std::uint64_t);
    std::size_t find(Key) const noexcept;
    Shape completed(Key) const;
    Shape typeShape(std::uint32_t);
    std::size_t emit(Key, const Shape&);
    void run(std::uint32_t, core::pmr::Vector<SpirvBufferDeclarationLayout>&);
    SpirvInstructionView module_;
    BufferDeclarationTypeLayouts* types_ = nullptr;
    SpirvPushConstantDeclarationLayout* push_ = nullptr;
    core::pmr::Vector<Node> nodes_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Work> work_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::shader
