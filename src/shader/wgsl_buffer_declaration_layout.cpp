#include "shader/wgsl_buffer_declaration_layout.h"

#include "rhi/frontend/memory_budget_ledger.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace arp::shader {
void WgslBufferDeclarationLayoutVerifier::require(bool ok, const char* diagnostic)
{
    if (!ok)
        throw Rejection{diagnostic};
}

WgslBufferDeclarationLayoutVerifier::Token WgslBufferDeclarationLayoutVerifier::peek()
{
    if (!pending_) {
        next_ = cursor_.next();
        pending_ = true;
    }
    require(next_.kind != Kind::Invalid, "SHADER-WGSL-LAYOUT-LEXICAL");
    return next_;
}

WgslBufferDeclarationLayoutVerifier::Token WgslBufferDeclarationLayoutVerifier::take()
{
    const auto result = peek();
    pending_ = false;
    return result;
}

bool WgslBufferDeclarationLayoutVerifier::optional(std::string_view text)
{
    if (peek().text != text || peek().kind == Kind::End)
        return false;
    take();
    return true;
}

void WgslBufferDeclarationLayoutVerifier::expect(std::string_view text)
{
    require(optional(text), "SHADER-WGSL-LAYOUT-GRAMMAR");
}

std::string_view WgslBufferDeclarationLayoutVerifier::identifier()
{
    const auto t = take();
    require(t.kind == Kind::Identifier, "SHADER-WGSL-LAYOUT-IDENTIFIER");
    return t.text;
}

std::uint64_t WgslBufferDeclarationLayoutVerifier::literal()
{
    std::string_view constructor;
    if (peek().kind == Kind::Identifier) {
        constructor = identifier();
        require(constructor == "i32" || constructor == "u32", "SHADER-WGSL-LAYOUT-LITERAL");
        expect("(");
    }
    const auto t = take();
    std::uint64_t result = 0u;
    require(t.kind == Kind::Decimal && (t.text.size() == 1u || t.text.front() != '0'),
        "SHADER-WGSL-LAYOUT-LITERAL");
    const auto parsed = std::from_chars(t.text.data(), t.text.data() + t.text.size(), result);
    require(parsed.ec == std::errc{} && parsed.ptr == t.text.data() + t.text.size()
            && result
                <= (constructor == "i32" ? std::uint64_t{INT32_MAX} : std::uint64_t{UINT32_MAX}),
        "SHADER-WGSL-LAYOUT-LITERAL");
    if (!constructor.empty()) {
        optional(",");
        expect(")");
    }
    return result;
}

void WgslBufferDeclarationLayoutVerifier::define(std::string_view name)
{
    require(std::find(names_.begin(), names_.end(), name) == names_.end(),
        "SHADER-WGSL-LAYOUT-DUPLICATE-NAME");
    require(name != "i32" && name != "u32" && name != "f32" && name != "f16" && name != "bool"
            && name != "array" && name != "atomic"
            && !((name.size() == 4u || name.size() == 5u) && name.substr(0u, 3u) == "vec"
                && name[3] >= '2' && name[3] <= '4')
            && !((name.size() == 6u || name.size() == 7u) && name.substr(0u, 3u) == "mat"
                && name[3] >= '2' && name[3] <= '4' && name[4] == 'x' && name[5] >= '2'
                && name[5] <= '4'),
        "SHADER-WGSL-LAYOUT-RESERVED-NAME");
    names_.push_back(name);
}

