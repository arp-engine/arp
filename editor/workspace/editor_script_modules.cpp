#include "editor/workspace/editor_script_modules.h"

#include "core/io/blocking_file_reader.h"
#include "core/io/filesystem_path.h"
#include "core/text/text_decoder.h"

#include <algorithm>
#include <exception>

namespace arp::editor {
namespace {
bool decodeScriptInput(const ProjectScriptModule& declaration,
    core::StringView bytes,
    core::pmr::String& output,
    core::pmr::String& diagnostic)
{
    core::TextDecodeDesc desc;
    desc.bom = core::TextBomPolicy::Strip;
    desc.nul = core::TextNulPolicy::Reject;
    desc.maxOutputBytes = std::min<std::uint64_t>(
        declaration.authoring->maxSourceBytes, ProjectInputBudget{}.maxFileBytes);
    if (bytes.size() > desc.maxOutputBytes) {
        diagnostic = "EDITOR-SCRIPT-SOURCE-READ-OR-BUDGET";
        return false;
    }
    auto result = core::TextDecoder::decode(bytes, desc);
    if (result.status != core::TextDecodeStatus::Success) {
        diagnostic = result.diagnosticId;
        return false;
    }
    output = std::move(result.text);
    return true;
}
} // namespace

bool EditorScriptModules::prepare(const std::filesystem::path& root,
    const ProjectManifest& manifest,
    scriptvm::ModuleRegistry& registry,
    core::SharedPtr<const EditorScriptModules>& destination,
    core::pmr::String& diagnostic)
{
    try {
        // A zero-module project must obey the same root contract as one with
        // source bindings; an empty snapshot is not evidence of a valid root.
        std::error_code rootError;
        const auto canonicalRoot = std::filesystem::canonical(root, rootError);
        if (rootError || canonicalRoot != root
            || !std::filesystem::is_directory(canonicalRoot, rootError) || rootError) {
            diagnostic = "PROJECT-INPUT-ROOT";
            return false;
        }
        ProjectInputSnapshot sources(root);
        SourceReader reader([&](const ProjectScriptModule& declaration,
                                core::pmr::String& text,
                                ProjectInputSnapshot& identities,
                                core::pmr::String& issue) {
            ProjectDiagnostic pathIssue;
            std::filesystem::path file;
            if (!ProjectManifestCodec::resolvePath(
                    root, declaration.source, ProjectPathIntent::ExistingFile, file, pathIssue)) {
                issue = pathIssue.code;
                issue += ": ";
                issue += pathIssue.field;
                return false;
            }
            core::io::FileReadRequest request;
            request.contractVersion = 1;
            request.pathScope = core::io::FilePathScope::HostFilesystem;
            request.path = core::FilePath::utf8(file);
            request.maxBytes = std::min<std::uint64_t>(
                declaration.authoring->maxSourceBytes, ProjectInputBudget{}.maxFileBytes);
            const auto read = core::io::BlockingFileReader().read(request);
            if (read.status != core::io::FileIoStatus::Success) {
                issue = "EDITOR-SCRIPT-SOURCE-READ-OR-BUDGET: ";
                issue += read.diagnostics;
                return false;
            }
            const core::StringView bytes(
                read.bytes.empty() ? "" : reinterpret_cast<const char*>(read.bytes.data()),
                read.bytes.size());
            return identities.record(file, bytes, issue)
                && decodeScriptInput(declaration, bytes, text, issue);
        });
        SourceVerifier verify([&](core::pmr::String& issue) {
            return sources.verify(issue);
        });
        return prepareUsing(
            manifest, registry, destination, diagnostic, sources, reader, verify, nullptr);
    } catch (const std::bad_alloc&) {
        diagnostic = "EDITOR-SCRIPT-PREPARATION-MEMORY-BUDGET";
        return false;
    }
}

bool EditorScriptModules::prepareSources(const ProjectSourceSnapshot& snapshot,
    core::io::FileService& files,
    std::uint64_t epoch,
    const ProjectManifest& manifest,
    scriptvm::ModuleRegistry& registry,
    core::SharedPtr<const EditorScriptModules>& destination,
    core::pmr::String& diagnostic)
{
    try {
        const char* issue = nullptr;
        if (!snapshot.validate(files, epoch, issue)) {
            diagnostic = issue;
            return false;
        }
        ProjectInputSnapshot identities({});
        SourceReader reader([&](const ProjectScriptModule& declaration,
                                core::pmr::String& text,
                                ProjectInputSnapshot& inputs,
                                core::pmr::String& report) {
            const auto* source = snapshot.find(declaration.source);
            if (!source) {
                report = "EDITOR-SCRIPT-SOURCE-NOT-ACQUIRED";
                return false;
            }
            const auto& raw = source->receipt.bytes;
            const core::StringView bytes(
                raw.empty() ? "" : reinterpret_cast<const char*>(raw.data()), raw.size());
            return inputs.recordLogical(declaration.source, bytes, report)
                && decodeScriptInput(declaration, bytes, text, report);
        });
        SourceVerifier verify([&](core::pmr::String& report) {
            const char* id = nullptr;
            if (snapshot.validate(files, epoch, id))
                return true;
            report = id;
            return false;
        });
        return prepareUsing(manifest,
            registry,
            destination,
            diagnostic,
            identities,
            reader,
            verify,
            &snapshot.location());
    } catch (const std::bad_alloc&) {
        diagnostic = "EDITOR-SCRIPT-PREPARATION-MEMORY-BUDGET";
        return false;
    }
}

bool EditorScriptModules::prepareUsing(const ProjectManifest& manifest,
    scriptvm::ModuleRegistry& registry,
    core::SharedPtr<const EditorScriptModules>& destination,
    core::pmr::String& diagnostic,
    ProjectInputSnapshot& sources,
    SourceReader& readSource,
    SourceVerifier& verifySource,
    const ProjectSourceLocation* location)
{
    diagnostic.clear();
    try {
        ProjectDiagnostic issue;
        core::pmr::String validated;
        if (!ProjectManifestCodec::encode(manifest, validated, issue)) {
            diagnostic = issue.code;
            diagnostic += ": ";
            diagnostic += issue.field;
            return false;
        }
        core::pmr::Vector<std::size_t> order;
        if (!ProjectManifestCodec::moduleOrder(manifest, order, issue)) {
            diagnostic = issue.code;
            return false;
        }
        auto candidate = core::makeShared<EditorScriptModules>();
        candidate->entries_.reserve(order.size());
        for (const auto index : order) {
            const auto& declaration = manifest.scriptModules[index];
            const auto fail = [&](const char* code) {
                diagnostic = code;
                diagnostic += ": ";
                diagnostic += declaration.id;
                return false;
            };
            if (!declaration.authoring)
                return fail("EDITOR-SCRIPT-AUTHORING-CONFIGURATION-REQUIRED");
            // The existing per-component session consumes native ModuleExports.
            // Source-to-source exports need their own engine contract; never
            // pretend ordering/concatenation supplies actual imported symbols.
            if (!declaration.dependencies.empty())
                return fail("EDITOR-SCRIPT-SOURCE-IMPORT-CONTRACT-REQUIRED");
            const auto& authoring = *declaration.authoring;
            Entry entry;
            entry.declaration = declaration;
            if (!readSource(declaration, entry.source, sources, diagnostic))
                return false;
            for (const auto& import : authoring.imports) {
                const auto exports =
                    location ? registry.findLoadedModule(import) : registry.loadModule(import);
                if (!exports || !exports->moduleVersion || exports->exportContractHash.empty()
                    || exports->reflectionRegistryHash.empty()) {
                    fail("EDITOR-SCRIPT-IMPORT-CONTRACT-UNAVAILABLE");
                    diagnostic += ": ";
                    diagnostic += import;
                    return false;
                }
                entry.imports.push_back({import,
                    exports->exportContractHash,
                    exports->reflectionRegistryHash,
                    exports->moduleVersion});
            }
            auto quota = core::makeShared<core::CpuMemoryQuota>(authoring.preparationBytes);
            {
                core::CpuMemoryQuotaScope scope(quota);
                entry.module = core::makeShared<scriptvm::ScriptComponentModule>();
                if (!entry.module->load(entry.source,
                        declaration.id,
                        declaration.resourceId,
                        declaration.schemaVersion,
                        registry,
                        authoring.imports,
                        authoring.execution,
                        authoring.maxInstances)) {
                    fail("EDITOR-SCRIPT-MODULE-PREPARATION-FAILED");
                    diagnostic += ": ";
                    diagnostic += entry.module->diagnostic();
                    return false;
                }
            }
            candidate->entries_.push_back(std::move(entry));
        }
        if (location)
            candidate->sourceLocation_ = *location;
        if (!verifySource(diagnostic))
            return false;
        candidate->sources_ = sources.takeBindings();
        destination = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        diagnostic = "EDITOR-SCRIPT-PREPARATION-MEMORY-BUDGET";
        return false;
    } catch (const std::exception& error) {
        diagnostic = "EDITOR-SCRIPT-PREPARATION-FAILED: ";
        diagnostic += error.what();
        return false;
    }
}

scriptvm::ScriptComponentModule* EditorScriptModules::find(
    const scriptvm::ScriptComponentIdentity& identity) const noexcept
{
    for (const auto& entry : entries_)
        if (entry.declaration.resourceId == identity.scriptResourceId
            && entry.declaration.schemaVersion == identity.moduleSchemaVersion
            && entry.module->find(identity.scriptType))
            return entry.module.get();
    return nullptr;
}

bool EditorScriptModules::matchesManifest(const ProjectManifest& manifest) const
{
    if (entries_.size() != manifest.scriptModules.size())
        return false;
    auto prepared = manifest;
    for (auto& module : prepared.scriptModules) {
        const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
            return entry.declaration.resourceId == module.resourceId;
        });
        if (found == entries_.end())
            return false;
        module = found->declaration;
    }
    ProjectDiagnostic diagnostic;
    core::pmr::String expected, actual;
    return ProjectManifestCodec::encode(manifest, expected, diagnostic)
        && ProjectManifestCodec::encode(prepared, actual, diagnostic) && actual == expected;
}

