#pragma once
#include "shader/buffer_declaration_type_layout.h"

namespace arp::shader {
// Declaration-only relation over independently collected type subtrees. Physical
// binding, selected-entry custody, access, range and execution are separate joins.
class SpirvMslBufferMemoryLowering final {
public:
    using Graph = BufferDeclarationTypeLayouts;
    enum class Side : std::uint32_t { Source = 1u, Target };
    enum class RootForm : std::uint32_t { Block = 1u, Value, RuntimeElement };

    struct TargetElement {
        std::uint64_t strideBytes = 0u, alignmentBytes = 0u;
    };

    struct ArrayWrapper {
        Side side = Side::Source;
        std::size_t wrapperType = Graph::absent;
    };

    struct MatrixColumns {
        std::size_t sourceType = Graph::absent, targetType = Graph::absent;
    };

    struct Plan {
        std::uint32_t contractVersion = 0u;
        std::size_t maxPairs = 0u, maxWrappers = 0u, maxMatrices = 0u;
        const ArrayWrapper* wrappers = nullptr;
        std::size_t wrapperCount = 0u;
        const MatrixColumns* matrices = nullptr;
        std::size_t matrixCount = 0u;
        RootForm rootForm = RootForm::Block;
    };

    struct Facts {
        std::uint64_t occupiedBytes = 0u;
        std::size_t comparedPairs = 0u, arrayWrappers = 0u, matrixColumns = 0u;
        bool hasRuntimeArray = false;
        std::uint64_t sourceRuntimeStrideBytes = 0u, targetRuntimeStrideBytes = 0u;
    };

    // Framing only: explicit forms choose a source value, never infer target
    // members or manufacture an element from a fixed-count array.
    static const char* sourceValue(const Graph& source,
        std::size_t root,
        const Plan& plan,
        std::size_t& value,
        std::uint64_t& stride) noexcept
    {
        value = Graph::absent;
        stride = 0u;
        if (plan.contractVersion != 1u && plan.contractVersion != 2u)
            return "SHADER-MSL-MEMORY-PLAN-VERSION";
        if ((plan.contractVersion == 1u && plan.rootForm != RootForm::Block)
            || (plan.rootForm != RootForm::Block && plan.rootForm != RootForm::Value
                && plan.rootForm != RootForm::RuntimeElement))
            return "SHADER-MSL-MEMORY-ROOT-FORM";
        if (root >= source.nodes().size())
            return "SHADER-MSL-MEMORY-ROOT";
        if (plan.rootForm == RootForm::Block) {
            value = root;
            return nullptr;
        }
        const auto& block = source.nodes()[root];
        if (block.kind != Graph::Kind::Struct || block.memberCount != 1u
            || block.memberBegin >= source.members().size())
            return "SHADER-MSL-MEMORY-ROOT-FRAMING";
        const auto& member = source.members()[block.memberBegin];
        if (block.alignmentKnown || block.targetSizeKnown || member.alignmentKnown
            || member.reservedSizeKnown)
            return "SHADER-MSL-MEMORY-SOURCE-ENCODING";
        if (member.offsetBytes || member.type >= source.nodes().size())
            return "SHADER-MSL-MEMORY-ROOT-FRAMING";
        const auto& child = source.nodes()[member.type];
        if (plan.rootForm == RootForm::Value) {
            if (!block.occupiedExtentBytes
                || block.occupiedExtentBytes != child.occupiedExtentBytes)
                return "SHADER-MSL-MEMORY-ROOT-FRAMING";
            value = member.type;
            return nullptr;
        }
        if (block.occupiedExtentBytes || child.kind != Graph::Kind::RuntimeArray || child.count
            || child.occupiedExtentBytes || !child.strideBytes
            || child.child >= source.nodes().size()
            || !source.nodes()[child.child].occupiedExtentBytes
            || source.nodes()[child.child].occupiedExtentBytes > child.strideBytes)
            return "SHADER-MSL-MEMORY-RUNTIME-FRAMING";
        if (child.alignmentKnown || child.targetSizeKnown
            || child.matrixEncoding != Graph::MatrixEncoding::None)
            return "SHADER-MSL-MEMORY-SOURCE-ENCODING";
        value = child.child;
        stride = child.strideBytes;
        return nullptr;
    }

