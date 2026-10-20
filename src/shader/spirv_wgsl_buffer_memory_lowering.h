#pragma once
#include "shader/buffer_declaration_type_layout.h"

namespace arp::shader {
// Cold declaration relations, never an original or execution authority.
// /1 keeps matrices closed. /2 admits only explicitly paired column-memory
// declarations; numerical matrix multiplication still needs execution evidence.
class SpirvWgslBufferMemoryLowering final {
    using Graph = BufferDeclarationTypeLayouts;

public:
    enum class RootForm : std::uint32_t {
        BlockStruct = 1,
        UniformValue,
        StorageRuntimeArray,
        PushBlockStruct
    };

    struct ArrayValueWrapper {
        std::size_t sourceType = Graph::absent, targetType = Graph::absent;
    };
    enum class MatrixTargetForm : std::uint32_t { Matrix = 1u, ColumnArrayStruct };

    struct MatrixColumnValue {
        std::size_t sourceType = Graph::absent, targetType = Graph::absent;
        MatrixTargetForm targetForm = MatrixTargetForm::Matrix;
    };

    struct Plan {
        const ArrayValueWrapper* arrayValueWrappers;
        std::size_t count;
        const MatrixColumnValue* matrixColumnValues = nullptr;
        std::size_t matrixCount = 0u;
        std::uint32_t contractVersion = 1u;
    };

    struct Facts {
        RootForm form = RootForm::BlockStruct;
        std::uint64_t sourceOccupiedBytes = 0u, targetDeclarationBytes = 0u;
        std::size_t comparedPairs = 0u, arrayValueWrappers = 0u;
        std::size_t matrixColumnValues = 0u;
    };

    static const char* selectSourceType(const Graph& source,
        std::size_t sourceRoot,
        const Graph& target,
        std::size_t targetRoot,
        RootForm form,
        std::size_t& left) noexcept
    {
        left = Graph::absent;
        if (sourceRoot >= source.roots().size() || targetRoot >= target.roots().size())
            return "SHADER-BUFFER-LOWERING-ROOT";
        const auto& a = source.roots()[sourceRoot];
        const auto& b = target.roots()[targetRoot];
        const bool push = form == RootForm::PushBlockStruct;
        if ((a.storage == Graph::Storage::PushConstant) != push)
            return "SHADER-BUFFER-LOWERING-ROOT-FORM";
        if (a.descriptorCount != (push ? 0u : 1u) || b.descriptorCount != 1u
            || a.descriptorType != Graph::absent || b.descriptorType != Graph::absent)
            return "SHADER-BUFFER-LOWERING-DESCRIPTOR-ARRAY";
        if (a.type >= source.nodes().size() || b.type >= target.nodes().size())
            return "SHADER-BUFFER-LOWERING-FRAMING";
        const bool uniform = a.storage == Graph::Storage::Uniform || push;
        if ((!uniform && a.storage != Graph::Storage::StorageAccessUnspecified)
            || (uniform ? b.storage != Graph::Storage::Uniform
                        : b.storage != Graph::Storage::StorageRead
                        && b.storage != Graph::Storage::StorageReadWrite))
            return "SHADER-BUFFER-LOWERING-STORAGE";
        const auto& block = source.nodes()[a.type];
        const auto& value = target.nodes()[b.type];
        if (block.kind != Graph::Kind::Struct || !membersValid(source, block))
            return "SHADER-BUFFER-LOWERING-BLOCK";
        if (block.alignmentKnown || block.targetSizeKnown)
            return "SHADER-BUFFER-LOWERING-ENCODING";
        left = a.type;
        switch (form) {
        case RootForm::BlockStruct:
        case RootForm::PushBlockStruct:
            if (value.kind != Graph::Kind::Struct)
                return "SHADER-BUFFER-LOWERING-ROOT-FORM";
            break;
        case RootForm::UniformValue:
        case RootForm::StorageRuntimeArray: {
            if (block.memberCount != 1u)
                return "SHADER-BUFFER-LOWERING-ROOT-FORM";
            const auto& m = source.members()[block.memberBegin];
            if (m.offsetBytes || m.type >= source.nodes().size())
                return "SHADER-BUFFER-LOWERING-ROOT-FORM";
            left = m.type;
            if (m.alignmentKnown || m.reservedSizeKnown
                || block.occupiedExtentBytes != source.nodes()[left].occupiedExtentBytes)
                return "SHADER-BUFFER-LOWERING-ENCODING";
            if (form == RootForm::UniformValue) {
                if (!uniform || value.kind == Graph::Kind::RuntimeArray)
                    return "SHADER-BUFFER-LOWERING-ROOT-FORM";
            } else if (uniform || source.nodes()[left].kind != Graph::Kind::RuntimeArray
                || value.kind != Graph::Kind::RuntimeArray)
                return "SHADER-BUFFER-LOWERING-ROOT-FORM";
            break;
        }
        default:
            return "SHADER-BUFFER-LOWERING-ROOT-FORM";
        }
        return nullptr;
    }

