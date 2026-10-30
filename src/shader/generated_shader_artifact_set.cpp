#include "shader/generated_shader_artifact_set.h"

#include "shader/shader_artifact_reflection_contract.h"
#include "shader/shader_pipeline_reflection_validator.h"

#include <cstring>

namespace arp::shader {
namespace {
using Y = core::YamlContract;
constexpr std::array names{"source", "spirv", "metal", "wgsl", "reflection"};
constexpr std::array<core::StringView, 5> suffixes{
    ".slang", ".spv", ".metal", ".wgsl", ".reflection.yaml"};

// ShaderReflection/3 stage bits, independent of backend/native enum values.
constexpr std::array<core::StringView, 5> stageNames{
    "Vertex", "Fragment", "Compute", "TessellationControl", "TessellationEvaluation"};
constexpr std::array<std::uint32_t, 5> stageBits{1u, 2u, 4u, 8u, 16u};

const char* unavailableDiagnostic(ShaderArtifactRole role, core::StringView reason) noexcept
{
    if (role == ShaderArtifactRole::Metal)
        return reason == "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED"
            ? "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED"
            : nullptr;
    if (role != ShaderArtifactRole::Wgsl)
        return nullptr;
    for (const auto* id : {"SHADER-WGSL-BUFFER-ARRAY-UNSUPPORTED",
             "SHADER-WGSL-BUFFER-ARRAY-ABI-UNAVAILABLE",
             "SHADER-WGSL-GRAPHICS-ATOMIC-UNSUPPORTED"})
        if (reason == id)
            return id;
    return reason == "WGSL compilation failed" ? "SHADER-GENERATED-WGSL-UNAVAILABLE" : nullptr;
}

void validateText(const core::ByteBuffer& bytes)
{
    core::TextDecodeDesc desc;
    desc.bom = core::TextBomPolicy::Strip;
    desc.nul = core::TextNulPolicy::Reject;
    desc.maxOutputBytes = bytes.size();
    const auto text = core::TextDecoder::decode(shaderArtifactText(bytes), desc);
    if (text.status == core::TextDecodeStatus::AllocationFailed)
        throw std::bad_alloc();
    requireShaderArtifact(text.status == core::TextDecodeStatus::Success, text.diagnosticId);
}
} // namespace

struct GeneratedShaderArtifactSet::Prepared {
    std::array<ShaderArtifactFiles::File, 5> files;

    struct Binding {
        core::pmr::String path{&core::defaultCpuMemoryResource()};
    };

