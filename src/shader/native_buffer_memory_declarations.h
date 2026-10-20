#pragma once
#include "shader/buffer_declaration_type_path.h"
#include "shader/spirv_msl_buffer_memory_lowering.h"

namespace arp::shader {
// Cold compiler/lowering intent, never a driver or execution receipt. Every
// pointer is borrowed only during the original package's synchronous load.
struct NativeBufferMemoryLoadRequest {
    using Graph = BufferDeclarationTypeLayouts;
    using Lowering = SpirvMslBufferMemoryLowering;

    struct MatrixTargetPath {
        std::size_t sourceType = Graph::absent;
        BufferDeclarationTypePath::Input target;
    };

    struct Binding {
        std::uint32_t set = 0u, binding = 0u;
        Graph::Budget targetTypes;
        Lowering::Plan relation;
        // Request/3: exact member/array paths, resolved only against the real
        // driver's graph. No compiler-generated target node number is guessed.
        const MatrixTargetPath* matrixPaths = nullptr;
        std::size_t matrixPathCount = 0u, maxPathSteps = 0u;
    };

    std::uint32_t contractVersion = 0u;
    Graph::Budget sourceTypes;
    const Binding* bindings = nullptr;
    std::size_t bindingCount = 0u;
};

// Metadata inside the existing package module. Source types and all rule bytes
// are frozen here; only that package can authenticate a borrowed lookup.
class NativeBufferMemoryDeclarations final {
public:
    using Request = NativeBufferMemoryLoadRequest;
    using Graph = Request::Graph;
    using Lowering = Request::Lowering;

    struct Record {
        struct MatrixPath {
            std::size_t sourceType = Graph::absent;
            core::pmr::Vector<BufferDeclarationTypePath::Step> steps;
        };

