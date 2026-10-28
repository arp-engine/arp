#include "editor/web/browser_workspace.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"

#include <algorithm>
#include <cstdio>

namespace arp::editor {
namespace {
using Y = core::YamlContract;
using Node = Y::Node;

void require(bool value, const char* diagnostic)
{
    if (!value)
        throw core::DiagnosticError(diagnostic);
}

Node scalar(core::StringView value)
{
    return Node(Node::string_type(value.data(), value.size()));
}
} // namespace

BrowserWorkspace::Archive BrowserWorkspace::decode(core::StringView source)
try {
    require(!source.empty() && source.size() <= maxArchiveBytes, "EDITOR-WEB-ARCHIVE-BUDGET");
    const auto node = Y::parseSingleDocument(source);
    Y::onlyKeys(node, {"type", "contractVersion", "scene", "files"});
    require(
        Y::text(node, "type") == "arp.editor.workspace" && Y::uint32(node, "contractVersion") == 1,
        "EDITOR-WEB-ARCHIVE-VERSION");
    Archive archive;
    archive.scene = Y::text(node, "scene");
    require(archive.scene.empty() || core::FilePath::isSafeRelativePath(archive.scene),
        "EDITOR-WEB-ARCHIVE-SCENE");
    const auto& files = Y::sequence(node, "files");
    require(!files.empty() && files.size() <= maxFiles, "EDITOR-WEB-ARCHIVE-FILE-BUDGET");
    std::size_t bytes = 0;
    for (const auto& entry : files) {
        Y::onlyKeys(entry, {"path", "base64"});
        File file;
        file.path = Y::text(entry, "path");
        require(core::FilePath::isSafeRelativePath(file.path)
                && file.path.find('\0') == core::pmr::String::npos,
            "EDITOR-WEB-ARCHIVE-PATH");
        for (const auto& previous : archive.files) {
            require(previous.path != file.path, "EDITOR-WEB-ARCHIVE-DUPLICATE");
            require(previous.path.rfind(file.path + "/", 0) != 0
                    && file.path.rfind(previous.path + "/", 0) != 0,
                "EDITOR-WEB-ARCHIVE-PATH-CONFLICT");
        }
        const auto encoded = Y::text(entry, "base64");
        require(encoded.size() <= ((maxDecodedBytes - bytes + 2) / 3) * 4,
            "EDITOR-WEB-ARCHIVE-BYTE-BUDGET");
        require(encoded.empty() || core::Base64::decode(encoded, file.bytes),
            "EDITOR-WEB-ARCHIVE-BASE64");
        require(file.bytes.size() <= maxDecodedBytes - bytes, "EDITOR-WEB-ARCHIVE-BYTE-BUDGET");
        bytes += file.bytes.size();
        archive.files.push_back(std::move(file));
    }
    const auto manifest =
        std::find_if(archive.files.begin(), archive.files.end(), [](const File& f) {
            return f.path == "project.arp.yaml";
        });
    require(manifest != archive.files.end(), "EDITOR-WEB-ARCHIVE-PROJECT-MISSING");
    ProjectManifest project;
    ProjectDiagnostic diagnostic;
    require(ProjectManifestCodec::decode(
                {reinterpret_cast<const char*>(manifest->bytes.data()), manifest->bytes.size()},
                project,
                diagnostic),
        "EDITOR-WEB-ARCHIVE-PROJECT-INVALID");
    require(archive.scene.empty()
            || std::any_of(archive.files.begin(),
                archive.files.end(),
                [&](const File& f) {
                    return f.path == archive.scene;
                }),
        "EDITOR-WEB-ARCHIVE-SCENE-MISSING");
    return archive;
} catch (const core::YamlContractError&) {
    throw core::DiagnosticError("EDITOR-WEB-ARCHIVE-YAML");
} catch (const fkyaml::exception&) {
    throw core::DiagnosticError("EDITOR-WEB-ARCHIVE-YAML");
}

BrowserWorkspace::Staged BrowserWorkspace::stage(core::StringView source, std::uint64_t serial)
{
    auto archive = decode(source);
    char name[96];
    std::snprintf(
        name, sizeof(name), "/workspace/import-%020llu", static_cast<unsigned long long>(serial));
    auto directory = core::makeUnique<core::io::StagedDirectory>();
    require(directory->begin(name), "EDITOR-WEB-ARCHIVE-STAGE");
    for (const auto& file : archive.files) {
        const auto path = directory->path() / std::filesystem::path(file.path);
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        require(!error, "EDITOR-WEB-ARCHIVE-DIRECTORY");
        require(core::io::replaceFileAtomically(core::FilePath::utf8(path), file.bytes).success,
            "EDITOR-WEB-ARCHIVE-WRITE");
    }
    Staged result;
    // MEMFS has no exclusive directory-rename primitive. Keep the owned staging
    // directory alive and publish only the fully validated Shell pointer. No
    // disk-atomic publication is claimed for a browser workspace.
    result.root = directory->path();
    result.scene = std::move(archive.scene);
    result.directory = std::move(directory);
    return result;
}

core::pmr::String BrowserWorkspace::encode(
    const std::filesystem::path& root, core::StringView scene)
{
    Node node = Node::mapping();
    node["type"] = "arp.editor.workspace";
    node["contractVersion"] = 1;
    node["scene"] = scalar(scene);
    node["files"] = Node::sequence();
    core::pmr::Vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        require(!entry.is_symlink(), "EDITOR-WEB-ARCHIVE-SYMLINK");
        if (!entry.is_regular_file())
            continue;
        require(paths.size() < maxFiles, "EDITOR-WEB-ARCHIVE-FILE-BUDGET");
        paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    std::size_t bytes = 0;
    for (const auto& path : paths) {
        auto contents = core::readBinaryFile(path, maxDecodedBytes - bytes);
        require(contents.success, "EDITOR-WEB-ARCHIVE-READ");
        bytes += contents.bytes.size();
        Node file = Node::mapping();
        file["path"] = scalar(core::FilePath::utf8(path.lexically_relative(root)));
        file["base64"] = scalar(core::Base64::encode(contents.bytes));
        node["files"].get_value_ref<Node::sequence_type&>().push_back(std::move(file));
    }
    auto encoded = Y::serialize(node);
    (void)decode(encoded);
    return encoded;
}
} // namespace arp::editor