    std::array<Binding, 5> bindings;
    core::ByteBuffer publication{&core::defaultCpuMemoryResource()};
    core::ByteBuffer identity{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> words{&core::defaultCpuMemoryResource()};
    ShaderReflectionModule module;
    std::uint64_t inputBytes = 0;
};

GeneratedShaderArtifactSet::GeneratedShaderArtifactSet(ConstructionKey)
    : prepared_(core::makeUnique<Prepared>())
{}

GeneratedShaderArtifactSet::~GeneratedShaderArtifactSet() = default;

const char* GeneratedShaderArtifactSet::validateOriginal(
    const core::SharedPtr<const GeneratedShaderArtifactSet>& self) noexcept
{
    return self && self->hasQuotaBoundSharedOwner(self) ? nullptr
                                                        : "SHADER-GENERATED-ORIGINAL-OWNER";
}

core::SharedPtr<const GeneratedShaderArtifactSet> GeneratedShaderArtifactSet::load(
    const std::filesystem::path& path,
    const GeneratedShaderArtifactSetBudget& budget,
    core::SharedPtr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    try {
        requireShaderArtifact(budget.maxFileBytes && budget.maxFileBytes < SIZE_MAX
                && budget.maxTotalBytes && budget.maxTotalBytes < SIZE_MAX
                && budget.maxCpuMetadataBytes && budget.maxCpuMetadataBytes < SIZE_MAX
                && budget.maxBindings && budget.maxEntryPoints,
            "SHADER-GENERATED-LOAD-BUDGET");
        requireShaderArtifact(quota && quota->limitBytes() == budget.maxCpuMetadataBytes,
            "SHADER-GENERATED-CPU-QUOTA");
        core::CpuMemoryQuotaScope scope(quota);
        auto owner = core::makeUnique<GeneratedShaderArtifactSet>(ConstructionKey{});
        auto& output = *owner->prepared_;
        ShaderArtifactInput input(budget.maxFileBytes, budget.maxTotalBytes, budget.source);
        output.publication = input.read(path);
        output.identity = core::Sha256::digest(shaderArtifactText(output.publication));
        const auto root = shaderArtifactYaml(shaderArtifactText(output.publication));
        requireShaderArtifact(root.is_mapping() && root.size() == 5
                && Y::text(root, "type") == "GeneratedShaderArtifactSet"
                && Y::uint32(root, "contractVersion") == 1
                && Y::text(root, "matrixLayout") == "row-major",
            "SHADER-GENERATED-PUBLICATION-CONTRACT");
        const auto& files = Y::required(root, "files");
        const auto& bindings = Y::required(root, "reflectionBindings");
        requireShaderArtifact(files.is_mapping() && files.size() == names.size()
                && bindings.is_mapping() && bindings.size() == names.size() - 1,
            "SHADER-GENERATED-ROLE-COVERAGE");
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto& node = Y::required(files, names[i]);
            auto& file = output.files[i];
            if (node.is_mapping() && node.contains("status")) {
                const auto reason = Y::text(node, "reason");
                const auto* id = unavailableDiagnostic(static_cast<ShaderArtifactRole>(i), reason);
                requireShaderArtifact(node.size() == 3 && id
                        && Y::text(node, "status") == "DiagnosticFailure"
                        && Y::text(node, "diagnosticId") == id,
                    "SHADER-GENERATED-UNAVAILABLE-CONTRACT");
                file.diagnostic = id;
            } else {
                file = ShaderArtifactFiles::loadAvailableFile(
                    node, path.parent_path(), input, suffixes[i]);
                for (std::size_t j = 0; j < i; ++j)
                    requireShaderArtifact(
                        file.name != output.files[j].name, "SHADER-GENERATED-FILE-ALIAS");
                if (i != static_cast<std::size_t>(ShaderArtifactRole::Spirv))
                    validateText(file.bytes);
            }
        }
        const auto reflection = shaderArtifactYaml(shaderArtifactText(output.files[4].bytes));
        const auto& targets = Y::required(reflection, "targets");
        for (std::size_t i = 0; i < names.size() - 1; ++i) {
            const auto& reflected =
                i == 0 ? Y::required(reflection, "sourcePath") : Y::required(targets, names[i]);
            const auto& binding = Y::required(bindings, names[i]);
            if (!output.files[i].diagnostic.empty()) {
                requireShaderArtifact(binding.is_null() && reflected.is_null(),
                    "SHADER-GENERATED-UNAVAILABLE-BINDING");
                const auto* field = i == 2 ? "metalBindingSpaceUnavailableReason"
                                           : "webGpuBindingSpaceUnavailableReason";
                requireShaderArtifact(
                    Y::text(reflection, field) == Y::text(Y::required(files, names[i]), "reason"),
                    "SHADER-GENERATED-UNAVAILABLE-REASON");
                requireShaderArtifact(
                    Y::text(
                        reflection, i == 2 ? "metalBindingSpaceStatus" : "webGpuBindingSpaceStatus")
                        == "Unavailable",
                    "SHADER-GENERATED-UNAVAILABLE-STATUS");
                if (i == 2)
                    ShaderArtifactReflectionContract::validateUnavailableMetalRuntimeLength(
                        reflection);
            } else {
                output.bindings[i].path = Y::text(bindings, names[i]);
                requireShaderArtifact(!output.bindings[i].path.empty()
                        && output.bindings[i].path.find('\0') == core::pmr::String::npos
                        && Y::text(reflected) == output.bindings[i].path,
                    "SHADER-GENERATED-REFLECTION-BINDING");
            }
        }
        requireShaderArtifact(
            core::Sha256::matchesHexDigest(
                shaderArtifactText(output.files[0].bytes), Y::text(reflection, "sourceSha256")),
            "SHADER-GENERATED-SOURCE-HASH");
        const auto& entries = Y::sequence(reflection, "entryPoints");
        requireShaderArtifact(!entries.empty() && entries.size() <= budget.maxEntryPoints,
            "SHADER-GENERATED-ENTRY-BUDGET");
        std::uint32_t selectedStages = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto name = Y::text(entries[i], "name"), stage = Y::text(entries[i], "stage");
            const auto found = std::find(stageNames.begin(), stageNames.end(), stage);
            requireShaderArtifact(!name.empty() && name.find('\0') == core::pmr::String::npos
                    && found != stageNames.end(),
                "SHADER-GENERATED-ENTRY");
            selectedStages |= stageBits[static_cast<std::size_t>(found - stageNames.begin())];
            for (std::size_t j = 0; j < i; ++j)
                requireShaderArtifact(
                    name != Y::text(entries[j], "name") || stage != Y::text(entries[j], "stage"),
                    "SHADER-GENERATED-ENTRY-DUPLICATE");
        }
        ShaderArtifactReflectionContract::validateBindings(
            reflection, budget.maxBindings, selectedStages, false);
        ShaderArtifactReflectionContract::validatePhysicalTables(
            reflection, budget.maxBindings, true);
        auto parsed = ShaderReflectionCache::loadFromYaml(
            core::pmr::String(shaderArtifactText(output.files[4].bytes)));
        requireShaderArtifact(parsed.success
                && ShaderPipelineReflectionValidator::validateGeneratedArtifact(
                    parsed.module.reflection)
                    .success,
            "SHADER-GENERATED-REFLECTION-CONTRACT");
        requireShaderArtifact(parsed.module.reflection.bindlessContractVersion.empty()
                && parsed.module.reflection.bindlessExecution.empty()
                && parsed.module.reflection.bindlessLoweringSha256.empty(),
            "SHADER-GENERATED-BINDLESS-UNAUTHORIZED");
        output.module = std::move(parsed.module);
        const auto& spirv = output.files[1].bytes;
        requireShaderArtifact(
            spirv.size() >= 5 * sizeof(std::uint32_t) && spirv.size() % sizeof(std::uint32_t) == 0,
            "SHADER-GENERATED-SPIRV-SIZE");
        output.words.resize(spirv.size() / sizeof(std::uint32_t));
        std::memcpy(output.words.data(), spirv.data(), spirv.size());
        requireShaderArtifact(output.words[0] == 0x07230203u, "SHADER-GENERATED-SPIRV-MAGIC");
        output.inputBytes = input.consumed();
        return shareOriginal(std::move(owner));
    } catch (const ShaderArtifactLoadError& error) {
        diagnostic = error.diagnostic;
    } catch (const core::YamlContractError& error) {
        diagnostic = error.diagnostic;
    } catch (const std::bad_alloc&) {
        diagnostic = "SHADER-GENERATED-CPU-ALLOCATION";
    } catch (const std::exception&) {
        diagnostic = "SHADER-GENERATED-YAML-INVALID";
    }
    return {};
}