    static const char* compare(const Graph& source,
        std::size_t sourceType,
        const Graph& target,
        std::size_t targetType,
        const Plan& plan,
        Facts& output,
        TargetElement element = {0u, 0u})
    {
        output = {};
        const auto sourceRoot = sourceType;
        std::size_t value = Graph::absent;
        std::uint64_t runtimeStride = 0u;
        if (const auto* id = sourceValue(source, sourceType, plan, value, runtimeStride))
            return id;
        sourceType = value;
        if (!plan.maxPairs || plan.wrapperCount > plan.maxWrappers
            || plan.matrixCount > plan.maxMatrices || (plan.wrapperCount && !plan.wrappers)
            || (plan.matrixCount && !plan.matrices))
            return "SHADER-MSL-MEMORY-PLAN-BUDGET";
        if (sourceType >= source.nodes().size() || targetType >= target.nodes().size())
            return "SHADER-MSL-MEMORY-ROOT";
        if (runtimeStride
            && (!element.strideBytes || !element.alignmentBytes
                || (element.alignmentBytes & (element.alignmentBytes - 1u))
                || element.strideBytes % element.alignmentBytes
                || element.strideBytes != runtimeStride
                || !target.nodes()[targetType].occupiedExtentBytes
                || target.nodes()[targetType].occupiedExtentBytes > element.strideBytes))
            return "SHADER-MSL-MEMORY-RUNTIME-STRIDE";
        for (std::size_t i = 0u; i < plan.wrapperCount; ++i) {
            const auto& rule = plan.wrappers[i];
            if (rule.side != Side::Source && rule.side != Side::Target)
                return "SHADER-MSL-MEMORY-WRAPPER-FRAMING";
            const auto& graph = rule.side == Side::Source ? source : target;
            if (rule.wrapperType >= graph.nodes().size())
                return "SHADER-MSL-MEMORY-WRAPPER-FRAMING";
            const auto& wrapper = graph.nodes()[rule.wrapperType];
            if (wrapper.kind != Graph::Kind::Struct || wrapper.memberCount != 1u
                || wrapper.memberBegin >= graph.members().size())
                return "SHADER-MSL-MEMORY-WRAPPER-FRAMING";
            const auto& member = graph.members()[wrapper.memberBegin];
            if (rule.side == Side::Source
                && (wrapper.alignmentKnown || wrapper.targetSizeKnown || member.alignmentKnown
                    || member.reservedSizeKnown))
                return "SHADER-MSL-MEMORY-SOURCE-ENCODING";
            if (member.offsetBytes || member.type >= graph.nodes().size()
                || graph.nodes()[member.type].kind != Graph::Kind::FixedArray
                || wrapper.occupiedExtentBytes != graph.nodes()[member.type].occupiedExtentBytes)
                return "SHADER-MSL-MEMORY-WRAPPER-FRAMING";
            for (std::size_t j = 0u; j < i; ++j)
                if (rule.side == plan.wrappers[j].side
                    && rule.wrapperType == plan.wrappers[j].wrapperType)
                    return "SHADER-MSL-MEMORY-PLAN-DUPLICATE";
        }
        for (std::size_t i = 0u; i < plan.matrixCount; ++i) {
            const auto& rule = plan.matrices[i];
            if (rule.sourceType >= source.nodes().size()
                || rule.targetType >= target.nodes().size())
                return "SHADER-MSL-MEMORY-MATRIX-FRAMING";
            const auto& a = source.nodes()[rule.sourceType];
            const auto& b = target.nodes()[rule.targetType];
            if (a.kind != Graph::Kind::Matrix || b.kind != Graph::Kind::Matrix
                || a.matrixEncoding != Graph::MatrixEncoding::SpirvColMajorDecoration
                || b.matrixEncoding != Graph::MatrixEncoding::MslColumnVectors)
                return "SHADER-MSL-MEMORY-MATRIX-FRAMING";
            for (std::size_t j = 0u; j < i; ++j)
                if (rule.sourceType == plan.matrices[j].sourceType
                    && rule.targetType == plan.matrices[j].targetType)
                    return "SHADER-MSL-MEMORY-PLAN-DUPLICATE";
        }
        try {
            core::pmr::Vector<std::uint8_t> usedWrappers{&core::defaultCpuMemoryResource()};
            core::pmr::Vector<std::uint8_t> usedMatrices{&core::defaultCpuMemoryResource()};
            usedWrappers.resize(plan.wrapperCount, 0u);
            usedMatrices.resize(plan.matrixCount, 0u);
            const auto resolve = [&](std::size_t& left, std::size_t& right) -> const char* {
                for (std::size_t i = 0u; i < plan.wrapperCount; ++i) {
                    const auto& rule = plan.wrappers[i];
                    auto& index = rule.side == Side::Source ? left : right;
                    const auto& graph = rule.side == Side::Source ? source : target;
                    if (index == rule.wrapperType) {
                        index = graph.members()[graph.nodes()[index].memberBegin].type;
                        usedWrappers[i] = 1u;
                    }
                }
                return nullptr;
            };
            bool equal = false;
            std::size_t pairs = 0u;
            const auto* id = Graph::visitPairs(
                source,
                sourceType,
                target,
                targetType,
                plan.maxPairs,
                [&](const Graph::Node& a, const Graph::Node& b, bool& matches) -> const char* {
                    const auto valueKind = [](Graph::Kind kind) noexcept {
                        return kind == Graph::Kind::Scalar || kind == Graph::Kind::Vector
                            || kind == Graph::Kind::Matrix || kind == Graph::Kind::FixedArray
                            || kind == Graph::Kind::Struct;
                    };
                    if (!valueKind(a.kind) || !valueKind(b.kind))
                        return "SHADER-MSL-MEMORY-DOMAIN-UNSUPPORTED";
                    if (a.alignmentKnown || a.targetSizeKnown)
                        return "SHADER-MSL-MEMORY-SOURCE-ENCODING";
                    if (a.kind == Graph::Kind::Matrix || b.kind == Graph::Kind::Matrix) {
                        bool declared = false;
                        for (std::size_t i = 0u; i < plan.matrixCount; ++i)
                            if (&a == &source.nodes()[plan.matrices[i].sourceType]
                                && &b == &target.nodes()[plan.matrices[i].targetType]) {
                                declared = true;
                                usedMatrices[i] = 1u;
                            }
                        if (!declared)
                            return "SHADER-MSL-MEMORY-MATRIX-PROOF";
                    } else if (a.matrixEncoding != Graph::MatrixEncoding::None
                        || b.matrixEncoding != Graph::MatrixEncoding::None)
                        return "SHADER-MSL-MEMORY-ENCODING";
                    matches = a.kind == b.kind && a.scalar == b.scalar
                        && a.scalarBits == b.scalarBits && a.rows == b.rows
                        && a.columns == b.columns && a.count == b.count
                        && a.strideBytes == b.strideBytes
                        && a.occupiedExtentBytes == b.occupiedExtentBytes
                        && a.memberCount == b.memberCount
                        && (a.child == Graph::absent) == (b.child == Graph::absent);
                    for (std::size_t i = 0u; matches && i < a.memberCount; ++i) {
                        const auto& x = source.members()[a.memberBegin + i];
                        const auto& y = target.members()[b.memberBegin + i];
                        if (x.alignmentKnown || x.reservedSizeKnown)
                            return "SHADER-MSL-MEMORY-SOURCE-ENCODING";
                        matches = x.offsetBytes == y.offsetBytes;
                    }
                    return nullptr;
                },
                equal,
                pairs,
                resolve);
            if (id)
                return id;
            if (!equal)
                return "SHADER-MSL-MEMORY-MISMATCH";
            for (const auto used : usedWrappers)
                if (!used)
                    return "SHADER-MSL-MEMORY-PLAN-UNUSED";
            for (const auto used : usedMatrices)
                if (!used)
                    return "SHADER-MSL-MEMORY-PLAN-UNUSED";
            output = {source.nodes()[sourceRoot].occupiedExtentBytes,
                pairs,
                plan.wrapperCount,
                plan.matrixCount,
                bool(runtimeStride),
                runtimeStride,
                runtimeStride ? element.strideBytes : 0u};
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "SHADER-MSL-MEMORY-ALLOCATION";
        }
    }
};
} // namespace arp::shader