WgslBufferDeclarationLayoutVerifier::Attributes WgslBufferDeclarationLayoutVerifier::attributes()
{
    Attributes result;
    core::pmr::Vector<std::string_view> seen{&core::defaultCpuMemoryResource()};
    while (optional("@")) {
        const auto name = identifier();
        require(std::find(seen.begin(), seen.end(), name) == seen.end(),
            "SHADER-WGSL-LAYOUT-DUPLICATE-ATTRIBUTE");
        seen.push_back(name);
        if (name == "group" || name == "binding" || name == "align" || name == "size") {
            expect("(");
            const auto value = literal();
            optional(",");
            expect(")");
            if (name == "group") {
                result.groupSet = true;
                result.group = value;
            } else if (name == "binding") {
                result.bindingSet = true;
                result.binding = value;
            } else if (name == "align") {
                result.alignSet = true;
                result.align = value;
            } else {
                result.sizeSet = true;
                result.size = value;
            }
        } else {
            result.other = true;
            if (optional("("))
                require(cursor_.skipBalanced('(', ')'), "SHADER-WGSL-LAYOUT-ATTRIBUTE-FRAMING");
        }
    }
    return result;
}

std::size_t WgslBufferDeclarationLayoutVerifier::atom()
{
    Node node;
    node.name = identifier();
    const auto name = node.name;
    if (name == "u32" || name == "i32" || name == "f32" || name == "f16")
        node.type = Type::Scalar;
    else if (name == "array")
        node.type = Type::Array;
    else if (name == "atomic")
        node.type = Type::Atomic;
    else if ((name.size() == 4u || name.size() == 5u) && name.substr(0u, 3u) == "vec"
        && name[3] >= '2' && name[3] <= '4') {
        node.type = Type::Vector;
        node.rows = name[3] - '0';
    } else if ((name.size() == 6u || name.size() == 7u) && name.substr(0u, 3u) == "mat"
        && name[3] >= '2' && name[3] <= '4' && name[4] == 'x' && name[5] >= '2' && name[5] <= '4') {
        node.type = Type::Matrix;
        node.columns = name[3] - '0';
        node.rows = name[5] - '0';
    }
    if (node.type == Type::Vector || node.type == Type::Matrix) {
        const auto base = node.type == Type::Vector ? 4u : 6u;
        if (name.size() > base) {
            const auto suffix = name.back();
            Node scalar;
            scalar.type = Type::Scalar;
            if (suffix == 'f')
                scalar.name = "f32";
            else if (suffix == 'h')
                scalar.name = "f16";
            else if (suffix == 'u' && node.type == Type::Vector)
                scalar.name = "u32";
            else if (suffix == 'i' && node.type == Type::Vector)
                scalar.name = "i32";
            else
                require(false, "SHADER-WGSL-LAYOUT-TYPE-UNSUPPORTED");
            node.child = nodes_.size();
            nodes_.push_back(std::move(scalar));
        } else {
            expect("<");
            node.generic = true;
        }
    } else if (node.type == Type::Array || node.type == Type::Atomic) {
        expect("<");
        node.generic = true;
    } else
        require(peek().text != "<", "SHADER-WGSL-LAYOUT-TYPE-UNSUPPORTED");
    const auto index = nodes_.size();
    nodes_.push_back(std::move(node));
    return index;
}

std::size_t WgslBufferDeclarationLayoutVerifier::type()
{
    typeWork_.clear();
    const auto root = atom();
    if (nodes_[root].generic)
        typeWork_.push_back(root);
    while (!typeWork_.empty()) {
        const auto index = typeWork_.back();
        if (nodes_[index].child == absent) {
            const auto child = atom();
            nodes_[index].child = child;
            if (nodes_[child].generic) {
                typeWork_.push_back(child);
                continue;
            }
        }
        if (nodes_[index].type == Type::Array && optional(",")) {
            nodes_[index].count = literal();
            require(nodes_[index].count, "SHADER-WGSL-LAYOUT-ARRAY-LENGTH");
            optional(",");
        } else if (nodes_[index].type != Type::Array)
            optional(",");
        expect(">");
        typeWork_.pop_back();
    }
    return root;
}

