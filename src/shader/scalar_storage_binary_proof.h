#pragma once

#include "shader/spirv_instruction_view.h"

#include <array>

namespace arp::shader {

// Shared Fragment reader/atomic and Compute fixed-word storage binary acceptance language. Cold, bounded by input
// bytes, no allocations or source/name inference. Unknown instructions reject.
class ScalarStorageBinaryProof final {
    using I = SpirvInstructionView::Instruction;

    // SPIR-V core grammar opcode and enum values, independent of native APIs.
    enum Op : std::uint32_t {
        Source = 3,
        Name = 5,
        MemberName = 6,
        Extension = 10,
        MemoryModel = 14,
        EntryPoint = 15,
        ExecutionMode = 16,
        Capability = 17,
        TypeVoid = 19,
        TypeBool = 20,
        TypeInt = 21,
        TypeFloat = 22,
        TypeVector = 23,
        TypeRuntimeArray = 29,
        TypeStruct = 30,
        TypePointer = 32,
        TypeFunction = 33,
        Constant = 43,
        ConstantComposite = 44,
        Function = 54,
        FunctionEnd = 56,
        Variable = 59,
        Load = 61,
        Store = 62,
        AccessChain = 65,
        Decorate = 71,
        MemberDecorate = 72,
        Select = 169,
        IEqual = 170,
        AtomicExchange = 229,
        Label = 248,
        Return = 253
    };

    enum : std::uint32_t {
        StorageUniform = 2,
        StorageOutput = 3,
        StorageBuffer = 12,
        Fragment = 4,
        Compute = 5,
        OriginUpperLeft = 7,
        LocalSize = 17,
        Block = 2,
        ArrayStride = 6,
        NonWritable = 24,
        Location = 30,
        Binding = 33,
        DescriptorSet = 34,
        Offset = 35
    };

public:
    static constexpr std::uint32_t uniformStorageClass = StorageUniform,
                                   storageBufferClass = StorageBuffer;
    enum class Operation { AtomicWrite, EqualityRead, UniformEqualityRead, ComputeFixedWordsWrite };
    static constexpr std::size_t fixedWordCount = 5u;

    struct Facts {
        std::uint32_t contractVersion = 0u, set = 0u, binding = 0u, replacement = 0u;
        std::uint64_t byteOffset = 0u, byteCount = 0u;
        std::uint32_t storageClass = 0u;
        std::array<std::uint32_t, fixedWordCount> fixedWords{};
    };

    static const char* verify(const void* bytes,
        std::size_t count,
        core::StringView selectedEntry,
        Facts& output,
        Operation operation) noexcept
    {
        output = {};
        ScalarStorageBinaryProof verifier(bytes, count, selectedEntry, operation);
        Facts facts;
        if (const auto* id = verifier.run(facts))
            return id;
        facts.contractVersion = 1u;
        facts.byteCount = sizeof(std::uint32_t) * (verifier.computeWriting() ? fixedWordCount : 1u);
        output = facts;
        return nullptr;
    }

private:
    ScalarStorageBinaryProof(
        const void* bytes, std::size_t count, core::StringView entry, Operation operation) noexcept
        : operation_(operation), module_(bytes, count), entry_(entry)
    {}

    static int resultOperand(const I& i) noexcept
    {
        switch (i.opcode()) {
        case TypeVoid:
        case TypeBool:
        case TypeInt:
        case TypeFloat:
        case TypeVector:
        case TypeRuntimeArray:
        case TypeStruct:
        case TypePointer:
        case TypeFunction:
        case Label:
            return 0;
        case Constant:
        case ConstantComposite:
        case Variable:
        case Function:
        case AccessChain:
        case AtomicExchange:
        case Load:
        case IEqual:
        case Select:
            return 1;
        default:
            return -1;
        }
    }

    bool definition(std::uint32_t id, I& out) const noexcept
    {
        return module_.definition(id, resultOperand, out);
    }

    bool typed(std::uint32_t id, Op op, std::uint32_t operands, I& out) const noexcept
    {
        return definition(id, out) && out.opcode() == op && out.operands() == operands;
    }

    bool uint32Type(std::uint32_t id) const noexcept
    {
        I i;
        return typed(id, TypeInt, 3u, i) && i.operand(1) == 32u && i.operand(2) == 0u;
    }

