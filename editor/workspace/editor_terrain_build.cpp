#include "editor/workspace/editor_terrain_build.h"

#include "core/encoding/yaml_contract.h"
#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "resource/terrain_asset_build_service.h"

namespace arp::editor {
namespace {
constexpr std::size_t manifestBudget = EditorProjectResources::maxManifestBytes;
using Yaml = core::YamlContract;
} // namespace

struct EditorTerrainBuild::Task final {
    std::filesystem::path root, authoring, package;
    core::pmr::String packageRelative{&core::defaultCpuMemoryResource()};
    core::pmr::String previousManifest{&core::defaultCpuMemoryResource()};
    core::pmr::String nextManifest{&core::defaultCpuMemoryResource()};
    std::optional<core::pmr::String> expectedManifest;
    bool manifestExisted = false;
    bool completed = false;
    bool ownsOutput = false;
    bool published = false;
    Result result;

    ~Task()
    {
        if (!ownsOutput || published)
            return;
        std::error_code error;
        const auto resolved = std::filesystem::canonical(package, error);
        if (!error && resolved == package && core::FilePath::contains(root, resolved))
            std::filesystem::remove_all(resolved, error);
    }

    static core::runtime::WorkStatus execute(void* context)
    {
        auto& task = *static_cast<Task*>(context);
        try {
            std::error_code error;
            task.manifestExisted = std::filesystem::exists(task.root / "project.arp.yaml", error);
            if (error) {
                task.result.diagnostic = "EDITOR-TERRAIN-MANIFEST-STAT";
                return core::runtime::WorkStatus::Failed;
            }
            Yaml::Node manifest;
            if (task.manifestExisted) {
                const auto path = std::filesystem::canonical(task.root / "project.arp.yaml", error);
                if (error || !core::FilePath::contains(task.root, path)) {
                    task.result.diagnostic = "EDITOR-TERRAIN-MANIFEST-PATH";
                    return core::runtime::WorkStatus::Failed;
                }
                const auto file = core::readTextFile(path, manifestBudget);
                if (!file.success) {
                    task.result.diagnostic = file.diagnostics;
                    return core::runtime::WorkStatus::Failed;
                }
                task.previousManifest = file.text;
                manifest = Yaml::parseSingleDocument(file.text);
            } else {
                manifest = Yaml::Node::mapping();
                manifest["format"] = "ArpEditorProject";
                manifest["version"] = 1;
                manifest["assetPackages"] = Yaml::Node::sequence();
            }
            if (task.expectedManifest && task.previousManifest != *task.expectedManifest) {
                task.result.diagnostic = "EDITOR-TERRAIN-PROJECT-RELOAD-REQUIRED";
                return core::runtime::WorkStatus::Failed;
            }
            // Validate the current project before producing a new package.
            EditorProjectResources previous;
            const auto validated = previous.loadManifest(task.root, Yaml::serialize(manifest));
            if (!validated.success) {
                task.result.diagnostic = validated.diagnostics;
                return core::runtime::WorkStatus::Failed;
            }
            auto& packages = manifest["assetPackages"].get_value_ref<Yaml::Node::sequence_type&>();
            if (packages.size() >= EditorProjectResources::maxAssetPackages) {
                task.result.diagnostic = "EDITOR-PROJECT-MANIFEST-BUDGET";
                return core::runtime::WorkStatus::Failed;
            }
            // Fresh output only: a failed import cannot replace a loaded asset.
            const auto status = std::filesystem::symlink_status(task.package, error);
            if (status.type() != std::filesystem::file_type::not_found
                || (error && error != std::errc::no_such_file_or_directory)) {
                task.result.diagnostic = "EDITOR-TERRAIN-OUTPUT-MUST-BE-NEW";
                return core::runtime::WorkStatus::Failed;
            }
            const auto built =
                resource::TerrainAssetBuildService().build(task.authoring, task.package);
            if (!built.success) {
                task.result.diagnostic = built.diagnostics;
                return core::runtime::WorkStatus::Failed;
            }
            task.ownsOutput = true;
            packages.emplace_back(task.packageRelative.c_str());
            task.nextManifest = Yaml::serialize(manifest);
            task.result.resources = core::makeUnique<EditorProjectResources>();
            const auto loaded = task.result.resources->loadManifest(task.root, task.nextManifest);
            task.result.success = loaded.success;
            task.result.diagnostic = loaded.diagnostics;
            return loaded.success ? core::runtime::WorkStatus::Completed
                                  : core::runtime::WorkStatus::Failed;
        } catch (...) {
            task.result.diagnostic = "EDITOR-TERRAIN-PREPARATION-FAILED";
            return core::runtime::WorkStatus::Failed;
        }
    }

