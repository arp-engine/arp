#pragma once
#include "shader/native_buffer_memory_declarations.h"
#include "shader/spirv_msl_buffer_memory_lowering.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {
class SpirvMslBufferMemoryLoweringFixture final {
public:
    using Lowering = shader::SpirvMslBufferMemoryLowering;
    using Graph = Lowering::Graph;

    struct Shape {
        Graph graph;
        std::size_t matrix = Graph::absent, wrapper = Graph::absent, root = Graph::absent;
    };

    // Independently authored validator inputs, never driver/layout evidence.
    static Shape shape(bool native,
        std::uint64_t count = 256u,
        std::uint64_t stride = 64u,
        std::uint64_t lastOffset = 16428u,
        Graph::Scalar scalar = Graph::Scalar::FloatingPoint,
        Graph::Kind arrayKind = Graph::Kind::FixedArray,
        bool targetWrapper = false)
    {
        Shape out;
        auto& graph = out.graph;
        graph.begin({8u, 8u, 1u});
        const auto append =
            [&](Graph::Node node, const Graph::Member* members = nullptr, std::size_t size = 0u) {
                std::size_t result;
                assert(!graph.append(node, members, size, result));
                return result;
            };
        Graph::Node s;
        s.scalar = scalar;
        s.scalarBits = 32u;
        s.occupiedExtentBytes = 4u;
        const auto floating = append(s);
        s.scalar = Graph::Scalar::UnsignedInteger;
        const auto integer = append(s);
        Graph::Node vector;
        vector.kind = Graph::Kind::Vector;
        vector.child = floating;
        vector.count = 4u;
        vector.occupiedExtentBytes = 16u;
        const auto v = append(vector);
        Graph::Node matrix;
        matrix.kind = Graph::Kind::Matrix;
        matrix.child = v;
        matrix.rows = matrix.columns = 4u;
        matrix.strideBytes = 16u;
        matrix.occupiedExtentBytes = 64u;
        matrix.matrixEncoding = native ? Graph::MatrixEncoding::MslColumnVectors
                                       : Graph::MatrixEncoding::SpirvColMajorDecoration;
        out.matrix = append(matrix);
        Graph::Node array;
        array.kind = arrayKind;
        array.child = out.matrix;
        array.count = count;
        array.strideBytes = stride;
        array.occupiedExtentBytes = (count - 1u) * stride + 64u;
        auto palette = append(array);
        if ((!native || targetWrapper) && arrayKind == Graph::Kind::FixedArray) {
            Graph::Node wrapper;
            wrapper.kind = Graph::Kind::Struct;
            wrapper.occupiedExtentBytes = array.occupiedExtentBytes;
            const Graph::Member member{palette};
            out.wrapper = palette = append(wrapper, &member, 1u);
        }
        Graph::Node block;
        block.kind = Graph::Kind::Struct;
        block.occupiedExtentBytes = 16432u;
        const std::array members{Graph::Member{palette, 0u},
            Graph::Member{v, 16384u},
            Graph::Member{v, 16400u},
            Graph::Member{integer, 16416u},
            Graph::Member{integer, 16420u},
            Graph::Member{integer, 16424u},
            Graph::Member{integer, lastOffset}};
        out.root = append(block, members.data(), members.size());
        assert(!graph.appendRoot({0u, 0u, Graph::Storage::Uniform, out.root, 1u}));
        return out;
    }

