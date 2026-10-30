#include "rhi/frontend/artifact_owned_shader_reflection.h"
#include "shader/generated_shader_artifact_set.h"

#include <algorithm>

namespace arp::rhi {
namespace {
using Role = shader::ShaderArtifactRole;

bool pathMatches(
    std::string_view path, std::string_view publication, std::string_view filename) noexcept
{
    if (filename.empty())
        return path.empty();
    const auto slash = publication.find_last_of("/\\");
    const auto parentBytes = slash == std::string_view::npos ? 0 : slash + 1;
    if (path.size() != parentBytes + filename.size() || path.substr(parentBytes) != filename)
        return false;
    for (std::size_t i = 0; i < parentBytes; ++i) {
        const auto a = path[i], b = publication[i];
        if (a != b && !((a == '/' || a == '\\') && (b == '/' || b == '\\')))
            return false;
    }
    return true;
}

ShaderStage stageFor(shader::ShaderStage stage) noexcept
{
    switch (stage) {
    case shader::ShaderStage::Vertex:
        return ShaderStage::Vertex;
    case shader::ShaderStage::Fragment:
        return ShaderStage::Fragment;
    case shader::ShaderStage::Compute:
        return ShaderStage::Compute;
    case shader::ShaderStage::TessellationControl:
        return ShaderStage::TessellationControl;
    case shader::ShaderStage::TessellationEvaluation:
        return ShaderStage::TessellationEvaluation;
    }
    return ShaderStage::None;
}
} // namespace

ValidationResult validateGeneratedShaderPackageSource(const ShaderPackageDesc& package,
    const core::SharedPtr<const shader::GeneratedShaderArtifactSet>& owner,
    std::string_view publication)
{
    if (const auto* id = shader::GeneratedShaderArtifactSet::validateOriginal(owner))
        return {false, id};
    constexpr std::string_view suffix = ".artifact-set.yaml";
    if (publication.size() <= suffix.size()
        || publication.substr(publication.size() - suffix.size()) != suffix
        || publication.find('\0') != std::string_view::npos)
        return {false, "RHI-GENERATED-PACKAGE-PUBLICATION"};
    const std::array<std::string_view, 5> paths{package.slangModulePath,
        package.artifacts.spirvPath,
        package.artifacts.mslPath,
        package.artifacts.wgslPath,
        package.artifacts.reflectionPath};
    for (std::size_t i = 0; i < paths.size(); ++i)
        if (!pathMatches(paths[i], publication, owner->filename(static_cast<Role>(i))))
            return {false, "RHI-GENERATED-PACKAGE-ROLE-BINDING"};
    return {true, {}};
}

ValidationResult validateGeneratedShaderPackageInput(
    const ShaderPackageDesc& package, bool required)
{
    if (!package.generatedArtifactSet)
        return required || !package.artifacts.publicationPath.empty()
            ? ValidationResult{false, "RHI-GENERATED-PACKAGE-INPUT-MISSING"}
            : ValidationResult{true, {}};
    const auto source = validateGeneratedShaderPackageSource(
        package, package.generatedArtifactSet, package.artifacts.publicationPath);
    if (!source.success)
        return source;
    if (!package.generatedReflectionHydrated)
        return {false, "RHI-GENERATED-PACKAGE-NOT-HYDRATED"};
    if (package.entrypoints.empty())
        return {false, "RHI-GENERATED-PACKAGE-ENTRY-ABI"};
    const auto& reflected = package.generatedArtifactSet->module().reflection;
    if (!ArtifactOwnedShaderReflection::matches(package.reflection, reflected))
        return {false, "RHI-GENERATED-PACKAGE-REFLECTION-MISMATCH"};
    for (const auto& entry : package.entrypoints) {
        const auto found = std::find_if(
            reflected.entryPoints.begin(), reflected.entryPoints.end(), [&](const auto& candidate) {
                return candidate.name == entry.entryPoint
                    && stageFor(candidate.stage) == entry.stage;
            });
        if (found == reflected.entryPoints.end() || entry.modulePath != package.slangModulePath
            || entry.sourceLanguage != ShaderSourceLanguage::Slang
            || found->computeWorkgroupSizeX != entry.computeWorkgroupSizeX
            || found->computeWorkgroupSizeY != entry.computeWorkgroupSizeY
            || found->computeWorkgroupSizeZ != entry.computeWorkgroupSizeZ
            || found->writesRenderTargetArrayIndex != entry.writesRenderTargetArrayIndex)
            return {false, "RHI-GENERATED-PACKAGE-ENTRY-ABI"};
    }
    return {true, {}};
}

bool generatedShaderPackageArtifactsEqual(
    const ShaderPackageDesc& a, const ShaderPackageDesc& b) noexcept
{
    if (bool(a.generatedArtifactSet) != bool(b.generatedArtifactSet))
        return false;
    return !a.generatedArtifactSet
        || (!shader::GeneratedShaderArtifactSet::validateOriginal(a.generatedArtifactSet)
            && !shader::GeneratedShaderArtifactSet::validateOriginal(b.generatedArtifactSet)
            && a.generatedArtifactSet->identity() == b.generatedArtifactSet->identity());
}

std::uint64_t hashGeneratedShaderPackageArtifacts(const ShaderPackageDesc& package) noexcept
{
    if (shader::GeneratedShaderArtifactSet::validateOriginal(package.generatedArtifactSet))
        return 0;
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto byte : package.generatedArtifactSet->identity()) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

ValidationResult selectGeneratedShaderPackageTarget(const ShaderPackageDesc& package,
    shader::TargetBackend target,
    GeneratedShaderTargetInput& output)
{
    output = {};
    const auto valid = validateGeneratedShaderPackageInput(package);
    if (!valid.success)
        return valid;
    Role role;
    switch (target) {
    case shader::TargetBackend::SpirV:
        role = Role::Spirv;
        break;
    case shader::TargetBackend::Msl:
        role = Role::Metal;
        break;
    case shader::TargetBackend::Wgsl:
        role = Role::Wgsl;
        break;
    default:
        return {false, "RHI-GENERATED-PACKAGE-TARGET"};
    }
    const auto& owner = *package.generatedArtifactSet;
    if (!owner.targetDiagnostic(role).empty())
        return {false, owner.targetDiagnostic(role)};
    output.bytes = &owner.bytes(role);
    if (role == Role::Spirv)
        output.spirvWords = &owner.spirvWords();
    else {
        output.text = shader::shaderArtifactText(*output.bytes);
        constexpr std::string_view bom = "\xEF\xBB\xBF";
        if (output.text.substr(0, bom.size()) == bom)
            output.text.remove_prefix(bom.size());
    }
    return {true, {}};
}
} // namespace arp::rhi
