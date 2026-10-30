#include "renderer/shader_package_reflection.h"

#include "core/io/filesystem_path.h"
#include "rhi/gpu_pipeline.h"

#include <algorithm>
#include <cstdio>

namespace arp::renderer {

namespace {

rhi::ShaderStage rhiStageFor(shader::ShaderStage stage)
{
    switch (stage) {
    case shader::ShaderStage::Vertex:
        return rhi::ShaderStage::Vertex;
    case shader::ShaderStage::Fragment:
        return rhi::ShaderStage::Fragment;
    case shader::ShaderStage::Compute:
        return rhi::ShaderStage::Compute;
    case shader::ShaderStage::TessellationControl:
        return rhi::ShaderStage::TessellationControl;
    case shader::ShaderStage::TessellationEvaluation:
        return rhi::ShaderStage::TessellationEvaluation;
    }
    return rhi::ShaderStage::None;
}

} // namespace

bool hydrateShaderPackageReflectionFromArtifact(rhi::ShaderPackageDesc& package,
    const ShaderPackagePreparationDesc& desc,
    core::pmr::String& diagnostic)
{
    package.generatedReflectionHydrated = false;
    diagnostic.clear();
    try {
        if (desc.prepared && desc.budget.source) {
            diagnostic = "RHI-GENERATED-PACKAGE-SOURCE-AMBIGUOUS";
            return false;
        }
        auto prepared = desc.prepared;
        if (!prepared) {
            auto quota = core::makeShared<core::CpuMemoryQuota>(desc.budget.maxCpuMetadataBytes);
            prepared = shader::GeneratedShaderArtifactSet::load(
                desc.publication, desc.budget, quota, diagnostic);
            if (!prepared)
                return false;
        }
        const auto publication = core::FilePath::utf8(desc.publication);
        if (!package.artifacts.publicationPath.empty()
            && core::FilePath::utf8(
                   std::filesystem::u8path(package.artifacts.publicationPath.begin(),
                       package.artifacts.publicationPath.end()))
                != publication) {
            diagnostic = "RHI-GENERATED-PACKAGE-PUBLICATION-MISMATCH";
            return false;
        }
        const auto binding =
            rhi::validateGeneratedShaderPackageSource(package, prepared, publication);
        if (!binding.success) {
            diagnostic = binding.diagnostics;
            return false;
        }
        const auto& reflection = prepared->module().reflection;
        for (auto& entrypoint : package.entrypoints) {
            const auto found = std::find_if(reflection.entryPoints.begin(),
                reflection.entryPoints.end(),
                [&](const auto& reflected) {
                    return reflected.name == entrypoint.entryPoint
                        && rhiStageFor(reflected.stage) == entrypoint.stage;
                });
            if (found == reflection.entryPoints.end()) {
                diagnostic = "RHI-GENERATED-PACKAGE-ENTRY-ABI";
                return false;
            }
            entrypoint.computeWorkgroupSizeX = found->computeWorkgroupSizeX;
            entrypoint.computeWorkgroupSizeY = found->computeWorkgroupSizeY;
            entrypoint.computeWorkgroupSizeZ = found->computeWorkgroupSizeZ;
            entrypoint.writesRenderTargetArrayIndex = found->writesRenderTargetArrayIndex;
        }
        rhi::copyArtifactOwnedShaderReflection(package.reflection, reflection);
        package.artifacts.publicationPath = publication;
        package.generatedArtifactSet = std::move(prepared);
        package.generatedReflectionHydrated = true;
        return true;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-GENERATED-PACKAGE-ALLOCATION";
    } catch (const std::exception&) {
        diagnostic = "RHI-GENERATED-PACKAGE-PREPARATION";
    }
    return false;
}

bool hydrateShaderPackageReflectionFromArtifact(
    rhi::ShaderPackageDesc& package, core::pmr::String& diagnostic)
{
    // ConventionalGenerated/1 is an explicit cold compiler-output profile.
    // Supplied immutable input always wins; neither backend nor draw code calls this.
    ShaderPackagePreparationDesc desc;
    desc.prepared = package.generatedArtifactSet;
    if (!package.artifacts.publicationPath.empty())
        desc.publication = std::filesystem::u8path(
            package.artifacts.publicationPath.begin(), package.artifacts.publicationPath.end());
    else {
        constexpr std::string_view suffix = ".reflection.yaml";
        const std::string_view reflection = package.artifacts.reflectionPath;
        if (reflection.size() <= suffix.size()
            || reflection.substr(reflection.size() - suffix.size()) != suffix) {
            package.generatedReflectionHydrated = false;
            diagnostic = "RHI-GENERATED-PACKAGE-PUBLICATION";
            return false;
        }
        core::pmr::String publication(reflection.substr(0, reflection.size() - suffix.size()),
            &core::defaultCpuMemoryResource());
        publication += ".artifact-set.yaml";
        desc.publication = std::filesystem::u8path(publication.begin(), publication.end());
    }
    // ConventionalGenerated/1 finite cold admission limits, also configurable
    // through the explicit overload: 16MiB/file, 64MiB total/CPU, 4096 bindings,
    // 64 reflected entries. These are budgets, never semantic fallback values.
    desc.budget = {16u * 1024u * 1024u, 64u * 1024u * 1024u, 64u * 1024u * 1024u, 4096u, 64u};
    return hydrateShaderPackageReflectionFromArtifact(package, desc, diagnostic);
}

bool hydrateShaderPackageReflectionFromArtifact(rhi::ShaderPackageDesc& package)
{
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    if (hydrateShaderPackageReflectionFromArtifact(package, diagnostic))
        return true;
    std::fprintf(stderr,
        "[shader-reflection] prepare failed path='%s': %s\n",
        package.artifacts.reflectionPath.c_str(),
        diagnostic.c_str());
    return false;
}

} // namespace arp::renderer