    static void verify()
    {
        verifyElements();
        verifyTargetPaths();
        auto source = shape(false), target = shape(true);
        const Lowering::ArrayWrapper wrapper{Lowering::Side::Source, source.wrapper};
        const Lowering::MatrixColumns matrix{source.matrix, target.matrix};
        const Lowering::Plan plan{1u, 7u, 1u, 1u, &wrapper, 1u, &matrix, 1u};
        const auto check = [&](const Shape& a,
                               const Shape& b,
                               const Lowering::Plan& request,
                               const char* expected = nullptr) {
            Lowering::Facts facts{UINT64_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
            const auto* id = Lowering::compare(a.graph, a.root, b.graph, b.root, request, facts);
            if (expected) {
                if (!id || std::strcmp(id, expected))
                    std::fprintf(
                        stderr, "MSL memory expected %s got %s\n", expected, id ? id : "success");
                assert(id && !std::strcmp(id, expected) && !facts.occupiedBytes
                    && !facts.comparedPairs && !facts.arrayWrappers && !facts.matrixColumns);
            } else {
                if (id)
                    std::fprintf(stderr, "MSL memory unexpected %s\n", id);
                assert(!id && facts.occupiedBytes == 16432u && facts.comparedPairs
                    && facts.arrayWrappers == request.wrapperCount
                    && facts.matrixColumns == request.matrixCount);
            }
        };
        check(source, target, plan);
        auto wrappedTarget = shape(
            true, 256u, 64u, 16428u, Graph::Scalar::FloatingPoint, Graph::Kind::FixedArray, true);
        const std::array bothWrappers{
            wrapper, Lowering::ArrayWrapper{Lowering::Side::Target, wrappedTarget.wrapper}};
        auto bothPlan = plan;
        bothPlan.maxWrappers = bothPlan.wrapperCount = bothWrappers.size();
        bothPlan.wrappers = bothWrappers.data();
        check(source, wrappedTarget, bothPlan);
        auto changed = plan;
        changed.contractVersion = 0u;
        check(source, target, changed, "SHADER-MSL-MEMORY-PLAN-VERSION");
        changed = plan;
        changed.maxPairs = 0u;
        check(source, target, changed, "SHADER-MSL-MEMORY-PLAN-BUDGET");
        changed = plan;
        changed.maxPairs = 1u;
        check(source, target, changed, "SHADER-BUFFER-TYPE-PAIR-BUDGET");
        changed = plan;
        changed.maxMatrices = 0u;
        check(source, target, changed, "SHADER-MSL-MEMORY-PLAN-BUDGET");
        changed = plan;
        changed.maxWrappers = 0u;
        check(source, target, changed, "SHADER-MSL-MEMORY-PLAN-BUDGET");
        changed = plan;
        changed.matrices = nullptr;
        check(source, target, changed, "SHADER-MSL-MEMORY-PLAN-BUDGET");
        changed = plan;
        changed.matrixCount = 0u;
        check(source, target, changed, "SHADER-MSL-MEMORY-MATRIX-PROOF");
        changed = plan;
        changed.wrapperCount = 0u;
        check(source, target, changed, "SHADER-MSL-MEMORY-MISMATCH");
        const std::array duplicates{matrix, matrix};
        changed = plan;
        changed.maxMatrices = changed.matrixCount = 2u;
        changed.matrices = duplicates.data();
        check(source, target, changed, "SHADER-MSL-MEMORY-PLAN-DUPLICATE");
        const std::array duplicateWrappers{wrapper, wrapper};
        changed = plan;
        changed.maxWrappers = changed.wrapperCount = duplicateWrappers.size();
        changed.wrappers = duplicateWrappers.data();
        check(source, target, changed, "SHADER-MSL-MEMORY-PLAN-DUPLICATE");
        const Lowering::MatrixColumns wrongMatrix{source.root, target.matrix};
        changed = plan;
        changed.matrices = &wrongMatrix;
        check(source, target, changed, "SHADER-MSL-MEMORY-MATRIX-FRAMING");
        const Lowering::ArrayWrapper wrongWrapper{Lowering::Side::Source, source.root};
        changed = plan;
        changed.wrappers = &wrongWrapper;
        check(source, target, changed, "SHADER-MSL-MEMORY-WRAPPER-FRAMING");
        auto offset = shape(true, 256u, 64u, 16424u);
        check(source, offset, plan, "SHADER-MSL-MEMORY-MISMATCH");
        auto stride = shape(true, 256u, 80u);
        check(source, stride, plan, "SHADER-MSL-MEMORY-MISMATCH");
        auto count = shape(true, 255u);
        check(source, count, plan, "SHADER-MSL-MEMORY-MISMATCH");
        auto signedScalar = shape(true, 256u, 64u, 16428u, Graph::Scalar::SignedInteger);
        check(source, signedScalar, plan, "SHADER-MSL-MEMORY-MISMATCH");
        auto runtime =
            shape(true, 256u, 64u, 16428u, Graph::Scalar::FloatingPoint, Graph::Kind::RuntimeArray);
        check(source, runtime, plan, "SHADER-MSL-MEMORY-DOMAIN-UNSUPPORTED");
        for (const auto kind :
            {Graph::Kind::Atomic, Graph::Kind::DescriptorArray, static_cast<Graph::Kind>(99u)}) {
            auto unsupported = shape(true, 256u, 64u, 16428u, Graph::Scalar::FloatingPoint, kind);
            check(source, unsupported, plan, "SHADER-MSL-MEMORY-DOMAIN-UNSUPPORTED");
        }
        // A source-shaped target cannot impersonate independently collected MSL.
        check(source, source, plan, "SHADER-MSL-MEMORY-MATRIX-FRAMING");
        std::size_t unusedMatrix;
        assert(
            !source.graph.append(source.graph.nodes()[source.matrix], nullptr, 0u, unusedMatrix));
        const std::array unusedRules{matrix, Lowering::MatrixColumns{unusedMatrix, target.matrix}};
        changed = plan;
        changed.maxMatrices = changed.matrixCount = unusedRules.size();
        changed.matrices = unusedRules.data();
        check(source, target, changed, "SHADER-MSL-MEMORY-PLAN-UNUSED");
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        check(source, target, plan);
        std::pmr::set_default_resource(previous);
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            check(source, target, plan, "SHADER-MSL-MEMORY-ALLOCATION");
        }
        assert(!quota->liveBytes());
        std::puts(
            "SPIRV_MSL_MEMORY1_CONTRACT_OK indexed256-shape offsets stride scalar matrix explicit-rules bounded-core-pmr no-native-authority");
    }