    static void complete(void* context, core::runtime::WorkResult)
    {
        static_cast<Task*>(context)->completed = true;
    }
};

EditorTerrainBuild::EditorTerrainBuild() : runtime_({4u, 1u, 1u, 1u})
{}

EditorTerrainBuild::~EditorTerrainBuild()
{
    runtime_.shutdown(core::runtime::AsyncRuntimeShutdownPolicy::Drain);
}

bool EditorTerrainBuild::start(const std::filesystem::path& projectRoot,
    std::string_view authoringPath,
    std::string_view packagePath,
    std::optional<std::string_view> expectedManifest)
{
    if (task_ || !core::FilePath::isSafeRelativePath(authoringPath)
        || !core::FilePath::isSafeRelativePath(packagePath))
        return false;
    auto candidate = core::makeUnique<Task>();
    if (expectedManifest)
        candidate->expectedManifest.emplace(*expectedManifest, &core::defaultCpuMemoryResource());
    std::error_code error;
    candidate->root = std::filesystem::canonical(projectRoot, error);
    if (error)
        return false;
    candidate->authoring =
        std::filesystem::canonical(candidate->root / std::filesystem::u8path(authoringPath), error);
    if (error || !core::FilePath::contains(candidate->root, candidate->authoring))
        return false;
    candidate->package = std::filesystem::weakly_canonical(
        candidate->root / std::filesystem::u8path(packagePath), error);
    if (error || !core::FilePath::contains(candidate->root, candidate->package)
        || !std::filesystem::is_directory(candidate->package.parent_path(), error) || error)
        return false;
    candidate->packageRelative.assign(packagePath.data(), packagePath.size());
    task_ = std::move(candidate);
    const auto submitted = runtime_.workService().submit(
        {Task::execute, task_.get(), Task::complete, task_.get(), {}});
    if (!submitted.accepted()) {
        task_.reset();
        return false;
    }
    return true;
}

bool EditorTerrainBuild::ready()
{
    runtime_.dispatch();
    return task_ && task_->completed;
}

EditorTerrainBuild::Result EditorTerrainBuild::finish(
    const std::filesystem::path& currentProjectRoot)
{
    Result failed;
    if (!ready()) {
        failed.diagnostic = "EDITOR-TERRAIN-NOT-COMPLETE";
        return failed;
    }
    auto task = std::move(task_);
    if (!task->result.success)
        return std::move(task->result);
    std::error_code error;
    if (std::filesystem::canonical(currentProjectRoot, error) != task->root || error) {
        failed.diagnostic = "EDITOR-TERRAIN-PROJECT-CHANGED";
        return failed;
    }
    const auto path = task->root / "project.arp.yaml";
    const bool exists = std::filesystem::exists(path, error);
    if (error || exists != task->manifestExisted) {
        failed.diagnostic = "EDITOR-TERRAIN-MANIFEST-CONFLICT";
        return failed;
    }
    if (exists) {
        const auto canonical = std::filesystem::canonical(path, error);
        const auto current = core::readTextFile(path, manifestBudget);
        if (error || !core::FilePath::contains(task->root, canonical) || !current.success
            || current.text != task->previousManifest) {
            failed.diagnostic = "EDITOR-TERRAIN-MANIFEST-CONFLICT";
            return failed;
        }
    }
    core::pmr::Vector<std::uint8_t> bytes(
        task->nextManifest.begin(), task->nextManifest.end(), &core::defaultCpuMemoryResource());
    const auto written = core::io::replaceFileAtomically(core::FilePath::utf8(path), bytes);
    if (!written.success) {
        failed.diagnostic = written.diagnostic;
        return failed;
    }
    task->published = true;
    return std::move(task->result);
}
} // namespace arp::editor