void WgslBufferDeclarationLayoutVerifier::structure()
{
    Node node;
    node.type = Type::Struct;
    node.name = identifier();
    define(node.name);
    expect("{");
    while (!optional("}")) {
        Member member;
        member.attributes = attributes();
        member.name = identifier();
        expect(":");
        member.type = type();
        for (const auto& old : node.members)
            require(old.name != member.name, "SHADER-WGSL-LAYOUT-DUPLICATE-MEMBER");
        node.members.push_back(member);
        if (peek().text != "}")
            expect(",");
        else
            optional(",");
    }
    optional(";");
    nodes_.push_back(std::move(node));
}

void WgslBufferDeclarationLayoutVerifier::validateBinding(
    const Attributes& attrs, std::uint32_t maxBindings)
{
    require(bindings_.size() + (opaque_ ? opaque_->size() : 0u) < maxBindings,
        "SHADER-WGSL-LAYOUT-BINDING-BUDGET");
    require(attrs.groupSet && attrs.bindingSet && !attrs.alignSet && !attrs.sizeSet && !attrs.other
            && attrs.group <= UINT32_MAX && attrs.binding <= UINT32_MAX,
        "SHADER-WGSL-LAYOUT-BINDING-ATTRIBUTE");
    for (const auto& old : bindings_)
        require(old.facts.group != attrs.group || old.facts.binding != attrs.binding,
            "SHADER-WGSL-LAYOUT-DUPLICATE-BINDING");
    if (opaque_)
        for (const auto& old : *opaque_)
            require(old.group != attrs.group || old.binding != attrs.binding,
                "SHADER-WGSL-LAYOUT-DUPLICATE-BINDING");
}

void WgslBufferDeclarationLayoutVerifier::opaqueVariable(
    const Attributes& attrs, std::uint32_t maxBindings)
{
    validateBinding(attrs, maxBindings);
    WgslOpaqueDeclaration declaration;
    declaration.group = static_cast<std::uint32_t>(attrs.group);
    declaration.binding = static_cast<std::uint32_t>(attrs.binding);
    declaration.type = identifier();
    const auto type = declaration.type;
    if (type == "texture_2d" || type == "texture_2d_array" || type == "texture_3d"
        || type == "texture_cube") {
        expect("<");
        declaration.scalar = identifier();
        require(declaration.scalar == "f32" || declaration.scalar == "i32"
                || declaration.scalar == "u32",
            "SHADER-WGSL-OPAQUE-SCALAR");
        optional(",");
        expect(">");
    } else if (type == "texture_storage_2d" || type == "texture_storage_2d_array"
        || type == "texture_storage_3d") {
        expect("<");
        declaration.format = identifier();
        expect(",");
        declaration.access = identifier();
        require(declaration.access == "read" || declaration.access == "write"
                || declaration.access == "read_write",
            "SHADER-WGSL-OPAQUE-ACCESS");
        optional(",");
        expect(">");
    } else {
        require(type == "sampler" || type == "sampler_comparison" || type == "texture_depth_2d"
                || type == "texture_depth_2d_array" || type == "texture_depth_cube",
            "SHADER-WGSL-OPAQUE-TYPE-UNSUPPORTED");
    }
    expect(";");
    opaque_->push_back(declaration);
}