    static void verifyTargetPaths()
    {
        using Native = shader::NativeBufferMemoryDeclarations;
        using Request = shader::NativeBufferMemoryLoadRequest;
        using Path = shader::BufferDeclarationTypePath;
        auto source = shape(false), target = shape(true);
        std::array<Path::Step, 2u> steps{
            {{Path::Kind::Member, 0u}, {Path::Kind::ArrayElement, 0u}}};
        const Request::MatrixTargetPath path{source.matrix, {steps.data(), steps.size()}};
        const Lowering::ArrayWrapper wrapper{Lowering::Side::Source, source.wrapper};
        const Request::Binding binding{
            0u, 0u, {8u, 8u, 1u}, {2u, 32u, 1u, 1u, &wrapper, 1u, nullptr, 0u}, &path, 1u, 2u};
        Request request{3u, source.graph.budget(), &binding, 1u};
        assert(!Native::validateRequest(request, 1u));
        request.contractVersion = 2u;
        assert(Native::validateRequest(request, 1u));
        request.contractVersion = 3u;
        Native declarations;
        assert(!declarations.build(std::move(source.graph), request, 1u));
        const auto* frozen = declarations.lookup(0u, 0u);
        assert(frozen && declarations.version() == 3u);
        steps[0u].member = 1u; // Caller mutation cannot change frozen paths.
        core::pmr::Vector<Lowering::MatrixColumns> matrices;
        Lowering::Plan resolved;
        assert(!frozen->resolveTargetPlan(target.graph, target.root, matrices, resolved));
        assert(resolved.matrixCount == 1u && matrices[0u].targetType == target.matrix);
        Lowering::Facts facts;
        assert(!Lowering::compare(declarations.sourceTypes(),
            frozen->sourceType,
            target.graph,
            target.root,
            resolved,
            facts));
        assert(facts.occupiedBytes == 16432u && facts.matrixColumns == 1u);
        assert(frozen->resolveTargetPlan(target.graph, target.matrix, matrices, resolved));
        assert(matrices.empty() && !resolved.contractVersion && !resolved.matrixCount);
        std::size_t type = 0u;
        assert(Path::resolve(target.graph, target.root, {steps.data(), steps.size()}, 1u, type));
        assert(type == Graph::absent);
        steps[0u].member = 0u;
        steps[1u].member = 1u;
        assert(Path::resolve(target.graph, target.root, {steps.data(), steps.size()}, 2u, type));
        assert(type == Graph::absent);
        steps[1u].member = 0u;
        steps[0u].kind = static_cast<Path::Kind>(99u);
        assert(Path::resolve(target.graph, target.root, {steps.data(), steps.size()}, 2u, type));
        assert(type == Graph::absent);
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            core::pmr::Vector<Lowering::MatrixColumns> exhaustedMatrices;
            assert(
                frozen->resolveTargetPlan(target.graph, target.root, exhaustedMatrices, resolved));
            assert(exhaustedMatrices.empty() && !resolved.contractVersion && !resolved.matrixCount);
        }
        assert(!quota->liveBytes());
    }

    // Ordinary source blocks and independent target values, not driver evidence.
    static Shape elementShape(bool source,
        bool runtime,
        bool record,
        Graph::Scalar scalar = Graph::Scalar::FloatingPoint,
        std::uint64_t lastOffset = 48u,
        std::uint64_t prefix = 0u,
        bool nested = false)
    {
        Shape out;
        out.graph.begin({8u, 8u, 1u});
        const auto append =
            [&](Graph::Node node, const Graph::Member* members = nullptr, std::size_t count = 0u) {
                std::size_t index;
                assert(!out.graph.append(node, members, count, index));
                return index;
            };
        Graph::Node node;
        node.scalar = scalar;
        node.scalarBits = 32u;
        node.occupiedExtentBytes = 4u;
        auto value = append(node);
        if (record) {
            node = {};
            node.kind = Graph::Kind::Vector;
            node.child = value;
            node.count = 4u;
            node.occupiedExtentBytes = 16u;
            value = append(node);
            node = {};
            node.kind = Graph::Kind::Struct;
            node.occupiedExtentBytes = 64u;
            const std::array members{Graph::Member{value, 0u},
                Graph::Member{value, 16u},
                Graph::Member{value, 32u},
                Graph::Member{value, lastOffset}};
            value = append(node, members.data(), members.size());
        }
        if (source) {
            if (runtime) {
                node = {};
                node.kind = Graph::Kind::RuntimeArray;
                node.child = value;
                node.strideBytes = record ? 64u : 4u;
                value = append(node);
                if (nested) {
                    node.child = value;
                    value = append(node);
                }
            }
            node = {};
            node.kind = Graph::Kind::Struct;
            node.occupiedExtentBytes = runtime ? prefix : record ? 64u : 4u;
            const Graph::Member member{value, prefix};
            value = append(node, &member, 1u);
        }
        out.root = value;
        assert(!out.graph.appendRoot({2u,
            3u,
            runtime ? Graph::Storage::StorageAccessUnspecified : Graph::Storage::Uniform,
            value,
            1u}));
        return out;
    }

    static void verifyElements()
    {
        const Lowering::Plan plan{
            2u, 8u, 0u, 0u, nullptr, 0u, nullptr, 0u, Lowering::RootForm::RuntimeElement};
        const auto check = [&](const Shape& source,
                               const Shape& target,
                               const Lowering::Plan& intent,
                               Lowering::TargetElement element,
                               const char* expected = nullptr) {
            Lowering::Facts facts{
                UINT64_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, true, UINT64_MAX, UINT64_MAX};
            const auto* id = Lowering::compare(
                source.graph, source.root, target.graph, target.root, intent, facts, element);
            if (expected) {
                if (!id || std::strcmp(id, expected))
                    std::fprintf(
                        stderr, "MSL element expected %s got %s\n", expected, id ? id : "success");
                assert(id && !std::strcmp(id, expected) && !facts.occupiedBytes
                    && !facts.comparedPairs && !facts.arrayWrappers && !facts.matrixColumns
                    && !facts.hasRuntimeArray && !facts.sourceRuntimeStrideBytes
                    && !facts.targetRuntimeStrideBytes);
            } else {
                if (id)
                    std::fprintf(stderr, "MSL element unexpected %s\n", id);
                const bool runtime = intent.rootForm == Lowering::RootForm::RuntimeElement;
                assert(!id && facts.comparedPairs && !facts.arrayWrappers && !facts.matrixColumns
                    && facts.occupiedBytes == source.graph.nodes()[source.root].occupiedExtentBytes
                    && facts.hasRuntimeArray == runtime
                    && facts.sourceRuntimeStrideBytes == (runtime ? element.strideBytes : 0u)
                    && facts.targetRuntimeStrideBytes == (runtime ? element.strideBytes : 0u));
            }
        };
        auto source = elementShape(true, true, true), target = elementShape(false, true, true);
        check(source, target, plan, {64u, 16u});
        auto scalarSource = elementShape(true, true, false, Graph::Scalar::UnsignedInteger);
        auto scalarTarget = elementShape(false, true, false, Graph::Scalar::UnsignedInteger);
        check(scalarSource, scalarTarget, plan, {4u, 4u});
        for (const auto element :
            {Lowering::TargetElement{}, {64u, 0u}, {64u, 3u}, {64u, 128u}, {80u, 16u}, {32u, 16u}})
            check(source, target, plan, element, "SHADER-MSL-MEMORY-RUNTIME-STRIDE");
        auto wrongScalar = elementShape(false, true, true, Graph::Scalar::SignedInteger);
        check(source, wrongScalar, plan, {64u, 16u}, "SHADER-MSL-MEMORY-MISMATCH");
        auto wrongOffset = elementShape(false, true, true, Graph::Scalar::FloatingPoint, 44u);
        check(source, wrongOffset, plan, {64u, 16u}, "SHADER-MSL-MEMORY-MISMATCH");
        auto prefix = elementShape(true, true, true, Graph::Scalar::FloatingPoint, 48u, 16u);
        check(prefix, target, plan, {64u, 16u}, "SHADER-MSL-MEMORY-ROOT-FRAMING");
        auto nested = elementShape(true, true, true, Graph::Scalar::FloatingPoint, 48u, 0u, true);
        check(nested, target, plan, {64u, 16u}, "SHADER-MSL-MEMORY-RUNTIME-FRAMING");
        auto changed = plan;
        changed.contractVersion = 1u;
        check(source, target, changed, {64u, 16u}, "SHADER-MSL-MEMORY-ROOT-FORM");
        changed = plan;
        changed.maxPairs = 1u;
        check(source, target, changed, {64u, 16u}, "SHADER-BUFFER-TYPE-PAIR-BUDGET");
        changed = plan;
        changed.rootForm = Lowering::RootForm::Block;
        check(source, target, changed, {}, "SHADER-MSL-MEMORY-MISMATCH");
        auto valueSource = elementShape(true, false, true);
        changed.rootForm = Lowering::RootForm::Value;
        check(valueSource, target, changed, {});
        check(source, target, changed, {}, "SHADER-MSL-MEMORY-ROOT-FRAMING");
        changed.rootForm = static_cast<Lowering::RootForm>(99u);
        check(source, target, changed, {}, "SHADER-MSL-MEMORY-ROOT-FORM");

        using Request = shader::NativeBufferMemoryLoadRequest;
        Request::Binding binding{2u, 3u, {8u, 8u, 1u}, plan};
        Request request{2u, {8u, 8u, 1u}, &binding, 1u};
        shader::NativeBufferMemoryDeclarations retained;
        auto frozen = elementShape(true, true, true);
        assert(!retained.build(std::move(frozen.graph), request, 1u));
        binding.relation.rootForm = Lowering::RootForm::Value;
        binding.relation.contractVersion = 99u;
        const auto* original = retained.lookup(2u, 3u);
        assert(original && retained.version() == 2u && original->relationVersion == 2u
            && original->rootForm == Lowering::RootForm::RuntimeElement);
        Lowering::Facts facts;
        assert(!Lowering::compare(retained.sourceTypes(),
                   original->sourceType,
                   target.graph,
                   target.root,
                   original->plan(),
                   facts,
                   {64u, 16u})
            && facts.hasRuntimeArray);
        binding.relation = plan;
        request.contractVersion = 1u;
        assert(
            std::string_view(shader::NativeBufferMemoryDeclarations::validateRequest(request, 1u))
            == "SHADER-NATIVE-MEMORY-PLAN");
        request.contractVersion = 2u;
        auto uniform = elementShape(true, false, true);
        assert(retained.build(std::move(uniform.graph), request, 1u) && !retained.version()
            && !retained.lookup(2u, 3u));
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        check(source, target, plan, {64u, 16u});
        std::pmr::set_default_resource(previous);
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            check(source, target, plan, {64u, 16u}, "SHADER-BUFFER-TYPE-ALLOCATION");
        }
        assert(!quota->liveBytes());
        std::puts(
            "SPIRV_MSL_MEMORY2_CONTRACT_OK runtime-record64 scalar4 explicit-value frozen-source stride alignment offsets budgets no-native-authority");
    }
};
} // namespace arp::test
