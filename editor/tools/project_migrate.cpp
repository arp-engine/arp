#include "core/encoding/yaml_contract.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "editor/project/project_manifest.h"
#include "editor/project/project_metadata.h"
#include "editor/tools/migration_io.h"

#include <cstdio>
#include <exception>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace arp;
using Yaml = core::YamlContract;
using Node = Yaml::Node;

bool write(const std::filesystem::path& directory, core::StringView path, core::StringView text)
{
    const auto result = editor::EditorMigrationIo::writeNewFile(
        directory, path, text, editor::ProjectManifestBudget{}.maxDocumentBytes);
    if (result.status != core::io::FileIoStatus::Success)
        std::fprintf(
            stderr, "%s\n", result.diagnosticId ? result.diagnosticId : "EDITOR-MIGRATION-SAVE");
    return result.status == core::io::FileIoStatus::Success;
}

int migrate(int argc, char** argv)
{
    if (argc < 11 || argc % 2 == 0) {
        std::fprintf(stderr,
            "Usage: arp_editor_project_migrate <source> <new-output-directory> "
            "--project-id <uuid> --name <name> --engine-contract <identity> "
            "--entry-scene <uuid|none> [--editor-metadata <relative-yaml>]\n");
        return 2;
    }
    core::pmr::UnorderedMap<core::pmr::String, core::pmr::String> options{
        &core::defaultCpuMemoryResource()};
    for (int i = 3; i < argc; i += 2) {
        const std::string_view key(argv[i]);
        if ((key != "--project-id" && key != "--name" && key != "--engine-contract"
                && key != "--entry-scene" && key != "--editor-metadata")
            || !options.emplace(key, argv[i + 1]).second) {
            std::fprintf(stderr, "PROJECT-MIGRATION-ARGUMENT-INVALID\n");
            return 2;
        }
    }
    for (const auto* key : {"--project-id", "--name", "--engine-contract", "--entry-scene"})
        if (options.find(core::pmr::String(key, &core::defaultCpuMemoryResource()))
            == options.end()) {
            std::fprintf(stderr, "PROJECT-MIGRATION-ARGUMENT-REQUIRED: %s\n", key);
            return 2;
        }
    const auto source = editor::EditorMigrationIo::readSource(
        std::filesystem::u8path(argv[1]), editor::ProjectManifestBudget{}.maxDocumentBytes);
    if (source.status() != core::io::FileIoStatus::Success) {
        std::fprintf(stderr,
            "%s\n",
            source.diagnosticId() ? source.diagnosticId() : "EDITOR-MIGRATION-SOURCE-READ");
        return 1;
    }
    try {
        const auto legacy = Yaml::parseSingleDocument(source.decoded.text);
        Yaml::onlyKeys(
            legacy, {"format", "version", "assetPackages", "physics2d", "terrainPreviews"});
        if (Yaml::text(legacy, "format") != "ArpEditorProject"
            || Yaml::uint32(legacy, "version") != 1) {
            std::fprintf(stderr, "PROJECT-MIGRATION-SOURCE-VERSION\n");
            return 1;
        }
        auto converted = Node::mapping();
        converted["type"] = "arp.project";
        converted["contractVersion"] = editor::ProjectManifestCodec::contractVersion;
        converted["id"] =
            options.at(core::pmr::String("--project-id", &core::defaultCpuMemoryResource()))
                .c_str();
        converted["name"] =
            options.at(core::pmr::String("--name", &core::defaultCpuMemoryResource())).c_str();
        converted["engineContract"] =
            options.at(core::pmr::String("--engine-contract", &core::defaultCpuMemoryResource()))
                .c_str();
        const auto& scene =
            options.at(core::pmr::String("--entry-scene", &core::defaultCpuMemoryResource()));
        converted["entryScene"] = scene == "none" ? Node() : Node(scene.c_str());
        converted["execution"] = "Interpreter";
        converted["assetPackages"] = Yaml::required(legacy, "assetPackages");
        converted["scriptModules"] = Node::sequence();
        converted["physics2d"] = legacy.contains("physics2d") ? legacy["physics2d"] : Node();
        converted["editorMetadata"] = Node();
        core::pmr::String metadataPath{&core::defaultCpuMemoryResource()};
        core::pmr::String metadataText{&core::defaultCpuMemoryResource()};
        if (legacy.contains("terrainPreviews")) {
            const auto path = options.find(
                core::pmr::String("--editor-metadata", &core::defaultCpuMemoryResource()));
            if (path == options.end()) {
                std::fprintf(stderr, "PROJECT-MIGRATION-EDITOR-METADATA-REQUIRED\n");
                return 1;
            }
            metadataPath = path->second;
            const auto& previews = Yaml::sequence(legacy, "terrainPreviews");
            if (previews.size() > editor::ProjectManifestBudget{}.maxAssetPackages)
                Yaml::fail();
            for (const auto& value : previews) {
                const auto text = Yaml::text(value);
                if (!core::FilePath::isSafeRelativePath(text))
                    Yaml::fail();
            }
            auto metadata = Node::mapping();
            metadata["type"] = "arp.editor-project";
            metadata["contractVersion"] = 1;
            metadata["terrainPreviews"] = legacy["terrainPreviews"];
            metadataText = Yaml::serialize(metadata);
            editor::ProjectMetadata checkedMetadata;
            editor::ProjectDiagnostic metadataDiagnostic;
            if (!editor::ProjectMetadataCodec::decode(
                    metadataText, checkedMetadata, metadataDiagnostic)) {
                std::fprintf(
                    stderr, "%s: %s\n", metadataDiagnostic.code, metadataDiagnostic.field.c_str());
                return 1;
            }
            converted["editorMetadata"] = metadataPath.c_str();
        } else if (options.find(
                       core::pmr::String("--editor-metadata", &core::defaultCpuMemoryResource()))
            != options.end()) {
            std::fprintf(stderr, "PROJECT-MIGRATION-EDITOR-METADATA-UNUSED\n");
            return 1;
        }
        editor::ProjectManifest manifest;
        editor::ProjectDiagnostic diagnostic;
        if (!editor::ProjectManifestCodec::decode(
                Yaml::serialize(converted), manifest, diagnostic)) {
            std::fprintf(stderr, "%s: %s\n", diagnostic.code, diagnostic.field.c_str());
            return 1;
        }
        core::pmr::String output{&core::defaultCpuMemoryResource()};
        if (!editor::ProjectManifestCodec::encode(manifest, output, diagnostic)) {
            std::fprintf(stderr, "%s: %s\n", diagnostic.code, diagnostic.field.c_str());
            return 1;
        }
        core::io::StagedDirectory staging;
        if (!staging.begin(std::filesystem::u8path(argv[2]))) {
            std::fprintf(stderr, "%s\n", staging.diagnostic().c_str());
            return 1;
        }
        if (!metadataPath.empty()) {
            if (metadataPath == "project.arp.yaml"
                || !core::FilePath::isSafeRelativePath(metadataPath)) {
                std::fprintf(stderr, "PROJECT-MIGRATION-EDITOR-METADATA-PATH\n");
                return 1;
            }
            const auto path = staging.path() / std::filesystem::u8path(metadataPath);
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            std::filesystem::path resolved;
            if (error
                || !editor::ProjectManifestCodec::resolvePath(staging.path(),
                    metadataPath,
                    editor::ProjectPathIntent::NewFile,
                    resolved,
                    diagnostic)
                || !write(staging.path(),
                    core::FilePath::utf8(resolved.lexically_relative(staging.path())),
                    metadataText))
                return 1;
        }
        if (!write(staging.path(), "project.arp.yaml", output) || !staging.publish()) {
            std::fprintf(stderr, "%s\n", staging.diagnostic().c_str());
            return 1;
        }
        std::fprintf(stdout,
            "PROJECT-MIGRATION-CONVERTED: source preserved; output is a conversion "
            "bundle, not a copied game project. Validate and adopt its files under the original project root.\n");
        return 0;
    } catch (const core::YamlContractError&) {
        std::fprintf(stderr, "PROJECT-MIGRATION-SOURCE-FIELD\n");
    } catch (const std::exception&) {
        std::fprintf(stderr, "PROJECT-MIGRATION-PREPARATION-FAILED\n");
    }
    return 1;
}
} // namespace

#if defined(_WIN32)
// Native command lines are UTF-16. Convert once at the tool boundary so YAML
// identities, names and filesystem paths share the engine's UTF-8 contract.
int wmain(int argc, wchar_t** argv)
{
    arp::core::pmr::Vector<arp::core::pmr::String> arguments{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<char*> pointers{&arp::core::defaultCpuMemoryResource()};
    arguments.reserve(argc);
    pointers.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        const int length = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (length == 0) {
            std::fprintf(stderr, "PROJECT-MIGRATION-ARGUMENT-ENCODING\n");
            return 2;
        }
        arp::core::pmr::String value(
            static_cast<std::size_t>(length), '\0', &arp::core::defaultCpuMemoryResource());
        if (WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, value.data(), length, nullptr, nullptr)
            != length) {
            std::fprintf(stderr, "PROJECT-MIGRATION-ARGUMENT-ENCODING\n");
            return 2;
        }
        value.resize(static_cast<std::size_t>(length - 1));
        arguments.push_back(std::move(value));
    }
    for (auto& argument : arguments)
        pointers.push_back(argument.data());
    return migrate(argc, pointers.data());
}
#else
int main(int argc, char** argv)
{
    return migrate(argc, argv);
}
#endif
