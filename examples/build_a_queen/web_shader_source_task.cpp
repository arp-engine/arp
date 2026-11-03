#include "web_shader_source_task.h"

#include "core/diagnostics/diagnostic_error.h"
#include "resource/resource_graph_executor.h"
#include "resource/shader_artifact_resource.h"
#include "resource/shader_artifact_source.h"

namespace arp::build_a_queen {
namespace {
using Task = WebShaderSourceTask;
constexpr const char* resourceId = "948375d8-b79c-454d-94a4-a6802470e19c";

shader::WebGlShaderLoadBudget budget()
{
    return {
        {Task::maxFileBytes, Task::maxMetadataBytes, Task::maxMetadataBytes, 32}, 256, 32, 1024};
}

// One graph node keeps all four programs' acquired inputs. The immutable
// snapshot outlives graph retirement and contains no backend or FileService.
class ShaderSourcesResource final : public resource::ResourceBase {
public:
    static constexpr const char* resourceTypeName() { return "QueenWebShaderSources"; }

    explicit ShaderSourcesResource(resource::ResourceFileOrigin origin)
        : ResourceBase(
              {resourceId, resourceTypeName(), "queen.web.shaders", "Prototype Complete", {}}),
          origin_(origin)
    {}

    bool planFileReadIntents(resource::ResourceManager& manager,
        const resource::ResourceLoadContext& context,
        core::pmr::Vector<resource::ResourceFileReadIntent>& pending,
        core::pmr::String& diagnostic) const override
    {
        for (const auto* publication : Task::publications) {
            resource::ShaderArtifactResourceDesc desc;
            desc.publication = publication;
            desc.origin = origin_;
            desc.budget = budget();
            resource::ShaderArtifactResource planner(reference(), std::move(desc));
            if (!planner.planFileReadIntents(manager, context, pending, diagnostic))
                return false;
        }
        return true;
    }

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        core::pmr::Vector<resource::ResourceFileReadIntent> pending;
        core::pmr::String diagnostic;
        if (!context.sources || !context.requirePreloadedSources
            || context.sources->size() > Task::maxFiles
            || context.sources->totalBytes() > Task::maxSourceBytes
            || !planFileReadIntents(manager, context, pending, diagnostic) || !pending.empty())
            return {false,
                resource::ResourceState::Failed,
                reference().id,
                reference().type,
                diagnostic.empty() ? "QUEEN-SHADER-SOURCES-INCOMPLETE" : diagnostic};
        auto candidate = core::makeUnique<Snapshot>(origin_);
        for (std::size_t i = 0; i < context.sources->size(); ++i)
            if (!candidate->files.add(*context.sources->sourceAt(i)))
                return {false,
                    resource::ResourceState::Failed,
                    reference().id,
                    reference().type,
                    "QUEEN-SHADER-SOURCES-AMBIGUOUS"};
        snapshot_ = std::move(candidate);
        return manager.admitResourceMetadata(*this, context.policy);
    }

    std::size_t processedByteCount() const noexcept override { return fileCountBytes(); }

    void appendContentFingerprint(core::StableHash64& hash) const noexcept override
    {
        resource::appendResourceOriginFingerprint(hash, origin_);
        if (snapshot_)
            for (std::size_t i = 0; i < snapshot_->files.size(); ++i) {
                const auto& input = *snapshot_->files.sourceAt(i);
                hash.appendString(input.path);
                hash.appendBytes(input.bytes.data(), input.bytes.size());
            }
    }

    const shader::ShaderArtifactSource* source() const noexcept
    {
        return snapshot_ ? &snapshot_->projection : nullptr;
    }

    std::size_t fileCount() const noexcept { return snapshot_ ? snapshot_->files.size() : 0; }

private:
    struct Snapshot {
        resource::ResourceSourceSet files;
        resource::ResourceLoadContext context;
        resource::AcquiredShaderArtifactSource projection;

        explicit Snapshot(resource::ResourceFileOrigin origin)
            : projection(context, origin, nullptr)
        {
            context.requirePreloadedSources = true;
            context.maxDecodedBytes = Task::maxMetadataBytes;
            context.sources = &files;
        }
    };

    std::size_t fileCountBytes() const noexcept
    {
        return snapshot_ ? snapshot_->files.totalBytes() : 0;
    }

    resource::ResourceFileOrigin origin_;
    core::UniquePtr<Snapshot> snapshot_;
};
} // namespace

WebShaderSourceTask::WebShaderSourceTask(core::io::FileService& files,
    core::runtime::CompletionQueue& queue,
    core::PoolHandle mount,
    core::runtime::CancellationToken cancellation)
{
    resource::ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = resourceId;
    resource::ResourceGraphNode node;
    node.resource = core::makeUnique<ShaderSourcesResource>(
        resource::ResourceFileOrigin{core::io::FilePathScope::MountedResource, mount});
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, maxFiles, maxSourceBytes, maxMetadataBytes, 2};
    task_ = core::makeUnique<StartupResourceTask>(
        files, queue, std::move(request), queenStartupDiagnostics, std::move(cancellation));
}

WebShaderSourceTask::~WebShaderSourceTask() = default;

void WebShaderSourceTask::cancel() noexcept
{
    task_->cancel();
}

void WebShaderSourceTask::advance() noexcept
{
    task_->advance();
}

WebShaderSourcePhase WebShaderSourceTask::phase() const noexcept
{
    return task_->phase();
}

std::string_view WebShaderSourceTask::diagnostic() const noexcept
{
    if (phase() == WebShaderSourcePhase::Cancelled)
        return "QUEEN-SHADER-SOURCES-CANCELLED";
    const auto diagnostic = task_->diagnostic();
    if (diagnostic == "QUEEN-STARTUP-GRAPH-NOT-ACCEPTED")
        return "QUEEN-SHADER-GRAPH-NOT-ACCEPTED";
    if (diagnostic == "QUEEN-STARTUP-RESOURCE-PREPARATION")
        return "QUEEN-SHADER-SOURCES-PREPARATION";
    return diagnostic;
}

const shader::ShaderArtifactSource* WebShaderSourceTask::source() const noexcept
{
    const auto* prepared = phase() == WebShaderSourcePhase::Ready
        ? task_->resources().find<ShaderSourcesResource>(resourceId).get()
        : nullptr;
    return prepared ? prepared->source() : nullptr;
}

std::size_t WebShaderSourceTask::fileCount() const noexcept
{
    const auto* prepared = phase() == WebShaderSourcePhase::Ready
        ? task_->resources().find<ShaderSourcesResource>(resourceId).get()
        : nullptr;
    return prepared ? prepared->fileCount() : 0;
}

std::uint64_t WebShaderSourceTask::readCount() const noexcept
{
    return task_->readCount();
}

std::uint64_t WebShaderSourceTask::bytesRead() const noexcept
{
    return task_->bytesRead();
}
} // namespace arp::build_a_queen
