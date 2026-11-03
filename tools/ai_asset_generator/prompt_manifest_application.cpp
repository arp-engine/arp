#include "core/diagnostics/diagnostic_error.h"
#include "core/io/file_save_contract.h"
#include "tools/ai_asset_generator/ai_asset_generator.h"
#include "tools/io/cold_tool_files.h"

#include <charconv>

namespace arp::tools::ai_asset_generator {

Options parseOptions(int argc, const char* const* argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const core::StringView arg(argv[index]);
        if (arg == "--self-test")
            options.selfTest = true;
        else if (arg == "--manifest-only")
            options.manifestOnly = true;
        else if (arg == "--out" && index + 1 < argc) {
            options.outputDirectory = argv[++index];
            options.valid = options.valid && !options.outputDirectory.empty();
        } else if (arg == "--max-document-bytes" && index + 1 < argc) {
            const core::StringView number(argv[++index]);
            const auto parsed = std::from_chars(
                number.data(), number.data() + number.size(), options.maxDocumentBytes);
            options.valid = options.valid && parsed.ec == std::errc{}
                && parsed.ptr == number.data() + number.size()
                && io::ColdToolFiles::validBudget(options.maxDocumentBytes);
        } else
            options.valid = false;
    }
    return options;
}

Result PromptManifestApplication::run(const Options& options) const
{
    using namespace core::io;
    Result result;
    if (!options.valid || !io::ColdToolFiles::validBudget(options.maxDocumentBytes)
        || options.outputDirectory.empty()) {
        result.diagnostics = "AI-MANIFEST-OPTIONS";
        return result;
    }
    if (!options.selfTest && !options.manifestOnly) {
        result.diagnostics =
            "AI-MANIFEST-MODE-REQUIRED: use --manifest-only --out <existing-directory>";
        return result;
    }
    try {
        auto manifest = buildProductionDemoPromptManifest();
        manifest.outputDirectory = options.outputDirectory;
        if (options.selfTest) {
            auto prepared = PromptManifestCodec::encode(manifest, options.maxDocumentBytes);
            result.success = prepared.success;
            result.diagnostics = prepared.diagnostics;
            return result;
        }
        const auto directory = std::filesystem::absolute(
            std::filesystem::u8path(options.outputDirectory.begin(), options.outputDirectory.end()))
                                   .lexically_normal();
        io::ColdToolFiles files(options.maxDocumentBytes, 1);
        const auto mount = files.mountDirectory("prompt-output", directory, true);
        if (!mount || !files.service().saveCapability(mount).observedBaselineReplace)
            throw core::DiagnosticError("AI-MANIFEST-SAVE-UNSUPPORTED");
        FileReadRequest read;
        read.contractVersion = 1;
        read.pathScope = FilePathScope::MountedResource;
        read.mount = mount;
        read.path = "asset_manifest.yaml";
        read.maxBytes = options.maxDocumentBytes;
        const auto baseline = files.read(read);
        const bool missing = baseline.status == FileIoStatus::MissingFile;
        if (!missing && baseline.status != FileIoStatus::Success)
            throw core::DiagnosticError(
                baseline.diagnosticId ? baseline.diagnosticId : "AI-MANIFEST-BASELINE");
        auto prepared = PromptManifestCodec::encode(manifest, options.maxDocumentBytes);
        if (!prepared.success)
            throw core::DiagnosticError(prepared.diagnostics.c_str());
        FileSaveRequest commit;
        commit.mount = mount;
        commit.pathScope = FilePathScope::MountedResource;
        commit.path = read.path;
        commit.maxWriteBytes = options.maxDocumentBytes;
        commit.bytes.assign(prepared.yaml.begin(), prepared.yaml.end());
        if (!missing) {
            core::Sha256 hash;
            hash.update(
                {reinterpret_cast<const char*>(baseline.bytes.data()), baseline.bytes.size()});
            commit.precondition = FileSavePrecondition::ContentSha256;
            commit.expectedVersion =
                FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
            commit.maxBaselineBytes = options.maxDocumentBytes;
        }
        const auto expected = FileSaveContract::contentVersion(commit);
        result.saved = files.save(std::move(commit));
        files.retire();
        if (result.saved.status != FileIoStatus::Success
            || result.saved.deliveryStatus != FileIoStatus::Success
            || result.saved.commitState != FileCommitState::Committed
            || result.saved.committedVersion != expected) {
            result.diagnostics =
                result.saved.diagnosticId ? result.saved.diagnosticId : "AI-MANIFEST-COMMIT";
            return result;
        }
        result.success = true;
        result.diagnostics =
            "PromptManifestIo/1: wrote asset_manifest.yaml (offline prompts only; no assets generated; legacy JSON unchanged)";
    } catch (const std::bad_alloc&) {
        result.diagnostics = "AI-MANIFEST-ALLOCATION";
    } catch (const std::exception& error) {
        result.diagnostics = error.what();
    }
    return result;
}
} // namespace arp::tools::ai_asset_generator