const core::ByteBuffer& GeneratedShaderArtifactSet::bytes(ShaderArtifactRole role) const
{
    return prepared_->files.at(static_cast<std::size_t>(role)).bytes;
}

const core::pmr::String& GeneratedShaderArtifactSet::filename(ShaderArtifactRole role) const
{
    return prepared_->files.at(static_cast<std::size_t>(role)).name;
}

const core::pmr::String& GeneratedShaderArtifactSet::targetDiagnostic(ShaderArtifactRole role) const
{
    return prepared_->files.at(static_cast<std::size_t>(role)).diagnostic;
}

const core::pmr::String& GeneratedShaderArtifactSet::reflectedPath(ShaderArtifactRole role) const
{
    return prepared_->bindings.at(static_cast<std::size_t>(role)).path;
}

const core::pmr::Vector<std::uint32_t>& GeneratedShaderArtifactSet::spirvWords() const noexcept
{
    return prepared_->words;
}

const ShaderReflectionModule& GeneratedShaderArtifactSet::module() const noexcept
{
    return prepared_->module;
}

const core::ByteBuffer& GeneratedShaderArtifactSet::publicationBytes() const noexcept
{
    return prepared_->publication;
}

const core::ByteBuffer& GeneratedShaderArtifactSet::identity() const noexcept
{
    return prepared_->identity;
}

std::uint64_t GeneratedShaderArtifactSet::inputBytes() const noexcept
{
    return prepared_->inputBytes;
}

} // namespace arp::shader