void WgslBufferDeclarationLayoutVerifier::variable(
    const Attributes& attrs, std::uint32_t maxBindings)
{
    std::string_view space, access;
    if (optional("<")) {
        space = identifier();
        if (optional(","))
            access = identifier();
        expect(">");
    }
    const auto name = identifier();
    define(name);
    expect(":");
    if (space != "uniform" && space != "storage") {
        if (opaque_ && (space.empty() || attrs.groupSet || attrs.bindingSet)) {
            require(space.empty() && access.empty(), "SHADER-WGSL-OPAQUE-ADDRESS-SPACE");
            opaqueVariable(attrs, maxBindings);
            return;
        }
        require(!attrs.alignSet && !attrs.sizeSet, "SHADER-WGSL-LAYOUT-VARIABLE-ATTRIBUTE");
        require(cursor_.skipOpaqueUntil(';'), "SHADER-WGSL-LAYOUT-VARIABLE-FRAMING");
        return;
    }
    validateBinding(attrs, maxBindings);
    require((space == "uniform" && access.empty())
            || (space == "storage"
                && (access.empty() || access == "read" || access == "read_write")),
        "SHADER-WGSL-LAYOUT-ADDRESS-SPACE");
    Binding binding;
    binding.facts.group = static_cast<std::uint32_t>(attrs.group);
    binding.facts.binding = static_cast<std::uint32_t>(attrs.binding);
    binding.facts.addressSpace = space == "uniform"
        ? WgslBufferDeclarationLayout::AddressSpace::Uniform
        : access == "read_write" ? WgslBufferDeclarationLayout::AddressSpace::StorageReadWrite
                                 : WgslBufferDeclarationLayout::AddressSpace::StorageRead;
    binding.type = type();
    binding.name = name;
    expect(";");
    bindings_.push_back(binding);
}

void WgslBufferDeclarationLayoutVerifier::parse(std::uint32_t maxBindings)
{
    while (peek().kind != Kind::End) {
        if (optional(";"))
            continue;
        const auto attrs = attributes();
        const auto declaration = identifier();
        if (declaration == "var") {
            variable(attrs, maxBindings);
            continue;
        }
        if (declaration == "fn") {
            require(!attrs.groupSet && !attrs.bindingSet && !attrs.alignSet && !attrs.sizeSet,
                "SHADER-WGSL-LAYOUT-FUNCTION-ATTRIBUTE");
            define(identifier());
            expect("(");
            require(cursor_.skipBalanced('(', ')'), "SHADER-WGSL-LAYOUT-FUNCTION-FRAMING");
            require(cursor_.skipOpaqueUntil('{') && cursor_.skipBalanced('{', '}'),
                "SHADER-WGSL-LAYOUT-FUNCTION-FRAMING");
            continue;
        }
        require(!attrs.groupSet && !attrs.bindingSet && !attrs.alignSet && !attrs.sizeSet
                && !attrs.other,
            "SHADER-WGSL-LAYOUT-DECLARATION-ATTRIBUTE");
        if (declaration == "struct")
            structure();
        else if (declaration == "alias") {
            Node node;
            node.type = Type::Alias;
            node.name = identifier();
            define(node.name);
            expect("=");
            node.child = type();
            expect(";");
            nodes_.push_back(std::move(node));
        } else if (declaration == "enable") {
            require(identifier() == "f16" && !f16_, "SHADER-WGSL-LAYOUT-EXTENSION-UNSUPPORTED");
            f16_ = true;
            expect(";");
        } else if (declaration == "const" || declaration == "override") {
            define(identifier());
            require(cursor_.skipOpaqueUntil(';'), "SHADER-WGSL-LAYOUT-CONSTANT-FRAMING");
        } else
            require(false, "SHADER-WGSL-LAYOUT-DECLARATION-UNSUPPORTED");
    }
}

std::size_t WgslBufferDeclarationLayoutVerifier::named(std::string_view name) const
{
    for (std::size_t i = 0u; i < nodes_.size(); ++i)
        if ((nodes_[i].type == Type::Struct || nodes_[i].type == Type::Alias)
            && nodes_[i].name == name)
            return i;
    require(false, "SHADER-WGSL-LAYOUT-TYPE-UNSUPPORTED");
    return absent;
}

std::uint64_t WgslBufferDeclarationLayoutVerifier::add(std::uint64_t a, std::uint64_t b)
{
    require(core::MemoryBudgetLedger::canAdd(a, b), "SHADER-WGSL-LAYOUT-OVERFLOW");
    return a + b;
}

