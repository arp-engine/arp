#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "editor/workspace/editor_scene_authoring_codec.h"

#include <cstdio>
#include <string_view>

namespace {
using namespace arp;

class SceneMigration final {
public:
    static int run(const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::string_view identity)
    {
        const auto input =
            core::readTextFile(source, editor::EditorSceneAuthoringCodec::maxDocumentBytes);
        if (!input.success) {
            std::fprintf(stderr, "%s\n", input.diagnostics.c_str());
            return 1;
        }
        core::pmr::String output, diagnostic;
        if (!editor::EditorSceneAuthoringCodec::migrateVersion2(input.text,
                identity == "none" ? std::string_view{} : identity,
                output,
                diagnostic)) {
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
            return 1;
        }
        // Publish a new bundle only. Existing files and the v2 source never
        // participate in an in-place replacement or implicit schema upgrade.
        core::io::StagedDirectory staging;
        if (!staging.begin(destination)) {
            std::fprintf(stderr, "%s\n", staging.diagnostic().c_str());
            return 1;
        }
        const auto written =
            core::io::replaceFileAtomically(core::FilePath::utf8(staging.path() / "scene.arp.yaml"),
                {output.begin(), output.end()});
        if (!written.success || !staging.publish()) {
            std::fprintf(stderr,
                "%s\n",
                written.success ? staging.diagnostic().c_str() : written.diagnostic.c_str());
            return 1;
        }
        std::puts("Editor scene v2 -> v3 migrated; resource readiness is checked on project load.");
        return 0;
    }

    static int usage()
    {
        std::fprintf(stderr,
            "Usage: arp_editor_scene_migrate <source-v2> <new-output-directory> "
            "--runtime-scene <uuid|none>\n");
        return 2;
    }
};
} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
    if (argc != 5 || std::wstring_view(argv[3]) != L"--runtime-scene")
        return SceneMigration::usage();
    // Paths remain native UTF-16; the UUID/none grammar is explicitly ASCII.
    // Reject before allocation rather than interpreting an identity as a path.
    const std::wstring_view input(argv[4]);
    if (input.empty() || input.size() > 36)
        return SceneMigration::usage();
    arp::core::pmr::String identity;
    for (wchar_t c : input) {
        if (c > 127) {
            std::fprintf(stderr, "EDITOR-SCENE-RUNTIME-IDENTITY-INVALID\n");
            return 2;
        }
        identity.push_back(static_cast<char>(c));
    }
    return SceneMigration::run(argv[1], argv[2], identity);
}
#else
int main(int argc, char** argv)
{
    if (argc != 5 || std::string_view(argv[3]) != "--runtime-scene")
        return SceneMigration::usage();
    return SceneMigration::run(
        std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]), argv[4]);
}
#endif
