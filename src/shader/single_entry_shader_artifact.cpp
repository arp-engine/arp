#include "shader/single_entry_shader_artifact.h"

#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/io/artifact_path.h"
#include "core/io/file_io.h"
#include "shader/fragment_atomic_write_proof.h"
#include "shader/shader_artifact_reflection_contract.h"
#include "shader/shader_pipeline_reflection_validator.h"

#include <algorithm>

namespace arp::shader {
namespace {
using Y = core::YamlContract;
using Rejection = ShaderArtifactLoadError;

void require(bool valid, const char* diagnostic)
{
    if (!valid)
        throw Rejection{diagnostic};
}

core::StringView text(const core::ByteBuffer& bytes) noexcept
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void validateBufferDeclarations(const Y::Node& section,
    const core::ByteBuffer& binary,
    const ShaderReflection& reflection,
    std::uint32_t maxBindings,
    core::pmr::Vector<SpirvBufferDeclarationLayout>& output)
{
    require(section.is_mapping() && section.size() == 5u
            && Y::text(section, "type") == "SpirvBufferDeclarationLayouts"
            && Y::uint32(section, "contractVersion") == 1u
            && Y::text(section, "status") == "DeclarationOnly",
        "SHADER-ENTRY-BUFFER-LAYOUT-CONTRACT");
    require(core::Sha256::matchesHexDigest(text(binary), Y::text(section, "spirvSha256")),
        "SHADER-ENTRY-BUFFER-LAYOUT-SPIRV-HASH");
    const auto& records = Y::sequence(section, "bindings");
    require(records.size() <= maxBindings, "SHADER-ENTRY-BUFFER-LAYOUT-BINDING-BUDGET");
    if (const auto* id = SpirvBufferDeclarationLayoutVerifier::verify(
            binary.data(), binary.size(), maxBindings, output))
        throw Rejection{id};
    require(records.size() == output.size(), "SHADER-ENTRY-BUFFER-LAYOUT-COVERAGE");
    for (std::size_t index = 0u; index < records.size(); ++index) {
        const auto& record = records[index];
        require(record.is_mapping() && record.size() == 11u, "SHADER-ENTRY-BUFFER-LAYOUT-RECORD");
        SpirvBufferDeclarationLayout facts;
        facts.set = Y::uint32(record, "set");
        facts.binding = Y::uint32(record, "binding");
        facts.spirvId = Y::uint32(record, "spirvId");
        facts.blockTypeId = Y::uint32(record, "blockTypeId");
        facts.storageClass = Y::uint32(record, "storageClass");
        facts.descriptorCount = Y::hex64(record, "descriptorCountHex");
        facts.staticOccupiedExtentBytes = Y::hex64(record, "staticOccupiedExtentBytesHex");
        facts.hasRuntimeArray = Y::boolean(record, "hasRuntimeArray");
        facts.runtimeArrayOffsetBytes = Y::hex64(record, "runtimeArrayOffsetBytesHex");
        facts.runtimeArrayStrideBytes = Y::hex64(record, "runtimeArrayStrideBytesHex");
        facts.runtimeElementOccupiedExtentBytes =
            Y::hex64(record, "runtimeElementOccupiedExtentBytesHex");
        for (std::size_t previous = 0u; previous < index; ++previous)
            require(facts.set != Y::uint32(records[previous], "set")
                    || facts.binding != Y::uint32(records[previous], "binding"),
                "SHADER-ENTRY-BUFFER-LAYOUT-DUPLICATE");
        const auto found = std::find_if(output.begin(), output.end(), [&](const auto& actual) {
            return actual.set == facts.set && actual.binding == facts.binding;
        });
        require(found != output.end() && found->matches(facts),
            "SHADER-ENTRY-BUFFER-LAYOUT-BINARY-MISMATCH");
        const auto binding = std::find_if(
            reflection.bindings.begin(), reflection.bindings.end(), [&](const auto& value) {
                return value.set == facts.set && value.binding == facts.binding;
            });
        require(binding != reflection.bindings.end() && binding->arrayCount == facts.descriptorCount
                && ((facts.storageClass == 2u && binding->type == BindingType::UniformBuffer)
                    || (facts.storageClass == 12u
                        && (binding->type == BindingType::StorageBuffer
                            || binding->type == BindingType::ReadOnlyStorageBuffer))),
            "SHADER-ENTRY-BUFFER-LAYOUT-REFLECTION-MISMATCH");
    }
    const auto bufferCount = std::count_if(
        reflection.bindings.begin(), reflection.bindings.end(), [](const auto& binding) {
            return binding.type == BindingType::UniformBuffer
                || binding.type == BindingType::StorageBuffer
                || binding.type == BindingType::ReadOnlyStorageBuffer;
        });
    require(static_cast<std::size_t>(bufferCount) == output.size(),
        "SHADER-ENTRY-BUFFER-LAYOUT-REFLECTION-COVERAGE");
}
} // namespace

std::shared_ptr<const SingleEntryShaderArtifact> SingleEntryShaderArtifact::load(
    const std::filesystem::path& publication,
    const SingleEntryShaderLoadBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    try {
        require(budget.maxFileBytes && budget.maxTotalBytes && budget.maxCpuMetadataBytes,
            "SHADER-ENTRY-LOAD-BUDGET");
        require(quota && quota->limitBytes() == budget.maxCpuMetadataBytes,
            "SHADER-ENTRY-CPU-QUOTA-MISMATCH");
        require(!budget.targetMemory
                || TargetBufferMemoryLoadRequest::supportsVersion(
                    budget.targetMemory->contractVersion),
            "SHADER-ENTRY-TARGET-MEMORY-REQUEST-VERSION");
        require(!budget.runtimeLength
                || (budget.runtimeLength->contractVersion == 1u && budget.targetMemory),
            "SHADER-ENTRY-RUNTIME-LENGTH-REQUEST");
        if (budget.nativeMemory)
            if (const auto* id = NativeBufferMemoryDeclarations::validateRequest(
                    *budget.nativeMemory, budget.maxBindings))
                throw Rejection{id};
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeUnique<SingleEntryShaderArtifact>(ConstructionKey{}, budget);
        ShaderArtifactInput input(budget.maxFileBytes, budget.maxTotalBytes);
        result->publication_ = input.read(publication);
        result->identity_ = core::Sha256::digest(text(result->publication_));
        const auto publicationText = text(result->publication_);
        const auto root = Y::Node::deserialize(publicationText.begin(), publicationText.end());
        const auto version = Y::uint32(root, "contractVersion");
        require(Y::text(root, "type") == "SingleEntryShaderArtifact" && version >= 1u
                && version <= 5u && root.size() == (version >= 3u ? 7u : 6u)
                && Y::text(root, "matrixLayout") == "row-major",
            "SHADER-ENTRY-PUBLICATION-CONTRACT");
        const bool unavailableWgsl = version == 2u || version == 4u;
        const bool unavailableMetal = version == 5u;
        require(!unavailableMetal || budget.runtimeLength,
            "SHADER-ENTRY-RUNTIME-LENGTH-REQUEST-REQUIRED");
        result->entry_ = Y::text(root, "entry");
        require(!result->entry_.empty(), "SHADER-ENTRY-NAME-MISSING");
        const auto stage = Y::text(root, "stage");
        const std::uint32_t mask = stage == "Vertex" ? 1u
            : stage == "Fragment"                    ? 2u
            : stage == "Compute"                     ? 4u
                                                     : 0u;
        require(mask != 0u, "SHADER-ENTRY-STAGE-INVALID");
        result->stage_ = mask == 1u ? ShaderStage::Vertex
            : mask == 2u            ? ShaderStage::Fragment
                                    : ShaderStage::Compute;
        result->files_.load(Y::required(root, "files"),
            publication.parent_path(),
            unavailableMetal      ? 3u
                : unavailableWgsl ? 2u
                                  : 1u,
            input);
        result->inputBytes_ = input.consumed();
        const auto reflectionBytes = text(result->files_[4].bytes);
        const auto reflectionNode =
            Y::Node::deserialize(reflectionBytes.begin(), reflectionBytes.end());
        if (unavailableMetal)
            ShaderArtifactReflectionContract::validateUnavailableMetalRuntimeLength(reflectionNode);
        const auto& entries = Y::sequence(reflectionNode, "entryPoints");
        require(entries.size() == 1u && Y::text(entries[0], "name") == result->entry_
                && Y::text(entries[0], "stage") == stage,
            "SHADER-ENTRY-REFLECTION-AMBIGUOUS");
        Y::boolean(entries[0], "writesRenderTargetArrayIndex");
        require(Y::text(reflectionNode, "vertexInputContractVersion") == "VertexInputInterface/1",
            "SHADER-ENTRY-VERTEX-INPUT-CONTRACT");
        const auto& vertexInputs = Y::sequence(reflectionNode, "vertexAttributes");
        require(mask == 1u || vertexInputs.empty(), "SHADER-ENTRY-VERTEX-INPUT-STAGE");
        for (std::size_t i = 0; i < vertexInputs.size(); ++i) {
            const auto& attribute = vertexInputs[i];
            const auto format = Y::text(attribute, "format");
            require(
                format == "Float" || format == "Float2" || format == "Float3" || format == "Float4",
                "SHADER-ENTRY-VERTEX-INPUT-FORMAT");
            require(!Y::text(attribute, "name").empty() && !Y::text(attribute, "semantic").empty(),
                "SHADER-ENTRY-VERTEX-INPUT-NAME");
            const auto location = Y::uint32(attribute, "location");
            require(location != UINT32_MAX, "SHADER-ENTRY-VERTEX-INPUT-LOCATION");
            for (std::size_t j = 0; j < i; ++j)
                require(location != Y::uint32(vertexInputs[j], "location"),
                    "SHADER-ENTRY-VERTEX-INPUT-DUPLICATE");
        }
        const auto& bindings = Y::sequence(reflectionNode, "bindings");
        ShaderArtifactReflectionContract::validateBindings(
            reflectionNode, budget.maxBindings, mask, true);
        if (unavailableWgsl) {
            const auto& targetDiagnostic =
                result->files_[static_cast<std::size_t>(ShaderArtifactRole::Wgsl)].diagnostic;
            const bool graphicsAtomic =
                targetDiagnostic == "SHADER-WGSL-GRAPHICS-ATOMIC-UNSUPPORTED";
            bool bufferArray = false;
            for (const auto& binding : bindings) {
                const auto type = Y::text(binding, "type");
                if ((type == "UniformBuffer" || type == "ReadOnlyStorageBuffer"
                        || type == "StorageBuffer")
                    && Y::uint32(binding, "arrayCount") > 1u)
                    bufferArray = true;
            }
            if (graphicsAtomic) {
                const auto& binary =
                    result->files_[static_cast<std::size_t>(ShaderArtifactRole::Spirv)].bytes;
                FragmentAtomicWriteProof::Facts proof;
                if (const auto* id = FragmentAtomicWriteProof::verify(
                        binary.data(), binary.size(), result->entry_, proof))
                    throw Rejection{id};
                require(mask == 2u && bindings.size() == 1u
                        && Y::text(bindings[0], "type") == "StorageBuffer"
                        && Y::uint32(bindings[0], "arrayCount") == 1u
                        && Y::uint32(bindings[0], "set") == proof.set
                        && Y::uint32(bindings[0], "binding") == proof.binding,
                    "SHADER-ENTRY-GRAPHICS-WRITE-PROOF-BINDING");
            }
            require((graphicsAtomic || bufferArray)
                    && Y::text(reflectionNode, "webGpuBindingSpaceStatus") == "Unavailable"
                    && Y::text(reflectionNode, "webGpuBindingSpaceUnavailableReason")
                        == targetDiagnostic
                    && Y::sequence(reflectionNode, "webGpuBindingSpace").empty()
                    && Y::required(Y::required(reflectionNode, "targets"), "wgsl").is_null(),
                "SHADER-ENTRY-TARGET-DIAGNOSTIC-REFLECTION");
        }
        ShaderArtifactReflectionContract::validatePhysicalTables(
            reflectionNode, budget.maxBindings, version >= 3u);
        auto parsed = ShaderReflectionCache::loadFromYaml(
            core::pmr::String(reflectionBytes, &core::defaultCpuMemoryResource()));
        require(parsed.success, "SHADER-ENTRY-REFLECTION-INVALID");
        result->module_ = std::move(parsed.module);
        const auto& module = result->module_;
        const auto& reflection = module.reflection;
        require(ShaderPipelineReflectionValidator::validateGeneratedArtifact(reflection).success,
            "SHADER-ENTRY-REFLECTION-CONTRACT");
        require(reflection.bindlessContractVersion.empty() && reflection.bindlessExecution.empty()
                && reflection.bindlessLoweringSha256.empty(),
            "SHADER-ENTRY-BINDLESS-UNAUTHORIZED");
        require(reflection.entryPoints.size() == 1u
                && reflection.entryPoints[0].name == result->entry_
                && reflection.entryPoints[0].stage == result->stage_
                && reflection.bindings.size() == bindings.size(),
            "SHADER-ENTRY-REFLECTION-MISMATCH");
        if (result->stage_ == ShaderStage::Compute)
            require(reflection.entryPoints[0].computeWorkgroupSizeX
                    && reflection.entryPoints[0].computeWorkgroupSizeY
                    && reflection.entryPoints[0].computeWorkgroupSizeZ,
                "SHADER-ENTRY-WORKGROUP-MISSING");
        const std::array<core::StringView, 4> paths{
            module.sourcePath, module.spirvPath, module.metalPath, module.wgslPath};
        for (std::size_t i = 0u; i < paths.size(); ++i)
            require(core::artifactFilename(paths[i]) == result->files_[i].name,
                "SHADER-ENTRY-REFLECTION-FILE-MISMATCH");
        require(
            core::Sha256::matchesHexDigest(text(result->files_[0].bytes), reflection.sourceSha256),
            "SHADER-ENTRY-SOURCE-HASH");
        if (version >= 3u) {
            validateBufferDeclarations(Y::required(root, "bufferDeclarationLayout"),
                result->bytes(ShaderArtifactRole::Spirv),
                reflection,
                budget.maxBindings,
                result->bufferDeclarations_);
            result->bufferDeclarationsAvailable_ = true;
        } else
            require(!root.contains("bufferDeclarationLayout"), "SHADER-ENTRY-BUFFER-LAYOUT-LEGACY");
        if (budget.nativeMemory) {
            require(version >= 3u, "SHADER-ENTRY-NATIVE-MEMORY-LEGACY");
            BufferDeclarationTypeLayouts source;
            core::pmr::Vector<SpirvBufferDeclarationLayout> declarations{
                &core::defaultCpuMemoryResource()};
            const auto& binary = result->bytes(ShaderArtifactRole::Spirv);
            if (const auto* id =
                    SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(binary.data(),
                        binary.size(),
                        budget.maxBindings,
                        budget.nativeMemory->sourceTypes,
                        declarations,
                        source))
                throw Rejection{id};
            require(declarations.size() == result->bufferDeclarations_.size()
                    && declarations.size() == budget.nativeMemory->bindingCount,
                "SHADER-ENTRY-NATIVE-MEMORY-COVERAGE");
            for (std::size_t i = 0u; i < declarations.size(); ++i)
                require(declarations[i].matches(result->bufferDeclarations_[i]),
                    "SHADER-ENTRY-NATIVE-MEMORY-BINARY");
            if (const auto* id = result->nativeMemory_.build(
                    std::move(source), *budget.nativeMemory, budget.maxBindings))
                throw Rejection{id};
            for (const auto& declaration : declarations)
                require(result->nativeMemory_.lookup(declaration.set, declaration.binding),
                    "SHADER-ENTRY-NATIVE-MEMORY-COVERAGE");
        }
        if (budget.targetMemory) {
            if (version < 3u)
                result->targetMemory_.unavailable("SHADER-ENTRY-TARGET-MEMORY-LEGACY");
            else if (unavailableWgsl)
                result->targetMemory_.unavailable(
                    result->targetDiagnostic(ShaderArtifactRole::Wgsl).c_str());
            else if (const auto* id =
                         result->targetMemory_.build(result->bytes(ShaderArtifactRole::Spirv),
                             text(result->bytes(ShaderArtifactRole::Wgsl)),
                             reflection,
                             result->bufferDeclarations_,
                             budget.maxBindings,
                             *budget.targetMemory))
                throw Rejection{id};
        }
        if (budget.runtimeLength) {
            require(version >= 3u && !unavailableWgsl,
                "SHADER-ENTRY-RUNTIME-LENGTH-TARGET-UNAVAILABLE");
            if (const auto* id =
                    RuntimeArrayLengthOperations::verify(result->bytes(ShaderArtifactRole::Spirv),
                        text(result->bytes(ShaderArtifactRole::Wgsl)),
                        reflection,
                        budget.maxBindings,
                        *budget.targetMemory,
                        budget.runtimeLength->operations,
                        result->runtimeLength_))
                throw Rejection{id};
            require(!unavailableMetal || !result->runtimeLength_.empty(),
                "SHADER-ENTRY-METAL-LENGTH-OPERATION-REQUIRED");
            result->runtimeLengthDiagnostic_ = nullptr;
        }
        return shareOriginal(std::move(result));
    } catch (const Rejection& error) {
        diagnostic = error.diagnostic;
    } catch (const core::YamlContractError& error) {
        diagnostic = error.diagnostic;
    } catch (const std::bad_alloc&) {
        diagnostic = "SHADER-ENTRY-CPU-ALLOCATION-FAILED";
    } catch (const std::exception&) {
        diagnostic = "SHADER-ENTRY-YAML-INVALID";
    }
    return {};
}

} // namespace arp::shader
