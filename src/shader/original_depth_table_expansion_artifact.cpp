#include "shader/original_depth_table_expansion_artifact.h"

#include "shader/shader_artifact_reflection_contract.h"
#include "shader/shader_pipeline_reflection_validator.h"
#if defined(ARP_ORIGINAL_DEPTH_TABLE_COMPILER_PROOF)
#include "original-depth-table-expansion/compute/compiler_identity.h"
#include "original-depth-table-expansion/graphics/compiler_identity.h"
#include "original-depth-table-expansion/graphicsgeometry/compiler_identity.h"
#endif

namespace arp::shader {
namespace {
using Y = core::YamlContract;

void require(bool valid, const char* id)
{
    requireShaderArtifact(valid, id);
}

void field(const Y::Node& n, const char* key, core::StringView expected)
{
    require(Y::text(n, key) == expected, "SHADER-ORIGINAL-TABLE-EXPANSION-SHAPE");
}

void number(const Y::Node& n, const char* key, std::uint32_t expected)
{
    require(Y::uint32(n, key) == expected, "SHADER-ORIGINAL-TABLE-EXPANSION-SHAPE");
}
} // namespace

std::shared_ptr<const OriginalDepthTableExpansionArtifact>
OriginalDepthTableExpansionArtifact::load(const std::filesystem::path& path,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    try {
        require(budget.contractVersion == 1u, "SHADER-ORIGINAL-TABLE-EXPANSION-VERSION");
        require(budget.explicitOptIn, "SHADER-ORIGINAL-TABLE-EXPANSION-OPT-IN");
        require(budget.maxFileBytes && budget.maxTotalBytes && budget.maxCpuMetadataBytes,
            "SHADER-ORIGINAL-TABLE-EXPANSION-BUDGET");
        require(quota && quota->limitBytes() == budget.maxCpuMetadataBytes,
            "SHADER-ORIGINAL-TABLE-EXPANSION-QUOTA");
#if !defined(ARP_ORIGINAL_DEPTH_TABLE_COMPILER_PROOF)
        (void)path;
        require(false, "SHADER-ORIGINAL-TABLE-EXPANSION-COMPILER-PROOF-UNAVAILABLE");
#else
        core::CpuMemoryQuotaScope scope(quota);
        auto object =
            core::makeUnique<OriginalDepthTableExpansionArtifact>(ConstructionKey{}, budget);
        ShaderArtifactInput input(budget.maxFileBytes, budget.maxTotalBytes, budget.source);
        object->publication_ = input.read(path);
        object->identity_ = core::Sha256::digest(shaderArtifactText(object->publication_));
        const auto text = shaderArtifactText(object->publication_);
        const auto root = shaderArtifactYaml(text);
        // The compiler publication's exact fields are a closed schema.
        constexpr std::array keys{"type",
            "contractVersion",
            "schema",
            "matrixLayout",
            "executionAvailable",
            "originalSourceSha256",
            "generatedSourceSha256",
            "logicalTable",
            "elementMap",
            "physicalBindingCount",
            "switchCases",
            "backendDecisions",
            "compilerVersion",
            "originalSource",
            "generatedSource",
            "entries",
            "artifactBytes"};
        require(root.is_mapping() && root.size() == keys.size(),
            "SHADER-ORIGINAL-TABLE-EXPANSION-PUBLICATION");
        for (const auto* key : keys)
            Y::required(root, key);
        field(root, "type", "OriginalDepthTableScalarExpansion");
        number(root, "contractVersion", 1u);
        field(root, "matrixLayout", "row-major");
        require(!Y::boolean(root, "executionAvailable"),
            "SHADER-ORIGINAL-TABLE-EXPANSION-EXECUTION-PROMOTION");
        const auto& schema = Y::required(root, "schema");
        require(
            schema.is_mapping() && schema.size() == 8u, "SHADER-ORIGINAL-TABLE-EXPANSION-SCHEMA");
        field(schema, "type", "OriginalDepthTableScalarExpansion");
        number(schema, "contractVersion", 1u);
        number(schema, "capacity", 3u);
        const auto profile = Y::text(schema, "profile");
        require(profile == "Compute" || profile == "Graphics" || profile == "GraphicsGeometry",
            "SHADER-ORIGINAL-TABLE-EXPANSION-PROFILE");
        const bool compute = profile == "Compute";
        object->profile_ = compute ? Profile::Compute : Profile::Graphics;
        const bool geometry = profile == "GraphicsGeometry";
        object->vertexInputContractVersion_ = geometry ? 1u : 0u;
        const std::uint32_t logicalCount = compute ? 2u : 1u, physicalCount = compute ? 4u : 3u,
                            entryCount = compute ? 1u : 2u;
        require(budget.maxLogicalBindings >= logicalCount
                && budget.maxPhysicalBindings >= physicalCount && budget.maxElements >= 3u
                && budget.maxEntries >= entryCount,
            "SHADER-ORIGINAL-TABLE-EXPANSION-COUNT-BUDGET");
        require(Y::uint32(schema, "maxPhysicalBindings") >= physicalCount
                && Y::uint32(schema, "maxSwitchCases") >= 3u
                && Y::uint32(schema, "maxArtifactBytes"),
            "SHADER-ORIGINAL-TABLE-EXPANSION-AUTHORED-BUDGET");
        number(root, "physicalBindingCount", physicalCount);
        number(root, "switchCases", 3u);
        const core::StringView originalSha = compute ? compiler_contract::computeOriginalSha256
            : geometry ? compiler_contract::graphicsgeometry::graphicsgeometryOriginalSha256
                       : compiler_contract::graphicsOriginalSha256;
        const core::StringView generatedSha = compute ? compiler_contract::computeGeneratedSha256
            : geometry ? compiler_contract::graphicsgeometry::graphicsgeometryGeneratedSha256
                       : compiler_contract::graphicsGeneratedSha256;
        require(Y::text(root, "originalSourceSha256") == originalSha
                && Y::text(schema, "sourceSha256") == originalSha
                && Y::text(root, "generatedSourceSha256") == generatedSha,
            "SHADER-ORIGINAL-TABLE-EXPANSION-SOURCE-PROVENANCE");
        require(core::Sha256::matchesHexDigest(Y::text(root, "compilerVersion"),
                    compute ? compiler_contract::computeCompilerVersionSha256
                        : geometry
                        ? compiler_contract::graphicsgeometry::graphicsgeometryCompilerVersionSha256
                        : compiler_contract::graphicsCompilerVersionSha256),
            "SHADER-ORIGINAL-TABLE-EXPANSION-COMPILER-IDENTITY");
        const auto& decisions = Y::required(root, "backendDecisions");
        // Frozen publication/1 scope. A fifth key (even refusal-only) is not
        // a compatible migration and never supplies a WebGL/ES100 artifact.
        require(decisions.is_mapping() && decisions.size() == 4u,
            "SHADER-ORIGINAL-TABLE-EXPANSION-BACKENDS");
        for (const auto* name : {"Null", "Vulkan", "Metal", "WebGPU"}) {
            const auto& d = Y::required(decisions, name);
            require(d.is_mapping() && d.size() == 3u, "SHADER-ORIGINAL-TABLE-EXPANSION-BACKENDS");
            field(d, "mode", "DiagnosticFailure");
            field(d, "diagnostic", "RHI-ORIGINAL-TYPED-INPUT-TABLE-PROFILE-UNAVAILABLE");
            require(!Y::boolean(d, "executionAvailable"),
                "SHADER-ORIGINAL-TABLE-EXPANSION-EXECUTION-PROMOTION");
        }
        const auto& table = Y::required(root, "logicalTable");
        require(table.is_mapping() && table.size() == 8u,
            "SHADER-ORIGINAL-TABLE-EXPANSION-LOGICAL-TABLE");
        field(table, "name", "Inputs");
        field(table, "type", "SampledTexture");
        field(table, "dimension", "D2");
        field(table, "stage", compute ? "Compute" : "Fragment");
        number(table, "set", 0u);
        number(table, "binding", 0u);
        number(table, "capacity", 3u);
        require(Y::boolean(table, "depthTexture"), "SHADER-ORIGINAL-TABLE-EXPANSION-LOGICAL-TABLE");
        const auto& map = Y::sequence(root, "elementMap");
        require(map.size() == 3u, "SHADER-ORIGINAL-TABLE-EXPANSION-ELEMENT-MAP");
        constexpr std::array<core::StringView, 3> names{"Inputs_0", "Inputs_1", "Inputs_2"};
        for (std::uint32_t i = 0u; i < map.size(); ++i) {
            require(map[i].is_mapping() && map[i].size() == 4u,
                "SHADER-ORIGINAL-TABLE-EXPANSION-ELEMENT-MAP");
            number(map[i], "arrayElement", i);
            number(map[i], "set", 0u);
            number(map[i], "binding", i);
            field(map[i], "name", names[i]);
            auto& e = object->elements_[i];
            e.arrayElement = i;
            e.binding = i;
            e.name = names[i];
        }
        object->original_ = ShaderArtifactFiles::loadAvailableFile(
            Y::required(root, "originalSource"), path.parent_path(), input, ".slang");
        object->generated_ = ShaderArtifactFiles::loadAvailableFile(
            Y::required(root, "generatedSource"), path.parent_path(), input, ".slang");
        require(object->original_.name == "original.slang"
                && object->generated_.name == "expanded.slang"
                && object->original_.sha256 == originalSha
                && object->generated_.sha256 == generatedSha,
            "SHADER-ORIGINAL-TABLE-EXPANSION-SOURCE-PROVENANCE");
        // Complete target/physical reflection bytes match fresh trusted build
        // compiler output. Rehashing edited operations never supplies authority.
        constexpr std::array<core::StringView, 4> computeHashes{
            compiler_contract::computeMainspirvSha256,
            compiler_contract::computeMainmetalSha256,
            compiler_contract::computeMainwgslSha256,
            compiler_contract::computeMainreflectionSha256};
        constexpr std::array<core::StringView, 4> vertexHashes{
            compiler_contract::vertexMainspirvSha256,
            compiler_contract::vertexMainmetalSha256,
            compiler_contract::vertexMainwgslSha256,
            compiler_contract::vertexMainreflectionSha256};
        constexpr std::array<core::StringView, 4> fragmentHashes{
            compiler_contract::fragmentMainspirvSha256,
            compiler_contract::fragmentMainmetalSha256,
            compiler_contract::fragmentMainwgslSha256,
            compiler_contract::fragmentMainreflectionSha256};
        constexpr std::array<core::StringView, 4> geometryVertexHashes{
            compiler_contract::graphicsgeometry::vertexMainspirvSha256,
            compiler_contract::graphicsgeometry::vertexMainmetalSha256,
            compiler_contract::graphicsgeometry::vertexMainwgslSha256,
            compiler_contract::graphicsgeometry::vertexMainreflectionSha256};
        constexpr std::array<core::StringView, 4> geometryFragmentHashes{
            compiler_contract::graphicsgeometry::fragmentMainspirvSha256,
            compiler_contract::graphicsgeometry::fragmentMainmetalSha256,
            compiler_contract::graphicsgeometry::fragmentMainwgslSha256,
            compiler_contract::graphicsgeometry::fragmentMainreflectionSha256};
        const auto& entries = Y::sequence(root, "entries");
        require(entries.size() == entryCount, "SHADER-ORIGINAL-TABLE-EXPANSION-ENTRY-COUNT");
        for (std::size_t i = 0u; i < entries.size(); ++i) {
            const auto& node = entries[i];
            require(
                node.is_mapping() && node.size() == 3u, "SHADER-ORIGINAL-TABLE-EXPANSION-ENTRY");
            const core::StringView name = compute ? "computeMain"
                : i                               ? "fragmentMain"
                                                  : "vertexMain";
            const core::StringView stage = compute ? "Compute" : i ? "Fragment" : "Vertex";
            field(node, "entry", name);
            field(node, "stage", stage);
            auto& entry = object->entries_[i];
            entry.files.loadTargets(Y::required(node, "files"), path.parent_path(), input);
            const auto& hashes = compute ? computeHashes
                : geometry               ? (i ? geometryFragmentHashes : geometryVertexHashes)
                                         : (i ? fragmentHashes : vertexHashes);
            for (std::size_t role = 0u; role < hashes.size(); ++role)
                require(entry.files[role + 1u].sha256 == hashes[role],
                    "SHADER-ORIGINAL-TABLE-EXPANSION-TARGET-PROVENANCE");
            const auto reflectionText = shaderArtifactText(entry.files[4u].bytes);
            const auto raw = shaderArtifactYaml(reflectionText);
            ShaderArtifactReflectionContract::validateBindings(raw,
                budget.maxPhysicalBindings,
                compute ? 4u
                    : i ? 2u
                        : 1u,
                true);
            ShaderArtifactReflectionContract::validatePhysicalTables(
                raw, budget.maxPhysicalBindings, true);
            auto parsed = ShaderReflectionCache::loadFromYaml(
                core::pmr::String(reflectionText, &core::defaultCpuMemoryResource()));
            require(parsed.success, "SHADER-ORIGINAL-TABLE-EXPANSION-REFLECTION");
            entry.physical = std::move(parsed.module);
            require(ShaderPipelineReflectionValidator::validateGeneratedArtifact(
                        entry.physical.reflection)
                        .success,
                "SHADER-ORIGINAL-TABLE-EXPANSION-REFLECTION");
        }
        object->inputBytes_ = input.consumed();
        const auto payload = object->inputBytes_ - object->publication_.size();
        require(Y::uint64(Y::required(root, "artifactBytes")) == payload
                && payload <= Y::uint32(schema, "maxArtifactBytes"),
            "SHADER-ORIGINAL-TABLE-EXPANSION-PAYLOAD-BUDGET");
        ShaderBinding binding;
        binding.name = "Inputs";
        binding.type = BindingType::SampledTexture;
        binding.arrayCount = 3u;
        binding.depthTexture = true;
        binding.stageMask = compute ? 4u : 2u;
        object->logical_.push_back(std::move(binding));
        if (compute) {
            ShaderBinding output;
            output.name = "Output";
            output.type = BindingType::StorageBuffer;
            output.set = 1u;
            output.stageMask = 4u;
            object->logical_.push_back(std::move(output));
        }
        return shareOriginal(std::move(object));
#endif
    } catch (const ShaderArtifactLoadError& e) {
        diagnostic = e.diagnostic;
    } catch (const core::YamlContractError& e) {
        diagnostic = e.diagnostic;
    } catch (const std::bad_alloc&) {
        diagnostic = "SHADER-ORIGINAL-TABLE-EXPANSION-CPU-ALLOCATION";
    } catch (const std::exception&) {
        diagnostic = "SHADER-ORIGINAL-TABLE-EXPANSION-YAML";
    }
    return {};
}
} // namespace arp::shader
