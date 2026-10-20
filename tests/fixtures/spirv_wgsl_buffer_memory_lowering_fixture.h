#pragma once
#include "buffer_declaration_type_layout_fixture.h"
#include "shader/spirv_wgsl_buffer_memory_lowering.h"

#include <array>
#include <cstring>

namespace arp::test {
class SpirvWgslBufferMemoryLoweringFixture final {
    using Graph = shader::BufferDeclarationTypeLayouts;
    using Lowering = shader::SpirvWgslBufferMemoryLowering;
    using Form = Lowering::RootForm;

    // Validator fixture only. Native representation proof uses real() below.
    static Graph binaryShape(const Graph& target,
        Form form,
        bool descriptorArray = false,
        core::pmr::Vector<Lowering::ArrayValueWrapper>* wrappers = nullptr)
    {
        Graph source;
        source.begin({2u * target.nodes().size() + 2u,
            target.members().size() + target.nodes().size() + 1u,
            1u});
        core::pmr::Vector<std::size_t> remap{&core::defaultCpuMemoryResource()};
        if (wrappers)
            wrappers->clear();
        for (std::size_t type = 0u; type < target.nodes().size(); ++type) {
            auto node = target.nodes()[type];
            core::pmr::Vector<Graph::Member> members{&core::defaultCpuMemoryResource()};
            for (std::size_t i = 0u; i < node.memberCount; ++i) {
                auto m = target.members()[node.memberBegin + i];
                m.type = remap.at(m.type);
                m.alignmentKnown = m.reservedSizeKnown = false;
                m.alignmentBytes = m.reservedSizeBytes = 0u;
                members.push_back(m);
            }
            if (node.child != Graph::absent)
                node.child = remap.at(node.child);
            node.alignmentKnown = node.targetSizeKnown = false;
            node.alignmentBytes = node.targetSizeBytes = 0u;
            if (node.kind == Graph::Kind::Matrix)
                node.matrixEncoding = Graph::MatrixEncoding::SpirvColMajorDecoration;
            std::size_t index;
            assert(!source.append(node, members.data(), members.size(), index));
            if (wrappers && node.kind == Graph::Kind::FixedArray) {
                Graph::Node wrapper;
                wrapper.kind = Graph::Kind::Struct;
                wrapper.occupiedExtentBytes = node.occupiedExtentBytes;
                Graph::Member member;
                member.type = index;
                assert(!source.append(wrapper, &member, 1u, index));
                wrappers->push_back({index, type});
            }
            remap.push_back(index);
        }
        auto r = target.roots().front();
        r.type = remap.at(r.type);
        r.storage = r.storage == Graph::Storage::Uniform ? Graph::Storage::Uniform
                                                         : Graph::Storage::StorageAccessUnspecified;
        if (form != Form::BlockStruct) {
            Graph::Node block;
            block.kind = Graph::Kind::Struct;
            block.occupiedExtentBytes = source.nodes()[r.type].occupiedExtentBytes;
            Graph::Member m;
            m.type = r.type;
            assert(!source.append(block, &m, 1u, r.type));
        }
        if (descriptorArray) {
            Graph::Node array;
            array.kind = Graph::Kind::DescriptorArray;
            array.child = r.type;
            array.count = 2u;
            assert(!source.append(array, nullptr, 0u, r.descriptorType));
            r.descriptorCount = 2u;
        }
        assert(!source.appendRoot(r));
        return source;
    }