std::uint64_t WgslBufferDeclarationLayoutVerifier::multiply(std::uint64_t a, std::uint64_t b)
{
    require(core::canMultiplyUnsigned(a, b), "SHADER-WGSL-LAYOUT-OVERFLOW");
    return a * b;
}

std::uint64_t WgslBufferDeclarationLayoutVerifier::round(
    std::uint64_t alignment, std::uint64_t value)
{
    std::uint64_t result = 0u;
    require(core::roundUpUnsigned(value, alignment, result), "SHADER-WGSL-LAYOUT-OVERFLOW");
    return result;
}

WgslBufferDeclarationLayoutVerifier::Shape WgslBufferDeclarationLayoutVerifier::complete(
    std::size_t index) const
{
    require(index < nodes_.size() && nodes_[index].state == 2u, "SHADER-WGSL-LAYOUT-DEPENDENCY");
    return nodes_[index].shape;
}

WgslBufferDeclarationLayoutVerifier::Shape WgslBufferDeclarationLayoutVerifier::shape(
    std::size_t root)
{
    work_.clear();
    work_.push_back({root, false});
    while (!work_.empty()) {
        const auto work = work_.back();
        work_.pop_back();
        const auto index = work.type;
        if (nodes_[index].state == 2u)
            continue;
        if (!work.finish) {
            require(nodes_[index].state == 0u, "SHADER-WGSL-LAYOUT-CYCLIC-TYPE");
            nodes_[index].state = 1u;
            if (nodes_[index].type == Type::Named)
                nodes_[index].child = named(nodes_[index].name);
            work_.push_back({index, true});
            if (nodes_[index].child != absent)
                work_.push_back({nodes_[index].child, false});
            for (auto m = nodes_[index].members.size(); m > 0u; --m)
                work_.push_back({nodes_[index].members[m - 1u].type, false});
        } else {
            nodes_[index].shape = evaluate(index);
            nodes_[index].state = 2u;
            if (types_)
                nodes_[index].layoutIndex = emit(index);
        }
    }
    return complete(root);
}