    bool integerLiteral(
        std::uint32_t id, std::uint32_t& value, bool unsignedOnly = false) const noexcept
    {
        I literal, type;
        if (!typed(id, Constant, 3u, literal) || !typed(literal.operand(0), TypeInt, 3u, type)
            || type.operand(1) != 32u || type.operand(2) > 1u || (unsignedOnly && type.operand(2)))
            return false;
        value = literal.operand(2);
        return true;
    }

    bool scalarType(std::uint32_t id) const noexcept
    {
        I type;
        return definition(id, type)
            && ((type.opcode() == TypeInt && type.operands() == 3u && type.operand(1) == 32u
                    && type.operand(2) <= 1u)
                || (type.opcode() == TypeFloat && type.operands() == 2u && type.operand(1) == 32u));
    }

    const char* validateDeclaration(const I& i) const noexcept
    {
        I type, child;
        switch (i.opcode()) {
        case TypeVector:
            if (!typed(i.operand(1), TypeFloat, 2u, type))
                return "SHADER-GRAPHICS-WRITE-DECLARATION";
            break;
        case TypeRuntimeArray:
            if (!uint32Type(i.operand(1)))
                return "SHADER-GRAPHICS-WRITE-DECLARATION";
            break;
        case TypeStruct:
            if (uniformReading() ? !uint32Type(i.operand(1))
                                 : !typed(i.operand(1), TypeRuntimeArray, 2u, type))
                return "SHADER-GRAPHICS-WRITE-DECLARATION";
            break;
        case TypePointer:
            if (i.operand(1) == bufferStorageClass()) {
                if (!uint32Type(i.operand(2)) && !typed(i.operand(2), TypeStruct, 2u, type))
                    return "SHADER-GRAPHICS-WRITE-DECLARATION";
            } else if (!typed(i.operand(2), TypeVector, 3u, type))
                return "SHADER-GRAPHICS-WRITE-DECLARATION";
            break;
        case TypeFunction:
            if (!typed(i.operand(1), TypeVoid, 1u, type))
                return "SHADER-GRAPHICS-WRITE-DECLARATION";
            break;
        case Constant:
            if (!scalarType(i.operand(0)))
                return "SHADER-GRAPHICS-WRITE-DECLARATION";
            break;
        case ConstantComposite:
            if (!typed(i.operand(0), TypeVector, 3u, type))
                return "SHADER-GRAPHICS-WRITE-OBSERVABLE-RESULT";
            for (std::uint32_t index = 2u; index < i.operands(); ++index)
                if (!typed(i.operand(index), Constant, 3u, child)
                    || child.operand(0) != type.operand(1))
                    return "SHADER-GRAPHICS-WRITE-OBSERVABLE-RESULT";
            break;
        case Variable:
            if (!typed(i.operand(0), TypePointer, 3u, type) || type.operand(1) != i.operand(2))
                return "SHADER-GRAPHICS-WRITE-DECLARATION";
            break;
        default:
            break;
        }
        return nullptr;
    }

    bool decoration(std::uint32_t id,
        std::uint32_t kind,
        std::uint32_t& value,
        bool marker = false) const noexcept
    {
        return module_.decoration(id, kind, value, marker);
    }

    bool memberOffsetZero(std::uint32_t id) const noexcept
    {
        bool found = false;
        for (auto offset = SpirvInstructionView::headerWords; offset < module_.sizeWords();) {
            I i;
            if (!module_.read(offset, i))
                return false;
            offset = i.next();
            if (i.opcode() != MemberDecorate || i.operand(0) != id)
                continue;
            if (found || i.operands() != 4u || i.operand(1) != 0u || i.operand(2) != Offset
                || i.operand(3) != 0u)
                return false;
            found = true;
        }
        return found;
    }

