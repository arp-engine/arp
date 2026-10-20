#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_buffer_declaration_layout_fixture.h"
#include "shader/graphics_shader_module_artifact.h"

#include <string_view>

namespace arp::test {
// Reused by real generated-artifact loaders and the browser's actual factory
// input. Ownership proof grants no pipeline byte consumption/execution authority.
class OriginalShaderArtifactOwnerFixture final {
public:
    template <class Artifact>
    static void verify(const std::shared_ptr<const Artifact>& original,
        const char* diagnostic,
        const char* quotaDiagnostic)
    {
        assert(original && !Artifact::validateOriginal(original));
        assert(!original->publicationBytes().empty()
            && core::Sha256::digest(shader::shaderArtifactText(original->publicationBytes()))
                == original->identity());
        assert(core::SharedOwnerProjection::sameOwner(
            original->quota(), original->originalMetadataQuota()));
        assert(!original->nativeExecutionProven());
        const auto quota = original->quota();
        const auto limit = quota->limitBytes();
        assert(limit);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (const auto changed : std::array<std::uint64_t, 2u>{limit - 1u, 0u}) {
            quota->setLimitBytes(changed);
            const auto* id = Artifact::validateOriginal(original);
            assert(id && std::string_view(id) == quotaDiagnostic);
        }
        quota->setLimitBytes(limit);
        assert(!Artifact::validateOriginal(original));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto forged = std::shared_ptr<const Artifact>(original->quota(), original.get());
        auto nonowning =
            std::shared_ptr<const Artifact>(std::shared_ptr<const Artifact>{}, original.get());
        auto missing = std::shared_ptr<const Artifact>{};
        assert(!nonowning.use_count() && forged.get() == original.get());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{original->quota()}, [&]() noexcept {
                for (const auto& invalid : std::array{forged, nonowning, missing}) {
                    const auto* id = Artifact::validateOriginal(invalid);
                    assert(id && std::string_view(id) == diagnostic);
                }
                return Artifact::validateOriginal(original);
            }));
    }

    static void entry(const std::shared_ptr<const shader::SingleEntryShaderArtifact>& original)
    {
        verify(original, "SHADER-ENTRY-ORIGINAL-OWNER", "SHADER-ENTRY-ORIGINAL-QUOTA-LIMIT");
        OriginalBufferDeclarationLayoutFixture::verify(original);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_ENTRY_ARTIFACT_OWNER_OK exact-published-quota-control-block forged-nonowning-missing-rejected hot-allocation0 consumption-native-closed");
    }

    static void module(const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>& original)
    {
        verify(original, "SHADER-MODULE-ORIGINAL-OWNER", "SHADER-MODULE-ORIGINAL-QUOTA-LIMIT");
        verify(
            original->vertex(), "SHADER-ENTRY-ORIGINAL-OWNER", "SHADER-ENTRY-ORIGINAL-QUOTA-LIMIT");
        verify(original->fragment(),
            "SHADER-ENTRY-ORIGINAL-OWNER",
            "SHADER-ENTRY-ORIGINAL-QUOTA-LIMIT");
        OriginalBufferDeclarationLayoutFixture::verify(original->vertex());
        OriginalBufferDeclarationLayoutFixture::verify(original->fragment());
        assert(!core::SharedOwnerProjection::sameControlBlock(
            original->vertex(), original->fragment()));
        std::puts(
            "BINDLESS_RHI_ORIGINAL_MODULE_ARTIFACT_OWNER_OK exact-published-quota-control-block original-children2 forged-nonowning-missing-rejected hot-allocation0 consumption-native-closed");
    }
};
} // namespace arp::test
