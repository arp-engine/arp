#pragma once

#include "core/io/versioned_yaml_store.h"
#include "editor/workspace/editor_scene_authoring_codec.h"

#include <filesystem>

namespace arp::editor {

class EditorScenePersistence final {
public:
    using RequestHandle = core::PoolHandle;

    EditorScenePersistence();
    RequestHandle load(const std::filesystem::path& projectRoot, std::string_view relativePath);
    RequestHandle save(const std::filesystem::path& projectRoot,
        std::string_view relativePath,
        const core::pmr::String& yaml);
    bool take(RequestHandle handle, core::io::AtomicFileResult& result);

    const char* diagnostic() const noexcept { return diagnostic_; }

private:
    struct SceneSchema {
        static constexpr std::string_view type() noexcept { return "arp.editor-scene"; }

        static constexpr std::uint32_t version() noexcept
        {
            return EditorSceneAuthoringCodec::contractVersion;
        }

        static bool validate(const core::YamlContract::Node& root, core::pmr::String& diagnostic);
    };

    static bool resolvePath(const std::filesystem::path& projectRoot,
        std::string_view relativePath,
        bool requireExistingFile,
        std::filesystem::path& resolved,
        const char*& diagnostic);

    core::io::AtomicFileService files_;
    core::io::VersionedYamlStore<SceneSchema> store_;
    const char* diagnostic_ = nullptr;
};

} // namespace arp::editor
