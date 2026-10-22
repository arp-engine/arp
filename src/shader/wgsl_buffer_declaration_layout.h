#pragma once

#include "core/api.h"
#include "core/math/unsigned_range.h"
#include "core/memory/cpu_allocator.h"
#include "core/text/ascii_token_cursor.h"
#include "shader/buffer_declaration_type_layout.h"

namespace arp::shader {
// Target declaration facts only. No original owner, entry-point interface,
// execution access, physical-map equivalence or native slice authority.
struct WgslBufferDeclarationLayout {
    enum class AddressSpace : std::uint32_t { Uniform = 1, StorageRead = 2, StorageReadWrite = 3 };
    std::uint32_t group = 0u, binding = 0u;
    AddressSpace addressSpace = AddressSpace::Uniform;
    std::uint64_t alignmentBytes = 0u, minimumBindingBytes = 0u;
    bool hasRuntimeArray = false;
    std::uint64_t runtimeArrayOffsetBytes = 0u, runtimeArrayStrideBytes = 0u;

    // A pure arithmetic query on a caller copy, not an execution receipt.
    const char* runtimeArrayLength(std::uint64_t bindingBytes, std::uint64_t& output) const noexcept
    {
        output = 0u;
        if (!hasRuntimeArray || !runtimeArrayStrideBytes || !minimumBindingBytes
            || bindingBytes < minimumBindingBytes || bindingBytes < runtimeArrayOffsetBytes)
            return "SHADER-WGSL-LAYOUT-RUNTIME-LENGTH";
        output = (bindingBytes - runtimeArrayOffsetBytes) / runtimeArrayStrideBytes;
        return nullptr;
    }
};

// Borrowed identities from the exact target source. No original/native authority.
struct WgslBufferRuntimeReference {
    WgslBufferDeclarationLayout declaration;
    std::string_view variable, runtimeMember;
};

// Borrowed declaration facts from the exact WGSL source. No sampler filtering,
// sampling-use, resource content or execution authority is inferred. Opaque
// descriptors are scalar; binding_array/aliases require a separate ABI proof.
struct WgslOpaqueDeclaration {
    std::uint32_t group = 0u, binding = 0u;
    std::string_view type, scalar, format, access;
};

class ARP_API WgslBufferDeclarationLayoutVerifier final {
public:
    static const char* verify(std::string_view,
        std::uint32_t maxBindings,
        core::pmr::Vector<WgslBufferDeclarationLayout>& output);
    static const char* verifyWithTypes(std::string_view,
        std::uint32_t maxBindings,
        BufferDeclarationTypeLayouts::Budget,
        core::pmr::Vector<WgslBufferDeclarationLayout>& output,
        BufferDeclarationTypeLayouts& types);
    static const char* verifyWithRuntimeReferences(std::string_view,
        std::uint32_t maxBindings,
        core::pmr::Vector<WgslBufferRuntimeReference>& output);
    // Reuses the same module lexer, attributes, names and declaration framing.
    // maxBindings covers buffers AND opaque bindings; both outputs roll back.
    static const char* verifyWithOpaqueDeclarations(std::string_view,
        std::uint32_t maxBindings,
        core::pmr::Vector<WgslBufferDeclarationLayout>& buffers,
        core::pmr::Vector<WgslOpaqueDeclaration>& opaque);

private:
    using Token = core::AsciiTokenCursor::Token;
    using Kind = core::AsciiTokenCursor::Kind;
    static constexpr std::size_t absent = static_cast<std::size_t>(-1);
    enum class Type { Named, Scalar, Vector, Matrix, Atomic, Array, Struct, Alias };

    struct Attributes {
        bool groupSet = false, bindingSet = false, alignSet = false, sizeSet = false, other = false;
        std::uint64_t group = 0u, binding = 0u, align = 0u, size = 0u;
    };

    struct Member {
        std::string_view name;
        std::size_t type = absent;
        Attributes attributes;
        std::uint64_t offsetBytes = 0u, alignmentBytes = 0u, reservedSizeBytes = 0u;
    };

    struct Shape {
        std::uint64_t align = 0u, size = 0u, runtimeOffset = 0u, runtimeStride = 0u;
        std::uint64_t occupied = 0u;
        bool runtime = false, uniformValid = true, atomic = false;
        Type resolved = Type::Named;
    };

    struct Node {
        Type type = Type::Named;
        std::string_view name;
        std::size_t child = absent;
        std::size_t layoutIndex = absent;
        std::uint64_t count = 0u;
        std::uint32_t columns = 0u, rows = 0u, state = 0u;
        bool generic = false;
        Shape shape;
        core::pmr::Vector<Member> members{&core::defaultCpuMemoryResource()};
    };

    struct Binding {
        WgslBufferDeclarationLayout facts;
        std::size_t type = absent;
        std::string_view name;
    };

    struct Work {
        std::size_t type = absent;
        bool finish = false;
    };

    struct Rejection {
        const char* diagnostic;
    };

    explicit WgslBufferDeclarationLayoutVerifier(
        std::string_view source, BufferDeclarationTypeLayouts* types = nullptr)
        : cursor_(source), types_(types)
    {}

    static void require(bool, const char*);
    Token peek();
    Token take();
    bool optional(std::string_view);
    void expect(std::string_view);
    std::string_view identifier();
    std::uint64_t literal();
    void define(std::string_view);
    Attributes attributes();
    std::size_t atom();
    std::size_t type();
    void structure();
    void variable(const Attributes&, std::uint32_t);
    void opaqueVariable(const Attributes&, std::uint32_t);
    void validateBinding(const Attributes&, std::uint32_t);
    void parse(std::uint32_t);
    std::size_t named(std::string_view) const;
    Shape shape(std::size_t);
    Shape complete(std::size_t) const;
    Shape evaluate(std::size_t);
    std::size_t emit(std::size_t);
    static std::uint64_t add(std::uint64_t, std::uint64_t);
    static std::uint64_t multiply(std::uint64_t, std::uint64_t);
    static std::uint64_t round(std::uint64_t alignment, std::uint64_t value);
    void run(std::uint32_t, core::pmr::Vector<WgslBufferDeclarationLayout>&);
    core::AsciiTokenCursor cursor_;
    Token next_;
    bool pending_ = false, f16_ = false;
    BufferDeclarationTypeLayouts* types_ = nullptr;
    core::pmr::Vector<std::string_view> names_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Node> nodes_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Binding> bindings_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::size_t> typeWork_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Work> work_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<WgslBufferRuntimeReference>* references_ = nullptr;
    core::pmr::Vector<WgslOpaqueDeclaration>* opaque_ = nullptr;
};
} // namespace arp::shader
