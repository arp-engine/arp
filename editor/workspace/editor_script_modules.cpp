#include "editor/workspace/editor_script_modules.h"

#include "core/io/file_io.h"

#include <algorithm>
#include <exception>

namespace arp::editor {

bool EditorScriptModules::prepare(const std::filesystem::path& root,
    const ProjectManifest& manifest,
    scriptvm::ModuleRegistry& registry,
    core::SharedPtr<const EditorScriptModules>& destination,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
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
        ProjectInputSnapshot sources(root);
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
            std::filesystem::path file;
            if (!ProjectManifestCodec::resolvePath(
                    root, declaration.source, ProjectPathIntent::ExistingFile, file, issue)) {
                diagnostic = issue.code;
                diagnostic += ": ";
                diagnostic += issue.field;
                return false;
            }
            const auto maxBytes =
                std::min<std::size_t>(static_cast<std::size_t>(authoring.maxSourceBytes),
                    ProjectInputBudget{}.maxFileBytes);
            const auto read = core::readTextFile(file, maxBytes);
            if (!read.success) {
                fail("EDITOR-SCRIPT-SOURCE-READ-OR-BUDGET");
                diagnostic += ": ";
                diagnostic += read.diagnostics;
                return false;
            }
            if (!sources.record(file, read.text, diagnostic))
                return false;
            Entry entry;
            entry.declaration = declaration;
            entry.source = read.text;
            for (const auto& import : authoring.imports) {
                const auto exports = registry.loadModule(import);
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
        if (!sources.verify(diagnostic))
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
    return ProjectInputSnapshot::verify(root, sources_, diagnostic);
}

} // namespace arp::editor
