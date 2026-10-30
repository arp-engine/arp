#pragma once

#include "core/ownership/quota_bound_shared_identity.h"
#include "shader/shader_artifact_files.h"
#include "shader/shader_reflection_cache.h"

namespace arp::shader {

struct GeneratedShaderArtifactSetBudget {
    std::uint64_t maxFileBytes = 0, maxTotalBytes = 0, maxCpuMetadataBytes = 0;
    std::uint32_t maxBindings = 0, maxEntryPoints = 0;
    const ShaderArtifactSource* source = nullptr;
};

// Cold, immutable custody of ordinary compiler output. This is not a shader
// execution proof; RHI capability and pipeline validation remain independent.
class ARP_API GeneratedShaderArtifactSet final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class GeneratedShaderArtifactSet;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    explicit GeneratedShaderArtifactSet(ConstructionKey);
    ~GeneratedShaderArtifactSet();
    GeneratedShaderArtifactSet(const GeneratedShaderArtifactSet&) = delete;
    GeneratedShaderArtifactSet& operator=(const GeneratedShaderArtifactSet&) = delete;

    static core::SharedPtr<const GeneratedShaderArtifactSet> load(const std::filesystem::path&,
        const GeneratedShaderArtifactSetBudget&,
        core::SharedPtr<core::CpuMemoryQuota>,
        core::pmr::String& diagnostic);
    static const char* validateOriginal(
        const core::SharedPtr<const GeneratedShaderArtifactSet>&) noexcept;

    const core::ByteBuffer& bytes(ShaderArtifactRole) const;
    const core::pmr::String& filename(ShaderArtifactRole) const;
    const core::pmr::String& targetDiagnostic(ShaderArtifactRole) const;
    const core::pmr::String& reflectedPath(ShaderArtifactRole) const;
    const core::pmr::Vector<std::uint32_t>& spirvWords() const noexcept;
    const ShaderReflectionModule& module() const noexcept;
    const core::ByteBuffer& publicationBytes() const noexcept;
    const core::ByteBuffer& identity() const noexcept;
    std::uint64_t inputBytes() const noexcept;

    const auto& quota() const noexcept { return originalMetadataQuota(); }

private:
    struct Prepared;
    core::UniquePtr<Prepared> prepared_;
};

} // namespace arp::shader
