#include "shader/graphics_shader_module_artifact.h"

#include "shader/shader_artifact_reflection_contract.h"
#include "shader/shader_pipeline_reflection_validator.h"

#include <algorithm>

namespace arp::shader {
namespace {
struct ChildLoadError {
    core::pmr::String diagnostic;
};
} // namespace

void GraphicsShaderModuleArtifact::buildRuntimeLength(const GraphicsShaderModuleLoadBudget& budget,
    const GraphicsRuntimeLengthOperationLoadRequest& request,
    bool unavailableMetal)
{
    requireShaderArtifact(
        !targetMemoryDiagnostic_, "SHADER-MODULE-RUNTIME-LENGTH-MEMORY-UNAVAILABLE");
    if (const auto* id = RuntimeArrayLengthOperations::verify(bytes(ShaderArtifactRole::Spirv),
            shaderArtifactText(bytes(ShaderArtifactRole::Wgsl)),
            module_.reflection,
            budget.maxModuleBindings,
            request.combinedMemory,
            request.combined.operations,
            runtimeLength_))
        throw ShaderArtifactLoadError{id};
    requireShaderArtifact(!unavailableMetal || !runtimeLength_.empty(),
        "SHADER-MODULE-METAL-LENGTH-OPERATION-REQUIRED");
    const auto sameShape = [](const auto& a, const auto& b) noexcept {
        return a.set == b.set && a.binding == b.binding && a.sourceMember == b.sourceMember
            && a.runtimeOffsetBytes == b.runtimeOffsetBytes
            && a.runtimeStrideBytes == b.runtimeStrideBytes;
    };
    for (const auto& child : entries_) {
        requireShaderArtifact(
            !child->runtimeLengthDiagnostic_, "SHADER-MODULE-RUNTIME-LENGTH-CHILD-UNAVAILABLE");
        if (unavailableMetal)
            requireShaderArtifact(child->targetDiagnostic(ShaderArtifactRole::Metal)
                    == "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED",
                "SHADER-MODULE-RUNTIME-LENGTH-CHILD-ROLE");
        for (const auto& fact : child->runtimeLength_) {
            bool found = false;
            for (const auto& combined : runtimeLength_)
                if (sameShape(fact, combined))
                    found = true;
            requireShaderArtifact(found, "SHADER-MODULE-RUNTIME-LENGTH-CHILD-COVERAGE");
        }
    }
    for (const auto& combined : runtimeLength_) {
        bool found = false;
        for (const auto& child : entries_)
            for (const auto& fact : child->runtimeLength_)
                if (sameShape(fact, combined))
                    found = true;
        requireShaderArtifact(found, "SHADER-MODULE-RUNTIME-LENGTH-COMBINED-COVERAGE");
    }
    runtimeLengthDiagnostic_ = nullptr;
}

const char* GraphicsShaderModuleArtifact::buildTargetMemory(
    const GraphicsShaderModuleLoadBudget& budget, const GraphicsTargetMemoryLoadRequest& request)
{
    using Graph = BufferDeclarationTypeLayouts;
    using Memory = TargetBufferMemoryDeclarations;
    const auto require = [](bool condition, const char* id) {
        requireShaderArtifact(condition, id);
    };
    require((request.contractVersion == 1u || request.contractVersion == 2u)
            && (request.contractVersion == 2u || !request.combinedPushBinary) && request.maxPairs
            && request.sourceTypes.maxNodes && request.targetTypes.maxNodes
            && request.sourceTypes.maxRoots <= UINT32_MAX
            && request.targetTypes.maxRoots <= UINT32_MAX,
        "SHADER-MODULE-TARGET-MEMORY-BUDGET");
    // Both children were loaded with explicit requests. Unsupported/legacy
    // children close the complete module proof instead of lending partial facts.
    const char* childUnavailable = nullptr;
    for (const auto& child : entries_)
        if (const auto* id = child->targetMemory_.diagnostic())
            if (!childUnavailable)
                childUnavailable = id;
    if (childUnavailable) {
        targetMemoryDiagnostic_ = childUnavailable;
        return nullptr;
    }
    const auto& reflection = module_.reflection;
    core::pmr::Vector<SpirvBufferDeclarationLayout> source{&core::defaultCpuMemoryResource()};
    SpirvPushConstantDeclarationLayout push;
    const auto& binary = bytes(ShaderArtifactRole::Spirv);
    if (const auto* id =
            SpirvBufferDeclarationLayoutVerifier::verifyWithPushConstants(binary.data(),
                binary.size(),
                budget.maxModuleBindings,
                request.sourceTypes,
                source,
                sourceTypes_,
                push))
        return id;
    if (const auto* id =
            Memory::pushBinaryAbi(sourceTypes_, push, reflection, request.combinedPushBinary))
        return id;
    std::size_t bufferCount = 0u;
    for (const auto& binding : reflection.bindings)
        if (Memory::isBufferBinding(binding.type))
            ++bufferCount;
    require(source.size() == bufferCount, "SHADER-MODULE-TARGET-MEMORY-BINARY-COVERAGE");
    core::pmr::Vector<WgslBufferDeclarationLayout> target{&core::defaultCpuMemoryResource()};
    if (const auto* id = WgslBufferDeclarationLayoutVerifier::verifyWithTypes(
            shaderArtifactText(bytes(ShaderArtifactRole::Wgsl)),
            static_cast<std::uint32_t>(request.targetTypes.maxRoots),
            request.targetTypes,
            target,
            targetTypes_)) {
        if (Memory::targetFailureIsBudget(id))
            return id;
        targetMemoryDiagnostic_ = id;
        return nullptr;
    }
    require(target.size() == bufferCount + (push.present ? 1u : 0u),
        "SHADER-MODULE-TARGET-MEMORY-TARGET-COVERAGE");
    core::pmr::Vector<std::uint8_t> claimed{&core::defaultCpuMemoryResource()};
    claimed.resize(target.size(), 0u);
    const auto targetRoot = [&](std::uint32_t group, std::uint32_t slot) {
        std::size_t found = Graph::absent;
        for (std::size_t i = 0u; i < target.size(); ++i)
            if (target[i].group == group && target[i].binding == slot)
                found = i;
        require(found != Graph::absent, "SHADER-MODULE-TARGET-MEMORY-PHYSICAL-DECLARATION");
        require(!claimed[found], "SHADER-MODULE-TARGET-MEMORY-PHYSICAL-DUPLICATE");
        claimed[found] = 1u;
        return found;
    };
    const auto compare = [&](const Graph& lhs,
                             std::size_t l,
                             const Graph& rhs,
                             std::size_t r,
                             Graph::Comparison policy,
                             const char* mismatch) {
        bool equal = false;
        if (const auto* id = Graph::compare(lhs, l, rhs, r, request.maxPairs, equal, policy))
            throw ShaderArtifactLoadError{id};
        require(equal, mismatch);
    };
    for (const auto& binding : reflection.bindings)
        if (Memory::isBufferBinding(binding.type)) {
            const SpirvBufferDeclarationLayout* binaryFacts = nullptr;
            for (const auto& f : source)
                if (f.set == binding.set && f.binding == binding.binding)
                    binaryFacts = &f;
            require(binaryFacts && binaryFacts->descriptorCount == binding.arrayCount
                    && Memory::sourceClassMatches(binding.type, binaryFacts->storageClass),
                "SHADER-MODULE-TARGET-MEMORY-BINARY-REFLECTION");
            std::size_t s = Graph::absent;
            for (std::size_t i = 0u; i < sourceTypes_.roots().size(); ++i) {
                const auto& root = sourceTypes_.roots()[i];
                if (root.storage != Graph::Storage::PushConstant && root.set == binding.set
                    && root.binding == binding.binding)
                    s = i;
            }
            require(s != Graph::absent, "SHADER-MODULE-TARGET-MEMORY-SOURCE-ROOT");
            const ShaderWebGpuBindingSpaceBinding* physical = nullptr;
            for (const auto& p : reflection.webGpuBindingSpace)
                if (p.active && p.set == binding.set && p.binding == binding.binding)
                    physical = &p;
            require(physical && physical->physicalType == binding.type
                    && physical->capacity == binding.arrayCount && !physical->arrayElement,
                "SHADER-MODULE-TARGET-MEMORY-PHYSICAL");
            const auto t = targetRoot(physical->group, physical->webGpuBinding);
            require(Memory::addressSpaceMatches(binding.type, target[t].addressSpace),
                "SHADER-MODULE-TARGET-MEMORY-ACCESS");
            TargetMemoryRecord aggregate;
            aggregate.facts = {binding.set,
                binding.binding,
                0u,
                binding.type,
                target[t],
                target[t].minimumBindingBytes};
            for (std::size_t stage = 0u; stage < entries_.size(); ++stage) {
                const auto& child = entries_[stage];
                const Memory::Record* consumer = nullptr;
                for (const auto& f : child->targetMemory_.records())
                    if (f.facts.set == binding.set && f.facts.binding == binding.binding)
                        consumer = &f;
                const auto stageMask = std::uint32_t{1u} << stage;
                require(bool(consumer) == bool(binding.stageMask & stageMask),
                    "SHADER-MODULE-TARGET-MEMORY-STAGE-COVERAGE");
                if (!consumer)
                    continue;
                require(consumer->facts.type == binding.type
                        && consumer->facts.source.descriptorCount == binding.arrayCount
                        && consumer->facts.target.hasRuntimeArray == target[t].hasRuntimeArray
                        && consumer->facts.target.runtimeArrayOffsetBytes
                            == target[t].runtimeArrayOffsetBytes
                        && consumer->facts.target.runtimeArrayStrideBytes
                            == target[t].runtimeArrayStrideBytes,
                    "SHADER-MODULE-TARGET-MEMORY-STAGE-SHAPE");
                const auto& childSource = child->targetMemory_.sourceTypes();
                const auto& childTarget = child->targetMemory_.targetTypes();
                compare(sourceTypes_,
                    sourceTypes_.roots()[s].type,
                    childSource,
                    childSource.roots()[consumer->sourceRoot].type,
                    Graph::Comparison::Exact,
                    "SHADER-MODULE-TARGET-MEMORY-STAGE-BINARY-TYPE");
                compare(targetTypes_,
                    targetTypes_.roots()[t].type,
                    childTarget,
                    childTarget.roots()[consumer->targetRoot].type,
                    Graph::Comparison::MemoryRepresentation,
                    "SHADER-MODULE-TARGET-MEMORY-STAGE-TARGET-TYPE");
                aggregate.facts.stageMask |= stageMask;
                aggregate.facts.target.minimumBindingBytes =
                    std::max(aggregate.facts.target.minimumBindingBytes,
                        consumer->facts.target.minimumBindingBytes);
                if (consumer->diagnostic && !aggregate.diagnostic)
                    aggregate.diagnostic = consumer->diagnostic;
            }
            require(aggregate.facts.stageMask == binding.stageMask,
                "SHADER-MODULE-TARGET-MEMORY-STAGE-COVERAGE");
            targetMemoryRecords_.push_back(aggregate);
        }
    for (const auto& child : entries_)
        for (const auto& consumer : child->targetMemory_.records()) {
            bool found = false;
            for (const auto& r : targetMemoryRecords_)
                if (r.facts.set == consumer.facts.set && r.facts.binding == consumer.facts.binding)
                    found = true;
            require(found, "SHADER-MODULE-TARGET-MEMORY-EXTRA-STAGE-BINDING");
        }
    if (push.present) {
        const auto t = targetRoot(
            reflection.pushConstantWebGpuBindingGroup, reflection.pushConstantWebGpuBindingIndex);
        require(target[t].addressSpace == WgslBufferDeclarationLayout::AddressSpace::Uniform
                && !target[t].hasRuntimeArray
                && target[t].minimumBindingBytes == reflection.pushConstantSizeBytes,
            "SHADER-MODULE-TARGET-PUSH-UNIFORM");
        std::size_t consumers = 0u;
        targetPushDiagnostic_ = nullptr;
        for (const auto& child : entries_) {
            if (!child->module().reflection.pushConstantSizeBytes)
                continue;
            const auto& memory = child->targetMemory_;
            const auto& facts = memory.pushFacts();
            require(memory.pushSourceRoot() != Graph::absent
                    && memory.pushTargetRoot() != Graph::absent
                    && facts.targetDeclarationBytes == reflection.pushConstantSizeBytes,
                "SHADER-MODULE-TARGET-PUSH-STAGE-SIZE");
            compare(sourceTypes_,
                sourceTypes_.roots()[push.rootIndex].type,
                memory.sourceTypes(),
                memory.sourceTypes().roots()[memory.pushSourceRoot()].type,
                Graph::Comparison::Exact,
                "SHADER-MODULE-TARGET-PUSH-STAGE-BINARY");
            compare(targetTypes_,
                targetTypes_.roots()[t].type,
                memory.targetTypes(),
                memory.targetTypes().roots()[memory.pushTargetRoot()].type,
                Graph::Comparison::MemoryRepresentation,
                "SHADER-MODULE-TARGET-PUSH-STAGE-TARGET");
            ++consumers;
            if (memory.pushDiagnostic() && !targetPushDiagnostic_)
                targetPushDiagnostic_ = memory.pushDiagnostic();
        }
        require(consumers != 0u, "SHADER-MODULE-TARGET-PUSH-STAGE-COVERAGE");
        targetPushFacts_ = {target[t].group,
            target[t].binding,
            push.occupiedExtentBytes,
            target[t].minimumBindingBytes,
            target[t].alignmentBytes};
    } else
        for (const auto& child : entries_)
            require(!child->module().reflection.pushConstantSizeBytes,
                "SHADER-MODULE-TARGET-PUSH-STAGE-COVERAGE");
    for (const auto c : claimed)
        require(c != 0u, "SHADER-MODULE-TARGET-MEMORY-EXTRA-DECLARATION");
    targetMemoryDiagnostic_ = nullptr;
    return nullptr;
}

std::shared_ptr<const GraphicsShaderModuleArtifact> GraphicsShaderModuleArtifact::load(
    const std::filesystem::path& path,
    const GraphicsShaderModuleLoadBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    using Y = core::YamlContract;
    diagnostic.clear();
    try {
        requireShaderArtifact(budget.maxFileBytes && budget.maxTotalInputBytes
                && budget.maxCpuMetadataBytes && budget.maxReferencePathBytes
                && budget.maxReferenceDepth,
            "SHADER-MODULE-LOAD-BUDGET");
        requireShaderArtifact(
            quota && quota->limitBytes() == budget.maxCpuMetadataBytes, "SHADER-MODULE-CPU-QUOTA");
        requireShaderArtifact(!budget.targetMemory || budget.targetMemory->contractVersion == 1u
                || budget.targetMemory->contractVersion == 2u,
            "SHADER-MODULE-TARGET-MEMORY-REQUEST-VERSION");
        requireShaderArtifact(!budget.runtimeLength
                || (budget.targetMemory && budget.runtimeLength->contractVersion == 1u
                    && budget.runtimeLength->vertex.contractVersion == 1u
                    && budget.runtimeLength->fragment.contractVersion == 1u
                    && budget.runtimeLength->combined.contractVersion == 1u),
            "SHADER-MODULE-RUNTIME-LENGTH-REQUEST");
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeUnique<GraphicsShaderModuleArtifact>(ConstructionKey{}, budget);
        ShaderArtifactInput input(budget.maxFileBytes, budget.maxTotalInputBytes);
        result->publication_ = input.read(path);
        result->identity_ = core::Sha256::digest(shaderArtifactText(result->publication_));
        const auto source = shaderArtifactText(result->publication_);
        const auto root = Y::Node::deserialize(source.begin(), source.end());
        const auto version = Y::uint32(root, "contractVersion");
        const bool unavailableMetal = version == 2u;
        requireShaderArtifact(Y::text(root, "type") == "GraphicsShaderModuleArtifact"
                && (version == 1u || (version == 2u && root.size() == 5u))
                && Y::text(root, "matrixLayout") == "row-major",
            "SHADER-MODULE-PUBLICATION-CONTRACT");
        requireShaderArtifact(!unavailableMetal || budget.runtimeLength,
            "SHADER-MODULE-RUNTIME-LENGTH-REQUEST-REQUIRED");
        result->files_.load(
            Y::required(root, "files"), path.parent_path(), unavailableMetal ? 3u : 1u, input);
        const auto reflectionText =
            shaderArtifactText(result->bytes(ShaderArtifactRole::Reflection));
        const auto reflection = Y::Node::deserialize(reflectionText.begin(), reflectionText.end());
        if (unavailableMetal)
            ShaderArtifactReflectionContract::validateUnavailableMetalRuntimeLength(reflection);
        requireShaderArtifact(core::Sha256::matchesHexDigest(
                                  shaderArtifactText(result->bytes(ShaderArtifactRole::Source)),
                                  Y::text(reflection, "sourceSha256")),
            "SHADER-MODULE-SOURCE-HASH");
        ShaderArtifactReflectionContract::validateBindings(
            reflection, budget.maxModuleBindings, 3u, false);
        ShaderArtifactReflectionContract::validatePhysicalTables(
            reflection, budget.maxModuleBindings, true);
        const auto& reflectedEntries = Y::sequence(reflection, "entryPoints");
        const auto& selected = Y::sequence(root, "entries");
        requireShaderArtifact(
            selected.size() == 2u && reflectedEntries.size() == 2u, "SHADER-MODULE-ENTRY-COVERAGE");
        constexpr std::array<core::StringView, 2> stages{"Vertex", "Fragment"};
        std::array<core::pmr::String, 2> references{
            core::pmr::String(&core::defaultCpuMemoryResource()),
            core::pmr::String(&core::defaultCpuMemoryResource())};
        for (std::size_t i = 0u; i < stages.size(); ++i) {
            const auto& item = selected[i];
            const auto entry = Y::text(item, "entry");
            requireShaderArtifact(
                !entry.empty() && Y::text(item, "stage") == stages[i], "SHADER-MODULE-ENTRY-STAGE");
            std::size_t matches = 0u;
            for (const auto& reflected : reflectedEntries) {
                if (Y::text(reflected, "name") == entry
                    && Y::text(reflected, "stage") == stages[i]) {
                    Y::boolean(reflected, "writesRenderTargetArrayIndex");
                    ++matches;
                }
            }
            requireShaderArtifact(matches == 1u, "SHADER-MODULE-REFLECTION-ENTRY");
            const auto& reference = Y::required(item, "publication");
            references[i] = Y::text(reference, "path");
            constexpr core::StringView publicationSuffix = ".entry.yaml";
            requireShaderArtifact(references[i].size() > publicationSuffix.size()
                    && references[i].size() <= budget.maxReferencePathBytes
                    && core::isRelativeArtifactPath(references[i], budget.maxReferenceDepth)
                    && core::StringView(references[i])
                            .substr(references[i].size() - publicationSuffix.size())
                        == publicationSuffix,
                "SHADER-MODULE-REFERENCE-PATH");
            for (std::size_t j = 0u; j < i; ++j)
                requireShaderArtifact(
                    references[i] != references[j], "SHADER-MODULE-REFERENCE-ALIAS");
            requireShaderArtifact(input.remaining() != 0u, "SHADER-ENTRY-INPUT-BUDGET");
            const SingleEntryShaderLoadBudget childBudget{budget.maxFileBytes,
                input.remaining(),
                budget.maxCpuMetadataBytes,
                budget.maxEntryBindings,
                budget.targetMemory
                    ? (i == 0u ? &budget.targetMemory->vertex : &budget.targetMemory->fragment)
                    : nullptr,
                budget.runtimeLength
                    ? (i == 0u ? &budget.runtimeLength->vertex : &budget.runtimeLength->fragment)
                    : nullptr};
            core::pmr::String childDiagnostic{&core::defaultCpuMemoryResource()};
            auto child = SingleEntryShaderArtifact::load(path.parent_path() / references[i].c_str(),
                childBudget,
                result->quota(),
                childDiagnostic);
            if (!child)
                throw ChildLoadError{std::move(childDiagnostic)};
            requireShaderArtifact(!SingleEntryShaderArtifact::validateOriginal(child),
                "SHADER-MODULE-ORIGINAL-CHILD-OWNER");
            if (unavailableMetal) {
                const auto childRoot =
                    Y::Node::deserialize(shaderArtifactText(child->publicationBytes()).begin(),
                        shaderArtifactText(child->publicationBytes()).end());
                requireShaderArtifact(Y::uint32(childRoot, "contractVersion") == 5u,
                    "SHADER-MODULE-RUNTIME-LENGTH-CHILD-VERSION");
            }
            input.debit(child->inputBytes());
            requireShaderArtifact(
                core::Sha256::matchesRawHexDigest(child->identity(), Y::text(reference, "sha256")),
                "SHADER-MODULE-REFERENCE-HASH");
            requireShaderArtifact(child->entry() == entry
                    && child->stage() == (i == 0u ? ShaderStage::Vertex : ShaderStage::Fragment),
                "SHADER-MODULE-REFERENCE-ENTRY");
            requireShaderArtifact(child->bytes(ShaderArtifactRole::Source)
                    == result->bytes(ShaderArtifactRole::Source),
                "SHADER-MODULE-REFERENCE-SOURCE");
            result->entries_[i] = std::move(child);
        }
        auto parsed = ShaderReflectionCache::loadFromYaml(
            core::pmr::String(reflectionText, &core::defaultCpuMemoryResource()));
        requireShaderArtifact(parsed.success
                && ShaderPipelineReflectionValidator::validateGeneratedArtifact(
                    parsed.module.reflection)
                    .success,
            "SHADER-MODULE-REFLECTION-CONTRACT");
        const auto& facts = parsed.module.reflection;
        requireShaderArtifact(facts.bindlessContractVersion.empty()
                && facts.bindlessExecution.empty() && facts.bindlessLoweringSha256.empty(),
            "SHADER-MODULE-BINDLESS-UNAUTHORIZED");
        const std::array<core::StringView, 4> targetPaths{parsed.module.sourcePath,
            parsed.module.spirvPath,
            parsed.module.metalPath,
            parsed.module.wgslPath};
        for (std::size_t i = 0u; i < targetPaths.size(); ++i)
            requireShaderArtifact(core::artifactFilename(targetPaths[i]) == result->files_[i].name,
                "SHADER-MODULE-REFLECTION-FILE");
        result->module_ = std::move(parsed.module);
        if (budget.targetMemory)
            if (const auto* id = result->buildTargetMemory(budget, *budget.targetMemory))
                throw ShaderArtifactLoadError{id};
        if (budget.runtimeLength)
            result->buildRuntimeLength(budget, *budget.runtimeLength, unavailableMetal);
        result->inputBytes_ = input.consumed();
        return shareOriginal(std::move(result));
    } catch (const ChildLoadError& error) {
        diagnostic = error.diagnostic;
    } catch (const ShaderArtifactLoadError& error) {
        diagnostic = error.diagnostic;
    } catch (const core::YamlContractError& error) {
        diagnostic = error.diagnostic;
    } catch (const std::bad_alloc&) {
        diagnostic = "SHADER-MODULE-CPU-ALLOCATION";
    } catch (const std::exception&) {
        diagnostic = "SHADER-MODULE-YAML-INVALID";
    }
    return {};
}
} // namespace arp::shader