WgslBufferDeclarationLayoutVerifier::Shape WgslBufferDeclarationLayoutVerifier::evaluate(
    std::size_t index)
{
    const auto& node = nodes_[index];
    Shape result;
    result.resolved = node.type;
    if (node.type == Type::Named || node.type == Type::Alias)
        return complete(node.child);
    if (node.type == Type::Scalar) {
        require(node.name != "f16" || f16_, "SHADER-WGSL-LAYOUT-F16-NOT-ENABLED");
        result.align = result.size = result.occupied = node.name == "f16" ? 2u : 4u;
        return result;
    }
    if (node.type != Type::Struct) {
        const auto child = complete(node.child);
        require(!child.runtime, "SHADER-WGSL-LAYOUT-RUNTIME-PLACEMENT");
        result.uniformValid = child.uniformValid;
        result.atomic = child.atomic;
        if (node.type == Type::Vector || node.type == Type::Matrix || node.type == Type::Atomic) {
            require(child.resolved == Type::Scalar, "SHADER-WGSL-LAYOUT-SCALAR-REQUIRED");
            // Alias scalar identity is followed iteratively; no inferred type widths.
            auto scalarIndex = node.child;
            while (
                nodes_[scalarIndex].type == Type::Named || nodes_[scalarIndex].type == Type::Alias)
                scalarIndex = nodes_[scalarIndex].child;
            const auto scalar = nodes_[scalarIndex].name;
            if (node.type == Type::Atomic) {
                require(scalar == "u32" || scalar == "i32", "SHADER-WGSL-LAYOUT-ATOMIC-TYPE");
                result.align = result.size = result.occupied = 4u;
                result.atomic = true;
                result.uniformValid = false;
                return result;
            }
            require(node.type != Type::Matrix || scalar == "f32" || scalar == "f16",
                "SHADER-WGSL-LAYOUT-MATRIX-TYPE");
            result.align = multiply(child.align, node.rows == 2u ? 2u : 4u);
            const auto vectorSize = multiply(child.size, node.rows);
            result.size = node.type == Type::Vector
                ? vectorSize
                : multiply(round(result.align, vectorSize), node.columns);
            result.occupied = node.type == Type::Vector
                ? vectorSize
                : add(multiply(round(result.align, vectorSize), node.columns - 1u), vectorSize);
            return result;
        }
        require(node.type == Type::Array, "SHADER-WGSL-LAYOUT-TYPE-UNSUPPORTED");
        result.align = child.align;
        const auto stride = round(child.align, child.size);
        result.size = multiply(stride, node.count ? node.count : 1u);
        result.runtime = !node.count;
        result.uniformValid = child.uniformValid && !result.runtime && stride % 16u == 0u;
        result.occupied =
            result.runtime ? 0u : add(multiply(stride, node.count - 1u), child.occupied);
        if (result.runtime)
            result.runtimeStride = stride;
        return result;
    }
    require(!node.members.empty(), "SHADER-WGSL-LAYOUT-EMPTY-STRUCT");
    std::uint64_t end = 0u, previousStructOffset = 0u, previousStructSize = 0u;
    bool previousStruct = false;
    for (std::size_t i = 0u; i < node.members.size(); ++i) {
        const auto& member = node.members[i];
        const auto& attrs = member.attributes;
        const auto child = complete(member.type);
        require(!attrs.groupSet && !attrs.bindingSet && !attrs.other,
            "SHADER-WGSL-LAYOUT-MEMBER-ATTRIBUTE");
        const auto alignment = attrs.alignSet ? attrs.align : child.align;
        require(alignment && !(alignment & (alignment - 1u)) && alignment >= child.align,
            "SHADER-WGSL-LAYOUT-ALIGNMENT");
        const auto memberSize = attrs.sizeSet ? attrs.size : child.size;
        require(memberSize >= child.size && (!attrs.sizeSet || !child.runtime),
            "SHADER-WGSL-LAYOUT-MEMBER-SIZE");
        const auto offset = round(alignment, end);
        result.align = std::max(result.align, alignment);
        auto& stored = nodes_[index].members[i];
        stored.offsetBytes = offset;
        stored.alignmentBytes = alignment;
        stored.reservedSizeBytes = memberSize;
        if (child.runtime) {
            require(i + 1u == node.members.size() && child.resolved == Type::Array,
                "SHADER-WGSL-LAYOUT-RUNTIME-PLACEMENT");
            result.runtime = true;
            result.runtimeOffset = offset;
            result.runtimeStride = child.runtimeStride;
        } else
            result.occupied = add(offset, child.occupied);
        const auto required = child.resolved == Type::Array || child.resolved == Type::Struct
            ? round(16u, child.align)
            : child.align;
        result.uniformValid = result.uniformValid && child.uniformValid && offset % required == 0u
            && (!previousStruct || offset - previousStructOffset >= round(16u, previousStructSize));
        previousStruct = child.resolved == Type::Struct;
        previousStructOffset = offset;
        previousStructSize = child.size;
        result.atomic = result.atomic || child.atomic;
        end = add(offset, memberSize);
    }
    result.size = round(result.align, end);
    return result;
}