    static const char* compare(const Graph& source,
        std::size_t sourceRoot,
        const Graph& target,
        std::size_t targetRoot,
        RootForm form,
        std::size_t maxPairs,
        Facts& output,
        Plan plan = {nullptr, 0u, nullptr, 0u, 1u})
    {
        output = {};
        if (plan.contractVersion != 1u && plan.contractVersion != 2u)
            return "SHADER-BUFFER-LOWERING-PLAN-VERSION";
        if (plan.contractVersion == 1u && plan.matrixCount)
            return "SHADER-BUFFER-LOWERING-MATRIX-PROOF";
        if (!maxPairs)
            return "SHADER-BUFFER-LOWERING-PAIR-BUDGET";
        if (plan.count > maxPairs || plan.matrixCount > maxPairs - plan.count
            || (plan.count && !plan.arrayValueWrappers)
            || (plan.matrixCount && !plan.matrixColumnValues))
            return "SHADER-BUFFER-LOWERING-PLAN-BUDGET";
        std::size_t left = Graph::absent;
        if (const auto* id = selectSourceType(source, sourceRoot, target, targetRoot, form, left))
            return id;
        const auto& a = source.roots()[sourceRoot];
        const auto& b = target.roots()[targetRoot];
        const auto& block = source.nodes()[a.type];
        const auto& value = target.nodes()[b.type];
        try {
            core::pmr::Vector<std::uint8_t> used{&core::defaultCpuMemoryResource()};
            used.resize(plan.count + plan.matrixCount, 0u);
            for (std::size_t i = 0u; i < plan.matrixCount; ++i) {
                const auto& rule = plan.matrixColumnValues[i];
                if (rule.sourceType >= source.nodes().size()
                    || rule.targetType >= target.nodes().size())
                    return "SHADER-BUFFER-LOWERING-MATRIX-PLAN-FRAMING";
                const auto& x = source.nodes()[rule.sourceType];
                const auto& y = target.nodes()[rule.targetType];
                if (x.kind != Graph::Kind::Matrix
                    || x.matrixEncoding != Graph::MatrixEncoding::SpirvColMajorDecoration
                    || x.alignmentKnown || x.targetSizeKnown || !y.alignmentKnown
                    || !y.targetSizeKnown)
                    return "SHADER-BUFFER-LOWERING-MATRIX-PLAN-FRAMING";
                if (rule.targetForm == MatrixTargetForm::Matrix) {
                    if (y.kind != Graph::Kind::Matrix
                        || y.matrixEncoding != Graph::MatrixEncoding::WgslColumnVectors)
                        return "SHADER-BUFFER-LOWERING-MATRIX-PLAN-FRAMING";
                } else if (rule.targetForm == MatrixTargetForm::ColumnArrayStruct) {
                    if (y.kind != Graph::Kind::Struct || y.memberCount != 1u
                        || !membersValid(target, y))
                        return "SHADER-BUFFER-LOWERING-MATRIX-PLAN-FRAMING";
                    const auto& member = target.members()[y.memberBegin];
                    if (member.offsetBytes || member.type >= target.nodes().size())
                        return "SHADER-BUFFER-LOWERING-MATRIX-PLAN-FRAMING";
                    const auto& array = target.nodes()[member.type];
                    if (array.kind != Graph::Kind::FixedArray || !array.targetSizeKnown
                        || !array.alignmentKnown
                        || y.occupiedExtentBytes != array.occupiedExtentBytes
                        || y.targetSizeBytes != array.targetSizeBytes
                        || y.alignmentBytes != array.alignmentBytes)
                        return "SHADER-BUFFER-LOWERING-MATRIX-PLAN-FRAMING";
                } else
                    return "SHADER-BUFFER-LOWERING-MATRIX-PLAN-FRAMING";
                for (std::size_t j = 0u; j < i; ++j)
                    if (rule.sourceType == plan.matrixColumnValues[j].sourceType
                        && rule.targetType == plan.matrixColumnValues[j].targetType)
                        return "SHADER-BUFFER-LOWERING-PLAN-DUPLICATE";
            }
            for (std::size_t i = 0u; i < plan.count; ++i) {
                const auto& rule = plan.arrayValueWrappers[i];
                if (rule.sourceType >= source.nodes().size()
                    || rule.targetType >= target.nodes().size())
                    return "SHADER-BUFFER-LOWERING-PLAN-FRAMING";
                const auto& wrapper = source.nodes()[rule.sourceType];
                const auto& array = target.nodes()[rule.targetType];
                if (wrapper.kind != Graph::Kind::Struct || wrapper.memberCount != 1u
                    || !membersValid(source, wrapper) || wrapper.alignmentKnown
                    || wrapper.targetSizeKnown || array.kind != Graph::Kind::FixedArray
                    || !array.targetSizeKnown || !array.alignmentKnown)
                    return "SHADER-BUFFER-LOWERING-PLAN-FRAMING";
                const auto& member = source.members()[wrapper.memberBegin];
                if (member.offsetBytes || member.alignmentKnown || member.reservedSizeKnown
                    || member.type >= source.nodes().size()
                    || source.nodes()[member.type].kind != Graph::Kind::FixedArray
                    || wrapper.occupiedExtentBytes
                        != source.nodes()[member.type].occupiedExtentBytes)
                    return "SHADER-BUFFER-LOWERING-PLAN-FRAMING";
                for (std::size_t j = 0u; j < i; ++j)
                    if (rule.sourceType == plan.arrayValueWrappers[j].sourceType
                        && rule.targetType == plan.arrayValueWrappers[j].targetType)
                        return "SHADER-BUFFER-LOWERING-PLAN-DUPLICATE";
            }
            const auto resolve = [&](std::size_t& l, std::size_t& r) -> const char* {
                for (std::size_t i = 0u; i < plan.count; ++i) {
                    const auto& rule = plan.arrayValueWrappers[i];
                    if (l == rule.sourceType && r == rule.targetType) {
                        l = source.members()[source.nodes()[l].memberBegin].type;
                        used[i] = 1u;
                        break;
                    }
                }
                for (std::size_t i = 0u; i < plan.matrixCount; ++i) {
                    const auto& rule = plan.matrixColumnValues[i];
                    if (l == rule.sourceType && r == rule.targetType
                        && rule.targetForm == MatrixTargetForm::ColumnArrayStruct) {
                        r = target.members()[target.nodes()[r].memberBegin].type;
                        break;
                    }
                }
                return nullptr;
            };
            bool equal = false;
            std::size_t compared = 0u;
            const auto* diagnostic = Graph::visitPairs(
                source,
                left,
                target,
                b.type,
                maxPairs,
                [&](const Graph::Node& x, const Graph::Node& y, bool& matches) -> const char* {
                    const bool matrix =
                        x.kind == Graph::Kind::Matrix || y.kind == Graph::Kind::Matrix;
                    if (matrix) {
                        bool declared = false;
                        for (std::size_t i = 0u; i < plan.matrixCount; ++i) {
                            const auto& rule = plan.matrixColumnValues[i];
                            const auto targetType =
                                rule.targetForm == MatrixTargetForm::ColumnArrayStruct
                                ? target.members()[target.nodes()[rule.targetType].memberBegin].type
                                : rule.targetType;
                            if (&x == &source.nodes()[rule.sourceType]
                                && &y == &target.nodes()[targetType]) {
                                used[plan.count + i] = 1u;
                                declared = true;
                                break;
                            }
                        }
                        if (!declared)
                            return "SHADER-BUFFER-LOWERING-MATRIX-PROOF";
                        if (y.kind == Graph::Kind::FixedArray) {
                            if (y.child >= target.nodes().size())
                                return "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH";
                            const auto& vector = target.nodes()[y.child];
                            if (vector.kind != Graph::Kind::Vector || vector.count != x.rows
                                || y.count != x.columns || x.strideBytes != y.strideBytes
                                || x.occupiedExtentBytes != y.occupiedExtentBytes || x.memberCount
                                || y.memberCount || x.child == Graph::absent)
                                return "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH";
                            matches = true;
                            return nullptr;
                        }
                    }
                    if (x.kind == Graph::Kind::Atomic || y.kind == Graph::Kind::Atomic)
                        return "SHADER-BUFFER-LOWERING-ATOMIC-PROOF";
                    if (x.kind == Graph::Kind::DescriptorArray
                        || y.kind == Graph::Kind::DescriptorArray)
                        return "SHADER-BUFFER-LOWERING-DESCRIPTOR-ARRAY";
                    if ((x.kind == Graph::Kind::Struct && y.kind == Graph::Kind::FixedArray)
                        || (x.kind == Graph::Kind::FixedArray && y.kind == Graph::Kind::Struct))
                        return "SHADER-BUFFER-LOWERING-ARRAY-WRAPPER-PROOF";
                    if (x.alignmentKnown || x.targetSizeKnown || !y.alignmentKnown
                        || !y.targetSizeKnown
                        || (!matrix
                            && (x.matrixEncoding != Graph::MatrixEncoding::None
                                || y.matrixEncoding != Graph::MatrixEncoding::None)))
                        return "SHADER-BUFFER-LOWERING-ENCODING";
                    if (x.kind != y.kind || x.scalar != y.scalar || x.scalarBits != y.scalarBits
                        || x.rows != y.rows || x.columns != y.columns || x.count != y.count
                        || x.strideBytes != y.strideBytes
                        || x.occupiedExtentBytes != y.occupiedExtentBytes
                        || x.memberCount != y.memberCount
                        || (x.child == Graph::absent) != (y.child == Graph::absent))
                        return "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH";
                    for (std::size_t j = 0u; j < x.memberCount; ++j) {
                        const auto& m = source.members()[x.memberBegin + j];
                        const auto& n = target.members()[y.memberBegin + j];
                        if (m.alignmentKnown || m.reservedSizeKnown || !n.alignmentKnown)
                            return "SHADER-BUFFER-LOWERING-ENCODING";
                        if (m.offsetBytes != n.offsetBytes)
                            return "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH";
                    }
                    matches = true;
                    return nullptr;
                },
                equal,
                compared,
                resolve);
            if (diagnostic)
                return diagnostic;
            if (!equal)
                return "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH";
            for (const auto consumed : used)
                if (!consumed)
                    return "SHADER-BUFFER-LOWERING-PLAN-UNUSED";
            output = {form,
                block.occupiedExtentBytes,
                value.targetSizeBytes,
                compared,
                plan.count,
                plan.matrixCount};
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "SHADER-BUFFER-LOWERING-ALLOCATION";
        }
    }

private:
    static bool membersValid(const Graph& graph, const Graph::Node& node) noexcept
    {
        return node.memberBegin <= graph.members().size()
            && node.memberCount <= graph.members().size() - node.memberBegin;
    }
};
} // namespace arp::shader
