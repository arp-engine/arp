#pragma once
#include "core/quota_bound_shared_identity.h"
#include "shader/shader_artifact_files.h"
#include "shader/shader_reflection_cache.h"

namespace arp::shader {
// Cold compiler-program custody. No original pipeline/table/command authority.
class ARP_API OriginalDepthTableExpansionArtifact final
    : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalDepthTableExpansionArtifact;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class Profile { Compute, Graphics };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool explicitOptIn = false;
        std::uint64_t maxFileBytes = 0u, maxTotalBytes = 0u, maxCpuMetadataBytes = 0u;
        std::uint32_t maxLogicalBindings = 0u, maxPhysicalBindings = 0u, maxElements = 0u,
                      maxEntries = 0u;
    };

    struct Element {
        std::uint32_t arrayElement = 0u, set = 0u, binding = 0u;
        core::pmr::String name{&core::defaultCpuMemoryResource()};
    };

    struct Entry {
        ShaderArtifactFiles files;
        ShaderReflectionModule physical;
    };

    OriginalDepthTableExpansionArtifact(ConstructionKey, Budget budget) noexcept : budget_(budget)
    {}

    OriginalDepthTableExpansionArtifact(const OriginalDepthTableExpansionArtifact&) = delete;
    OriginalDepthTableExpansionArtifact& operator=(
        const OriginalDepthTableExpansionArtifact&) = delete;
    static std::shared_ptr<const OriginalDepthTableExpansionArtifact> load(
        const std::filesystem::path&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String& diagnostic);

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalDepthTableExpansionArtifact>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "SHADER-ORIGINAL-TABLE-EXPANSION-OWNER";
        return self->originalMetadataQuota()->limitBytes() == self->budget_.maxCpuMetadataBytes
            ? nullptr
            : "SHADER-ORIGINAL-TABLE-EXPANSION-QUOTA-LIMIT";
    }

    Profile profile() const noexcept { return profile_; }

    // Issued only from a compiler-proven complete GraphicsGeometry program.
    std::uint32_t vertexInputContractVersion() const noexcept
    {
        return vertexInputContractVersion_;
    }

    const auto& logicalBindings() const noexcept { return logical_; }

    const auto& elements() const noexcept { return elements_; }

    const auto& entry(std::size_t index) const { return entries_.at(index); }

    std::size_t entryCount() const noexcept { return profile_ == Profile::Compute ? 1u : 2u; }

    const auto& originalSource() const noexcept { return original_.bytes; }

    const auto& originalSourceSha256() const noexcept { return original_.sha256; }

    const auto& generatedSource() const noexcept { return generated_.bytes; }

    const auto& identity() const noexcept { return identity_; }

    const auto& publicationBytes() const noexcept { return publication_; }

    const auto& quota() const noexcept { return originalMetadataQuota(); }

    std::uint64_t inputBytes() const noexcept { return inputBytes_; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const Budget budget_;
    Profile profile_ = Profile::Compute;
    std::uint32_t vertexInputContractVersion_ = 0u;
    ShaderArtifactFiles::File original_, generated_;
    core::ByteBuffer publication_{&core::defaultCpuMemoryResource()},
        identity_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderBinding> logical_{&core::defaultCpuMemoryResource()};
    std::array<Element, 3> elements_;
    std::array<Entry, 2> entries_;
    std::uint64_t inputBytes_ = 0u;
};
} // namespace arp::shader
