#include "../fixtures/original_shader_artifact_owner_fixture.h"
#include "core/io/artifact_path.h"
#include "shader/single_entry_shader_artifact.h"
#include "shadow_depth.shader_bindings.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <type_traits>

#ifdef NDEBUG
#error Artifact contract tests require assertions
#endif

int main(int argc, char** argv)
{
    using namespace arp;
    using namespace shader;
    static_assert(!std::is_copy_constructible_v<SingleEntryShaderArtifact>);
    static_assert(!std::is_copy_assignable_v<SingleEntryShaderArtifact>);
    static_assert(!std::is_default_constructible_v<SingleEntryShaderArtifact>);
    static_assert(sizeof(gen::WoccShadowDepthPushConstants) == 224u);
    static_assert(sizeof(gen::WoccShadowDepthPushConstants::lightViewProjection) == 192u);
    static_assert(offsetof(gen::WoccShadowDepthPushConstants, depthBiasAndFlags) == 192u);
    static_assert(offsetof(gen::WoccShadowDepthPushConstants, cascadeIndex) == 208u);
    assert(argc == 3);
    const std::filesystem::path publication(argv[1]);
    const core::StringView expected(argv[2]);
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    diagnostic.reserve(256u);
    const SingleEntryShaderLoadBudget budget{1024u * 1024u, 4u * 1024u * 1024u, 1024u * 1024u, 16u};
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    auto artifact = SingleEntryShaderArtifact::load(publication, budget, quota, diagnostic);
    if (expected != "valid" && expected != "legacy-valid") {
        if (artifact || diagnostic.find(expected) == diagnostic.npos)
            std::fprintf(stderr, "Expected %s, got %s\n", argv[2], diagnostic.c_str());
        assert(!artifact && diagnostic.find(expected) != diagnostic.npos && !quota->liveBytes());
        return 0;
    }
    if (!artifact)
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
    assert(artifact);
    assert(artifact->bufferDeclarationLayoutsAvailable() == (expected == "valid"));
    assert(artifact->identity().size() == 32u && artifact->quota() == quota && quota->liveBytes());
    assert(artifact->module().reflection.entryPoints.size() == 1u
        && artifact->module().reflection.entryPoints[0].name == artifact->entry());
    assert(artifact->module().reflection.bindings.get_allocator().resource()
        == &core::defaultCpuMemoryResource());
    for (const auto& binding : artifact->module().reflection.bindings)
        assert(binding.name.get_allocator().resource() == &core::defaultCpuMemoryResource());
    if (artifact->module().moduleName == "WoccShadowDepth") {
        const auto& reflection = artifact->module().reflection;
        assert(reflection.pushConstantSizeBytes == 224u
            && reflection.pushConstantMembers.size() == 6u);
        const auto& member = reflection.pushConstantMembers.front();
        assert(member.name == "lightViewProjection" && member.size == 192u && member.offset == 0u);
        assert(member.scalarWidth == 32u && member.matrixRows == 4u && member.matrixCols == 4u);
        assert(member.matrixStride == 16u && member.arrayStride == 64u);
        assert(member.arrayDims.size() == 1u && member.arrayDims[0] == 3u);
        const auto& sourceBytes = artifact->bytes(ShaderArtifactRole::Source);
        const core::StringView source(
            reinterpret_cast<const char*>(sourceBytes.data()), sourceBytes.size());
        assert(source.find("    float4x4 lightViewProjection[3];") != source.npos);
        assert(source.find("row_major float4x4 lightViewProjection") == source.npos);
    }
    if (artifact->filename(ShaderArtifactRole::Source) == "ordinary_graphics_push.slang") {
        const auto& reflection = artifact->module().reflection;
        assert(
            reflection.pushConstantSizeBytes == 32u && reflection.pushConstantMembers.size() == 2u);
        assert(reflection.pushConstantContractVersion == "AtomicFullBlockPush/1");
        assert(reflection.pushConstantStages
            == (artifact->stage() == ShaderStage::Vertex ? "VS" : "FS"));
        const auto& first = reflection.pushConstantMembers[0];
        const auto& second = reflection.pushConstantMembers[1];
        assert(first.name == "positionScaleBiasDepth" && first.offset == 0u && first.size == 16u);
        assert(second.name == "color" && second.offset == 16u && second.size == 16u);
        const auto& sourceBytes = artifact->bytes(ShaderArtifactRole::Source);
        const core::StringView source(
            reinterpret_cast<const char*>(sourceBytes.data()), sourceBytes.size());
        assert(source.find("[[vk::push_constant]] GraphicsPushConstants Push;") != source.npos);
    }
    if (artifact->filename(ShaderArtifactRole::Source) == "ordinary_graphics_storage.slang") {
        const auto& reflection = artifact->module().reflection;
        assert(!reflection.pushConstantSizeBytes);
        assert(reflection.bindings.size() == (artifact->stage() == ShaderStage::Vertex ? 1u : 2u));
        for (const auto& binding : reflection.bindings) {
            assert(binding.type == BindingType::ReadOnlyStorageBuffer && binding.set == 0u);
            assert(binding.binding == 3u
                || (artifact->stage() == ShaderStage::Fragment && binding.binding == 8u));
            assert(binding.arrayCount == 1u);
        }
    }
    if (artifact->filename(ShaderArtifactRole::Source) == "ordinary_graphics_storage_array.slang") {
        const auto& reflection = artifact->module().reflection;
        assert(!artifact->available(ShaderArtifactRole::Wgsl)
            && artifact->filename(ShaderArtifactRole::Wgsl).empty());
        assert(artifact->targetDiagnostic(ShaderArtifactRole::Wgsl)
                == "SHADER-WGSL-BUFFER-ARRAY-UNSUPPORTED"
            || artifact->targetDiagnostic(ShaderArtifactRole::Wgsl)
                == "SHADER-WGSL-BUFFER-ARRAY-ABI-UNAVAILABLE");
        assert(reflection.webGpuBindingSpaceStatus == "Unavailable"
            && reflection.webGpuBindingSpace.empty());
        assert(artifact->module().wgslPath.empty());
        assert(reflection.bindings.size() == (artifact->stage() == ShaderStage::Vertex ? 1u : 2u));
        for (const auto& binding : reflection.bindings)
            assert(binding.type == BindingType::ReadOnlyStorageBuffer && binding.arrayCount == 2u);
        assert(artifact->available(ShaderArtifactRole::Spirv)
            && artifact->available(ShaderArtifactRole::Metal));
    }
    if (artifact->filename(ShaderArtifactRole::Source) == "ordinary_graphics_uniform_array.slang") {
        const auto& reflection = artifact->module().reflection;
        assert(reflection.bindings.size() == (artifact->stage() == ShaderStage::Vertex ? 1u : 2u));
        for (const auto& binding : reflection.bindings)
            assert(binding.type == BindingType::UniformBuffer && binding.arrayCount == 2u);
        for (const auto role :
            {ShaderArtifactRole::Spirv, ShaderArtifactRole::Metal, ShaderArtifactRole::Wgsl})
            assert(artifact->available(role) && artifact->targetDiagnostic(role).empty());
        assert(reflection.webGpuBindingSpaceStatus == "Available");
    }
    assert(core::isFlatArtifactFilename("shader.vertex.entry.yaml"));
    for (const auto invalid : {"", ".", "..", "../a", "a/b", "a\\b", "C:a", "a b"})
        assert(!core::isFlatArtifactFilename(invalid));
    assert(core::artifactFilename("C:\\shaders\\a.metal") == "a.metal");
    auto hostileQuota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    auto hostile = SingleEntryShaderArtifact::load(publication, budget, hostileQuota, diagnostic);
    std::pmr::set_default_resource(previous);
    if (!hostile)
        std::fprintf(stderr, "Hostile PMR: %s\n", diagnostic.c_str());
    assert(hostile);
    assert(hostile->identity() == artifact->identity() && hostile->entry() == artifact->entry());
    hostile.reset();
    assert(!hostileQuota->liveBytes());
    auto reject = [&](SingleEntryShaderLoadBudget limits,
                      std::uint64_t quotaBytes,
                      const char* id,
                      bool exhaust = false) {
        auto rejectedQuota = core::makeShared<core::CpuMemoryQuota>(quotaBytes);
        core::CpuMemoryQuotaLease pressure(rejectedQuota, exhaust ? quotaBytes : 0u);
        assert(!SingleEntryShaderArtifact::load(publication, limits, rejectedQuota, diagnostic));
        assert(diagnostic.find(id) != diagnostic.npos);
        pressure.release();
        assert(!rejectedQuota->liveBytes());
    };
    auto limits = budget;
    limits.maxFileBytes = 0u;
    reject(limits, budget.maxCpuMetadataBytes, "LOAD-BUDGET");
    reject(budget, budget.maxCpuMetadataBytes - 1u, "CPU-QUOTA-MISMATCH");
    reject(budget, budget.maxCpuMetadataBytes, "CPU-ALLOCATION-FAILED", true);
    limits = budget;
    limits.maxCpuMetadataBytes = 1u;
    reject(limits, 1u, "CPU-ALLOCATION-FAILED");
    limits = budget;
    limits.maxCpuMetadataBytes = quota->peakBytes() - 1u;
    reject(limits, limits.maxCpuMetadataBytes, "CPU-ALLOCATION-FAILED");
    limits = budget;
    limits.maxFileBytes = 1u;
    reject(limits, budget.maxCpuMetadataBytes, "FILE-READ-FAILED");
    limits = budget;
    limits.maxTotalBytes = artifact->inputBytes() - 1u;
    reject(limits, budget.maxCpuMetadataBytes, "FILE-READ-FAILED");
    if (!artifact->module().reflection.bindings.empty()) {
        limits = budget;
        limits.maxBindings =
            static_cast<std::uint32_t>(artifact->module().reflection.bindings.size()) - 1u;
        reject(limits, budget.maxCpuMetadataBytes, "BINDING-BUDGET");
    }
    // The Python harness gives this process a private temporary copy. Removing
    // its complete publication/files proves subsequent access is memory-only.
    test::OriginalShaderArtifactOwnerFixture::entry(artifact);
    const auto source = artifact->bytes(ShaderArtifactRole::Source);
    const auto reflection = artifact->bytes(ShaderArtifactRole::Reflection);
    const auto publicationBytes = artifact->publicationBytes();
    std::filesystem::remove_all(publication.parent_path());
    assert(artifact->bytes(ShaderArtifactRole::Source) == source
        && artifact->bytes(ShaderArtifactRole::Reflection) == reflection);
    assert(artifact->publicationBytes() == publicationBytes
        && !SingleEntryShaderArtifact::validateOriginal(artifact));
    test::OriginalBufferDeclarationLayoutFixture::verify(artifact);
    artifact.reset();
    assert(!quota->liveBytes());
    std::puts(
        "singleEntryArtifact: real generated targets, exact identity, immutable source/metadata, hostile PMR, byte/binding/core quotas and partial cleanup pass");
}
