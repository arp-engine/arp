#pragma once

#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/workspace/editor_recovery_codec.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/script_document_session.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace arp::editor::tests {

// An isolated ordinary workspace. Recovery IO must never touch these formal
// source bytes; the smoke writes its own slot next to them.
class RecoveryFixture final {
public:
    RecoveryFixture()
    {
        root = std::filesystem::path(ARP_SOURCE_DIR) / "build"
            / ("recovery-codec-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assert(std::filesystem::create_directory(root));
    }

    ~RecoveryFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        assert(!error);
    }

    EditorRecoverySnapshot snapshot() const
    {
        EditorRecoverySnapshot value;
        value.sessionId = "4305ee93-3897-43ae-93da-ecbb0c569c83";
        value.sequence = 1;
        value.workspaceEpoch = 7;
        value.projectRoot = core::FilePath::utf8(root);
        value.workspace = "Script";
        value.activeScript = 0;
        EditorSceneDocument scene(false);
        assert(scene.createObject("Recovered object").value() != 0);
        core::pmr::String diagnostic;
        assert(EditorSceneAuthoringCodec::encode(scene, value.scene.yaml, diagnostic));
        value.scene.savedFileBytes = value.scene.yaml;
        value.scene.path = "authoring.arp.yaml";
        value.scene.boundTarget = "authoring.arp.yaml";
        assert(EditorSceneAuthoringCodec::contentHash(
            scene, value.scene.savedContentHash, diagnostic));
        EditorRecoveryScript script;
        script.path = "rules.arp";
        script.savedFileBytes = core::pmr::String("# disk\r\nline\rnext\n\0end", 22);
        script.savedText = ScriptDocumentSession::normalizeNewlines(script.savedFileBytes);
        script.text = "# unsaved edit\n";
        script.newline = "\r\n";
        script.cursor = 4;
        script.anchor = 1;
        script.scrollLine = 2;
        value.scripts.push_back(std::move(script));
        return value;
    }

    static core::pmr::String encode(
        const EditorRecoverySnapshot& value, EditorRecoveryBudget budget = {})
    {
        core::pmr::String result, diagnostic;
        if (!EditorRecoveryCodec::encode(value, result, diagnostic, budget)) {
            std::fprintf(stderr, "recovery encode: %s\n", diagnostic.c_str());
            assert(false);
        }
        return result;
    }

    static core::io::AtomicFileResult finish(
        core::io::AtomicFileService& files, core::PoolHandle handle)
    {
        assert(handle);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        core::io::AtomicFileResult result;
        while (std::chrono::steady_clock::now() < deadline) {
            if (files.take(handle, result))
                return result;
            std::this_thread::yield();
        }
        assert(false && "recovery file completion timeout");
        return {};
    }

    static void write(const std::filesystem::path& path, std::string_view text)
    {
        const core::pmr::Vector<std::uint8_t> bytes(text.begin(), text.end());
        const auto result = core::io::replaceFileAtomically(core::FilePath::utf8(path), bytes);
        assert(result.success && result.committed);
    }

    static core::pmr::String read(const std::filesystem::path& path)
    {
        const auto result = core::readTextFile(path, EditorRecoveryBudget{}.maxEnvelopeBytes);
        assert(result.success);
        return result.text;
    }

    std::filesystem::path root;
};

} // namespace arp::editor::tests
