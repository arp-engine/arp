#include "shader/spirv_buffer_declaration_layout.h"

#include "rhi/frontend/memory_budget_ledger.h"

namespace arp::shader {
void SpirvBufferDeclarationLayoutVerifier::require(bool value, const char* diagnostic)
{
    if (!value)
        throw Rejection{diagnostic};
}

int SpirvBufferDeclarationLayoutVerifier::resultOperand(const I& i) noexcept
{
    if (i.opcode() >= 19u && i.opcode() <= 33u)
        return 0;
    return i.opcode() == Constant || i.opcode() == SpecConstant || i.opcode() == Variable ? 1 : -1;
}

SpirvBufferDeclarationLayoutVerifier::I SpirvBufferDeclarationLayoutVerifier::definition(
    std::uint32_t id) const
{
    I result;
    require(module_.definition(id, resultOperand, result), "SHADER-BUFFER-LAYOUT-DEFINITION");
    return result;
}

SpirvBufferDeclarationLayoutVerifier::I SpirvBufferDeclarationLayoutVerifier::typed(
    std::uint32_t id, Op opcode, std::uint32_t count) const
{
    const auto result = definition(id);
    require(result.opcode() == opcode && result.operands() == count, "SHADER-BUFFER-LAYOUT-TYPE");
    return result;
}

std::uint32_t SpirvBufferDeclarationLayoutVerifier::decoration(
    std::uint32_t id, std::uint32_t kind, bool marker) const
{
    std::uint32_t value = 0u;
    require(module_.decoration(id, kind, value, marker), "SHADER-BUFFER-LAYOUT-DECORATION");
    return value;
}

std::uint32_t SpirvBufferDeclarationLayoutVerifier::memberOffset(
    std::uint32_t id, std::uint32_t member) const
{
    std::uint32_t value = 0u;
    require(module_.memberDecoration(id, member, 35u, value) == Status::Valid,
        "SHADER-BUFFER-LAYOUT-MEMBER-DECORATION");
    return value;
}

std::uint32_t SpirvBufferDeclarationLayoutVerifier::memberMatrixStride(
    std::uint32_t id, std::uint32_t member) const
{
    std::uint32_t stride = 0u, value = 0u;
    const auto strideStatus = module_.memberDecoration(id, member, 7u, stride);
    const auto col = module_.memberDecoration(id, member, 5u, value, true);
    const auto row = module_.memberDecoration(id, member, 4u, value, true);
    if (strideStatus == Status::Missing) {
        require(col == Status::Missing && row == Status::Missing,
            "SHADER-BUFFER-LAYOUT-MATRIX-MISSING");
        return 0u;
    }
    require(
        strideStatus == Status::Valid && stride && col == Status::Valid && row == Status::Missing,
        "SHADER-BUFFER-LAYOUT-MATRIX-MAJOR");
    return stride;
}

std::uint64_t SpirvBufferDeclarationLayoutVerifier::arrayLength(std::uint32_t id) const
{
    const auto literal = definition(id);
    require(literal.opcode() == Constant, "SHADER-BUFFER-LAYOUT-ARRAY-LENGTH");
    const auto scalar = typed(literal.operand(0), TypeInt, 3u);
    const auto width = scalar.operand(1), sign = scalar.operand(2);
    require((width == 32u || width == 64u) && sign <= 1u && literal.operands() == 2u + width / 32u,
        "SHADER-BUFFER-LAYOUT-ARRAY-LENGTH");
    const auto value = std::uint64_t{literal.operand(2)}
        | (width == 64u ? std::uint64_t{literal.operand(3)} << 32u : 0u);
    require(value && (!sign || !(value >> (width - 1u))), "SHADER-BUFFER-LAYOUT-ARRAY-LENGTH");
    return value;
}

std::uint64_t SpirvBufferDeclarationLayoutVerifier::add(std::uint64_t lhs, std::uint64_t rhs)
{
    require(core::MemoryBudgetLedger::canAdd(lhs, rhs), "SHADER-BUFFER-LAYOUT-OVERFLOW");
    return lhs + rhs;
}

std::uint64_t SpirvBufferDeclarationLayoutVerifier::multiply(std::uint64_t lhs, std::uint64_t rhs)
{
    require(core::canMultiplyUnsigned(lhs, rhs), "SHADER-BUFFER-LAYOUT-OVERFLOW");
    return lhs * rhs;
}

std::size_t SpirvBufferDeclarationLayoutVerifier::find(Key key) const noexcept
{
    for (std::size_t index = 0u; index < nodes_.size(); ++index)
        if (nodes_[index].key.matches(key))
            return index;
    return nodes_.size();
}

SpirvBufferDeclarationLayoutVerifier::Shape SpirvBufferDeclarationLayoutVerifier::completed(
    Key key) const
{
    const auto index = find(key);
    require(index < nodes_.size() && !nodes_[index].active, "SHADER-BUFFER-LAYOUT-DEPENDENCY");
    return nodes_[index].shape;
}

SpirvBufferDeclarationLayoutVerifier::Shape SpirvBufferDeclarationLayoutVerifier::typeShape(
    std::uint32_t root)
{
    work_.clear();
    work_.push_back({{root, 0u}, false});
    while (!work_.empty()) {
        const auto work = work_.back();
        work_.pop_back();
        const auto key = work.key;
        auto index = find(key);
        if (index < nodes_.size() && !nodes_[index].active)
            continue;
        const auto type = definition(key.id);
        const auto op = type.opcode();
        if (!work.finish) {
            require(index == nodes_.size(), "SHADER-BUFFER-LAYOUT-CYCLIC-TYPE");
            nodes_.push_back({key, {}, true});
            if (op == TypeInt || op == TypeFloat) {
                const auto width = type.operand(1);
                require(!key.matrixStride && type.operands() == (op == TypeInt ? 3u : 2u)
                        && (width == 8u || width == 16u || width == 32u || width == 64u)
                        && (op != TypeInt || type.operand(2) <= 1u),
                    "SHADER-BUFFER-LAYOUT-SCALAR");
                nodes_[index].shape.extent = width / 8u;
                nodes_[index].active = false;
                if (types_)
                    nodes_[index].layoutIndex = emit(key, nodes_[index].shape);
                continue;
            }
            work_.push_back({key, true});
            if (op == TypeVector || op == TypeMatrix) {
                require(type.operands() == 3u && type.operand(2) >= 2u && type.operand(2) <= 4u,
                    "SHADER-BUFFER-LAYOUT-VECTOR-MATRIX");
                if (op == TypeMatrix) {
                    const auto vector = typed(type.operand(1), TypeVector, 3u);
                    typed(vector.operand(1), TypeFloat, 2u);
                    require(key.matrixStride, "SHADER-BUFFER-LAYOUT-MATRIX-MISSING");
                } else {
                    const auto scalar = definition(type.operand(1));
                    require(!key.matrixStride
                            && (scalar.opcode() == TypeInt || scalar.opcode() == TypeFloat),
                        "SHADER-BUFFER-LAYOUT-VECTOR-MATRIX");
                }
                work_.push_back({{type.operand(1), 0u}, false});
            } else if (op == TypeArray || op == TypeRuntimeArray) {
                require(
                    type.operands() == (op == TypeArray ? 3u : 2u), "SHADER-BUFFER-LAYOUT-TYPE");
                work_.push_back({{type.operand(1), key.matrixStride}, false});
            } else if (op == TypeStruct) {
                require(!key.matrixStride && type.operands() > 1u, "SHADER-BUFFER-LAYOUT-TYPE");
                for (auto member = type.operands() - 1u; member > 0u; --member)
                    work_.push_back(
                        {{type.operand(member), memberMatrixStride(key.id, member - 1u)}, false});
            } else
                require(false, "SHADER-BUFFER-LAYOUT-TYPE-UNSUPPORTED");
            continue;
        }
        Shape result;
        if (op == TypeVector)
            result.extent = multiply(completed({type.operand(1), 0u}).extent, type.operand(2));
        else if (op == TypeMatrix) {
            const auto child = completed({type.operand(1), 0u});
            require(
                !child.runtime && key.matrixStride >= child.extent, "SHADER-BUFFER-LAYOUT-STRIDE");
            result.extent = add(multiply(key.matrixStride, type.operand(2) - 1u), child.extent);
        } else if (op == TypeArray || op == TypeRuntimeArray) {
            const auto child = completed({type.operand(1), key.matrixStride});
            const auto stride = decoration(key.id, 6u);
            require(
                !child.runtime && stride && stride >= child.extent, "SHADER-BUFFER-LAYOUT-STRIDE");
            if (op == TypeArray)
                result.extent =
                    add(multiply(stride, arrayLength(type.operand(2)) - 1u), child.extent);
            else {
                result.runtime = true;
                result.stride = stride;
                result.elementExtent = child.extent;
            }
        } else {
            for (std::uint32_t member = 1u; member < type.operands(); ++member) {
                const auto childId = type.operand(member);
                const auto child = completed({childId, memberMatrixStride(key.id, member - 1u)});
                const auto offset = memberOffset(key.id, member - 1u);
                require(offset >= result.extent, "SHADER-BUFFER-LAYOUT-OVERLAP");
                if (child.runtime) {
                    require(member + 1u == type.operands()
                            && definition(childId).opcode() == TypeRuntimeArray,
                        "SHADER-BUFFER-LAYOUT-RUNTIME-PLACEMENT");
                    result.runtime = true;
                    result.offset = offset;
                    result.stride = child.stride;
                    result.elementExtent = child.elementExtent;
                } else
                    result.extent = add(offset, child.extent);
            }
        }
        nodes_[index].shape = result;
        nodes_[index].active = false;
        if (types_)
            nodes_[index].layoutIndex = emit(key, result);
    }
    return completed({root, 0u});
}

void SpirvBufferDeclarationLayoutVerifier::run(
    std::uint32_t maxBindings, core::pmr::Vector<SpirvBufferDeclarationLayout>& output)
{
    if (const auto* id = module_.diagnostic())
        throw Rejection{id};
    for (auto offset = SpirvInstructionView::headerWords; offset < module_.sizeWords();) {
        I variable;
        require(module_.read(offset, variable), "SHADER-SPIRV-INSTRUCTION");
        offset = variable.next();
        if (variable.opcode() != Variable)
            continue;
        require(variable.operands() == 3u || variable.operands() == 4u,
            "SHADER-BUFFER-LAYOUT-VARIABLE");
        const auto storage = variable.operand(2);
        if (storage == 9u && push_) {
            require(!push_->present, "SHADER-PUSH-LAYOUT-DUPLICATE");
            typed(variable.operand(1), Variable, variable.operands());
            const auto pointer = typed(variable.operand(0), TypePointer, 3u);
            require(pointer.operand(1) == storage, "SHADER-BUFFER-LAYOUT-STORAGE-CLASS");
            const auto typeId = pointer.operand(2);
            require(definition(typeId).opcode() == TypeStruct, "SHADER-PUSH-LAYOUT-BLOCK");
            decoration(typeId, 2u, true);
            const auto shape = typeShape(typeId);
            require(!shape.runtime, "SHADER-PUSH-LAYOUT-RUNTIME");
            const auto rootType = nodes_[find({typeId, 0u})].layoutIndex;
            const auto rootIndex = types_->roots().size();
            if (const auto* id = types_->appendRoot(
                    {0u, 0u, BufferDeclarationTypeLayouts::Storage::PushConstant, rootType, 0u}))
                throw Rejection{id};
            *push_ = {true, variable.operand(1), typeId, rootIndex, shape.extent};
            continue;
        }
        if (storage != 2u && storage != 12u)
            continue;
        require(output.size() < maxBindings, "SHADER-BUFFER-LAYOUT-BINDING-BUDGET");
        typed(variable.operand(1), Variable, variable.operands());
        const auto pointer = typed(variable.operand(0), TypePointer, 3u);
        require(pointer.operand(1) == storage, "SHADER-BUFFER-LAYOUT-STORAGE-CLASS");
        auto typeId = pointer.operand(2);
        std::uint64_t count = 1u, hops = module_.sizeWords();
        core::pmr::Vector<std::uint64_t> descriptorDimensions{&core::defaultCpuMemoryResource()};
        for (;;) {
            const auto type = definition(typeId);
            if (type.opcode() != TypeArray)
                break;
            require(hops-- && type.operands() == 3u, "SHADER-BUFFER-LAYOUT-CYCLIC-TYPE");
            const auto length = arrayLength(type.operand(2));
            count = multiply(count, length);
            if (types_)
                descriptorDimensions.push_back(length);
            typeId = type.operand(1);
        }
        const auto block = definition(typeId);
        require(block.opcode() == TypeStruct, "SHADER-BUFFER-LAYOUT-TYPE");
        decoration(typeId, 2u, true);
        const auto shape = typeShape(typeId);
        require(!shape.runtime || storage == 12u, "SHADER-BUFFER-LAYOUT-RUNTIME-UNIFORM");
        SpirvBufferDeclarationLayout facts;
        facts.set = decoration(variable.operand(1), 34u);
        facts.binding = decoration(variable.operand(1), 33u);
        facts.spirvId = variable.operand(1);
        facts.blockTypeId = typeId;
        facts.storageClass = storage;
        facts.descriptorCount = count;
        facts.staticOccupiedExtentBytes = shape.extent;
        facts.hasRuntimeArray = shape.runtime;
        facts.runtimeArrayOffsetBytes = shape.offset;
        facts.runtimeArrayStrideBytes = shape.stride;
        facts.runtimeElementOccupiedExtentBytes = shape.elementExtent;
        for (const auto& previous : output)
            require(previous.set != facts.set || previous.binding != facts.binding,
                "SHADER-BUFFER-LAYOUT-BINDING-DUPLICATE");
        output.push_back(facts);
        if (types_) {
            using Graph = BufferDeclarationTypeLayouts;
            const auto rootType = nodes_[find({typeId, 0u})].layoutIndex;
            auto descriptorType = rootType;
            for (auto index = descriptorDimensions.size(); index > 0u; --index) {
                Graph::Node descriptor;
                descriptor.kind = Graph::Kind::DescriptorArray;
                descriptor.child = descriptorType;
                descriptor.count = descriptorDimensions[index - 1u];
                if (const auto* id = types_->append(descriptor, nullptr, 0u, descriptorType))
                    throw Rejection{id};
            }
            const auto graphStorage =
                storage == 2u ? Graph::Storage::Uniform : Graph::Storage::StorageAccessUnspecified;
            if (const auto* id = types_->appendRoot({facts.set,
                    facts.binding,
                    graphStorage,
                    rootType,
                    count,
                    descriptorDimensions.empty() ? Graph::absent : descriptorType}))
                throw Rejection{id};
        }
    }
}

const char* SpirvBufferDeclarationLayoutVerifier::verify(const void* bytes,
    std::size_t count,
    std::uint32_t maxBindings,
    core::pmr::Vector<SpirvBufferDeclarationLayout>& output)
{
    output.clear();
    try {
        SpirvBufferDeclarationLayoutVerifier verifier(bytes, count);
        verifier.run(maxBindings, output);
        return nullptr;
    } catch (const Rejection& error) {
        output.clear();
        return error.diagnostic;
    } catch (const std::bad_alloc&) {
        output.clear();
        throw;
    }
}

std::size_t SpirvBufferDeclarationLayoutVerifier::emit(Key key, const Shape& shape)
{
    using Graph = BufferDeclarationTypeLayouts;
    const auto type = definition(key.id);
    const auto op = type.opcode();
    Graph::Node record;
    record.occupiedExtentBytes = shape.extent;
    core::pmr::Vector<Graph::Member> members{&core::defaultCpuMemoryResource()};
    const auto child = [&](Key childKey) {
        const auto index = find(childKey);
        require(index < nodes_.size() && !nodes_[index].active, "SHADER-BUFFER-LAYOUT-DEPENDENCY");
        return nodes_[index].layoutIndex;
    };
    if (op == TypeInt || op == TypeFloat) {
        record.kind = Graph::Kind::Scalar;
        record.scalarBits = type.operand(1);
        record.scalar = op == TypeFloat ? Graph::Scalar::FloatingPoint
            : type.operand(2)           ? Graph::Scalar::SignedInteger
                                        : Graph::Scalar::UnsignedInteger;
    } else if (op == TypeVector) {
        record.kind = Graph::Kind::Vector;
        record.count = type.operand(2);
        record.child = child({type.operand(1), 0u});
    } else if (op == TypeMatrix) {
        const auto vector = definition(type.operand(1));
        record.kind = Graph::Kind::Matrix;
        record.rows = vector.operand(2);
        record.columns = type.operand(2);
        record.strideBytes = key.matrixStride;
        record.child = child({type.operand(1), 0u});
        record.matrixEncoding = Graph::MatrixEncoding::SpirvColMajorDecoration;
    } else if (op == TypeArray || op == TypeRuntimeArray) {
        record.kind = op == TypeArray ? Graph::Kind::FixedArray : Graph::Kind::RuntimeArray;
        record.count = op == TypeArray ? arrayLength(type.operand(2)) : 0u;
        record.strideBytes = decoration(key.id, 6u);
        record.child = child({type.operand(1), key.matrixStride});
    } else if (op == TypeStruct) {
        record.kind = Graph::Kind::Struct;
        for (std::uint32_t member = 1u; member < type.operands(); ++member)
            members.push_back(
                {child({type.operand(member), memberMatrixStride(key.id, member - 1u)}),
                    memberOffset(key.id, member - 1u)});
    } else
        throw Rejection{"SHADER-BUFFER-LAYOUT-TYPE-UNSUPPORTED"};
    std::size_t result = Graph::absent;
    if (const auto* id = types_->append(record, members.data(), members.size(), result))
        throw Rejection{id};
    return result;
}

const char* SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(const void* bytes,
    std::size_t count,
    std::uint32_t maxBindings,
    BufferDeclarationTypeLayouts::Budget budget,
    core::pmr::Vector<SpirvBufferDeclarationLayout>& output,
    BufferDeclarationTypeLayouts& types)
{
    output.clear();
    types.begin(budget);
    try {
        SpirvBufferDeclarationLayoutVerifier verifier(bytes, count);
        verifier.types_ = &types;
        verifier.run(maxBindings, output);
        return nullptr;
    } catch (const Rejection& error) {
        output.clear();
        types.clear();
        return error.diagnostic;
    } catch (const std::bad_alloc&) {
        output.clear();
        types.clear();
        return "SHADER-BUFFER-TYPE-ALLOCATION";
    }
}

const char* SpirvBufferDeclarationLayoutVerifier::verifyWithPushConstants(const void* bytes,
    std::size_t count,
    std::uint32_t maxBindings,
    BufferDeclarationTypeLayouts::Budget budget,
    core::pmr::Vector<SpirvBufferDeclarationLayout>& output,
    BufferDeclarationTypeLayouts& types,
    SpirvPushConstantDeclarationLayout& push)
{
    output.clear();
    types.begin(budget);
    push = {};
    try {
        SpirvBufferDeclarationLayoutVerifier verifier(bytes, count);
        verifier.types_ = &types;
        verifier.push_ = &push;
        verifier.run(maxBindings, output);
        return nullptr;
    } catch (const Rejection& error) {
        output.clear();
        types.clear();
        push = {};
        return error.diagnostic;
    } catch (const std::bad_alloc&) {
        output.clear();
        types.clear();
        push = {};
        return "SHADER-BUFFER-TYPE-ALLOCATION";
    }
}
} // namespace arp::shader
