#include "native_shader_source_task.h"

#include "resource/resource_source_io.h"
#include "resource/shader_artifact_resource.h"

namespace arp::build_a_queen {
namespace {
constexpr std::array<const char*, 7> ids{"092d07ab-e91b-410e-8217-33d5a1253f60",
    "092d07ab-e91b-410e-8217-33d5a1253f61",
    "092d07ab-e91b-410e-8217-33d5a1253f62",
    "092d07ab-e91b-410e-8217-33d5a1253f63",
    "092d07ab-e91b-410e-8217-33d5a1253f64",
    "092d07ab-e91b-410e-8217-33d5a1253f65",
    "092d07ab-e91b-410e-8217-33d5a1253f66"};

// A startup bundle declares its six resources. Every program's original resource
// owns dependency discovery, parsing, hash validation and immutable custody.
class Bundle final : public resource::ResourceBase {
public:
    Bundle()
        : ResourceBase({ids.back(),
              "QueenNativeShaderBundle",
              "queen.native.shaders",
              "Prototype Complete",
              {}})
    {
        for (std::size_t i = 0; i < NativeShaderSources::publications.size(); ++i)
            addDependency({ids[i], "startup-program"});
    }

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        for (std::size_t i = 0; i < NativeShaderSources::publications.size(); ++i) {
            const auto handle = manager.find<resource::ShaderArtifactResource>(ids[i]);
            if (!handle.ready() || !handle.get()->generatedSet())
                return resource::resourceLoadFailure(
                    *this, "QUEEN-NATIVE-SHADER-BUNDLE-INCOMPLETE");
        }
        return manager.admitResourceMetadata(*this, context.policy);
    }
};

resource::ResourceGraphExecutionRequest request(core::PoolHandle mount)
{
    resource::ResourceGraphExecutionRequest result;
    result.graph.rootResourceId = ids.back();
    for (std::size_t i = 0; i < NativeShaderSources::publications.size(); ++i) {
        resource::ShaderArtifactResourceDesc desc;
        desc.publication = NativeShaderSources::publications[i];
        desc.origin = {core::io::FilePathScope::MountedResource, mount};
        desc.budget = shader::GeneratedShaderArtifactSetBudget{NativeShaderSources::maxFileBytes,
            NativeShaderSources::maxSourceBytes,
            8 * 1024 * 1024,
            64,
            8};
        resource::ResourceGraphNode node;
        node.resource = core::makeUnique<resource::ShaderArtifactResource>(
            resource::ResourceReference{ids[i],
                resource::ShaderArtifactResource::resourceTypeName(),
                {},
                "Prototype Complete",
                {}},
            std::move(desc));
        result.graph.nodes.push_back(std::move(node));
    }
    resource::ResourceGraphNode root;
    root.resource = core::makeUnique<Bundle>();
    result.graph.nodes.push_back(std::move(root));
    result.budget = {ids.size(),
        NativeShaderSources::maxFiles,
        NativeShaderSources::maxSourceBytes,
        NativeShaderSources::maxDecodedBytes,
        2};
    return result;
}
} // namespace

bool NativeShaderSources::valid() const noexcept
{
    for (const auto& program : programs)
        if (shader::GeneratedShaderArtifactSet::validateOriginal(program))
            return false;
    return true;
}

NativeShaderSourceTask::NativeShaderSourceTask(core::io::FileService& files,
    core::runtime::CompletionQueue& queue,
    core::PoolHandle mount,
    core::runtime::CancellationToken cancellation)
    : task_(files, queue, request(mount), queenStartupDiagnostics, std::move(cancellation))
{}

void NativeShaderSourceTask::advance() noexcept
{
    task_.advance();
}

void NativeShaderSourceTask::cancel() noexcept
{
    task_.cancel();
}

StartupResourcePhase NativeShaderSourceTask::phase() const noexcept
{
    return task_.phase();
}

std::string_view NativeShaderSourceTask::diagnostic() const noexcept
{
    return task_.diagnostic();
}

std::uint64_t NativeShaderSourceTask::readCount() const noexcept
{
    return task_.readCount();
}

NativeShaderSources NativeShaderSourceTask::sources() const
{
    NativeShaderSources result;
    if (phase() != StartupResourcePhase::Ready)
        return result;
    for (std::size_t i = 0; i < result.programs.size(); ++i) {
        const auto handle = task_.resources().find<resource::ShaderArtifactResource>(ids[i]);
        if (handle.ready())
            result.programs[i] = handle.get()->generatedSet();
    }
    return result;
}
} // namespace arp::build_a_queen
