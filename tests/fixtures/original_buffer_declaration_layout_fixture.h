#pragma once
#include "cpu_quota_factory_fixture.h"
#include "shader/single_entry_shader_artifact.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalBufferDeclarationLayoutFixture final {
public:
    static void verify(const std::shared_ptr<const shader::SingleEntryShaderArtifact>& original)
    {
        using Artifact = shader::SingleEntryShaderArtifact;
        using Facts = shader::SpirvBufferDeclarationLayout;
        assert(original && !Artifact::validateOriginal(original));
        static_assert(core::canMultiplyUnsigned<std::uint64_t>(UINT64_MAX, 0u));
        static_assert(core::canMultiplyUnsigned<std::uint64_t>(UINT64_MAX, 1u));
        static_assert(!core::canMultiplyUnsigned<std::uint64_t>(UINT64_MAX, 2u));
        static_assert(!core::canMultiplyUnsigned<std::uint32_t>(UINT32_MAX, 2u));
        const auto available = original->bufferDeclarationLayoutsAvailable();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{original->quota()}, [&]() noexcept -> const char* {
                auto forged = std::shared_ptr<const Artifact>(original->quota(), original.get());
                auto nonowning = std::shared_ptr<const Artifact>(
                    std::shared_ptr<const Artifact>{}, original.get());
                for (const auto& owner :
                    std::array{std::shared_ptr<const Artifact>{}, forged, nonowning}) {
                    Facts output;
                    output.descriptorCount = UINT64_MAX;
                    const auto* id =
                        Artifact::originalBufferDeclarationLayout(owner, 0u, 0u, output);
                    assert(id && std::string_view(id) == "SHADER-ENTRY-ORIGINAL-OWNER"
                        && output.matches({}));
                }
                Facts missing;
                missing.descriptorCount = UINT64_MAX;
                const auto* absent = Artifact::originalBufferDeclarationLayout(
                    original, UINT32_MAX, UINT32_MAX, missing);
                assert(absent
                    && std::string_view(absent)
                        == (available ? "SHADER-ENTRY-BUFFER-LAYOUT-BINDING"
                                      : "SHADER-ENTRY-BUFFER-LAYOUT-UNAVAILABLE"));
                assert(missing.matches({}));
                for (const auto& binding : original->module().reflection.bindings) {
                    const bool uniform = binding.type == shader::BindingType::UniformBuffer;
                    const bool buffer = uniform
                        || binding.type == shader::BindingType::StorageBuffer
                        || binding.type == shader::BindingType::ReadOnlyStorageBuffer;
                    Facts output;
                    output.descriptorCount = UINT64_MAX;
                    const auto* id = Artifact::originalBufferDeclarationLayout(
                        original, binding.set, binding.binding, output);
                    if (!available || !buffer) {
                        assert(id && output.matches({}));
                        continue;
                    }
                    assert(!id && output.set == binding.set && output.binding == binding.binding
                        && output.descriptorCount == binding.arrayCount);
                    assert(output.spirvId && output.blockTypeId
                        && output.storageClass == (uniform ? 2u : 12u));
                    if (output.hasRuntimeArray)
                        assert(!uniform && output.runtimeArrayStrideBytes
                            && output.runtimeElementOccupiedExtentBytes);
                    else
                        assert(!output.runtimeArrayOffsetBytes && !output.runtimeArrayStrideBytes
                            && !output.runtimeElementOccupiedExtentBytes);
                    const auto originalFacts = output;
                    output.descriptorCount = UINT64_MAX;
                    Facts again;
                    assert(!Artifact::originalBufferDeclarationLayout(
                        original, binding.set, binding.binding, again));
                    assert(again.matches(originalFacts) && !again.matches(output));
                }
                return Artifact::validateOriginal(original);
            }));
        assert(!original->nativeExecutionProven());
        if (available)
            std::puts(
                "BINDLESS_RHI_ORIGINAL_BUFFER_DECLARATION_CUSTODY_OK immutable-original binary-reflection-matched memory-only allocation0 forged-owner-rejected native-closed");
        else
            std::puts(
                "BINDLESS_RHI_ORIGINAL_BUFFER_DECLARATION_LEGACY_OK no-facts no-native-authority");
    }
};
} // namespace arp::test