    bool globalShape(const I& i) const noexcept
    {
        const auto n = i.operands();
        switch (i.opcode()) {
        case Source:
            return n >= 2u;
        case Name:
            return n >= 2u;
        case MemberName:
            return n >= 3u;
        case Extension:
            return i.stringEquals(0u, "SPV_KHR_storage_buffer_storage_class");
        case Capability:
            return n == 1u && i.operand(0) == 1u; // Shader only.
        case MemoryModel:
            return n == 2u && !i.operand(0) && i.operand(1) == 1u; // Logical GLSL450.
        case EntryPoint:
            return n >= (computeWriting() ? 4u : 5u)
                && i.operand(0) == (computeWriting() ? Compute : Fragment);
        case ExecutionMode:
            return computeWriting() ? n == 5u && i.operand(1) == LocalSize && i.operand(2) == 1u
                    && i.operand(3) == 1u && i.operand(4) == 1u
                                    : n == 2u && i.operand(1) == OriginUpperLeft;
        case TypeVoid:
            return n == 1u;
        case TypeBool:
            return reading() && n == 1u;
        case TypeInt:
            return n == 3u && i.operand(1) == 32u && i.operand(2) <= 1u;
        case TypeFloat:
            return !computeWriting() && n == 2u && i.operand(1) == 32u;
        case TypeVector:
            return !computeWriting() && n == 3u && i.operand(2) == 4u;
        case TypeRuntimeArray:
        case TypeStruct:
        case TypeFunction:
            return n == 2u;
        case TypePointer:
            return n == 3u
                && (i.operand(1) == bufferStorageClass()
                    || (!computeWriting() && i.operand(1) == StorageOutput));
        case Constant:
            return n == 3u;
        case ConstantComposite:
            return !computeWriting() && n == 6u;
        case Variable:
            return n == 3u
                && (i.operand(2) == bufferStorageClass()
                    || (!computeWriting() && i.operand(2) == StorageOutput));
        case Decorate:
            return (n == 2u
                       && (i.operand(1) == Block
                           || (reading() && !uniformReading() && i.operand(1) == NonWritable)))
                || (n == 3u
                    && (i.operand(1) == ArrayStride
                        || (!computeWriting() && i.operand(1) == Location)
                        || i.operand(1) == Binding || i.operand(1) == DescriptorSet));
        case MemberDecorate:
            return n == 4u && !i.operand(1) && i.operand(2) == Offset;
        default:
            return false;
        }
    }

