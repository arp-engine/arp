#pragma once
#include "rhi/backends/webgpu/webgpu_owned_texture_readback_preparation.h"
#include "rhi/frontend/transfer_readback_owned_preparation.h"

namespace arp::test {
class WebGpuOwnedReadbackContextFixture;
class WebGpuOwnedReadbackAdvancementFixture;
} // namespace arp::test

namespace arp::rhi_webgpu {
// OwnedReadbackDeviceBudgetContext/1: actual-device budget/lifetime composition
// and selected explicit Prototype admission. Base capabilities stay closed.
class WebGpuOwnedTextureReadbackExtension final
    : public rhi::TextureReadbackOwnedPreparationExtension {
    using Plan = rhi::TextureReadbackColdBudgetPlan;
    using Staging = WebGpuOriginalTextureReadbackDeviceStagingBudget;
    using Publication = WebGpuOriginalTextureReadbackPublication;

    struct Context {
        std::shared_ptr<const Plan> plan;
        std::shared_ptr<WebGpuRhiResourceRegistry> registry;
        WebGpuTextureReadbackColdBudgetLowering::Recipe recipe;
        std::shared_ptr<const Staging> staging;
        std::shared_ptr<Publication> publication;
        std::shared_ptr<WebGpuOrdinaryCompletionOwner> completion;
    };

public:
    explicit WebGpuOwnedTextureReadbackExtension(
        std::shared_ptr<WebGpuRhiResourceRegistry> registry) noexcept
        : registry_(std::move(registry))
    {}

    ~WebGpuOwnedTextureReadbackExtension() override { (void)shutdownOriginal(); }

    const char* configureOriginalPlan(std::shared_ptr<const Plan> plan) override
    {
        if (context_)
            return "RHI-READBACK-OWNED-CONTEXT-BUSY";
        if (bindingConsumed_)
            return "RHI-READBACK-OWNED-CONTEXT-CONSUMED";
        if (const auto* id = Plan::validateOriginal(plan))
            return id;
        if (!Plan::originalLossBindingAvailable(plan))
            return "RHI-DEVICE-LOSS-REGISTRATION-REUSED-PLAN";
        if (!registry_ || !registry_->sharedOwnerMatches(registry_))
            return "RHI-READBACK-OWNED-CONTEXT-REGISTRY";
        WebGpuTextureReadbackColdBudgetLowering::Recipe recipe;
        if (const auto* id = WebGpuTextureReadbackColdBudgetLowering::lowerOriginal(plan, recipe))
            return id;
        const char* diagnostic = nullptr;
        try {
            core::CpuMemoryQuotaScope scope(
                plan->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Device)]);
            auto context = core::makeUnique<Context>();
            context->plan = plan;
            context->registry = registry_;
            context->recipe = std::move(recipe);
            context->staging = Staging::prepareOriginal(plan, registry_, diagnostic);
            if (!context->staging)
                return diagnostic;
            context->publication = Publication::prepareForDevicePlan(
                plan, registry_, context->recipe.publication, diagnostic);
            if (!context->publication)
                return diagnostic;
            // Final cold factory binds native loss. All context/sink/staging
            // allocations precede it, so subsequent publication cannot fail.
            context->completion = prepareWebGpuOriginalReadbackCompletionOwner(plan,
                context->recipe.completion,
                context->recipe.deviceLoss,
                plan->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Device)],
                diagnostic);
            // Native promises cannot unsubscribe. This actual GpuDevice may
            // attempt binding once, even across newly authored plan objects.
            // Metadata allocation failure before binding permits retry.
            bindingConsumed_ = !Plan::originalLossBindingAvailable(plan);
            if (!context->completion)
                return diagnostic;
            context_ = std::move(context);
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-READBACK-OWNED-CONTEXT-ALLOCATION";
        } catch (const std::length_error&) {
            return "RHI-READBACK-OWNED-CONTEXT-ALLOCATION";
        }
    }

    rhi::TextureReadbackOwnedPreparationContract::Decision
    executionDecision() const noexcept override
    {
        using Contract = rhi::TextureReadbackOwnedPreparationContract;
        if (!context_)
            return Contract::resolveLowering(rhi::Backend::WebGPU, {});
        return Contract::resolveLowering(rhi::Backend::WebGPU,
            {true,
                context_->completion->nativeDevice() != nullptr,
                context_->completion->originalLossArmed(),
                context_->publication->acceptingOriginal()
                    && context_->completion->retained.accepting(),
                context_->completion->originalLossDelivered() || webGpuNativeDeviceLost()});
    }

    std::shared_ptr<rhi::TextureReadbackOwnedPreparation> prepareOwnedOriginal(
        core::UniquePtr<rhi::GpuTexture>& source,
        std::shared_ptr<const rhi::TextureReadbackRequestDeclaration> request,
        std::shared_ptr<const rhi::TextureReadbackStatisticsPlan> statistics,
        const rhi::TextureReadbackOwnedPreparationIntent& intent,
        const char*& diagnostic) override
    {
        const auto decision = executionDecision();
        diagnostic = decision.diagnostic;
        if (!decision.executionAvailable)
            return {};
        return WebGpuOwnedTextureReadbackPreparation::prepareOriginal(context_->plan,
            context_->registry,
            context_->staging,
            context_->publication,
            context_->completion,
            source,
            std::move(request),
            std::move(statistics),
            intent,
            diagnostic);
    }

    bool pollOriginal(
        rhi::TextureReadbackOwnedDelivery& output, const char*& diagnostic) noexcept override
    {
        diagnostic = nullptr;
        if (!context_) {
            diagnostic = "RHI-READBACK-OWNED-CONTEXT-UNCONFIGURED";
            return false;
        }
        using Outcome = rhi::TextureReadbackOwnedDelivery::Outcome;
        if (output.outcome != Outcome::Empty || output.diagnostic || output.originalRequest
            || output.snapshot) {
            diagnostic = "RHI-READBACK-OWNED-OUTPUT-BUSY";
            return false;
        }
        Publication::Delivery original;
        if (!Publication::pollOriginal(context_->publication, original, diagnostic))
            return false;
        rhi::TextureReadbackOwnedDelivery delivered;
        delivered.originalRequest = std::move(original.originalRequest);
        delivered.diagnostic = original.diagnostic;
        switch (original.outcome) {
        case Publication::Outcome::Published:
            if (const auto* id =
                    rhi::TextureReadbackOwnedSnapshotContract::validatePublishedOriginal(
                        original.ownedSnapshot)) {
                delivered.outcome = Outcome::Rejected;
                delivered.diagnostic = id;
            } else {
                delivered.outcome = Outcome::Published;
                delivered.snapshot = std::move(original.ownedSnapshot);
            }
            break;
        case Publication::Outcome::Rejected:
            delivered.outcome = Outcome::Rejected;
            break;
        case Publication::Outcome::DeviceLost:
            delivered.outcome = Outcome::DeviceLost;
            break;
        case Publication::Outcome::Empty:
            diagnostic = "RHI-READBACK-OWNED-PUBLICATION-EMPTY";
            return false;
        }
        output = std::move(delivered);
        return true;
    }

    const char* shutdownOriginal() noexcept override
    {
        if (!context_)
            return "RHI-READBACK-OWNED-CONTEXT-UNCONFIGURED";
        if (const auto* id = Publication::cancelAllOriginal(context_->publication))
            return id;
        const rhi::TextureReadbackOwnedPreparationContract::ShutdownFacts facts{
            context_->publication->callbacksOutstandingOriginal() ? 1u : 0u,
            context_->completion->retained.failed() && context_->completion->retained.pending()
                ? 1u
                : 0u,
            context_->publication->pending(),
            context_->completion->originalLossDelivered()};
        using Shutdown = rhi::TextureReadbackOwnedPreparationContract::ShutdownDisposition;
        switch (rhi::TextureReadbackOwnedPreparationContract::shutdownDisposition(facts)) {
        case Shutdown::DrainIssuedCallbacks:
            return "RHI-READBACK-OWNED-CONTEXT-CALLBACKS-DRAINING";
        case Shutdown::AwaitNativeLoss:
            return "RHI-READBACK-OWNED-CONTEXT-QUARANTINE";
        case Shutdown::DrainUnretiredRequests:
            return "RHI-READBACK-OWNED-CONTEXT-PENDING-REQUESTS";
        case Shutdown::DetachIdleOrCompleted:
            break;
        }
        if (context_->staging->usedBytes())
            return "RHI-READBACK-OWNED-CONTEXT-RETAINED-STAGING";
        if (!context_->completion->originalLossDelivered())
            if (const auto* id =
                    WebGpuOrdinaryCompletionOwner::detachOriginalDeviceLoss(context_->completion))
                return id;
        context_.reset();
        return nullptr;
    }

private:
    friend class arp::test::WebGpuOwnedReadbackContextFixture;
    friend class arp::test::WebGpuOwnedReadbackAdvancementFixture;
    const std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    core::UniquePtr<Context> context_;
    bool bindingConsumed_ = false;
};
} // namespace arp::rhi_webgpu
