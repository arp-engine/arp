#pragma once

#include "core/io/versioned_yaml_store.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_script_file_preparation.h"

#include <filesystem>

namespace arp::editor {

class EditorScenePersistence final {
public:
    using RequestHandle = core::PoolHandle;

    EditorScenePersistence();
    RequestHandle load(const std::filesystem::path& projectRoot,
        std::string_view relativePath,
        std::filesystem::path* resolvedTarget = nullptr);
    RequestHandle save(const std::filesystem::path& projectRoot,
        std::string_view relativePath,
        const core::pmr::String& yaml,
        bool overwriteExisting = true);
    bool take(RequestHandle handle, core::io::AtomicFileResult& result);

    const char* diagnostic() const noexcept { return diagnostic_; }

    // Resolved targets retain the exact path admitted at submission. Content IO
    // stays on the existing worker, including the raw-byte save preflight.
    RequestHandle readResolved(const std::filesystem::path& target);
    RequestHandle saveResolved(const std::filesystem::path& target, const core::pmr::String& yaml);
    // Explicit v4 path. Complete contextual validation runs on the owner thread;
    // the worker owns only new/expected bytes and checks the disk precondition.
    RequestHandle saveScript(const EditorSceneDocument&,
        const EditorSceneScriptInputs&,
        const EditorSceneScriptFilePreparation::Prepared&,
        core::pmr::String& diagnostic);
    // Raw asynchronous read for v4. Successful IO is not scene admission: the
    // owner must prepare/decode through EditorSceneScriptCodec before publishing.
    RequestHandle readScript(const std::filesystem::path& projectRoot,
        std::string_view relativePath,
        std::filesystem::path* resolvedTarget = nullptr);
    static bool resolvePath(const std::filesystem::path& projectRoot,
        std::string_view relativePath,
        bool requireExistingFile,
        std::filesystem::path& resolved,
        const char*& diagnostic);

private:
    struct SceneSchema {
        static constexpr std::string_view type() noexcept { return "arp.editor-scene"; }

        static constexpr std::uint32_t version() noexcept
        {
            return EditorSceneAuthoringCodec::contractVersion;
        }

        static bool validate(const core::YamlContract::Node& root, core::pmr::String& diagnostic);
    };

    core::io::AtomicFileService files_;
    core::io::VersionedYamlStore<SceneSchema> store_;
    const char* diagnostic_ = nullptr;
};

} // namespace arp::editor
