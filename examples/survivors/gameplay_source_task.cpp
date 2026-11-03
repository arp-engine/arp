#include "gameplay_source_task.h"

#include "resource/resource_source_io.h"

namespace arp::survivors {
namespace {
constexpr const char* resourceId = "20000000-0000-4000-8000-000000009051";
constexpr examples::StartupResourceDiagnostics diagnostics{"SURVIVORS-GAMEPLAY-CANCELLED",
    "SURVIVORS-GAMEPLAY-GRAPH-NOT-ACCEPTED",
    "SURVIVORS-GAMEPLAY-PREPARATION"};
using Limits = GameplayTextLimits;

// This resource belongs to the example's package schema. The engine graph
// only sees explicit read intents and an admitted immutable result.
class GameplayTextResource final : public resource::ResourceBase {
public:
    explicit GameplayTextResource(core::PoolHandle mount)
        : ResourceBase({resourceId, "SurvivorsGameplayText", {}, "Prototype Complete", {}}),
          origin_{core::io::FilePathScope::MountedResource, mount}
    {}

    bool planFileReadIntents(resource::ResourceManager&,
        const resource::ResourceLoadContext& context,
        core::pmr::Vector<resource::ResourceFileReadIntent>& intents,
        core::pmr::String& diagnostic) const override
    {
        if (!resource::appendResourceReadIntent(
                "game.yaml", Limits::maxMetadataBytes, intents, diagnostic, origin_)
            || !resource::appendResourceReadIntent(
                "script-package.yaml", Limits::maxMetadataBytes, intents, diagnostic, origin_))
            return false;
        if (!context.sources || !context.sources->find("game.yaml")
            || !context.sources->find("script-package.yaml"))
            return true;
        core::pmr::String yaml;
        GameConfig config;
        core::pmr::Vector<core::pmr::String> paths;
        if (!metadata(context, yaml, config, paths, diagnostic))
            return false;
        for (const auto& path : paths)
            if (!resource::appendResourceReadIntent(
                    path, config.maxSourceBytes, intents, diagnostic, origin_))
                return false;
        return true;
    }

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        auto candidate = core::makeShared<PreparedGameplay>();
        core::pmr::String yaml, diagnostic;
        core::pmr::Vector<core::pmr::String> paths, parts;
        if (!metadata(context, yaml, candidate->config, paths, diagnostic))
            return {false,
                resource::ResourceState::Failed,
                reference().id,
                reference().type,
                std::move(diagnostic)};
        for (const auto& path : paths) {
            core::pmr::String part;
            if (!text(path, context, candidate->config.maxSourceBytes, part, diagnostic))
                return {false,
                    resource::ResourceState::Failed,
                    reference().id,
                    reference().type,
                    std::move(diagnostic)};
            parts.push_back(std::move(part));
        }
        if (!candidate->rules.prepare(yaml, parts, candidate->config.maxSourceBytes, diagnostic))
            return {false,
                resource::ResourceState::Failed,
                reference().id,
                reference().type,
                std::move(diagnostic)};
        prepared_ = std::move(candidate);
        return manager.admitResourceMetadata(*this, context.policy);
    }

    std::size_t processedByteCount() const noexcept override
    {
        return prepared_ ? prepared_->rules.source().size() + prepared_->rules.configYaml().size()
                         : 0;
    }

    void appendContentFingerprint(core::StableHash64& hash) const noexcept override
    {
        resource::appendResourceOriginFingerprint(hash, origin_);
        if (prepared_) {
            const auto& rules = prepared_->rules;
            hash.appendU64(rules.source().size());
            hash.appendString(rules.source());
            hash.appendU64(rules.configYaml().size());
            hash.appendString(rules.configYaml());
        }
    }

    const auto& prepared() const noexcept { return prepared_; }

private:
    bool text(const core::pmr::String& path,
        const resource::ResourceLoadContext& context,
        std::size_t budget,
        core::pmr::String& output,
        core::pmr::String& diagnostic) const
    {
        // Never permit a prepared-input failure to reopen a host path.
        if (!context.requirePreloadedSources || !context.sources) {
            diagnostic = "SURVIVORS-GAMEPLAY-SOURCE-CONTEXT";
            return false;
        }
        auto source = resource::readResourceSource(path, context, budget, true, &origin_);
        if (!source.success) {
            diagnostic = std::move(source.diagnostics);
            return false;
        }
        core::TextDecodeDesc desc;
        desc.bom = core::TextBomPolicy::Preserve;
        desc.nul = core::TextNulPolicy::Reject;
        desc.maxOutputBytes = budget;
        auto decoded = core::TextDecoder::decode(
            {source.bytes.empty() ? "" : reinterpret_cast<const char*>(source.bytes.data()),
                source.bytes.size()},
            desc);
        if (decoded.status != core::TextDecodeStatus::Success) {
            diagnostic = decoded.diagnosticId;
            return false;
        }
        output = std::move(decoded.text);
        return true;
    }

    bool metadata(const resource::ResourceLoadContext& context,
        core::pmr::String& yaml,
        GameConfig& config,
        core::pmr::Vector<core::pmr::String>& paths,
        core::pmr::String& diagnostic) const
    {
        core::pmr::String manifest;
        if (!text("game.yaml", context, Limits::maxMetadataBytes, yaml, diagnostic)
            || !config.decode(yaml, diagnostic)
            || !text("script-package.yaml", context, Limits::maxMetadataBytes, manifest, diagnostic)
            || !Rules::sourcePaths(manifest, paths, diagnostic))
            return false;
        if (!config.maxSourceBytes || config.maxSourceBytes > Limits::maxSourceBytes
            || paths.size() > Limits::maxScripts) {
            diagnostic = "SURVIVORS-GAMEPLAY-SOURCE-PROFILE";
            return false;
        }
        for (const auto& path : paths)
            if (path.size() > Limits::maxPathBytes) {
                diagnostic = "SURVIVORS-GAMEPLAY-SOURCE-PROFILE";
                return false;
            }
        return true;
    }

    resource::ResourceFileOrigin origin_;
    core::SharedPtr<const PreparedGameplay> prepared_;
};

resource::ResourceGraphExecutionRequest request(core::PoolHandle mount)
{
    resource::ResourceGraphExecutionRequest result;
    result.graph.rootResourceId = resourceId;
    resource::ResourceGraphNode node;
    node.resource = core::makeUnique<GameplayTextResource>(mount);
    result.graph.nodes.push_back(std::move(node));
    result.budget = {1, Limits::maxScripts + 2, Limits::maxInputBytes, Limits::maxInputBytes, 2};
    return result;
}
} // namespace

GameplaySourceTask::GameplaySourceTask(core::io::FileService& files,
    core::runtime::CompletionQueue& queue,
    core::PoolHandle mount,
    core::runtime::CancellationToken cancellation)
    : task_(files, queue, request(mount), diagnostics, std::move(cancellation))
{}

core::SharedPtr<const PreparedGameplay> GameplaySourceTask::preparation() const
{
    if (phase() != examples::StartupResourcePhase::Ready)
        return {};
    const auto root = task_.resources().find<GameplayTextResource>(resourceId);
    return root.ready() ? root.get()->prepared() : nullptr;
}
} // namespace arp::survivors