void WgslBufferDeclarationLayoutVerifier::run(
    std::uint32_t maxBindings, core::pmr::Vector<WgslBufferDeclarationLayout>& output)
{
    parse(maxBindings);
    for (auto binding : bindings_) {
        const auto layout = shape(binding.type);
        require(layout.align && layout.size, "SHADER-WGSL-LAYOUT-EMPTY-TYPE");
        require(binding.facts.addressSpace != WgslBufferDeclarationLayout::AddressSpace::Uniform
                || (layout.uniformValid && !layout.runtime && !layout.atomic),
            "SHADER-WGSL-LAYOUT-UNIFORM-CONSTRAINT");
        require(!layout.atomic
                || binding.facts.addressSpace
                    == WgslBufferDeclarationLayout::AddressSpace::StorageReadWrite,
            "SHADER-WGSL-LAYOUT-ATOMIC-ACCESS");
        binding.facts.alignmentBytes = layout.align;
        binding.facts.minimumBindingBytes = layout.size;
        binding.facts.hasRuntimeArray = layout.runtime;
        binding.facts.runtimeArrayOffsetBytes = layout.runtimeOffset;
        binding.facts.runtimeArrayStrideBytes = layout.runtimeStride;
        output.push_back(binding.facts);
        if (references_) {
            auto resolved = binding.type;
            while (nodes_[resolved].type == Type::Named || nodes_[resolved].type == Type::Alias)
                resolved = nodes_[resolved].child;
            std::string_view member;
            if (layout.runtime && nodes_[resolved].type == Type::Struct)
                member = nodes_[resolved].members.back().name;
            references_->push_back({binding.facts, binding.name, member});
        }
        if (types_) {
            using Storage = BufferDeclarationTypeLayouts::Storage;
            const auto space = binding.facts.addressSpace;
            const auto storage = space == WgslBufferDeclarationLayout::AddressSpace::Uniform
                ? Storage::Uniform
                : space == WgslBufferDeclarationLayout::AddressSpace::StorageRead
                ? Storage::StorageRead
                : Storage::StorageReadWrite;
            if (const auto* id = types_->appendRoot({binding.facts.group,
                    binding.facts.binding,
                    storage,
                    nodes_[binding.type].layoutIndex,
                    1u}))
                throw Rejection{id};
        }
    }
}

const char* WgslBufferDeclarationLayoutVerifier::verify(std::string_view source,
    std::uint32_t maxBindings,
    core::pmr::Vector<WgslBufferDeclarationLayout>& output)
{
    output.clear();
    try {
        WgslBufferDeclarationLayoutVerifier{source}.run(maxBindings, output);
        return nullptr;
    } catch (const Rejection& error) {
        output.clear();
        return error.diagnostic;
    } catch (const std::bad_alloc&) {
        output.clear();
        return "SHADER-WGSL-LAYOUT-ALLOCATION";
    }
}

std::size_t WgslBufferDeclarationLayoutVerifier::emit(std::size_t index)
{
    using Graph = BufferDeclarationTypeLayouts;
    const auto& node = nodes_[index];
    if (node.type == Type::Named || node.type == Type::Alias)
        return nodes_[node.child].layoutIndex;
    Graph::Node record;
    record.occupiedExtentBytes = node.shape.occupied;
    record.alignmentKnown = record.targetSizeKnown = true;
    record.alignmentBytes = node.shape.align;
    record.targetSizeBytes = node.shape.size;
    core::pmr::Vector<Graph::Member> members{&core::defaultCpuMemoryResource()};
    if (node.child != absent)
        record.child = nodes_[node.child].layoutIndex;
    switch (node.type) {
    case Type::Scalar:
        record.kind = Graph::Kind::Scalar;
        record.scalarBits = static_cast<std::uint32_t>(node.shape.size * 8u);
        record.scalar = node.name == "u32" ? Graph::Scalar::UnsignedInteger
            : node.name == "i32"           ? Graph::Scalar::SignedInteger
                                           : Graph::Scalar::FloatingPoint;
        break;
    case Type::Vector:
        record.kind = Graph::Kind::Vector;
        record.count = node.rows;
        break;
    case Type::Matrix:
        record.kind = Graph::Kind::Matrix;
        record.rows = node.rows;
        record.columns = node.columns;
        record.strideBytes = record.targetSizeBytes / node.columns;
        record.matrixEncoding = Graph::MatrixEncoding::WgslColumnVectors;
        {
            Graph::Node vector;
            vector.kind = Graph::Kind::Vector;
            vector.child = record.child;
            vector.count = node.rows;
            vector.occupiedExtentBytes = vector.targetSizeBytes =
                multiply(nodes_[node.child].shape.size, node.rows);
            vector.alignmentKnown = vector.targetSizeKnown = true;
            vector.alignmentBytes = record.alignmentBytes;
            if (const auto* id = types_->append(vector, nullptr, 0u, record.child))
                throw Rejection{id};
        }
        break;
    case Type::Atomic:
        record.kind = Graph::Kind::Atomic;
        break;
    case Type::Array:
        record.kind = node.count ? Graph::Kind::FixedArray : Graph::Kind::RuntimeArray;
        record.count = node.count;
        record.strideBytes = round(nodes_[node.child].shape.align, nodes_[node.child].shape.size);
        break;
    case Type::Struct:
        record.kind = Graph::Kind::Struct;
        for (const auto& member : node.members) {
            const auto runtime = nodes_[member.type].shape.runtime;
            members.push_back({nodes_[member.type].layoutIndex,
                member.offsetBytes,
                true,
                !runtime,
                member.alignmentBytes,
                runtime ? 0u : member.reservedSizeBytes});
        }
        break;
    default:
        throw Rejection{"SHADER-WGSL-LAYOUT-TYPE-UNSUPPORTED"};
    }
    std::size_t result = Graph::absent;
    if (const auto* id = types_->append(record, members.data(), members.size(), result))
        throw Rejection{id};
    return result;
}

