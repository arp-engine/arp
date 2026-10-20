#pragma once
#include "original_native_depth_root_preparation_fixture.h"
#include "rhi/backends/vulkan/vulkan_bindless_root.h"
#include "vulkan_original_depth_execution_fixture.h"

namespace arp::test {
struct VulkanDepthRootBackendFixture {
    using Closure = rhi_vulkan::VulkanBindlessRootNativeClosure;

    template <class... Args> static void execute(Args&&... args)
    {
        VulkanOriginalDepthExecutionFixture::execute(std::forward<Args>(args)...);
    }

    static void verifyStandalone(rhi::GpuDevice& device,
        const std::shared_ptr<rhi::ComputePipeline>& pipeline,
        const std::shared_ptr<const rhi::BindlessPreparedComputeBinding>&,
        bool execute)
    {
        auto encoder = device.createCommandEncoder({"native cold depth execution gate", 1u, 1u});
        assert(encoder);
        auto pass = encoder->beginComputePass({"native cold depth execution gate"});
        assert(pass);
        const auto denied = pass->setPipeline(*pipeline);
        assert(!denied.success
            && denied.diagnostics
                == (execute ? "RHI-BINDLESS-ORIGINAL-EXECUTION-PUBLIC-ROOT-REQUIRED"
                            : "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE"));
        assert(pass->end().success);
    }

    template <class Root, class Snapshot, class Proof>
    static void verifyNativeProofs(const Root& root, const Snapshot& snapshot, const Proof& proof)
    {
        using namespace rhi;
        const auto nativeSet = rhi_vulkan::vulkanRhiNativePayload(*snapshot->descriptorSet());
        assert(nativeSet);
        std::size_t proofReferences = 0u;
        for (std::size_t i = 0u; i < root->elements().size(); ++i) {
            const auto selected = root->originalCompletedDepthAt(i);
            if (!selected)
                continue;
            ++proofReferences;
            assert(selected.get() == proof.get()
                && core::SharedOwnerProjection::sameOwner(selected, proof));
            const auto& write = root->elements()[i].declaration.write;
            const auto physical = descriptorElementIndex(
                snapshot->descriptorSet()->layout(), write.binding, write.arrayElement);
            const auto nativeProof =
                nativeSet->backendVulkanDescriptorElementDepthContent(physical);
            assert(nativeProof.get() == proof.get()
                && core::SharedOwnerProjection::sameOwner(nativeProof, proof));
        }
        assert(proofReferences == 2u); // One source shared by two physical array elements.
    }

    static void reportCold()
    {
        std::puts(
            "BINDLESS_VULKAN_PUBLIC_DEPTH_READ_PLAN12 same-inventory Content12 Sequence2 RootState4 control4 exact-less-quota hot0 CPU-claim-cancel original-generation no-native-shader-execution");
        std::puts(
            "BINDLESS_VULKAN_PUBLIC_DEPTH_ROOT6 Native-cold Source2 Publication4 ReadInput2 Closure9 exact-original-inventory physical-proof budgets dedup hot0 no-shader-execution");
    }
};

using VulkanOriginalDepthRootPreparationFixture =
    OriginalNativeDepthRootPreparationFixture<VulkanDepthRootBackendFixture>;
} // namespace arp::test