    const char* run(Facts& facts) const noexcept
    {
        if (const auto* id = module_.diagnostic())
            return id;
        std::array<I, 2u * fixedWordCount + 4u> body{};
        const std::size_t expectedBody = computeWriting() ? body.size() : reading() ? 9u : 7u;
        std::size_t bodyCount = 0u;
        I entry, execution, storage, output;
        std::uint32_t entryCount = 0u, executionCount = 0u, storageCount = 0u, outputCount = 0u,
                      memoryCount = 0u, capabilityCount = 0u;
        bool function = false, ended = false;
        for (auto offset = SpirvInstructionView::headerWords; offset < module_.sizeWords();) {
            I i;
            if (!module_.read(offset, i))
                return "SHADER-SPIRV-INSTRUCTION";
            offset = i.next();
            const auto resultIndex = resultOperand(i);
            if (resultIndex >= 0) {
                I unique;
                if (static_cast<std::uint32_t>(resultIndex) >= i.operands()
                    || !definition(i.operand(resultIndex), unique))
                    return "SHADER-GRAPHICS-WRITE-ID";
            }
            if (i.opcode() == Function) {
                if (function || ended)
                    return "SHADER-GRAPHICS-WRITE-FUNCTION";
                function = true;
            }
            if (function) {
                if (bodyCount == expectedBody)
                    return "SHADER-GRAPHICS-WRITE-BODY";
                body[bodyCount++] = i;
                if (i.opcode() == FunctionEnd) {
                    function = false;
                    ended = true;
                }
                continue;
            }
            if (ended || !globalShape(i))
                return "SHADER-GRAPHICS-WRITE-INSTRUCTION-DOMAIN";
            switch (i.opcode()) {
            case EntryPoint:
                entry = i;
                ++entryCount;
                break;
            case ExecutionMode:
                execution = i;
                ++executionCount;
                break;
            case MemoryModel:
                ++memoryCount;
                break;
            case Capability:
                ++capabilityCount;
                break;
            case Variable:
                if (i.operand(2) == bufferStorageClass()) {
                    storage = i;
                    ++storageCount;
                } else {
                    output = i;
                    ++outputCount;
                }
                break;
            default:
                break;
            }
        }
        if (!ended || function || bodyCount != expectedBody || entryCount != 1u
            || executionCount != 1u || storageCount != 1u
            || outputCount != (computeWriting() ? 0u : 1u) || memoryCount != 1u
            || capabilityCount != 1u)
            return "SHADER-GRAPHICS-WRITE-MODULE-DOMAIN";
        for (auto offset = SpirvInstructionView::headerWords; offset < module_.sizeWords();) {
            I i;
            if (!module_.read(offset, i))
                return "SHADER-SPIRV-INSTRUCTION";
            offset = i.next();
            if (const auto* id = validateDeclaration(i))
                return id;
        }
        if (entry_.empty() || !entry.stringEquals(2u, entry_, computeWriting() ? 1u : 2u))
            return "SHADER-GRAPHICS-WRITE-ENTRY";
        const auto nameWords =
            (entry_.size() + 1u + sizeof(std::uint32_t) - 1u) / sizeof(std::uint32_t);
        constexpr std::array<Op, 7> opcodes{
            Function, Label, AccessChain, AtomicExchange, Store, Return, FunctionEnd};
        constexpr std::array<std::uint32_t, 7> counts{4u, 1u, 5u, 6u, 2u, 0u, 0u};
        constexpr std::array<Op, 9> readOps{
            Function, Label, AccessChain, Load, IEqual, Select, Store, Return, FunctionEnd};
        const std::array<std::uint32_t, 9> readCounts{
            4u, 1u, uniformReading() ? 4u : 5u, 3u, 4u, 5u, 2u, 0u, 0u};
        for (std::size_t i = 0u; i < expectedBody; ++i) {
            const auto computeOp = i == 0u ? Function
                : i == 1u                  ? Label
                : i == expectedBody - 2u   ? Return
                : i == expectedBody - 1u   ? FunctionEnd
                : i % 2u == 0u             ? AccessChain
                                           : Store;
            const auto computeCount = i == 0u ? 4u
                : i == 1u                     ? 1u
                : i >= expectedBody - 2u      ? 0u
                : i % 2u == 0u                ? 5u
                                              : 2u;
            if (body[i].opcode()
                    != (computeWriting() ? computeOp
                            : reading()  ? readOps[i]
                                         : opcodes[i])
                || body[i].operands()
                    != (computeWriting() ? computeCount
                            : reading()  ? readCounts[i]
                                         : counts[i]))
                return "SHADER-GRAPHICS-WRITE-BODY";
        }
        const auto& fn = body[0];
        const auto& address = body[2];
        const auto& atomic = body[3];
        const auto& store = body[reading() ? 6u : 4u];
        I voidType, functionType;
        if (fn.operand(1) != entry.operand(1) || execution.operand(0) != fn.operand(1)
            || fn.operand(2) || !typed(fn.operand(0), TypeVoid, 1u, voidType)
            || !typed(fn.operand(3), TypeFunction, 2u, functionType)
            || functionType.operand(1) != fn.operand(0))
            return "SHADER-GRAPHICS-WRITE-ENTRY";
        const auto storageId = storage.operand(1), outputId = output.operand(1);
        const auto interfaceIndex = static_cast<std::uint32_t>(2u + nameWords);
        if (computeWriting() ? entry.operand(interfaceIndex) != storageId
                             : !((entry.operand(interfaceIndex) == storageId
                                     && entry.operand(interfaceIndex + 1u) == outputId)
                                   || (entry.operand(interfaceIndex + 1u) == storageId
                                       && entry.operand(interfaceIndex) == outputId)))
            return "SHADER-GRAPHICS-WRITE-INTERFACE";
        I pointer, structure, array, element;
        std::uint32_t stride = 0u, marker = 0u;
        if (!typed(storage.operand(0), TypePointer, 3u, pointer)
            || pointer.operand(1) != bufferStorageClass()
            || !typed(pointer.operand(2), TypeStruct, 2u, structure))
            return "SHADER-GRAPHICS-WRITE-STORAGE-ABI";
        std::uint32_t elementType = structure.operand(1);
        if (!uniformReading()) {
            if (!typed(structure.operand(1), TypeRuntimeArray, 2u, array))
                return "SHADER-GRAPHICS-WRITE-STORAGE-ABI";
            elementType = array.operand(1);
            if (!decoration(array.operand(0), ArrayStride, stride)
                || stride != sizeof(std::uint32_t))
                return "SHADER-GRAPHICS-WRITE-STORAGE-ABI";
        }
        if (!uint32Type(elementType) || !decoration(structure.operand(0), Block, marker, true)
            || !memberOffsetZero(structure.operand(0))
            || !decoration(storageId, DescriptorSet, facts.set)
            || !decoration(storageId, Binding, facts.binding))
            return "SHADER-GRAPHICS-WRITE-STORAGE-ABI";
        facts.storageClass = bufferStorageClass();
        // Every accepted annotation has a known target and one exact value.
        // Irrelevant-looking conflicting annotations cannot reach a native compiler.
        for (auto offset = SpirvInstructionView::headerWords; offset < module_.sizeWords();) {
            I annotation;
            if (!module_.read(offset, annotation))
                return "SHADER-SPIRV-INSTRUCTION";
            offset = annotation.next();
            if (annotation.opcode() == MemberDecorate
                && annotation.operand(0) != structure.operand(0))
                return "SHADER-GRAPHICS-WRITE-DECORATION";
            if (annotation.opcode() != Decorate)
                continue;
            const auto target = annotation.operand(0), kind = annotation.operand(1);
            std::uint32_t value = 0u;
            if (!decoration(target, kind, value, kind == Block || kind == NonWritable))
                return "SHADER-GRAPHICS-WRITE-DECORATION";
            bool valid = false;
            switch (kind) {
            case Block:
                valid = target == structure.operand(0);
                break;
            case NonWritable:
                valid = reading() && target == storageId;
                break;
            case Binding:
            case DescriptorSet:
                valid = target == storageId;
                break;
            case Location:
                valid = target == outputId && !value;
                break;
            case ArrayStride: {
                I annotatedPointer;
                valid = !uniformReading() && value == sizeof(std::uint32_t)
                    && (target == array.operand(0)
                        || (typed(target, TypePointer, 3u, annotatedPointer)
                            && annotatedPointer.operand(1) == StorageBuffer
                            && annotatedPointer.operand(2) == elementType));
                break;
            }
            default:
                break;
            }
            if (!valid)
                return "SHADER-GRAPHICS-WRITE-DECORATION";
        }
        if (computeWriting())
            return validateComputeWords(facts, body, storageId, elementType);
        std::uint32_t member = 0u, index = 0u, scope = 0u, semantics = 0u;
        if (address.operand(2) != storageId || !typed(address.operand(0), TypePointer, 3u, element)
            || element.operand(1) != bufferStorageClass() || element.operand(2) != elementType
            || !integerLiteral(address.operand(3), member) || member
            || (!uniformReading() && (!integerLiteral(address.operand(4), index) || index)))
            return "SHADER-GRAPHICS-WRITE-ADDRESS";
        if (reading())
            return validateRead(facts, body, storageId, outputId, output, elementType);
        if (atomic.operand(0) != array.operand(1) || atomic.operand(2) != address.operand(1)
            || !integerLiteral(atomic.operand(3), scope, true) || scope != 1u         // Device
            || !integerLiteral(atomic.operand(4), semantics, true) || semantics != 0u // Relaxed
            || !integerLiteral(atomic.operand(5), facts.replacement, true))
            return "SHADER-GRAPHICS-WRITE-ATOMIC";
        // The only observable store must be a literal float4 color. No use of
        // atomic result, storage read, branch, call, alias or second writer fits.
        I outputPointer, vector, color, floatType;
        std::uint32_t location = 0u;
        if (store.operand(0) != outputId
            || !typed(output.operand(0), TypePointer, 3u, outputPointer)
            || outputPointer.operand(1) != StorageOutput
            || !typed(outputPointer.operand(2), TypeVector, 3u, vector)
            || !typed(vector.operand(1), TypeFloat, 2u, floatType)
            || !typed(store.operand(1), ConstantComposite, 6u, color)
            || color.operand(0) != vector.operand(0) || !decoration(outputId, Location, location)
            || location)
            return "SHADER-GRAPHICS-WRITE-OBSERVABLE-RESULT";
        for (std::uint32_t i = 2u; i < color.operands(); ++i) {
            I literal;
            if (!typed(color.operand(i), Constant, 3u, literal)
                || literal.operand(0) != floatType.operand(0))
                return "SHADER-GRAPHICS-WRITE-OBSERVABLE-RESULT";
        }
        return nullptr;
    }