    static void check(const Graph& source,
        const Graph& target,
        Form form,
        const char* expected = nullptr,
        std::size_t pairs = 128u,
        Lowering::Plan plan = {nullptr, 0u})
    {
        Lowering::Facts out;
        out.sourceOccupiedBytes = UINT64_MAX;
        out.targetDeclarationBytes = UINT64_MAX;
        out.comparedPairs = SIZE_MAX;
        out.arrayValueWrappers = SIZE_MAX;
        out.matrixColumnValues = SIZE_MAX;
        const auto* id = Lowering::compare(source, 0u, target, 0u, form, pairs, out, plan);
        if (expected) {
            if (!id || std::strcmp(id, expected))
                std::fprintf(
                    stderr, "Lowering expected %s, got %s\n", expected, id ? id : "success");
            assert(id && !std::strcmp(id, expected) && !out.sourceOccupiedBytes
                && !out.targetDeclarationBytes && !out.comparedPairs && !out.arrayValueWrappers
                && !out.matrixColumnValues);
        } else {
            if (id)
                std::fprintf(stderr, "Lowering unexpected %s\n", id);
            assert(!id && out.comparedPairs
                && out.targetDeclarationBytes
                    == target.nodes()[target.roots()[0].type].targetSizeBytes
                && out.arrayValueWrappers == plan.count
                && out.matrixColumnValues == plan.matrixCount);
        }
    }

public:
    static void verify()
    {
        const auto wgsl = [](std::string_view s) {
            return BufferDeclarationTypeLayoutFixture::wgsl(s);
        };
        auto scalar = wgsl("@group(0) @binding(0) var<uniform> v:u32;");
        auto sourceScalar = binaryShape(scalar, Form::UniformValue);
        check(sourceScalar, scalar, Form::UniformValue);
        auto signedScalar = wgsl("@group(0) @binding(0) var<uniform> v:i32;");
        check(sourceScalar,
            signedScalar,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH");
        auto first = wgsl("struct S { a:u32, b:f32 }; @group(0) @binding(0) var<storage> v:S;");
        auto source = binaryShape(first, Form::BlockStruct);
        auto swapped = wgsl("struct T { a:f32, b:u32 }; @group(0) @binding(0) var<storage> v:T;");
        auto renamed =
            wgsl("alias U=u32; struct T { x:U, y:f32 }; @group(0) @binding(0) var<storage> v:T;");
        check(source, first, Form::BlockStruct);
        check(source, renamed, Form::BlockStruct);
        check(source, swapped, Form::BlockStruct, "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH");
        auto nested = wgsl(
            "struct T { a:u32,b:f32 }; struct S { v:T }; @group(0) @binding(0) var<storage> v:S;");
        check(source, nested, Form::BlockStruct, "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH");
        auto reserved =
            wgsl("struct S { @size(16) a:u32 }; @group(0) @binding(0) var<storage> v:S;");
        auto sourceReserved = binaryShape(reserved, Form::BlockStruct);
        check(sourceReserved, reserved, Form::BlockStruct);
        Lowering::Facts facts;
        assert(
            !Lowering::compare(sourceReserved, 0u, reserved, 0u, Form::BlockStruct, 128u, facts));
        assert(facts.sourceOccupiedBytes == 4u && facts.targetDeclarationBytes == 16u);
        auto vector = wgsl("struct S { a:vec3f,b:u32 }; @group(0) @binding(0) var<storage> v:S;");
        auto sourceVector = binaryShape(vector, Form::BlockStruct);
        check(sourceVector, vector, Form::BlockStruct);
        auto offset =
            wgsl("struct S { a:vec3f,@align(16) b:u32 }; @group(0) @binding(0) var<storage> v:S;");
        check(sourceVector, offset, Form::BlockStruct, "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH");
        auto runtime = wgsl("@group(0) @binding(0) var<storage> v:array<vec3f>;");
        auto sourceRuntime = binaryShape(runtime, Form::StorageRuntimeArray);
        check(sourceRuntime, runtime, Form::StorageRuntimeArray);
        auto wrongRuntime =
            wgsl("struct S { @size(32) v:vec3f }; @group(0) @binding(0) var<storage> v:array<S>;");
        check(sourceRuntime,
            wrongRuntime,
            Form::StorageRuntimeArray,
            "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH");
        auto fixed = wgsl("@group(0) @binding(0) var<uniform> v:array<vec4u,6>;");
        auto sourceFixed = binaryShape(fixed, Form::UniformValue);
        auto dimensions = wgsl("@group(0) @binding(0) var<uniform> v:array<array<vec4u,3>,2>;");
        check(sourceFixed, fixed, Form::UniformValue);
        check(
            sourceFixed, dimensions, Form::UniformValue, "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH");
        core::pmr::Vector<Lowering::ArrayValueWrapper> wrappers{&core::defaultCpuMemoryResource()};
        auto wrapped = binaryShape(dimensions, Form::UniformValue, false, &wrappers);
        const Lowering::Plan plan{wrappers.data(), wrappers.size()};
        check(wrapped, dimensions, Form::UniformValue, nullptr, 128u, plan);
        check(
            wrapped, dimensions, Form::UniformValue, "SHADER-BUFFER-LOWERING-ARRAY-WRAPPER-PROOF");
        check(wrapped,
            dimensions,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-PLAN-BUDGET",
            128u,
            {nullptr, 1u});
        check(wrapped,
            dimensions,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-PLAN-BUDGET",
            1u,
            plan);
        const Lowering::ArrayValueWrapper invalid{Graph::absent, 0u};
        check(wrapped,
            dimensions,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-PLAN-FRAMING",
            128u,
            {&invalid, 1u});
        const Lowering::ArrayValueWrapper scalarRule{0u, 0u};
        check(wrapped,
            dimensions,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-PLAN-FRAMING",
            128u,
            {&scalarRule, 1u});
        const std::array duplicate{wrappers.front(), wrappers.front()};
        check(wrapped,
            dimensions,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-PLAN-DUPLICATE",
            128u,
            {duplicate.data(), duplicate.size()});
        // A valid rule for an unrelated graph branch must be consumed, never
        // merely listed as lowering telemetry.
        check(wrapped,
            dimensions,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-ARRAY-WRAPPER-PROOF",
            128u,
            {wrappers.data(), 1u});
        auto matrix = wgsl("@group(0) @binding(0) var<uniform> v:mat4x4f;");
        auto sourceMatrix = binaryShape(matrix, Form::UniformValue);
        check(sourceMatrix, matrix, Form::UniformValue, "SHADER-BUFFER-LOWERING-MATRIX-PROOF");
        verifyMatrixColumns();
        auto integer = wgsl("@group(0) @binding(0) var<storage,read_write> v:array<u32>;");
        auto sourceInteger = binaryShape(integer, Form::StorageRuntimeArray);
        auto atomic = wgsl("@group(0) @binding(0) var<storage,read_write> v:array<atomic<u32>>;");
        check(sourceInteger,
            atomic,
            Form::StorageRuntimeArray,
            "SHADER-BUFFER-LOWERING-ATOMIC-PROOF");
        check(sourceScalar, scalar, Form::BlockStruct, "SHADER-BUFFER-LOWERING-ROOT-FORM");
        check(sourceScalar, scalar, static_cast<Form>(99u), "SHADER-BUFFER-LOWERING-ROOT-FORM");
        check(sourceScalar, scalar, Form::UniformValue, "SHADER-BUFFER-LOWERING-PAIR-BUDGET", 0u);
        auto descriptor = binaryShape(scalar, Form::UniformValue, true);
        check(descriptor, scalar, Form::UniformValue, "SHADER-BUFFER-LOWERING-DESCRIPTOR-ARRAY");
        facts = {Form::UniformValue, UINT64_MAX, UINT64_MAX, SIZE_MAX};
        assert(Lowering::compare(
                   sourceScalar, Graph::absent, scalar, 0u, Form::UniformValue, 128u, facts)
            && !facts.comparedPairs && !facts.targetDeclarationBytes && !facts.sourceOccupiedBytes);
        check(source, first, Form::BlockStruct, "SHADER-BUFFER-TYPE-PAIR-BUDGET", 1u);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        check(source, first, Form::BlockStruct);
        std::pmr::set_default_resource(previous);
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            check(source, first, Form::BlockStruct, "SHADER-BUFFER-TYPE-ALLOCATION");
        }
        assert(!quota->liveBytes());
        core::pmr::String deep{&core::defaultCpuMemoryResource()};
        deep = "struct S { v:";
        for (unsigned i = 0u; i < 1500u; ++i)
            deep += "array<";
        deep += "u32";
        for (unsigned i = 0u; i < 1500u; ++i)
            deep += ",1>";
        deep += " }; @group(0) @binding(0) var<storage> v:S;";
        auto targetDeep = BufferDeclarationTypeLayoutFixture::wgsl(deep, {1502u, 1u, 1u});
        auto sourceDeep = binaryShape(targetDeep, Form::BlockStruct);
        check(sourceDeep, targetDeep, Form::BlockStruct, nullptr, 1502u);
        check(sourceDeep, targetDeep, Form::BlockStruct, "SHADER-BUFFER-TYPE-PAIR-BUDGET", 1501u);
        auto wrappedDeep = binaryShape(targetDeep, Form::BlockStruct, false, &wrappers);
        check(wrappedDeep,
            targetDeep,
            Form::BlockStruct,
            nullptr,
            1502u,
            {wrappers.data(), wrappers.size()});
        check(wrappedDeep,
            targetDeep,
            Form::BlockStruct,
            "SHADER-BUFFER-TYPE-PAIR-BUDGET",
            1501u,
            {wrappers.data(), wrappers.size()});
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            check(wrappedDeep,
                targetDeep,
                Form::BlockStruct,
                "SHADER-BUFFER-LOWERING-ALLOCATION",
                1502u,
                {wrappers.data(), wrappers.size()});
        }
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_BUFFER_MEMORY_LOWERING_OK full-type-offset-rank-stride explicit-consumed-array-wrappers deep1500 matrix-v1-closed matrix-columns-v2 atomic-closed bounded-core-pmr no-original-native-authority");
    }