const char* WgslBufferDeclarationLayoutVerifier::verifyWithTypes(std::string_view source,
    std::uint32_t maxBindings,
    BufferDeclarationTypeLayouts::Budget budget,
    core::pmr::Vector<WgslBufferDeclarationLayout>& output,
    BufferDeclarationTypeLayouts& types)
{
    output.clear();
    types.begin(budget);
    try {
        WgslBufferDeclarationLayoutVerifier{source, &types}.run(maxBindings, output);
        return nullptr;
    } catch (const Rejection& error) {
        output.clear();
        types.clear();
        return error.diagnostic;
    } catch (const std::bad_alloc&) {
        output.clear();
        types.clear();
        return "SHADER-WGSL-LAYOUT-ALLOCATION";
    }
}

const char* WgslBufferDeclarationLayoutVerifier::verifyWithRuntimeReferences(
    std::string_view source,
    std::uint32_t maxBindings,
    core::pmr::Vector<WgslBufferRuntimeReference>& output)
{
    output.clear();
    try {
        core::pmr::Vector<WgslBufferDeclarationLayout> declarations{
            &core::defaultCpuMemoryResource()};
        WgslBufferDeclarationLayoutVerifier verifier(source);
        verifier.references_ = &output;
        verifier.run(maxBindings, declarations);
        return nullptr;
    } catch (const Rejection& error) {
        output.clear();
        return error.diagnostic;
    } catch (const std::bad_alloc&) {
        output.clear();
        return "SHADER-WGSL-LAYOUT-ALLOCATION";
    }
}

const char* WgslBufferDeclarationLayoutVerifier::verifyWithOpaqueDeclarations(
    std::string_view source,
    std::uint32_t maxBindings,
    core::pmr::Vector<WgslBufferDeclarationLayout>& buffers,
    core::pmr::Vector<WgslOpaqueDeclaration>& opaque)
{
    buffers.clear();
    opaque.clear();
    try {
        WgslBufferDeclarationLayoutVerifier verifier(source);
        verifier.opaque_ = &opaque;
        verifier.run(maxBindings, buffers);
        return nullptr;
    } catch (const Rejection& error) {
        buffers.clear();
        opaque.clear();
        return error.diagnostic;
    } catch (const std::bad_alloc&) {
        buffers.clear();
        opaque.clear();
        return "SHADER-WGSL-LAYOUT-ALLOCATION";
    }
}
} // namespace arp::shader