        std::uint32_t set = 0u, binding = 0u;
        std::size_t sourceType = Graph::absent;
        std::uint32_t relationVersion = 1u;
        Lowering::RootForm rootForm = Lowering::RootForm::Block;
        Graph::Budget targetTypes;
        std::size_t maxPairs = 0u, maxWrappers = 0u, maxMatrices = 0u;
        core::pmr::Vector<Lowering::ArrayWrapper> wrappers{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<Lowering::MatrixColumns> matrices{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<MatrixPath> matrixPaths{&core::defaultCpuMemoryResource()};
        std::size_t maxPathSteps = 0u;

        Lowering::Plan plan() const noexcept
        {
            return {relationVersion,
                maxPairs,
                maxWrappers,
                maxMatrices,
                wrappers.data(),
                wrappers.size(),
                matrices.data(),
                matrices.size(),
                rootForm};
        }

        // The borrowed Plan lives only as long as resolved. A failed resolution
        // clears both outputs, so no partial relation can reach the driver.
        const char* resolveTargetPlan(const Graph& target,
            std::size_t root,
            core::pmr::Vector<Lowering::MatrixColumns>& resolved,
            Lowering::Plan& output) const
        {
            resolved.clear();
            output = {};
            try {
                for (const auto& rule : matrices)
                    resolved.push_back(rule);
                std::size_t visits = 0u;
                for (const auto& rule : matrixPaths) {
                    if (rule.steps.size() > maxPathSteps - visits) {
                        resolved.clear();
                        return "SHADER-NATIVE-MEMORY-TARGET-PATH-BUDGET";
                    }
                    visits += rule.steps.size();
                    std::size_t type = Graph::absent;
                    if (const auto* id = BufferDeclarationTypePath::resolve(target,
                            root,
                            {rule.steps.data(), rule.steps.size()},
                            maxPathSteps,
                            type)) {
                        resolved.clear();
                        return id;
                    }
                    if (target.nodes()[type].kind != Graph::Kind::Matrix) {
                        resolved.clear();
                        return "SHADER-NATIVE-MEMORY-TARGET-PATH-MATRIX";
                    }
                    resolved.push_back({rule.sourceType, type});
                }
                output = plan();
                output.matrices = resolved.data();
                output.matrixCount = resolved.size();
                return nullptr;
            } catch (const std::bad_alloc&) {
                resolved.clear();
                return "SHADER-NATIVE-MEMORY-ALLOCATION";
            }
        }
    };

    static const char* validateRequest(const Request& request, std::size_t maxBindings) noexcept
    {
        if ((request.contractVersion != 1u && request.contractVersion != 2u
                && request.contractVersion != 3u)
            || !request.sourceTypes.maxNodes || !request.sourceTypes.maxMembers
            || !request.sourceTypes.maxRoots || !request.bindingCount
            || request.bindingCount > maxBindings || !request.bindings)
            return "SHADER-NATIVE-MEMORY-REQUEST";
        for (std::size_t i = 0u; i < request.bindingCount; ++i) {
            const auto& binding = request.bindings[i];
            const auto& plan = binding.relation;
            if ((binding.matrixPathCount && !binding.matrixPaths)
                || plan.matrixCount > plan.maxMatrices
                || binding.matrixPathCount > plan.maxMatrices - plan.matrixCount
                || (request.contractVersion != 3u
                    && (binding.matrixPaths || binding.matrixPathCount || binding.maxPathSteps))
                || (binding.matrixPathCount
                    && (plan.contractVersion != 2u || !binding.maxPathSteps)))
                return "SHADER-NATIVE-MEMORY-TARGET-PATH-REQUEST";
            std::size_t pathSteps = 0u;
            for (std::size_t p = 0u; p < binding.matrixPathCount; ++p) {
                const auto& path = binding.matrixPaths[p].target;
                if ((path.count && !path.steps) || path.count > binding.maxPathSteps - pathSteps)
                    return "SHADER-NATIVE-MEMORY-TARGET-PATH-BUDGET";
                pathSteps += path.count;
                for (std::size_t s = 0u; s < path.count; ++s)
                    if ((path.steps[s].kind != BufferDeclarationTypePath::Kind::Member
                            && path.steps[s].kind != BufferDeclarationTypePath::Kind::ArrayElement)
                        || (path.steps[s].kind == BufferDeclarationTypePath::Kind::ArrayElement
                            && path.steps[s].member))
                        return "SHADER-NATIVE-MEMORY-TARGET-PATH-KIND";
            }
            if (!binding.targetTypes.maxNodes || !binding.targetTypes.maxMembers
                || !binding.targetTypes.maxRoots
                || (plan.contractVersion != 1u && plan.contractVersion != 2u)
                || (request.contractVersion == 1u && plan.contractVersion != 1u)
                || (plan.contractVersion == 1u && plan.rootForm != Lowering::RootForm::Block)
                || (plan.rootForm != Lowering::RootForm::Block
                    && plan.rootForm != Lowering::RootForm::Value
                    && plan.rootForm != Lowering::RootForm::RuntimeElement)
                || !plan.maxPairs || plan.wrapperCount > plan.maxWrappers
                || plan.matrixCount > plan.maxMatrices || (plan.wrapperCount && !plan.wrappers)
                || (plan.matrixCount && !plan.matrices))
                return "SHADER-NATIVE-MEMORY-PLAN";
            for (std::size_t j = 0u; j < i; ++j)
                if (binding.set == request.bindings[j].set
                    && binding.binding == request.bindings[j].binding)
                    return "SHADER-NATIVE-MEMORY-BINDING-DUPLICATE";
        }
        return nullptr;
    }

    const char* build(Graph&& source, const Request& request, std::size_t maxBindings)
    {
        clear();
        if (const auto* id = validateRequest(request, maxBindings))
            return id;
        if (source.budget().maxNodes != request.sourceTypes.maxNodes
            || source.budget().maxMembers != request.sourceTypes.maxMembers
            || source.budget().maxRoots != request.sourceTypes.maxRoots)
            return "SHADER-NATIVE-MEMORY-SOURCE-BUDGET";
        try {
            for (std::size_t i = 0u; i < request.bindingCount; ++i) {
                const auto& binding = request.bindings[i];
                Record record;
                record.set = binding.set;
                record.binding = binding.binding;
                record.targetTypes = binding.targetTypes;
                record.maxPairs = binding.relation.maxPairs;
                record.maxWrappers = binding.relation.maxWrappers;
                record.maxMatrices = binding.relation.maxMatrices;
                record.maxPathSteps = binding.maxPathSteps;
                for (const auto& root : source.roots())
                    if (root.set == binding.set && root.binding == binding.binding) {
                        if (record.sourceType != Graph::absent || root.descriptorCount != 1u
                            || (root.storage != Graph::Storage::Uniform
                                && root.storage != Graph::Storage::StorageAccessUnspecified))
                            throw Rejection{"SHADER-NATIVE-MEMORY-SOURCE-ROOT"};
                        record.sourceType = root.type;
                    }
                if (record.sourceType == Graph::absent)
                    throw Rejection{"SHADER-NATIVE-MEMORY-SOURCE-ROOT"};
                record.relationVersion = binding.relation.contractVersion;
                record.rootForm = binding.relation.rootForm;
                for (std::size_t j = 0u; j < binding.relation.wrapperCount; ++j)
                    record.wrappers.push_back(binding.relation.wrappers[j]);
                for (std::size_t j = 0u; j < binding.relation.matrixCount; ++j)
                    record.matrices.push_back(binding.relation.matrices[j]);
                for (std::size_t j = 0u; j < binding.matrixPathCount; ++j) {
                    Record::MatrixPath path;
                    path.sourceType = binding.matrixPaths[j].sourceType;
                    const auto& input = binding.matrixPaths[j].target;
                    for (std::size_t s = 0u; s < input.count; ++s)
                        path.steps.push_back(input.steps[s]);
                    record.matrixPaths.push_back(std::move(path));
                }
                if (const auto* id = validateSource(source, record))
                    throw Rejection{id};
                records_.push_back(std::move(record));
            }
            source_ = std::move(source);
            version_ = request.contractVersion;
            return nullptr;
        } catch (const Rejection& rejected) {
            clear();
            return rejected.id;
        } catch (const std::bad_alloc&) {
            clear();
            return "SHADER-NATIVE-MEMORY-ALLOCATION";
        }
    }

    const Record* lookup(std::uint32_t set, std::uint32_t binding) const noexcept
    {
        if (version_ != 1u && version_ != 2u && version_ != 3u)
            return nullptr;
        for (const auto& record : records_)
            if (record.set == set && record.binding == binding)
                return &record;
        return nullptr;
    }

    const Graph& sourceTypes() const noexcept { return source_; }

    std::uint32_t version() const noexcept { return version_; }

    void clear() noexcept
    {
        version_ = 0u;
        source_.clear();
        records_.clear();
    }

private:
    struct Rejection {
        const char* id;
    };

    static const char* validateSource(const Graph& graph, const Record& record)
    {
        std::size_t value = Graph::absent;
        std::uint64_t stride = 0u;
        if (const auto* id =
                Lowering::sourceValue(graph, record.sourceType, record.plan(), value, stride))
            return id;
        if (stride) {
            bool storage = false;
            for (const auto& root : graph.roots())
                if (root.set == record.set && root.binding == record.binding)
                    storage = root.storage == Graph::Storage::StorageAccessUnspecified;
            if (!storage)
                return "SHADER-NATIVE-MEMORY-RUNTIME-STORAGE";
        }
        for (std::size_t i = 0u; i < record.matrices.size(); ++i) {
            const auto& rule = record.matrices[i];
            if (rule.sourceType >= graph.nodes().size()
                || rule.targetType >= record.targetTypes.maxNodes
                || graph.nodes()[rule.sourceType].kind != Graph::Kind::Matrix
                || graph.nodes()[rule.sourceType].matrixEncoding
                    != Graph::MatrixEncoding::SpirvColMajorDecoration)
                return "SHADER-NATIVE-MEMORY-MATRIX";
            for (std::size_t j = 0u; j < i; ++j)
                if (rule.sourceType == record.matrices[j].sourceType
                    && rule.targetType == record.matrices[j].targetType)
                    return "SHADER-NATIVE-MEMORY-RULE-DUPLICATE";
        }
        for (std::size_t i = 0u; i < record.wrappers.size(); ++i) {
            const auto& rule = record.wrappers[i];
            if (rule.side == Lowering::Side::Source) {
                if (rule.wrapperType >= graph.nodes().size())
                    return "SHADER-NATIVE-MEMORY-WRAPPER";
                const auto& node = graph.nodes()[rule.wrapperType];
                if (node.kind != Graph::Kind::Struct || node.memberCount != 1u
                    || node.memberBegin >= graph.members().size())
                    return "SHADER-NATIVE-MEMORY-WRAPPER";
                const auto& member = graph.members()[node.memberBegin];
                if (node.alignmentKnown || node.targetSizeKnown || member.alignmentKnown
                    || member.reservedSizeKnown || member.offsetBytes
                    || member.type >= graph.nodes().size()
                    || graph.nodes()[member.type].kind != Graph::Kind::FixedArray
                    || node.occupiedExtentBytes != graph.nodes()[member.type].occupiedExtentBytes)
                    return "SHADER-NATIVE-MEMORY-WRAPPER";
            } else if (rule.side != Lowering::Side::Target
                || rule.wrapperType >= record.targetTypes.maxNodes)
                return "SHADER-NATIVE-MEMORY-WRAPPER";
            for (std::size_t j = 0u; j < i; ++j)
                if (rule.side == record.wrappers[j].side
                    && rule.wrapperType == record.wrappers[j].wrapperType)
                    return "SHADER-NATIVE-MEMORY-RULE-DUPLICATE";
        }
        for (const auto& rule : record.matrixPaths)
            if (rule.sourceType >= graph.nodes().size()
                || graph.nodes()[rule.sourceType].kind != Graph::Kind::Matrix
                || graph.nodes()[rule.sourceType].matrixEncoding
                    != Graph::MatrixEncoding::SpirvColMajorDecoration)
                return "SHADER-NATIVE-MEMORY-MATRIX";
        core::pmr::Vector<std::uint8_t> matrices(record.matrices.size() + record.matrixPaths.size(),
            0u,
            &core::defaultCpuMemoryResource());
        core::pmr::Vector<std::uint8_t> wrappers(
            record.wrappers.size(), 0u, &core::defaultCpuMemoryResource());
        bool equal = false;
        std::size_t pairs = 0u;
        const auto* id = Graph::visitPairs(
            graph,
            value,
            graph,
            value,
            record.maxPairs,
            [&](const Graph::Node& a, const Graph::Node&, bool& matches) -> const char* {
                if (a.kind != Graph::Kind::Scalar && a.kind != Graph::Kind::Vector
                    && a.kind != Graph::Kind::Matrix && a.kind != Graph::Kind::FixedArray
                    && a.kind != Graph::Kind::Struct)
                    return "SHADER-NATIVE-MEMORY-DOMAIN";
                if (a.alignmentKnown || a.targetSizeKnown)
                    return "SHADER-NATIVE-MEMORY-SOURCE-ENCODING";
                for (std::size_t i = 0u; i < a.memberCount; ++i) {
                    const auto& member = graph.members()[a.memberBegin + i];
                    if (member.alignmentKnown || member.reservedSizeKnown)
                        return "SHADER-NATIVE-MEMORY-SOURCE-ENCODING";
                }
                if (a.kind == Graph::Kind::Matrix) {
                    bool declared = false;
                    for (std::size_t i = 0u; i < record.matrices.size(); ++i)
                        if (&a == &graph.nodes()[record.matrices[i].sourceType]) {
                            matrices[i] = 1u;
                            declared = true;
                        }
                    for (std::size_t i = 0u; i < record.matrixPaths.size(); ++i)
                        if (&a == &graph.nodes()[record.matrixPaths[i].sourceType]) {
                            matrices[record.matrices.size() + i] = 1u;
                            declared = true;
                        }
                    if (!declared)
                        return "SHADER-NATIVE-MEMORY-MATRIX-PROOF";
                } else if (a.matrixEncoding != Graph::MatrixEncoding::None)
                    return "SHADER-NATIVE-MEMORY-SOURCE-ENCODING";
                for (std::size_t i = 0u; i < record.wrappers.size(); ++i)
                    if (record.wrappers[i].side == Lowering::Side::Source
                        && &a == &graph.nodes()[record.wrappers[i].wrapperType])
                        wrappers[i] = 1u;
                matches = true;
                return nullptr;
            },
            equal,
            pairs);
        if (id)
            return id;
        for (const auto used : matrices)
            if (!used)
                return "SHADER-NATIVE-MEMORY-RULE-UNUSED";
        for (std::size_t i = 0u; i < wrappers.size(); ++i)
            if (record.wrappers[i].side == Lowering::Side::Source && !wrappers[i])
                return "SHADER-NATIVE-MEMORY-RULE-UNUSED";
        return equal ? nullptr : "SHADER-NATIVE-MEMORY-SOURCE";
    }

    Graph source_;
    core::pmr::Vector<Record> records_{&core::defaultCpuMemoryResource()};
    std::uint32_t version_ = 0u;
};
} // namespace arp::shader