bool EditorScriptModules::verifySources(
    const std::filesystem::path& root, core::pmr::String& diagnostic) const
{
    if (sourceLocation_) {
        diagnostic = "EDITOR-SCRIPT-MOUNTED-SOURCE-VERIFY-REQUIRED";
        return false;
    }
    return ProjectInputSnapshot::verify(root, sources_, diagnostic);
}

bool EditorScriptModules::verifySources(const ProjectSourceSnapshot& snapshot,
    core::io::FileService& files,
    std::uint64_t epoch,
    core::pmr::String& diagnostic) const
{
    const char* issue = nullptr;
    if (!sourceLocation_ || !(sourceLocation_->mount == snapshot.location().mount)
        || sourceLocation_->root != snapshot.location().root || sourceLocation_->epoch != epoch) {
        diagnostic = "EDITOR-SCRIPT-SOURCE-ORIGIN";
        return false;
    }
    if (!snapshot.validate(files, epoch, issue)) {
        diagnostic = issue;
        return false;
    }
    for (const auto& binding : sources_) {
        const auto found = std::find_if(
            snapshot.identities().begin(), snapshot.identities().end(), [&](const auto& input) {
                return input.path == binding.path;
            });
        if (found == snapshot.identities().end() || found->byteCount != binding.byteCount
            || found->fingerprint != binding.fingerprint) {
            diagnostic = "PROJECT-INPUT-CONTENT-CHANGED";
            return false;
        }
    }
    diagnostic.clear();
    return true;
}

} // namespace arp::editor