    static void verifyMatrixColumns()
    {
        const auto ruleFor = [](const Graph& source, const Graph& target) {
            Lowering::MatrixColumnValue rule;
            for (std::size_t i = 0u; i < source.nodes().size(); ++i)
                if (source.nodes()[i].kind == Graph::Kind::Matrix) {
                    assert(rule.sourceType == Graph::absent);
                    rule.sourceType = i;
                }
            for (std::size_t i = 0u; i < target.nodes().size(); ++i)
                if (target.nodes()[i].kind == Graph::Kind::Matrix) {
                    assert(rule.targetType == Graph::absent);
                    rule.targetType = i;
                }
            assert(rule.sourceType != Graph::absent && rule.targetType != Graph::absent);
            return rule;
        };
        // Non-square shapes catch transposed dimensions. Explicit pair rules
        // prove column memory only; no matrix operation or length is granted.
        for (unsigned columns = 2u; columns <= 4u; ++columns)
            for (unsigned rows = 2u; rows <= 4u; ++rows) {
                core::pmr::String text = "@group(0) @binding(0) var<uniform> v:mat";
                text += char('0' + columns);
                text += 'x';
                text += char('0' + rows);
                text += "f;";
                auto target = BufferDeclarationTypeLayoutFixture::wgsl(text);
                auto source = binaryShape(target, Form::UniformValue);
                const auto rule = ruleFor(source, target);
                check(source, target, Form::UniformValue, "SHADER-BUFFER-LOWERING-MATRIX-PROOF");
                check(source,
                    target,
                    Form::UniformValue,
                    nullptr,
                    128u,
                    {nullptr, 0u, &rule, 1u, 2u});
                check(source,
                    target,
                    Form::UniformValue,
                    "SHADER-BUFFER-LOWERING-MATRIX-PROOF",
                    128u,
                    {nullptr, 0u, &rule, 1u, 1u});
            }
        auto palette = BufferDeclarationTypeLayoutFixture::wgsl(
            "struct Palette { matrices:array<mat4x4f,64>, count:vec4u };"
            "struct Temporal { current:Palette, previous:Palette };"
            "@group(0) @binding(0) var<uniform> v:Temporal;");
        core::pmr::Vector<Lowering::ArrayValueWrapper> wrappers{&core::defaultCpuMemoryResource()};
        auto source = binaryShape(palette, Form::BlockStruct, false, &wrappers);
        const auto rule = ruleFor(source, palette);
        const Lowering::Plan plan{wrappers.data(), wrappers.size(), &rule, 1u, 2u};
        check(source, palette, Form::BlockStruct, nullptr, 128u, plan);
        check(source,
            palette,
            Form::BlockStruct,
            "SHADER-BUFFER-LOWERING-MATRIX-PROOF",
            128u,
            {wrappers.data(), wrappers.size()});
        check(source,
            palette,
            Form::BlockStruct,
            "SHADER-BUFFER-LOWERING-ARRAY-WRAPPER-PROOF",
            128u,
            {nullptr, 0u, &rule, 1u, 2u});
        check(source,
            palette,
            Form::BlockStruct,
            "SHADER-BUFFER-LOWERING-PLAN-VERSION",
            128u,
            {nullptr, 0u, nullptr, 0u, 3u});
        check(source,
            palette,
            Form::BlockStruct,
            "SHADER-BUFFER-LOWERING-PLAN-BUDGET",
            128u,
            {nullptr, 0u, nullptr, 1u, 2u});
        check(source, palette, Form::BlockStruct, "SHADER-BUFFER-LOWERING-PLAN-BUDGET", 1u, plan);
        const Lowering::MatrixColumnValue missing{Graph::absent, rule.targetType}, scalar{0u, 0u};
        check(source,
            palette,
            Form::BlockStruct,
            "SHADER-BUFFER-LOWERING-MATRIX-PLAN-FRAMING",
            128u,
            {nullptr, 0u, &missing, 1u, 2u});
        check(source,
            palette,
            Form::BlockStruct,
            "SHADER-BUFFER-LOWERING-MATRIX-PLAN-FRAMING",
            128u,
            {nullptr, 0u, &scalar, 1u, 2u});
        const std::array duplicate{rule, rule};
        check(source,
            palette,
            Form::BlockStruct,
            "SHADER-BUFFER-LOWERING-PLAN-DUPLICATE",
            128u,
            {nullptr, 0u, duplicate.data(), duplicate.size(), 2u});
        // Slang's real WGSL matrix-storage declaration is a struct containing
        // a fixed column array. The source matrix and target remain intact;
        // this separate explicit rule compares their exact physical bytes.
        auto logical = BufferDeclarationTypeLayoutFixture::wgsl(
            "@group(0) @binding(0) var<uniform> v:mat4x3f;");
        auto logicalSource = binaryShape(logical, Form::UniformValue);
        auto stored = BufferDeclarationTypeLayoutFixture::wgsl(
            "struct M { data:array<vec3f,4> }; @group(0) @binding(0) var<uniform> v:M;");
        const auto matrixType = ruleFor(logicalSource, logical).sourceType;
        const Lowering::MatrixColumnValue storedRule{
            matrixType, stored.roots()[0].type, Lowering::MatrixTargetForm::ColumnArrayStruct};
        check(logicalSource, stored, Form::UniformValue, "SHADER-BUFFER-LOWERING-MATRIX-PROOF");
        check(logicalSource,
            stored,
            Form::UniformValue,
            nullptr,
            128u,
            {nullptr, 0u, &storedRule, 1u, 2u});
        auto transposed = BufferDeclarationTypeLayoutFixture::wgsl(
            "struct M { data:array<vec4f,3> }; @group(0) @binding(0) var<uniform> v:M;");
        const Lowering::MatrixColumnValue transposedRule{
            matrixType, transposed.roots()[0].type, Lowering::MatrixTargetForm::ColumnArrayStruct};
        check(logicalSource,
            transposed,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH",
            128u,
            {nullptr, 0u, &transposedRule, 1u, 2u});
        auto signedColumns = BufferDeclarationTypeLayoutFixture::wgsl(
            "struct M { data:array<vec3i,4> }; @group(0) @binding(0) var<uniform> v:M;");
        const Lowering::MatrixColumnValue signedRule{matrixType,
            signedColumns.roots()[0].type,
            Lowering::MatrixTargetForm::ColumnArrayStruct};
        check(logicalSource,
            signedColumns,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH",
            128u,
            {nullptr, 0u, &signedRule, 1u, 2u});
        auto padded = BufferDeclarationTypeLayoutFixture::wgsl(
            "struct M { @size(80) data:array<vec3f,4> }; @group(0) @binding(0) var<uniform> v:M;");
        const Lowering::MatrixColumnValue paddedRule{
            matrixType, padded.roots()[0].type, Lowering::MatrixTargetForm::ColumnArrayStruct};
        check(logicalSource,
            padded,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-MATRIX-PLAN-FRAMING",
            128u,
            {nullptr, 0u, &paddedRule, 1u, 2u});
        auto unknownForm = storedRule;
        unknownForm.targetForm = static_cast<Lowering::MatrixTargetForm>(99u);
        check(logicalSource,
            stored,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-MATRIX-PLAN-FRAMING",
            128u,
            {nullptr, 0u, &unknownForm, 1u, 2u});
        auto unrelated = BufferDeclarationTypeLayoutFixture::wgsl(
            "@group(0) @binding(0) var<uniform> scalar:u32; @group(0) @binding(1) var<uniform> matrix:mat4x4f;");
        auto unrelatedSource = binaryShape(unrelated, Form::UniformValue);
        const auto unusedRule = ruleFor(unrelatedSource, unrelated);
        check(unrelatedSource,
            unrelated,
            Form::UniformValue,
            "SHADER-BUFFER-LOWERING-PLAN-UNUSED",
            128u,
            {nullptr, 0u, &unusedRule, 1u, 2u});
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            check(source,
                palette,
                Form::BlockStruct,
                "SHADER-BUFFER-LOWERING-ALLOCATION",
                128u,
                plan);
        }
        assert(!quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        check(source, palette, Form::BlockStruct, nullptr, 128u, plan);
        std::pmr::set_default_resource(previous);
    }

