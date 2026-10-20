#include "renderer/frame/executable_render_feature.h"

#include <algorithm>
#include <cstring>

namespace arp::renderer {
namespace {

RenderFeatureResult failure(const char* id, const char* message)
{
    return {false, id, message};
}

RenderFeatureResult success()
{
    return {true, {}, {}};
}

} // namespace

ExecutableRenderFeatureSet::ExecutableRenderFeatureSet(ExecutableRenderFeatureSetDesc desc)
    : desc_(desc)
{}

RenderFeatureResult ExecutableRenderFeatureSet::registerFeature(IExecutableRenderFeature& feature)
{
    if (created_) {
        return failure("ARP-RENDER-FEATURE-REGISTER-AFTER-CREATE",
            "Executable render features must be registered before create.");
    }
    if (feature.id() == nullptr || feature.id()[0] == '\0') {
        return failure(
            "ARP-RENDER-FEATURE-ID-REQUIRED", "Executable render feature id must be non-empty.");
    }
    for (const auto* existing : features_) {
        if (std::strcmp(existing->id(), feature.id()) == 0) {
            return failure(
                "ARP-RENDER-FEATURE-ID-DUPLICATE", "Executable render feature ids must be unique.");
        }
    }
    if (features_.size() >= desc_.maxFeatureCount) {
        return failure("ARP-RENDER-FEATURE-BUDGET-EXCEEDED",
            "Executable render feature registration exceeded its explicit budget.");
    }
    features_.push_back(&feature);
    std::stable_sort(features_.begin(), features_.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->stage() < rhs->stage();
    });
    return success();
}

RenderFeatureResult ExecutableRenderFeatureSet::create(const RenderFeatureCreateContext& context)
{
    if (created_) {
        return failure("ARP-RENDER-FEATURE-ALREADY-CREATED",
            "Executable render feature set cannot be created twice.");
    }
    if (context.extent.width == 0u || context.extent.height == 0u) {
        return failure("ARP-RENDER-FEATURE-EXTENT-INVALID",
            "Executable render feature extent must be non-zero.");
    }
    createdFeatureCount_ = 0u;
    for (auto* feature : features_) {
        const auto result = feature->create(context);
        if (!result.success) {
            while (createdFeatureCount_ > 0u) {
                features_[--createdFeatureCount_]->release();
            }
            return result;
        }
        ++createdFeatureCount_;
    }
    created_ = true;
    return success();
}

RenderFeatureResult ExecutableRenderFeatureSet::resize(rhi::Extent2D extent)
{
    if (!created_) {
        return failure("ARP-RENDER-FEATURE-NOT-CREATED",
            "Executable render feature set must be created before resize.");
    }
    if (extent.width == 0u || extent.height == 0u) {
        return failure("ARP-RENDER-FEATURE-EXTENT-INVALID",
            "Executable render feature extent must be non-zero.");
    }
    for (auto* feature : features_) {
        const auto result = feature->resize(extent);
        if (!result.success)
            return result;
    }
    return success();
}

RenderFeatureResult ExecutableRenderFeatureSet::contributeGraph(
    const RenderFeatureGraphContext& context)
{
    if (!created_) {
        return failure("ARP-RENDER-FEATURE-NOT-CREATED",
            "Executable render feature set must be created before graph contribution.");
    }
    for (auto* feature : features_) {
        const auto result = feature->contributeGraph(context);
        if (!result.success)
            return result;
    }
    return success();
}

void ExecutableRenderFeatureSet::release() noexcept
{
    while (createdFeatureCount_ > 0u) {
        features_[--createdFeatureCount_]->release();
    }
    created_ = false;
}

std::uint32_t ExecutableRenderFeatureSet::size() const noexcept
{
    return static_cast<std::uint32_t>(features_.size());
}

} // namespace arp::renderer
