#include "content_source_task.h"

#include "core/hash/sha256.h"
#include "resource/resource_source_io.h"

namespace arp::survivors {

core::SharedPtr<PreparedContent> PreparedContent::prepare(
    const resource::ResourceLoadContext& context,
    resource::ResourceFileOrigin origin,
    const GameConfig& config,
    core::pmr::String& diagnostic)
{
    if (!context.sources || !context.requirePreloadedSources
        || context.sources->size() != ContentBindings::maxFiles) {
        diagnostic = "SURVIVORS-CONTENT-SOURCE-CONTEXT";
        return {};
    }
    auto candidate = core::makeShared<PreparedContent>();
    if (!candidate->content_.loadPrepared(context, origin, config, diagnostic))
        return {};
    // Composite identity uses original bytes in the graph's explicit input
    // order, with lengths separating paths and payloads. No YAML rewriting.
    core::Sha256 hash;
    for (std::size_t i = 0; i < context.sources->size(); ++i) {
        const auto& source = *context.sources->sourceAt(i);
        core::io::FileVersionEncoding::appendNumber(hash, source.path.size());
        hash.update(source.path);
        core::io::FileVersionEncoding::appendNumber(hash, source.bytes.size());
        hash.update({reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size()});
    }
    candidate->sourceIdentity_ = "sha256-base64:";
    candidate->sourceIdentity_ += core::Base64::encode(hash.finalize());
    return candidate;
}

namespace {
constexpr const char* rootId = "20000000-0000-4000-8000-000000009052";
constexpr examples::StartupResourceDiagnostics diagnostics{"SURVIVORS-CONTENT-CANCELLED",
    "SURVIVORS-CONTENT-GRAPH-NOT-ACCEPTED",
    "SURVIVORS-CONTENT-PREPARATION"};

bool validProfile(const GameConfig& config) noexcept
{
    return config.maxSourceBytes && config.maxSourceBytes <= ContentSourceTask::maxSourceFileBytes
        && config.maxTextureBytes
        && config.maxTextureBytes <= ContentSourceTask::maxTextureFileBytes
        && config.maxOrbitBlades < UINT32_MAX;
}

class ContentResource final : public resource::ResourceBase {
public:
    ContentResource(core::PoolHandle mount, const GameConfig& config)
        : ResourceBase({rootId, "SurvivorsContent", {}, "Prototype Complete", {}}),
          origin_{core::io::FilePathScope::MountedResource, mount}, config_(config)
    {}

    bool planFileReadIntents(resource::ResourceManager&,
        const resource::ResourceLoadContext& context,
        core::pmr::Vector<resource::ResourceFileReadIntent>& reads,
        core::pmr::String& diagnostic) const override
    {
        if (!validProfile(config_)) {
            diagnostic = "SURVIVORS-CONTENT-SOURCE-PROFILE";
            return false;
        }
        return Content::planSourceReads(context, origin_, config_, reads, diagnostic);
    }

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        core::pmr::String diagnostic;
        auto candidate = PreparedContent::prepare(context, origin_, config_, diagnostic);
        if (!candidate)
            return resource::resourceLoadFailure(*this, diagnostic.c_str());
        prepared_ = std::move(candidate);
        return manager.admitResourceMetadata(*this, context.policy);
    }

    std::size_t processedByteCount() const noexcept override
    {
        return prepared_ ? prepared_->content().processedByteCount() : 0;
    }

    void appendContentFingerprint(core::StableHash64& hash) const noexcept override
    {
        resource::appendResourceOriginFingerprint(hash, origin_);
        if (prepared_)
            hash.appendString(prepared_->sourceIdentity());
    }

    const auto& prepared() const noexcept { return prepared_; }

private:
    resource::ResourceFileOrigin origin_;
    GameConfig config_;
    core::SharedPtr<PreparedContent> prepared_;
};

resource::ResourceGraphExecutionRequest request(core::PoolHandle mount, const GameConfig& config)
{
    resource::ResourceGraphExecutionRequest result;
    result.graph.rootResourceId = rootId;
    resource::ResourceGraphNode node;
    node.resource = core::makeUnique<ContentResource>(mount, config);
    result.graph.nodes.push_back(std::move(node));
    // Admission constants belong to ContentBindings/1; invalid caller configs
    // are rejected by planning before any read, not multiplied unchecked here.
    result.budget = {1,
        ContentBindings::maxFiles,
        8 * ContentSourceTask::maxSourceFileBytes + 2 * ContentSourceTask::maxTextureFileBytes,
        8 * ContentSourceTask::maxSourceFileBytes + ContentSourceTask::maxTextureFileBytes,
        2};
    return result;
}
} // namespace

ContentSourceTask::ContentSourceTask(core::io::FileService& files,
    core::runtime::CompletionQueue& queue,
    core::PoolHandle mount,
    const GameConfig& config,
    core::runtime::CancellationToken cancellation)
    : task_(files, queue, request(mount, config), diagnostics, std::move(cancellation))
{}

core::SharedPtr<PreparedContent> ContentSourceTask::preparation() const
{
    if (phase() != examples::StartupResourcePhase::Ready)
        return {};
    const auto root = task_.resources().find<ContentResource>(rootId);
    return root.ready() ? root.get()->prepared() : nullptr;
}
} // namespace arp::survivors
