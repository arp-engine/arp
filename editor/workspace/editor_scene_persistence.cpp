#include "editor/workspace/editor_scene_persistence.h"

#include "core/io/filesystem_path.h"

#include <system_error>

namespace arp::editor {
EditorScenePersistence::EditorScenePersistence()
    : files_(4u, EditorSceneAuthoringCodec::maxDocumentBytes), store_(files_)
{}

bool EditorScenePersistence::SceneSchema::validate(
    const core::YamlContract::Node& root, core::pmr::String& diagnostic)
{
    core::pmr::String yaml(&core::defaultCpuMemoryResource());
    try {
        const auto serialized = core::YamlContract::serialize(root);
        yaml.assign(serialized.data(), serialized.size());
    } catch (...) {
        diagnostic = "EDITOR-SCENE-YAML-SERIALIZE";
        return false;
    }
    return EditorSceneAuthoringCodec::validateStructure(yaml, diagnostic);
}

bool EditorScenePersistence::resolvePath(const std::filesystem::path& projectRoot,
    std::string_view relativePath,
    bool requireExistingFile,
    std::filesystem::path& resolved,
    const char*& diagnostic)
{
    if (!core::FilePath::isSafeRelativePath(relativePath)) {
        diagnostic = "EDITOR-SCENE-PATH-UNSAFE";
        return false;
    }
    std::error_code error;
    const auto root = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(root, error) || error) {
        diagnostic = "EDITOR-SCENE-PROJECT-ROOT";
        return false;
    }
    const auto candidate = root / std::filesystem::u8path(relativePath);
    if (requireExistingFile) {
        resolved = std::filesystem::canonical(candidate, error);
        if (error || !std::filesystem::is_regular_file(resolved, error) || error) {
            diagnostic = "EDITOR-SCENE-FILE-UNREADABLE";
            return false;
        }
    } else {
        const auto parent = std::filesystem::canonical(candidate.parent_path(), error);
        if (error || !std::filesystem::is_directory(parent, error) || error) {
            diagnostic = "EDITOR-SCENE-PARENT-UNAVAILABLE";
            return false;
        }
        resolved = parent / candidate.filename();
        if (std::filesystem::exists(resolved, error)) {
            if (error || !std::filesystem::is_regular_file(resolved, error) || error) {
                diagnostic = "EDITOR-SCENE-TARGET-NOT-FILE";
                return false;
            }
            resolved = std::filesystem::canonical(resolved, error);
            if (error) {
                diagnostic = "EDITOR-SCENE-TARGET-UNRESOLVABLE";
                return false;
            }
        } else if (error) {
            diagnostic = "EDITOR-SCENE-TARGET-STAT";
            return false;
        }
    }
    if (!core::FilePath::contains(root, resolved)) {
        diagnostic = "EDITOR-SCENE-PATH-ESCAPES-PROJECT";
        return false;
    }
    return true;
}

EditorScenePersistence::RequestHandle EditorScenePersistence::load(
    const std::filesystem::path& projectRoot,
    std::string_view relativePath,
    std::filesystem::path* resolvedTarget)
{
    diagnostic_ = nullptr;
    std::filesystem::path resolved;
    if (!resolvePath(projectRoot, relativePath, true, resolved, diagnostic_))
        return {};
    const auto request = store_.load(core::FilePath::utf8(resolved));
    if (request && resolvedTarget)
        *resolvedTarget = std::move(resolved);
    return request;
}

EditorScenePersistence::RequestHandle EditorScenePersistence::save(
    const std::filesystem::path& projectRoot,
    std::string_view relativePath,
    const core::pmr::String& yaml,
    bool overwriteExisting)
{
    diagnostic_ = nullptr;
    if (yaml.empty() || yaml.size() > EditorSceneAuthoringCodec::maxDocumentBytes) {
        diagnostic_ = "EDITOR-SCENE-DOCUMENT-LIMIT";
        return {};
    }
    std::filesystem::path resolved;
    if (!resolvePath(projectRoot, relativePath, false, resolved, diagnostic_))
        return {};
    std::error_code error;
    const bool exists = std::filesystem::exists(resolved, error);
    if (error) {
        diagnostic_ = "EDITOR-SCENE-TARGET-STAT";
        return {};
    }
    if (exists && !overwriteExisting) {
        diagnostic_ = "EDITOR-SCENE-OVERWRITE-REQUIRED";
        return {};
    }
    return saveResolved(resolved, yaml);
}

EditorScenePersistence::RequestHandle EditorScenePersistence::readResolved(
    const std::filesystem::path& target)
{
    diagnostic_ = nullptr;
    return files_.read(core::FilePath::utf8(target));
}

EditorScenePersistence::RequestHandle EditorScenePersistence::saveResolved(
    const std::filesystem::path& target, const core::pmr::String& yaml)
{
    diagnostic_ = nullptr;
    if (yaml.empty() || yaml.size() > EditorSceneAuthoringCodec::maxDocumentBytes) {
        diagnostic_ = "EDITOR-SCENE-DOCUMENT-LIMIT";
        return {};
    }
    return store_.save(core::FilePath::utf8(target), yaml);
}

bool EditorScenePersistence::take(RequestHandle handle, core::io::AtomicFileResult& result)
{
    return store_.take(handle, result);
}

} // namespace arp::editor