    const char* validateComputeWords(Facts& facts,
        const std::array<I, 2u * fixedWordCount + 4u>& body,
        std::uint32_t storageId,
        std::uint32_t elementType) const noexcept
    {
        for (std::size_t word = 0u; word < fixedWordCount; ++word) {
            const auto& address = body[2u + word * 2u];
            const auto& store = body[3u + word * 2u];
            I pointer, literal;
            std::uint32_t member = 0u, index = 0u;
            if (address.operand(2) != storageId
                || !typed(address.operand(0), TypePointer, 3u, pointer)
                || pointer.operand(1) != StorageBuffer || pointer.operand(2) != elementType
                || !integerLiteral(address.operand(3), member) || member
                || !integerLiteral(address.operand(4), index) || index != word)
                return "SHADER-COMPUTE-INDIRECT-PRODUCER-ADDRESS";
            if (store.operand(0) != address.operand(1)
                || !typed(store.operand(1), Constant, 3u, literal)
                || literal.operand(0) != elementType
                || !integerLiteral(store.operand(1), facts.fixedWords[word], true))
                return "SHADER-COMPUTE-INDIRECT-PRODUCER-VALUE";
        }
        return nullptr;
    }

    const char* validateRead(Facts& facts,
        const std::array<I, 2u * fixedWordCount + 4u>& body,
        std::uint32_t storageId,
        std::uint32_t outputId,
        const I& output,
        std::uint32_t uintType) const noexcept
    {
        const auto& address = body[2];
        const auto& load = body[3];
        const auto& compare = body[4];
        const auto& select = body[5];
        const auto& store = body[6];
        std::uint32_t marker = 0u, location = 0u;
        I boolean, pointer, vector, floating;
        if ((!uniformReading() && !decoration(storageId, NonWritable, marker, true))
            || load.operand(0) != uintType || load.operand(2) != address.operand(1)
            || !typed(compare.operand(0), TypeBool, 1u, boolean)
            || compare.operand(2) != load.operand(1)
            || !integerLiteral(compare.operand(3), facts.replacement, true)
            || select.operand(2) != compare.operand(1))
            return "SHADER-GRAPHICS-READ-VALUE";
        I expected;
        if (!typed(compare.operand(3), Constant, 3u, expected) || expected.operand(0) != uintType)
            return "SHADER-GRAPHICS-READ-VALUE";
        if (store.operand(0) != outputId || store.operand(1) != select.operand(1)
            || !typed(output.operand(0), TypePointer, 3u, pointer)
            || pointer.operand(1) != StorageOutput
            || !typed(pointer.operand(2), TypeVector, 3u, vector)
            || select.operand(0) != vector.operand(0)
            || !typed(vector.operand(1), TypeFloat, 2u, floating)
            || !decoration(outputId, Location, location) || location)
            return "SHADER-GRAPHICS-READ-OUTPUT";
        // Exact IEEE754 literal colors certify full-word equality visibly;
        // a merely nonzero word or CPU-provided constant cannot satisfy proof.
        constexpr std::array<std::uint32_t, 4> green{0u, 0x3f800000u, 0u, 0x3f800000u};
        constexpr std::array<std::uint32_t, 4> red{0x3f800000u, 0u, 0u, 0x3f800000u};
        for (std::uint32_t branch = 0u; branch < 2u; ++branch) {
            I color;
            if (!typed(select.operand(3u + branch), ConstantComposite, 6u, color)
                || color.operand(0) != vector.operand(0))
                return "SHADER-GRAPHICS-READ-OUTPUT";
            for (std::uint32_t i = 0u; i < 4u; ++i) {
                I literal;
                if (!typed(color.operand(2u + i), Constant, 3u, literal)
                    || literal.operand(0) != floating.operand(0)
                    || literal.operand(2) != (branch ? red[i] : green[i]))
                    return "SHADER-GRAPHICS-READ-OUTPUT";
            }
        }
        return nullptr;
    }

    bool computeWriting() const noexcept { return operation_ == Operation::ComputeFixedWordsWrite; }

    bool reading() const noexcept
    {
        return operation_ == Operation::EqualityRead
            || operation_ == Operation::UniformEqualityRead;
    }

    bool uniformReading() const noexcept { return operation_ == Operation::UniformEqualityRead; }

    std::uint32_t bufferStorageClass() const noexcept
    {
        return uniformReading() ? StorageUniform : StorageBuffer;
    }

    Operation operation_;
    SpirvInstructionView module_;
    core::StringView entry_;
};

} // namespace arp::shader
