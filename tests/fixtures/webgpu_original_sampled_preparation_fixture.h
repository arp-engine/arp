#pragma once
#include "webgpu_original_bindless_root_preparation_fixture.h"

namespace arp::test {
struct WebGpuOriginalSampledPreparationRequest {
    static constexpr bool enabled = false;

    void configureRequest(rhi::BindlessRequirements& request) const
    {
        request.originalPreparation = {3u, true};
    }
};

// Public cold preparation only. The retained native views are intentionally
// uninitialized: no sampling, content, recording or completion proof is inferred.
class WebGpuOriginalSampledPreparationFixture final {
public:
    explicit WebGpuOriginalSampledPreparationFixture(bool completed = false) : completed_(completed)
    {}

    template <class Root, class Sources>
    void operator()(const Root& root, const Sources& inventory, rhi::GpuDevice* device) const
    {
        if (!device) {
            assert(retained_ && !retained_->validatePayload().success);
            retained_.reset();
            assert(!quota_->liveBytes());
            return;
        }
        using namespace rhi_webgpu;
        auto* extension = static_cast<rhi::BindlessCrossExtension*>(
            device->queryInterface(rhi::kExtensionBindlessCross));
        assert(extension
            && extension->capabilities().originalSampledGraphicsPreparationContractVersion == 3u
            && !extension->capabilities().executablePayload);
        const auto& binding = std::get<std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>>(
            root->originalPrepared());
        assert(binding && binding->validatePayload().success
            && root->matchesCanonicalSources(*inventory));
        retained_ = binding;
        const auto descriptors = webGpuNativePayloadFor(*root->originalObject());
        const auto physical = webGpuOriginalBindlessPhysicalGroup(binding);
        assert(descriptors && physical && physical->group
            && physical->bindings.size() == descriptors->bindings.size());
        unsigned views = 0u, samplers = 0u;
        for (auto& entry : descriptors->bindings) {
            if (entry.textureView) {
                ++views;
                const auto parent = entry.textureView->parentTexture;
                assert(parent && parent->texture);
                ++parent->extent.width;
                assert(!binding->validatePayload().success);
                --parent->extent.width;
                const auto key = entry.textureView->creationKey;
                ++entry.textureView->creationKey.baseMipLevel;
                assert(!binding->validatePayload().success);
                entry.textureView->creationKey = key;
                const auto actual = entry.textureView;
                entry.textureView =
                    std::shared_ptr<WebGpuNativeTextureViewPayload>(quota_, actual.get());
                assert(!binding->validatePayload().success);
                entry.textureView = actual;
                const auto actualParent = entry.textureView->parentTexture;
                entry.textureView->parentTexture =
                    std::shared_ptr<WebGpuNativeTexturePayload>(quota_, actualParent.get());
                assert(!binding->validatePayload().success);
                entry.textureView->parentTexture = actualParent;
            } else if (entry.sampler) {
                ++samplers;
                const auto actual = entry.sampler;
                entry.sampler = std::shared_ptr<WebGpuNativeSamplerPayload>(quota_, actual.get());
                assert(!binding->validatePayload().success);
                entry.sampler = actual;
                const auto handle = entry.sampler->sampler;
                entry.sampler->sampler = nullptr;
                assert(!binding->validatePayload().success);
                entry.sampler->sampler = handle;
            }
            assert(binding->validatePayload().success);
        }
        assert(views == 2u && samplers == 2u);
        if (completed_) {
            assert(descriptors->completedTexturePublications.size() == views
                && physical->completedTexturePublications.size() == views
                && physical->ordinaryTextureAccessDiagnostic());
            auto& proof = physical->completedTexturePublications.front();
            const auto actual = proof.source;
            proof.source =
                std::shared_ptr<const rhi::OriginalCommandTextureViewSource>(quota_, actual.get());
            assert(!binding->validatePayload().success);
            proof.source = actual;
            assert(binding->validatePayload().success);
        }
        core::pmr::String diagnostic;
        diagnostic.reserve(512u);
        const auto factory = [&] {
            core::CpuMemoryQuotaScope scope(quota_);
            return prepareWebGpuOriginalBindlessGraphics(*extension, binding->owners(), diagnostic);
        };
        // Reject a forged owner before a fresh physical group is emitted.
        // A previous prepared binding's frozen copy alone is not registry proof.
        for (auto& entry : descriptors->bindings) {
            if (entry.textureView) {
                const auto actual = entry.textureView;
                entry.textureView =
                    std::shared_ptr<WebGpuNativeTextureViewPayload>(quota_, actual.get());
                assert(!factory() && !diagnostic.empty());
                entry.textureView = actual;
            } else if (entry.sampler) {
                const auto actual = entry.sampler;
                entry.sampler = std::shared_ptr<WebGpuNativeSamplerPayload>(quota_, actual.get());
                assert(!factory() && !diagnostic.empty());
                entry.sampler = actual;
            }
        }
        CpuQuotaFactoryFixture allocation(quota_);
        const auto peak = allocation.measure(factory);
        allocation.verify(peak, factory);
        assert(!quota_->liveBytes());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota_}, [&]() noexcept -> const char* {
                return binding->validatePayload().success ? nullptr
                                                          : "sampled cold validation failed";
            }));
        auto request =
            BindlessPackageRequestFixture::make(root->authorization()->package().artifact(),
                root->originalSnapshot()->plan().deviceIdentity);
        request.originalPreparation = {3u, true};
        request.shaderPackageIdentity = 0u;
        for (const auto policy : {rhi::BindlessPolicy::Required, rhi::BindlessPolicy::Auto}) {
            const auto choice = rhi::semantics::selectBindlessVariant(policy,
                extension->capabilities(),
                request,
                &root->authorization()->package().artifact(),
                1u);
            assert(choice.path
                == (policy == rhi::BindlessPolicy::Required
                        ? rhi::BindlessSelectionPath::Rejected
                        : rhi::BindlessSelectionPath::Traditional));
            assert(core::StringView(choice.diagnosticId)
                == "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE");
        }
        if (completed_)
            std::printf(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPLETED_PREPARATION_OK profile3 publication2 control1 views2 samplers2 bytes%llu original-uploads same-inventory authenticated-content-physical-proof sampler-native-loss exact-less-quota hot0 no-state-change no-execution\n",
                static_cast<unsigned long long>(peak));
        else
            std::printf(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_SAMPLED_PREPARATION_OK profile3 views2 samplers2 bytes%llu original-parents same-inventory owner-shape-mutated-refused exact-less-quota hot0 no-content no-execution\n",
                static_cast<unsigned long long>(peak));
    }

private:
    bool completed_ = false;
    mutable std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> retained_;
    std::shared_ptr<core::CpuMemoryQuota> quota_ = core::makeShared<core::CpuMemoryQuota>(65536u);
};
} // namespace arp::test
