#include "shader/target_buffer_memory_declarations.h"

#include "shader/shader_artifact_files.h"

#include <cstring>

namespace arp::shader {
namespace {
bool same(const char* lhs, const char* rhs) noexcept
{
    return lhs && !std::strcmp(lhs, rhs);
}

bool independentProofDiagnostic(const char* id) noexcept
{
    return same(id, "SHADER-BUFFER-LOWERING-MATRIX-PROOF")
        || same(id, "SHADER-BUFFER-LOWERING-ATOMIC-PROOF")
        || same(id, "SHADER-BUFFER-LOWERING-DESCRIPTOR-ARRAY");
}
} // namespace

bool TargetBufferMemoryDeclarations::targetFailureIsBudget(const char* id) noexcept
{
    return same(id, "SHADER-WGSL-LAYOUT-ALLOCATION")
        || same(id, "SHADER-WGSL-LAYOUT-BINDING-BUDGET")
        || same(id, "SHADER-BUFFER-TYPE-NODE-BUDGET")
        || same(id, "SHADER-BUFFER-TYPE-MEMBER-BUDGET")
        || same(id, "SHADER-BUFFER-TYPE-ROOT-BUDGET");
}

namespace {
BufferDeclarationTypeLayouts::Scalar reflectedScalar(
    core::StringView spelling, std::uint32_t width) noexcept
{
    using Scalar = BufferDeclarationTypeLayouts::Scalar;
    if (spelling == "float")
        return Scalar::FloatingPoint;
    if (spelling == "double" && width == 64u)
        return Scalar::FloatingPoint;
    if ((spelling == "uint" && width == 32u) || (spelling == "uint8" && width == 8u)
        || (spelling == "uint16" && width == 16u) || (spelling == "uint32" && width == 32u)
        || (spelling == "uint64" && width == 64u))
        return Scalar::UnsignedInteger;
    if ((spelling == "int" && width == 32u) || (spelling == "int8" && width == 8u)
        || (spelling == "int16" && width == 16u) || (spelling == "int32" && width == 32u)
        || (spelling == "int64" && width == 64u))
        return Scalar::SignedInteger;
    return Scalar::None;
}
} // namespace

const char* TargetBufferMemoryDeclarations::lookup(
    std::uint32_t set, std::uint32_t binding, TargetBufferMemoryLayout& output) const noexcept
{
    output = {};
    if (diagnostic_)
        return diagnostic_;
    for (const auto& record : records_)
        if (record.facts.set == set && record.facts.binding == binding) {
            if (record.diagnostic)
                return record.diagnostic;
            output = record.facts;
            return nullptr;
        }
    return "SHADER-ENTRY-TARGET-MEMORY-BINDING";
}

const char* TargetBufferMemoryDeclarations::lookupPush(
    TargetPushMemoryLayout& output) const noexcept
{
    output = {};
    if (diagnostic_)
        return diagnostic_;
    if (pushDiagnostic_)
        return pushDiagnostic_;
    output = pushFacts_;
    return nullptr;
}

const char* TargetBufferMemoryDeclarations::pushBinaryAbi(const Graph& types,
    const SpirvPushConstantDeclarationLayout& push,
    const ShaderReflection& reflection,
    const PushBinaryMemoryPlan* plan)
{
    if (plan) {
        if (plan->contractVersion != 1u)
            return "SHADER-ENTRY-TARGET-PUSH-BINARY-PLAN-VERSION";
        if (!plan->count || !plan->members || plan->count > plan->maxMembers
            || plan->count > reflection.pushConstantMembers.size())
            return "SHADER-ENTRY-TARGET-PUSH-BINARY-PLAN-BUDGET";
        for (std::size_t i = 0u; i < plan->count; ++i) {
            const auto& rule = plan->members[i];
            if (rule.reflectionMember >= reflection.pushConstantMembers.size()
                || rule.sourceType >= types.nodes().size()
                || (!rule.arrayValueWrapper && !rule.slangRowMajorFloat4x4))
                return "SHADER-ENTRY-TARGET-PUSH-BINARY-PLAN-FRAMING";
            for (std::size_t j = 0u; j < i; ++j)
                if (rule.reflectionMember == plan->members[j].reflectionMember)
                    return "SHADER-ENTRY-TARGET-PUSH-BINARY-PLAN-DUPLICATE";
        }
    }
    if (push.present != bool(reflection.pushConstantSizeBytes))
        return "SHADER-ENTRY-TARGET-PUSH-COVERAGE";
    if (!push.present)
        return plan ? "SHADER-ENTRY-TARGET-PUSH-BINARY-PLAN-UNUSED" : nullptr;
    if (push.rootIndex >= types.roots().size())
        return "SHADER-ENTRY-TARGET-PUSH-BINARY-ROOT";
    const auto& root = types.roots()[push.rootIndex];
    if (root.storage != Graph::Storage::PushConstant || root.type >= types.nodes().size())
        return "SHADER-ENTRY-TARGET-PUSH-BINARY-ROOT";
    const auto& block = types.nodes()[root.type];
    if (block.kind != Graph::Kind::Struct || block.memberBegin > types.members().size()
        || block.memberCount > types.members().size() - block.memberBegin
        || block.memberCount != reflection.pushConstantMembers.size()
        || push.occupiedExtentBytes > reflection.pushConstantSizeBytes)
        return "SHADER-ENTRY-TARGET-PUSH-BINARY-SIZE";
    for (std::size_t i = 0u; i < block.memberCount; ++i) {
        const auto& edge = types.members()[block.memberBegin + i];
        const auto& member = reflection.pushConstantMembers[i];
        if (edge.offsetBytes != member.offset)
            return "SHADER-ENTRY-TARGET-PUSH-BINARY-OFFSET";
        auto index = edge.type;
        const PushBinaryMemoryPlan::Member* rule = nullptr;
        if (plan)
            for (std::size_t j = 0u; j < plan->count; ++j)
                if (plan->members[j].reflectionMember == i)
                    rule = &plan->members[j];
        if (index >= types.nodes().size())
            return "SHADER-ENTRY-TARGET-PUSH-BINARY-TYPE";
        if (rule) {
            if (rule->sourceType != index)
                return "SHADER-ENTRY-TARGET-PUSH-BINARY-PLAN-TYPE";
            if (types.nodes()[index].occupiedExtentBytes != member.size)
                return "SHADER-ENTRY-TARGET-PUSH-BINARY-MEMBER-SIZE";
            if (rule->arrayValueWrapper) {
                const auto& wrapper = types.nodes()[index];
                if (member.arrayDims.empty() || wrapper.kind != Graph::Kind::Struct
                    || wrapper.memberCount != 1u || wrapper.memberBegin >= types.members().size()
                    || wrapper.alignmentKnown || wrapper.targetSizeKnown)
                    return "SHADER-ENTRY-TARGET-PUSH-BINARY-WRAPPER";
                const auto& value = types.members()[wrapper.memberBegin];
                if (value.offsetBytes || value.alignmentKnown || value.reservedSizeKnown
                    || value.type >= types.nodes().size()
                    || types.nodes()[value.type].kind != Graph::Kind::FixedArray
                    || wrapper.occupiedExtentBytes != types.nodes()[value.type].occupiedExtentBytes)
                    return "SHADER-ENTRY-TARGET-PUSH-BINARY-WRAPPER";
                index = value.type;
            }
        }
        for (std::size_t dimension = 0u; dimension < member.arrayDims.size(); ++dimension) {
            if (index >= types.nodes().size())
                return "SHADER-ENTRY-TARGET-PUSH-BINARY-ARRAY";
            const auto& node = types.nodes()[index];
            if (node.kind != Graph::Kind::FixedArray || node.count != member.arrayDims[dimension]
                || (dimension == 0u && node.strideBytes != member.arrayStride))
                return "SHADER-ENTRY-TARGET-PUSH-BINARY-ARRAY";
            index = node.child;
        }
        if (index >= types.nodes().size())
            return "SHADER-ENTRY-TARGET-PUSH-BINARY-TYPE";
        const auto& node = types.nodes()[index];
        std::size_t scalarIndex = index;
        const bool rowMemory = rule && rule->slangRowMajorFloat4x4;
        // SPIRV-Reflect decoration flags are distinct from SPIR-V opcodes.
        constexpr std::uint32_t reflectedRowMajor = 0x4u, reflectedColMajor = 0x8u;
        if (rowMemory
            && (!member.rowMajor || member.matrixRows != 4u || member.matrixCols != 4u
                || member.matrixStride != 16u || member.scalarType != "float"
                || member.scalarWidth != 32u
                || (member.decorations & (reflectedRowMajor | reflectedColMajor))
                    != reflectedColMajor
                || node.matrixEncoding != Graph::MatrixEncoding::SpirvColMajorDecoration))
            return "SHADER-ENTRY-TARGET-PUSH-BINARY-MATRIX-RELATION";
        if (member.matrixRows || member.matrixCols || member.matrixStride) {
            if (node.kind != Graph::Kind::Matrix || node.rows != member.matrixRows
                || node.columns != member.matrixCols || node.strideBytes != member.matrixStride
                || (member.rowMajor && !rowMemory) || node.child >= types.nodes().size())
                return "SHADER-ENTRY-TARGET-PUSH-BINARY-MATRIX";
            const auto& vector = types.nodes()[node.child];
            if (vector.kind != Graph::Kind::Vector || vector.count != node.rows)
                return "SHADER-ENTRY-TARGET-PUSH-BINARY-MATRIX";
            scalarIndex = vector.child;
        } else if (member.vecSize) {
            if (node.kind != Graph::Kind::Vector || node.count != member.vecSize)
                return "SHADER-ENTRY-TARGET-PUSH-BINARY-VECTOR";
            scalarIndex = node.child;
        } else if (node.kind != Graph::Kind::Scalar)
            return "SHADER-ENTRY-TARGET-PUSH-BINARY-TYPE";
        if (scalarIndex >= types.nodes().size())
            return "SHADER-ENTRY-TARGET-PUSH-BINARY-SCALAR";
        const auto& scalar = types.nodes()[scalarIndex];
        const auto expected = reflectedScalar(member.scalarType, member.scalarWidth);
        if (expected == Graph::Scalar::None || scalar.kind != Graph::Kind::Scalar
            || scalar.scalar != expected || scalar.scalarBits != member.scalarWidth)
            return "SHADER-ENTRY-TARGET-PUSH-BINARY-SCALAR";
    }
    return nullptr;
}

const char* TargetBufferMemoryDeclarations::build(const core::ByteBuffer& binary,
    core::StringView target,
    const ShaderReflection& reflection,
    const core::pmr::Vector<SpirvBufferDeclarationLayout>& original,
    std::uint32_t maxBindings,
    const TargetBufferMemoryLoadRequest& request)
{
    sourceTypes_.clear();
    targetTypes_.clear();
    records_.clear();
    pushFacts_ = {};
    pushTargetRoot_ = pushSourceRoot_ = Graph::absent;
    diagnostic_ = "SHADER-ENTRY-TARGET-MEMORY-NOT-REQUESTED";
    pushDiagnostic_ = "SHADER-ENTRY-TARGET-PUSH-ABSENT";
    const auto* id = buildImpl(binary, target, reflection, original, maxBindings, request);
    if (id) {
        sourceTypes_.clear();
        targetTypes_.clear();
        records_.clear();
        pushFacts_ = {};
        pushTargetRoot_ = pushSourceRoot_ = Graph::absent;
        diagnostic_ = id;
        pushDiagnostic_ = "SHADER-ENTRY-TARGET-PUSH-ABSENT";
    }
    return id;
}

const char* TargetBufferMemoryDeclarations::independentProof(const Graph& types,
    std::size_t root,
    std::size_t maxPairs,
    const char*& required,
    bool matrixPlan)
{
    required = nullptr;
    bool matrix = false, atomic = false;
    bool equal = false;
    std::size_t count = 0u;
    const auto* id = Graph::visitPairs(
        types,
        root,
        types,
        root,
        maxPairs,
        [&](const Graph::Node& a, const Graph::Node&, bool& matches) -> const char* {
            matrix = matrix || a.kind == Graph::Kind::Matrix;
            atomic = atomic || a.kind == Graph::Kind::Atomic;
            matches = true;
            return nullptr;
        },
        equal,
        count);
    if (id)
        return id;
    if (!equal)
        return "SHADER-ENTRY-TARGET-MEMORY-INDEPENDENT-TRAVERSAL";
    required = matrix && !matrixPlan ? "SHADER-BUFFER-LOWERING-MATRIX-PROOF"
        : atomic                     ? "SHADER-BUFFER-LOWERING-ATOMIC-PROOF"
                                     : nullptr;
    return nullptr;
}

const char* TargetBufferMemoryDeclarations::validateRequest(
    const TargetBufferMemoryLoadRequest& request, std::uint32_t maxBindings) noexcept
{
    using Lowering = SpirvWgslBufferMemoryLowering;
    const auto require = [](bool condition, const char* id) {
        requireShaderArtifact(condition, id);
    };
    try {
        require(TargetBufferMemoryLoadRequest::supportsVersion(request.contractVersion),
            "SHADER-ENTRY-TARGET-MEMORY-REQUEST-VERSION");
        require(request.contractVersion == 2u || !request.maxMatrixRules,
            "SHADER-ENTRY-TARGET-MEMORY-MATRIX-REQUEST-VERSION");
        require(!request.pushBinary || (request.contractVersion == 2u && request.push),
            "SHADER-ENTRY-TARGET-PUSH-BINARY-REQUEST-VERSION");
        require(request.sourceTypes.maxNodes && request.targetTypes.maxNodes && request.maxPairs
                && request.sourceTypes.maxRoots <= UINT32_MAX
                && request.targetTypes.maxRoots <= UINT32_MAX,
            "SHADER-ENTRY-TARGET-MEMORY-BUDGET");
        require(request.bindingCount <= maxBindings && (!request.bindingCount || request.bindings),
            "SHADER-ENTRY-TARGET-MEMORY-PLAN-BUDGET");
        std::size_t matrixRules = 0u;
        const auto matrixPlan = [&](const Lowering::Plan& plan) {
            require((plan.contractVersion == 1u || plan.contractVersion == 2u)
                    && (request.contractVersion == 2u || plan.contractVersion == 1u)
                    && (plan.contractVersion == 2u || !plan.matrixCount),
                "SHADER-ENTRY-TARGET-MEMORY-MATRIX-REQUEST-VERSION");
            require(plan.matrixCount <= request.maxMatrixRules - matrixRules
                    && (!plan.matrixCount || plan.matrixColumnValues),
                "SHADER-ENTRY-TARGET-MEMORY-MATRIX-RULE-BUDGET");
            matrixRules += plan.matrixCount;
        };
        matrixPlan(request.pushArrays);
        std::size_t rules = request.pushArrays.count;
        require(rules <= request.maxArrayRules && (!rules || request.pushArrays.arrayValueWrappers),
            "SHADER-ENTRY-TARGET-MEMORY-RULE-BUDGET");
        for (std::size_t i = 0u; i < request.bindingCount; ++i) {
            const auto& plan = request.bindings[i];
            matrixPlan(plan.arrays);
            require(plan.arrays.count <= request.maxArrayRules - rules
                    && (!plan.arrays.count || plan.arrays.arrayValueWrappers),
                "SHADER-ENTRY-TARGET-MEMORY-RULE-BUDGET");
            rules += plan.arrays.count;
            for (std::size_t j = 0u; j < i; ++j)
                require(plan.set != request.bindings[j].set
                        || plan.binding != request.bindings[j].binding,
                    "SHADER-ENTRY-TARGET-MEMORY-PLAN-DUPLICATE");
        }
        return nullptr;
    } catch (const ShaderArtifactLoadError& error) {
        return error.diagnostic;
    }
}

const char* TargetBufferMemoryDeclarations::buildImpl(const core::ByteBuffer& binary,
    core::StringView target,
    const ShaderReflection& reflection,
    const core::pmr::Vector<SpirvBufferDeclarationLayout>& original,
    std::uint32_t maxBindings,
    const TargetBufferMemoryLoadRequest& request)
{
    using Lowering = SpirvWgslBufferMemoryLowering;
    const auto require = [](bool condition, const char* id) {
        requireShaderArtifact(condition, id);
    };
    try {
        if (const auto* id = validateRequest(request, maxBindings))
            return id;
        require(request.bindingCount == original.size()
                && request.push == bool(reflection.pushConstantSizeBytes)
                && (request.push || (!request.pushArrays.count && !request.pushArrays.matrixCount)),
            "SHADER-ENTRY-TARGET-MEMORY-PLAN-COVERAGE");
        for (const auto& facts : original) {
            bool found = false;
            for (std::size_t i = 0u; i < request.bindingCount; ++i)
                if (request.bindings[i].set == facts.set
                    && request.bindings[i].binding == facts.binding)
                    found = true;
            require(found, "SHADER-ENTRY-TARGET-MEMORY-PLAN-COVERAGE");
        }
        core::pmr::Vector<SpirvBufferDeclarationLayout> source{&core::defaultCpuMemoryResource()};
        SpirvPushConstantDeclarationLayout push;
        if (const auto* id =
                SpirvBufferDeclarationLayoutVerifier::verifyWithPushConstants(binary.data(),
                    binary.size(),
                    maxBindings,
                    request.sourceTypes,
                    source,
                    sourceTypes_,
                    push))
            return id;
        require(source.size() == original.size(), "SHADER-ENTRY-TARGET-MEMORY-BINARY-COVERAGE");
        for (const auto& facts : source) {
            bool found = false;
            for (const auto& old : original)
                if (facts.matches(old))
                    found = true;
            require(found, "SHADER-ENTRY-TARGET-MEMORY-BINARY-MISMATCH");
        }
        if (const auto* id = pushBinaryAbi(sourceTypes_, push, reflection, request.pushBinary))
            return id;
        core::pmr::Vector<WgslBufferDeclarationLayout> targetFacts{
            &core::defaultCpuMemoryResource()};
        if (const auto* id = WgslBufferDeclarationLayoutVerifier::verifyWithTypes(target,
                static_cast<std::uint32_t>(request.targetTypes.maxRoots),
                request.targetTypes,
                targetFacts,
                targetTypes_)) {
            if (targetFailureIsBudget(id))
                return id;
            unavailable(id);
            return nullptr;
        }
        require(targetFacts.size() == source.size() + (push.present ? 1u : 0u),
            "SHADER-ENTRY-TARGET-MEMORY-TARGET-COVERAGE");
        core::pmr::Vector<std::uint8_t> claimed{&core::defaultCpuMemoryResource()};
        claimed.resize(targetFacts.size(), 0u);
        const auto targetRoot = [&](std::uint32_t group, std::uint32_t binding) {
            std::size_t found = Graph::absent;
            for (std::size_t i = 0u; i < targetFacts.size(); ++i)
                if (targetFacts[i].group == group && targetFacts[i].binding == binding)
                    found = i;
            require(found != Graph::absent, "SHADER-ENTRY-TARGET-MEMORY-PHYSICAL-DECLARATION");
            require(!claimed[found], "SHADER-ENTRY-TARGET-MEMORY-PHYSICAL-DUPLICATE");
            claimed[found] = 1u;
            return found;
        };
        for (const auto& facts : source) {
            const ShaderBinding* logical = nullptr;
            for (const auto& b : reflection.bindings)
                if (b.set == facts.set && b.binding == facts.binding)
                    logical = &b;
            require(
                logical && isBufferBinding(logical->type), "SHADER-ENTRY-TARGET-MEMORY-LOGICAL");
            const ShaderWebGpuBindingSpaceBinding* physical = nullptr;
            for (const auto& p : reflection.webGpuBindingSpace)
                if (p.active && p.set == facts.set && p.binding == facts.binding)
                    physical = &p;
            require(physical && physical->physicalType == logical->type
                    && physical->capacity == facts.descriptorCount && !physical->arrayElement,
                "SHADER-ENTRY-TARGET-MEMORY-PHYSICAL");
            const auto t = targetRoot(physical->group, physical->webGpuBinding);
            const auto& targetFact = targetFacts[t];
            require(addressSpaceMatches(logical->type, targetFact.addressSpace),
                "SHADER-ENTRY-TARGET-MEMORY-ACCESS");
            std::size_t s = Graph::absent;
            for (std::size_t i = 0u; i < sourceTypes_.roots().size(); ++i) {
                const auto& r = sourceTypes_.roots()[i];
                if (r.storage != Graph::Storage::PushConstant && r.set == facts.set
                    && r.binding == facts.binding)
                    s = i;
            }
            require(s != Graph::absent, "SHADER-ENTRY-TARGET-MEMORY-SOURCE-ROOT");
            const TargetBufferMemoryLoadRequest::BindingPlan* plan = nullptr;
            for (std::size_t i = 0u; i < request.bindingCount; ++i)
                if (request.bindings[i].set == facts.set
                    && request.bindings[i].binding == facts.binding)
                    plan = &request.bindings[i];
            std::size_t selectedType = Graph::absent;
            if (const auto* id = Lowering::selectSourceType(
                    sourceTypes_, s, targetTypes_, t, plan->form, selectedType))
                return id;
            const char* sourceProof = nullptr;
            const char* targetProof = nullptr;
            if (const auto* id = independentProof(sourceTypes_,
                    sourceTypes_.roots()[s].type,
                    request.maxPairs,
                    sourceProof,
                    plan->arrays.contractVersion == 2u))
                return id;
            if (const auto* id = independentProof(targetTypes_,
                    targetTypes_.roots()[t].type,
                    request.maxPairs,
                    targetProof,
                    plan->arrays.contractVersion == 2u))
                return id;
            const char* id = sourceProof ? sourceProof : targetProof;
            if (id)
                require(!plan->arrays.count && !plan->arrays.matrixCount,
                    "SHADER-ENTRY-TARGET-MEMORY-INDEPENDENT-PLAN");
            Lowering::Facts relation;
            if (!id)
                id = Lowering::compare(sourceTypes_,
                    s,
                    targetTypes_,
                    t,
                    plan->form,
                    request.maxPairs,
                    relation,
                    plan->arrays);
            if (id
                && (!independentProofDiagnostic(id)
                    || (request.contractVersion == 2u
                        && same(id, "SHADER-BUFFER-LOWERING-MATRIX-PROOF"))))
                return id;
            require(facts.hasRuntimeArray == targetFact.hasRuntimeArray
                    && (!facts.hasRuntimeArray
                        || (facts.runtimeArrayOffsetBytes == targetFact.runtimeArrayOffsetBytes
                            && facts.runtimeArrayStrideBytes
                                == targetFact.runtimeArrayStrideBytes)),
                "SHADER-ENTRY-TARGET-MEMORY-RUNTIME-SHAPE");
            records_.push_back(
                {{facts.set, facts.binding, logical->type, targetFact, facts}, s, t, id});
        }
        if (push.present) {
            const auto t = targetRoot(reflection.pushConstantWebGpuBindingGroup,
                reflection.pushConstantWebGpuBindingIndex);
            const auto& facts = targetFacts[t];
            require(facts.addressSpace == WgslBufferDeclarationLayout::AddressSpace::Uniform
                    && !facts.hasRuntimeArray,
                "SHADER-ENTRY-TARGET-PUSH-UNIFORM");
            std::size_t selectedType = Graph::absent;
            if (const auto* id = Lowering::selectSourceType(sourceTypes_,
                    push.rootIndex,
                    targetTypes_,
                    t,
                    Lowering::RootForm::PushBlockStruct,
                    selectedType))
                return id;
            const char* sourceProof = nullptr;
            const char* targetProof = nullptr;
            if (const auto* id = independentProof(sourceTypes_,
                    sourceTypes_.roots()[push.rootIndex].type,
                    request.maxPairs,
                    sourceProof,
                    request.pushArrays.contractVersion == 2u))
                return id;
            if (const auto* id = independentProof(targetTypes_,
                    targetTypes_.roots()[t].type,
                    request.maxPairs,
                    targetProof,
                    request.pushArrays.contractVersion == 2u))
                return id;
            const char* id = sourceProof ? sourceProof : targetProof;
            if (id)
                require(!request.pushArrays.count && !request.pushArrays.matrixCount,
                    "SHADER-ENTRY-TARGET-MEMORY-INDEPENDENT-PLAN");
            Lowering::Facts relation;
            if (!id)
                id = Lowering::compare(sourceTypes_,
                    push.rootIndex,
                    targetTypes_,
                    t,
                    Lowering::RootForm::PushBlockStruct,
                    request.maxPairs,
                    relation,
                    request.pushArrays);
            if (id
                && (!independentProofDiagnostic(id)
                    || (request.contractVersion == 2u
                        && same(id, "SHADER-BUFFER-LOWERING-MATRIX-PROOF"))))
                return id;
            require(facts.minimumBindingBytes == reflection.pushConstantSizeBytes,
                "SHADER-ENTRY-TARGET-PUSH-TARGET-SIZE");
            pushDiagnostic_ = id;
            pushTargetRoot_ = t;
            pushSourceRoot_ = push.rootIndex;
            pushFacts_ = {facts.group,
                facts.binding,
                push.occupiedExtentBytes,
                facts.minimumBindingBytes,
                facts.alignmentBytes};
        }
        for (const auto c : claimed)
            require(c != 0u, "SHADER-ENTRY-TARGET-MEMORY-EXTRA-DECLARATION");
        diagnostic_ = nullptr;
        return nullptr;
    } catch (const ShaderArtifactLoadError& e) {
        return e.diagnostic;
    } catch (const std::bad_alloc&) {
        return "SHADER-ENTRY-TARGET-MEMORY-ALLOCATION";
    }
}
} // namespace arp::shader
