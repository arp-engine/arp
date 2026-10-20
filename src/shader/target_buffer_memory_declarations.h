#pragma once
#include "shader/shader_compiler.h"
#include "shader/spirv_buffer_declaration_layout.h"
#include "shader/spirv_wgsl_buffer_memory_lowering.h"
#include "shader/wgsl_buffer_declaration_layout.h"

namespace arp::shader {
// Explicit SPIR-V/reflection declaration relation. Rules name the original
// push member and its exact graph type, never a compiler-generated type name.
// /1 proves only the bounded float4x4 row-memory representation used by WOCC;
// numerical operations and target lowering need their own proofs.
struct PushBinaryMemoryPlan {
    struct Member {
        std::size_t reflectionMember = BufferDeclarationTypeLayouts::absent;
        std::size_t sourceType = BufferDeclarationTypeLayouts::absent;
        bool arrayValueWrapper = false;
        bool slangRowMajorFloat4x4 = false;
    };

    std::uint32_t contractVersion = 0u;
    const Member* members = nullptr;
    std::size_t count = 0u, maxMembers = 0u;
};

struct TargetBufferMemoryLoadRequest {
    using Lowering = SpirvWgslBufferMemoryLowering;

    struct BindingPlan {
        std::uint32_t set = 0u, binding = 0u;
        Lowering::RootForm form = Lowering::RootForm::BlockStruct;
        Lowering::Plan arrays{nullptr, 0u};
    };

    std::uint32_t contractVersion = 0u;
    BufferDeclarationTypeLayouts::Budget sourceTypes, targetTypes;
    std::size_t maxPairs = 0u, maxArrayRules = 0u;
    const BindingPlan* bindings = nullptr;
    std::size_t bindingCount = 0u;
    bool push = false;
    Lowering::Plan pushArrays{nullptr, 0u};
    // /1 retains its matrix refusal. /2 permits explicit Plan2 column-memory
    // pairs, bounded over all bindings and push data; every rule must be used.
    // These requests prove declarations only, never matrix operations or GPU use.
    std::size_t maxMatrixRules = 0u;
    // Request/2 only. Absent plans preserve the original binary refusal.
    const PushBinaryMemoryPlan* pushBinary = nullptr;

    static constexpr bool supportsVersion(std::uint32_t version) noexcept
    {
        return version == 1u || version == 2u;
    }
};

// Copied declaration facts, never an owner, length or execution receipt.
struct TargetBufferMemoryLayout {
    std::uint32_t set = 0u, binding = 0u;
    BindingType type = BindingType::UniformBuffer;
    WgslBufferDeclarationLayout target;
    SpirvBufferDeclarationLayout source;
};

struct TargetPushMemoryLayout {
    std::uint32_t group = 0u, binding = 0u;
    std::uint64_t sourceOccupiedBytes = 0u, targetDeclarationBytes = 0u, alignmentBytes = 0u;
};

// Cold declaration processor retained by the existing artifact object. Original
// lookup authority belongs exclusively to that artifact's owner check.
class ARP_API TargetBufferMemoryDeclarations final {
public:
    using Graph = BufferDeclarationTypeLayouts;

    struct Record {
        TargetBufferMemoryLayout facts;
        std::size_t sourceRoot = Graph::absent, targetRoot = Graph::absent;
        const char* diagnostic = nullptr;
    };

    TargetBufferMemoryDeclarations() = default;
    TargetBufferMemoryDeclarations(const TargetBufferMemoryDeclarations&) = delete;
    TargetBufferMemoryDeclarations& operator=(const TargetBufferMemoryDeclarations&) = delete;
    static const char* validateRequest(
        const TargetBufferMemoryLoadRequest&, std::uint32_t maxBindings) noexcept;
    const char* build(const core::ByteBuffer& binary,
        core::StringView target,
        const ShaderReflection&,
        const core::pmr::Vector<SpirvBufferDeclarationLayout>&,
        std::uint32_t maxBindings,
        const TargetBufferMemoryLoadRequest&);

    void unavailable(const char* id) noexcept { diagnostic_ = id; }

    const char* lookup(
        std::uint32_t set, std::uint32_t binding, TargetBufferMemoryLayout& output) const noexcept;
    const char* lookupPush(TargetPushMemoryLayout& output) const noexcept;

    const auto& records() const noexcept { return records_; }

    const auto& sourceTypes() const noexcept { return sourceTypes_; }

    const auto& targetTypes() const noexcept { return targetTypes_; }

    const char* diagnostic() const noexcept { return diagnostic_; }

    const char* pushDiagnostic() const noexcept { return pushDiagnostic_; }

    std::size_t pushTargetRoot() const noexcept { return pushTargetRoot_; }

    std::size_t pushSourceRoot() const noexcept { return pushSourceRoot_; }

    const auto& pushFacts() const noexcept { return pushFacts_; }

    static const char* pushBinaryAbi(const Graph&,
        const SpirvPushConstantDeclarationLayout&,
        const ShaderReflection&,
        const PushBinaryMemoryPlan* = nullptr);
    static bool targetFailureIsBudget(const char*) noexcept;

    static bool isBufferBinding(BindingType type) noexcept
    {
        return type == BindingType::UniformBuffer || type == BindingType::StorageBuffer
            || type == BindingType::ReadOnlyStorageBuffer;
    }

    static bool sourceClassMatches(BindingType type, std::uint32_t storageClass) noexcept
    {
        return (type == BindingType::UniformBuffer && storageClass == 2u)
            || ((type == BindingType::StorageBuffer || type == BindingType::ReadOnlyStorageBuffer)
                && storageClass == 12u);
    }

    static bool addressSpaceMatches(
        BindingType type, WgslBufferDeclarationLayout::AddressSpace space) noexcept
    {
        using Space = WgslBufferDeclarationLayout::AddressSpace;
        return (type == BindingType::UniformBuffer && space == Space::Uniform)
            || (type == BindingType::StorageBuffer && space == Space::StorageReadWrite)
            || (type == BindingType::ReadOnlyStorageBuffer && space == Space::StorageRead);
    }

private:
    const char* buildImpl(const core::ByteBuffer&,
        core::StringView,
        const ShaderReflection&,
        const core::pmr::Vector<SpirvBufferDeclarationLayout>&,
        std::uint32_t,
        const TargetBufferMemoryLoadRequest&);
    static const char* independentProof(const Graph&, std::size_t, std::size_t, const char*&, bool);
    Graph sourceTypes_, targetTypes_;
    core::pmr::Vector<Record> records_{&core::defaultCpuMemoryResource()};
    TargetPushMemoryLayout pushFacts_;
    std::size_t pushTargetRoot_ = Graph::absent;
    std::size_t pushSourceRoot_ = Graph::absent;
    const char* diagnostic_ = "SHADER-ENTRY-TARGET-MEMORY-NOT-REQUESTED";
    const char* pushDiagnostic_ = "SHADER-ENTRY-TARGET-PUSH-ABSENT";
};
} // namespace arp::shader
