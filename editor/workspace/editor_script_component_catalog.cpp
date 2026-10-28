#include "editor/workspace/editor_script_component_catalog.h"

#include <algorithm>
#include <exception>

namespace arp::editor {

bool EditorScriptComponentCatalog::prepare(core::SharedPtr<const EditorScriptModules> modules,
    EditorScriptCatalogBudget budget,
    core::SharedPtr<const EditorScriptComponentCatalog>& destination,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!modules || !budget.maxEntries || !budget.preparationBytes) {
        diagnostic = "EDITOR-SCRIPT-CATALOG-OWNER-OR-BUDGET";
        return false;
    }
    try {
        std::size_t count = 0;
        for (const auto& entry : modules->entries()) {
            if (entry.module->types().size() > budget.maxEntries - count) {
                diagnostic = "EDITOR-SCRIPT-CATALOG-CAPACITY";
                return false;
            }
            count += entry.module->types().size();
        }
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.preparationBytes);
        core::SharedPtr<EditorScriptComponentCatalog> candidate;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::pmr::Vector<core::SharedPtr<scriptvm::ScriptComponentModule>> prepared;
            prepared.reserve(modules->entries().size());
            for (const auto& entry : modules->entries())
                prepared.push_back(entry.module);
            scene::SceneComponentFactory factory;
            if (!prepared.empty()
                && !scriptvm::registerScriptComponentPersistence(factory,
                    std::move(prepared),
                    ProjectManifestBudget{}.maxScriptModules,
                    diagnostic))
                return false;
            const auto* persistence = factory.persistence("arp.script-component/1");
            if (count && (!persistence || !persistence->inspectConfiguration)) {
                diagnostic = "EDITOR-SCRIPT-CATALOG-PERSISTENCE-UNAVAILABLE";
                return false;
            }
            candidate = core::makeShared<EditorScriptComponentCatalog>();
            candidate->modules_ = std::move(modules);
            candidate->entries_.reserve(count);
            for (const auto& module : candidate->modules_->entries()) {
                for (const auto& type : module.module->types()) {
                    Entry row;
                    row.identity = {module.declaration.resourceId,
                        type->name,
                        module.declaration.schemaVersion};
                    row.moduleId = module.declaration.id;
                    row.source = module.declaration.source;
                    row.displayName = row.moduleId + " / " + type->name;
                    scene::SceneComponentFactory::Inspection inspection;
                    if (persistence->inspectConfiguration(
                            reflection::object(row.identity), inspection, row.diagnostic))
                        row.fields = std::move(inspection.descriptor);
                    // Keep unsupported declarations discoverable. The engine's
                    // exact diagnostic explains why they cannot be authored.
                    candidate->entries_.push_back(std::move(row));
                }
            }
            std::sort(candidate->entries_.begin(),
                candidate->entries_.end(),
                [](const Entry& a, const Entry& b) {
                    if (a.identity.scriptResourceId != b.identity.scriptResourceId)
                        return a.identity.scriptResourceId < b.identity.scriptResourceId;
                    if (a.identity.moduleSchemaVersion != b.identity.moduleSchemaVersion)
                        return a.identity.moduleSchemaVersion < b.identity.moduleSchemaVersion;
                    return a.identity.scriptType < b.identity.scriptType;
                });
        }
        destination = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        diagnostic = "EDITOR-SCRIPT-CATALOG-MEMORY-BUDGET";
        return false;
    } catch (const std::exception& error) {
        diagnostic = "EDITOR-SCRIPT-CATALOG-PREPARATION-FAILED: ";
        diagnostic += error.what();
        return false;
    }
}

const EditorScriptComponentCatalog::Entry* EditorScriptComponentCatalog::find(
    const scriptvm::ScriptComponentIdentity& identity) const noexcept
{
    for (const auto& entry : entries_)
        if (entry.identity.scriptResourceId == identity.scriptResourceId
            && entry.identity.moduleSchemaVersion == identity.moduleSchemaVersion
            && entry.identity.scriptType == identity.scriptType)
            return &entry;
    return nullptr;
}

} // namespace arp::editor