    static void real(const char* wgslPath, const char* binaryPath)
    {
        const auto file = core::readBinaryFile(wgslPath, std::filesystem::file_size(wgslPath));
        assert(file.success);
        const auto bytes = core::readBinaryFile(binaryPath, std::filesystem::file_size(binaryPath));
        assert(bytes.success);
        auto target = BufferDeclarationTypeLayoutFixture::wgsl(
            {reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()});
        auto source = BufferDeclarationTypeLayoutFixture::binary(bytes.bytes);
        assert(source.roots().size() == 3u && target.roots().size() == 3u);
        for (std::size_t i = 0u; i < source.roots().size(); ++i) {
            const auto& r = source.roots()[i];
            std::size_t t = Graph::absent;
            for (std::size_t j = 0u; j < target.roots().size(); ++j)
                if (r.set == target.roots()[j].set && r.binding == target.roots()[j].binding)
                    t = j;
            assert(t != Graph::absent);
            Lowering::Facts out;
            const auto form = r.binding == 0u ? Form::UniformValue : Form::StorageRuntimeArray;
            const auto* id = Lowering::compare(source, i, target, t, form, 128u, out);
            if (r.binding == 1u) {
                // Slang's SPIR-V fixed-array struct wrapper is retained. No
                // recursive/name-based unwrap may invent its lowering proof.
                assert(id && !std::strcmp(id, "SHADER-BUFFER-LOWERING-ARRAY-WRAPPER-PROOF")
                    && !out.comparedPairs && !out.targetDeclarationBytes);
                const auto& block = source.nodes()[r.type];
                const auto& runtime = source.nodes()[source.members()[block.memberBegin].type];
                const auto& element = source.nodes()[runtime.child];
                const auto& targetArray = target.nodes()[target.roots()[t].type];
                const auto& targetElement = target.nodes()[targetArray.child];
                const Lowering::ArrayValueWrapper rule{
                    source.members()[element.memberBegin + 2u].type,
                    target.members()[targetElement.memberBegin + 2u].type};
                assert(!Lowering::compare(source, i, target, t, form, 128u, out, {&rule, 1u})
                    && out.targetDeclarationBytes == 48u && out.arrayValueWrappers == 1u);
                SpirvBinaryMutationFixture offsetMutation(bytes.bytes);
                const auto arrayType = offsetMutation.operand(offsetMutation.locate(28u), 0u);
                const auto wrapperType =
                    offsetMutation.operand(offsetMutation.locate(30u, 1, arrayType), 0u);
                offsetMutation.patch(72u, 3u, 4u, 0, wrapperType);
                auto offsetChanged =
                    BufferDeclarationTypeLayoutFixture::binary(offsetMutation.bytes());
                const auto* offsetError =
                    Lowering::compare(offsetChanged, i, target, t, form, 128u, out, {&rule, 1u});
                assert(offsetError
                    && !std::strcmp(offsetError, "SHADER-BUFFER-LOWERING-PLAN-FRAMING")
                    && !out.targetDeclarationBytes && !out.arrayValueWrappers);
                // The same valid rule is not applicable to the scalar root.
                std::size_t scalarRoot = 0u, scalarTarget = 0u;
                for (std::size_t j = 0u; j < source.roots().size(); ++j)
                    if (source.roots()[j].binding == 0u)
                        scalarRoot = j;
                for (std::size_t j = 0u; j < target.roots().size(); ++j)
                    if (target.roots()[j].binding == 0u)
                        scalarTarget = j;
                const auto* unused = Lowering::compare(source,
                    scalarRoot,
                    target,
                    scalarTarget,
                    Form::UniformValue,
                    128u,
                    out,
                    {&rule, 1u});
                assert(unused && !std::strcmp(unused, "SHADER-BUFFER-LOWERING-PLAN-UNUSED")
                    && !out.targetDeclarationBytes && !out.arrayValueWrappers);
            } else
                assert(!id && out.targetDeclarationBytes == 4u);
        }
        SpirvBinaryMutationFixture mutation(bytes.bytes);
        mutation.patch(21u, 2u, 1u, 2, 0u);
        auto changed = BufferDeclarationTypeLayoutFixture::binary(mutation.bytes());
        for (std::size_t i = 0u; i < changed.roots().size(); ++i) {
            const auto& r = changed.roots()[i];
            std::size_t t = Graph::absent;
            for (std::size_t j = 0u; j < target.roots().size(); ++j)
                if (r.set == target.roots()[j].set && r.binding == target.roots()[j].binding)
                    t = j;
            Lowering::Facts out;
            const auto* id = Lowering::compare(changed,
                i,
                target,
                t,
                r.binding == 0u ? Form::UniformValue : Form::StorageRuntimeArray,
                128u,
                out);
            assert(id && !std::strcmp(id, "SHADER-BUFFER-LOWERING-MEMORY-MISMATCH")
                && !out.comparedPairs && !out.targetDeclarationBytes);
        }
        std::puts(
            "BINDLESS_RHI_BUFFER_MEMORY_LOWERING_REAL_OK actual-slang-three-matched exact-array-wrapper-consumed unused-rule-rejected signedness-mutation-rejected no-original-length-authority");
    }
};
} // namespace arp::test
