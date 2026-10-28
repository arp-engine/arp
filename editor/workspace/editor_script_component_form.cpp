#include "editor/workspace/editor_script_component_form.h"

#include "editor/workspace/editor_scene_document.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace arp::editor {
namespace {
using Y = core::YamlContract;

void require(bool condition, const char* diagnostic)
{
    if (!condition)
        throw std::invalid_argument(diagnostic);
}
} // namespace

EditorScriptComponentForm::EditorScriptComponentForm(const EditorSceneDocument& document,
    const scriptvm::ScriptComponentIdentity& identity,
    bool enabled,
    EditorScriptArchiveBudget budget)
    : catalog_(document.scriptCatalog()), owner_(document.ownerTicket()), identity_(identity),
      budget_(budget), enabled_(enabled)
{
    // Reuse archive admission for every explicit budget, even an empty schema.
    EditorScriptComponentArchive archive(document, budget_);
    requireOwner(document);
    const auto* entry = catalog_->find(identity_);
    require(entry && entry->available()
            && entry->fields->kind == reflection::SerializationDescriptor::Kind::Record,
        "EDITOR-SCRIPT-FORM-SCHEMA");
    require(entry->fields->fields.size() <= budget_.fields.maxCollectionLength
            && entry->fields->fields.size() < budget_.fields.maxValues,
        "EDITOR-SCRIPT-FORM-FIELD-BUDGET");
    values_.resize(entry->fields->fields.size());
}

EditorScriptComponentForm EditorScriptComponentForm::create(const EditorSceneDocument& document,
    const scriptvm::ScriptComponentIdentity& identity,
    bool enabled,
    EditorScriptArchiveBudget budget)
{
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget.preparationBytes));
    return EditorScriptComponentForm(document, identity, enabled, budget);
}

EditorScriptComponentForm EditorScriptComponentForm::fromState(const EditorSceneDocument& document,
    const EditorScriptComponentState& state,
    EditorScriptArchiveBudget budget)
{
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget.preparationBytes));
    auto result = EditorScriptComponentForm(document, state.identity, state.enabled, budget);
    EditorScriptComponentArchive archive(document, budget);
    const auto normalized = archive.prepare(state.identity, state.enabled, state.fields);
    require(EditorScriptComponentArchive::equal(normalized, state),
        "EDITOR-SCRIPT-FORM-STATE-CONFLICT");
    const auto fields = Y::parseSingleDocument(normalized.fields);
    // Only the outer field table is projected. Recursive YAML/type traversal,
    // numeric checks and reference conversion belong to SerializationArchive.
    for (std::size_t i = 0; i < result.size(); ++i)
        result.assign(document, i, Y::serialize(fields[core::toStdString(result.field(i).name)]));
    return result;
}

void EditorScriptComponentForm::requireOwner(const EditorSceneDocument& document) const
{
    require(owner_.sameCurrentEpoch(document.ownerTicket()) && catalog_
            && catalog_ == document.scriptCatalog(),
        "EDITOR-SCRIPT-FORM-OWNER-STALE");
}

const core::pmr::String& EditorScriptComponentForm::displayName() const noexcept
{
    return catalog_->find(identity_)->displayName;
}

const reflection::SerializationDescriptor::Field& EditorScriptComponentForm::field(
    std::size_t index) const
{
    return catalog_->find(identity_)->fields->fields.at(index);
}

const std::optional<core::pmr::String>& EditorScriptComponentForm::value(std::size_t index) const
{
    return values_.at(index);
}

bool EditorScriptComponentForm::complete() const noexcept
{
    return std::all_of(values_.begin(), values_.end(), [](const auto& value) {
        return value.has_value();
    });
}

void EditorScriptComponentForm::assign(
    const EditorSceneDocument& document, std::size_t index, std::string_view valueYaml)
{
    requireOwner(document);
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget_.preparationBytes));
    auto& current = values_.at(index);
    auto candidate = EditorScriptComponentArchive(document, budget_)
                         .prepareField(identity_, field(index).name, valueYaml);
    const auto retainedBytes = valueBytes_ - (current ? current->size() : 0);
    require(candidate.size() <= budget_.maxYamlBytes - retainedBytes,
        "EDITOR-SCRIPT-FORM-VALUE-BUDGET");
    const auto bytes = retainedBytes + candidate.size();
    static_assert(std::is_nothrow_move_constructible_v<core::pmr::String>);
    current.reset();
    current.emplace(std::move(candidate));
    valueBytes_ = bytes;
}

void EditorScriptComponentForm::clear(std::size_t index)
{
    auto& current = values_.at(index);
    valueBytes_ -= current ? current->size() : 0;
    current.reset();
}

EditorScriptComponentState EditorScriptComponentForm::prepare(
    const EditorSceneDocument& document) const
{
    requireOwner(document);
    require(complete(), "EDITOR-SCRIPT-FORM-UNASSIGNED");
    core::CpuMemoryQuotaScope scope(
        core::makeShared<core::CpuMemoryQuota>(budget_.preparationBytes));
    auto fields = Y::Node::mapping();
    for (std::size_t i = 0; i < size(); ++i)
        fields[core::toStdString(field(i).name)] = Y::parseSingleDocument(*values_[i]);
    // Final aggregate quotas and current exact references are revalidated here;
    // individually valid cells do not imply a valid complete configuration.
    return EditorScriptComponentArchive(document, budget_)
        .prepare(identity_, enabled_, Y::serialize(fields));
}
} // namespace arp::editor
