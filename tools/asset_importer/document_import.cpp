#include "tools/asset_importer/document_import.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/io/file_save_contract.h"
#include "core/io/filesystem_path.h"
#include "tools/asset_importer/asset_importer.h"
#include "tools/io/cold_tool_files.h"

namespace arp::tools::asset_importer {
namespace {
using namespace core::io;

struct Location {
    core::PoolHandle mount;
    core::pmr::String path{&core::defaultCpuMemoryResource()};
};

Location bindLocation(tools::io::ColdToolFiles& files,
    core::StringView name,
    core::StringView value,
    core::StringView httpBase,
    bool writable = false)
{
    if (value.empty())
        throw core::DiagnosticError("TOOL-DOCUMENT-LOCATION");
    Location output;
    if (!httpBase.empty()) {
        if (writable)
            throw core::DiagnosticError("TOOL-DOCUMENT-HTTP-SAVE-UNSUPPORTED");
        output.mount = files.mountHttp(name, httpBase);
        output.path = value;
    } else {
        const auto path =
            std::filesystem::absolute(std::filesystem::u8path(value.begin(), value.end()))
                .lexically_normal();
        output.mount = files.mountDirectory(name, path.parent_path(), writable);
        output.path = core::FilePath::utf8(path.filename());
    }
    if (!output.mount)
        throw core::DiagnosticError("TOOL-DOCUMENT-MOUNT");
    return output;
}

FileReadRequest request(const Location& location, std::uint64_t budget)
{
    FileReadRequest result;
    result.contractVersion = 1;
    result.pathScope = FilePathScope::MountedResource;
    result.mount = location.mount;
    result.path = location.path;
    result.maxBytes = budget;
    return result;
}

FileTextReadResult text(
    tools::io::ColdToolFiles& files, const Location& location, std::uint64_t budget)
{
    FileTextReadRequest desc;
    desc.file = request(location, budget);
    desc.text.maxOutputBytes = budget;
    desc.text.bom = core::TextBomPolicy::Strip;
    desc.text.nul = core::TextNulPolicy::Reject;
    auto input = files.readText(desc);
    if (input.status() != FileIoStatus::Success)
        throw core::DiagnosticError(
            input.diagnosticId() ? input.diagnosticId() : "TOOL-DOCUMENT-READ");
    return input;
}
} // namespace

int DocumentImporter::run(
    const DocumentImportOptions& options, std::ostream& out, std::ostream& err) const
{
    using tools::io::ColdToolFiles;
    const bool legacyPsd = !options.legacyPsd.empty() || !options.legacyManifest.empty();
    const unsigned modes = !options.material.empty() + !options.gltf.empty()
        + !options.legacyLayout.empty() + !options.uiIr.empty() + legacyPsd;
    if (modes != 1 || !ColdToolFiles::validBudget(options.maxDocumentBytes)) {
        err << "TOOL-DOCUMENT-OPTIONS\n";
        return 2;
    }
    if ((legacyPsd || !options.legacyLayout.empty()) && !options.legacyUiJson) {
        err << "TOOL-UI-LEGACY-JSON-OPT-IN-REQUIRED: use --ui-ir YAML or explicitly --legacy-ui-json\n";
        return 2;
    }
    if ((legacyPsd || !options.uiIr.empty()) && options.outputUi.empty()) {
        err << "TOOL-UI-OUTPUT-REQUIRED\n";
        return 2;
    }
    try {
        ColdToolFiles files(options.maxDocumentBytes, 3);
        const auto& sourcePath = !options.uiIr.empty() ? options.uiIr
            : legacyPsd                                ? options.legacyPsd
            : !options.legacyLayout.empty()            ? options.legacyLayout
            : !options.material.empty()                ? options.material
                                                       : options.gltf;
        auto source = text(files,
            bindLocation(files, "source", sourcePath, options.httpBase),
            options.maxDocumentBytes);
        if (!options.material.empty()) {
            const auto result = assets::importSceneMaterialFromJson(source.decoded.text);
            if (!result.success)
                throw core::DiagnosticError(result.diagnostics.c_str());
            out << "material ok: " << result.material.name << '\n';
        } else if (!options.gltf.empty()) {
            const auto result = assets::importStaticMeshFromGltfJson(source.decoded.text);
            if (!result.success)
                throw core::DiagnosticError(result.diagnostics.c_str());
            out << "mesh ok: vertices=" << result.mesh.vertices.size()
                << " indices=" << result.mesh.indices.size() << '\n';
        } else if (!options.legacyLayout.empty()) {
            const auto result = importUiLayoutFromJson(source.decoded.text);
            if (!result.success)
                throw core::DiagnosticError(result.diagnostics.c_str());
            out << "Legacy/GuiLayoutJson/1 ui layout ok: " << result.name
                << " nodes=" << result.cache.nodeCount << '\n';
        } else {
#if !defined(ARP_TOOL_UI_SHARED_AUTHORING) || !ARP_TOOL_UI_SHARED_AUTHORING
            if (legacyPsd)
                (void)text(files,
                    bindLocation(files, "manifest", options.legacyManifest, options.httpBase),
                    options.maxDocumentBytes);
            files.retire();
            throw core::DiagnosticError("TOOL-UI-AUTHORING-SHARED-API-UNAVAILABLE");
#else
            const auto output = bindLocation(files, "output", options.outputUi, {}, true);
            if (!files.service().saveCapability(output.mount).observedBaselineReplace)
                throw core::DiagnosticError("TOOL-UI-SAVE-UNSUPPORTED");
            const auto baseline = files.read(request(output, options.maxDocumentBytes));
            const bool missing = baseline.status == FileIoStatus::MissingFile;
            if (!missing && baseline.status != FileIoStatus::Success)
                throw core::DiagnosticError(
                    baseline.diagnosticId ? baseline.diagnosticId : "TOOL-UI-BASELINE");
            ToolUiDocumentResult compiled;
            if (legacyPsd) {
                auto manifest = text(files,
                    bindLocation(files, "manifest", options.legacyManifest, options.httpBase),
                    options.maxDocumentBytes);
                const auto imported =
                    importUiFromPsdManifest(source.decoded.text, manifest.decoded.text);
                if (!imported.success)
                    throw core::DiagnosticError(imported.diagnostics.c_str());
                compiled = ToolUiDocumentCompiler::compileLegacy(
                    imported.document, options.maxDocumentBytes);
            } else
                compiled = ToolUiDocumentCompiler::compileIr(
                    source.decoded.text, options.maxDocumentBytes);
            if (!compiled.success)
                throw core::DiagnosticError(compiled.diagnostics.c_str());
            FileSaveRequest commit;
            commit.pathScope = FilePathScope::MountedResource;
            commit.mount = output.mount;
            commit.path = output.path;
            commit.maxWriteBytes = options.maxDocumentBytes;
            commit.bytes.assign(compiled.yaml.begin(), compiled.yaml.end());
            if (!missing) {
                core::Sha256 hash;
                hash.update(
                    {reinterpret_cast<const char*>(baseline.bytes.data()), baseline.bytes.size()});
                commit.expectedVersion =
                    FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
                commit.precondition = FileSavePrecondition::ContentSha256;
                commit.maxBaselineBytes = options.maxDocumentBytes;
            }
            const auto expected = FileSaveContract::contentVersion(commit);
            const auto result = files.save(std::move(commit));
            if (result.status != FileIoStatus::Success
                || result.deliveryStatus != FileIoStatus::Success
                || result.commitState != FileCommitState::Committed
                || result.committedVersion != expected)
                throw core::DiagnosticError(
                    result.diagnosticId ? result.diagnosticId : "TOOL-UI-COMMIT");
            out << (legacyPsd ? "Legacy/MockPsdManifest/1 " : "")
                << "arpui YAML ok: nodes=" << compiled.nodeCount
                << " stableHash=" << compiled.stableHash << " output=" << options.outputUi << '\n';
#endif
        }
        files.retire();
        return 0;
    } catch (const std::exception& error) {
        err << error.what() << '\n';
        return 1;
    }
}
} // namespace arp::tools::asset_importer
