#include "scriptvm/plugin/script_source_snapshot.h"

#include "core/hash/sha256.h"
#include "core/io/file_read_contract.h"
#include "core/text/text_decoder.h"

#include <algorithm>

namespace arp::scriptvm {

bool ScriptSourceSnapshot::validBudget(const ScriptSourceBudget& budget) noexcept
{
    return budget.maxFiles && budget.maxFiles != SIZE_MAX && budget.maxFileBytes
        && budget.maxFileBytes < SIZE_MAX && budget.maxSourceBytes
        && budget.maxSourceBytes < SIZE_MAX && budget.maxTextBytes
        && budget.maxTextBytes < SIZE_MAX;
}

bool ScriptSourceSnapshot::add(core::StringView moduleId,
    const core::io::FileReadRequest& binding,
    core::io::FileReadResult source,
    const char*& diagnosticId) noexcept
{
    using namespace core::io;
    diagnosticId = nullptr;
    const auto fail = [&](const char* id) {
        diagnosticId = id;
        return false;
    };
    if (sealed_)
        return fail("ARP-SCRIPT-SOURCE-SEALED");
    if (!validBudget(budget_))
        return fail("ARP-SCRIPT-SOURCE-BUDGET");
    if (moduleId.empty() || moduleId.find('\0') != core::StringView::npos
        || core::Utf8Decoder::validate(moduleId))
        return fail("ARP-SCRIPT-SOURCE-MODULE");
    const auto validation = FileReadContract::validate(binding);
    if (validation.status != FileIoStatus::Success)
        return fail(validation.diagnostic);
    if (binding.contractVersion != 1 || binding.range.mode != FileReadRangeMode::WholeFile
        || binding.partialPolicy != FilePartialReadPolicy::DenyPartial)
        return fail("ARP-SCRIPT-SOURCE-WHOLE-FILE");
    if (binding.cancellation.cancelled())
        return fail("ARP-SCRIPT-SOURCE-CANCELLED");
    if (source.status != FileIoStatus::Success)
        return fail(source.diagnosticId ? source.diagnosticId : "ARP-SCRIPT-SOURCE-READ-FAILED");
    if (!source.version.valid() || source.version.kind == FileVersionKind::DirectorySnapshotSha256)
        return fail("ARP-SCRIPT-SOURCE-VERSION");
    if (binding.expectedVersion.kind != FileVersionKind::None
        && binding.expectedVersion != source.version)
        return fail("ARP-SCRIPT-SOURCE-VERSION");
    for (const auto& entry : entries_)
        if (entry.moduleId == moduleId)
            return fail("ARP-SCRIPT-SOURCE-DUPLICATE");
    if (entries_.size() == budget_.maxFiles || source.bytes.size() > binding.maxBytes
        || source.bytes.size() > budget_.maxFileBytes
        || source.bytes.size() > budget_.maxSourceBytes - sourceBytes_)
        return fail("ARP-SCRIPT-SOURCE-BUDGET");
    const core::StringView raw(
        reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size());
    try {
        if (source.version.kind == FileVersionKind::ContentSha256) {
            const auto digest = core::Sha256::digest(raw);
            if (!std::equal(digest.begin(),
                    digest.end(),
                    source.version.digest.begin(),
                    source.version.digest.end()))
                return fail("ARP-SCRIPT-SOURCE-INTEGRITY");
        }
        core::TextDecodeDesc desc;
        desc.bom = core::TextBomPolicy::Strip;
        desc.maxOutputBytes = std::min(budget_.maxFileBytes, budget_.maxTextBytes - textBytes_);
        auto decoded = core::TextDecoder::decode(raw, desc);
        if (decoded.status != core::TextDecodeStatus::Success)
            return fail(decoded.diagnosticId);
        Entry entry;
        entry.moduleId = moduleId;
        entry.binding = binding;
        entry.source = std::move(source);
        entry.text = std::move(decoded.text);
        entry.textHash = core::Sha256::hexDigest(entry.text);
        const auto rawSize = entry.source.bytes.size(), textSize = entry.text.size();
        entries_.push_back(std::move(entry));
        sourceBytes_ += rawSize;
        textBytes_ += textSize;
        return true;
    } catch (...) {
        return fail("ARP-SCRIPT-SOURCE-ALLOCATION");
    }
}

bool ScriptSourceSnapshot::seal(const char*& diagnosticId) noexcept
{
    diagnosticId = nullptr;
    if (sealed_)
        return true;
    if (!validBudget(budget_) || entries_.empty()) {
        diagnosticId = "ARP-SCRIPT-SOURCE-BUDGET";
        return false;
    }
    for (const auto& entry : entries_)
        if (entry.binding.cancellation.cancelled()) {
            diagnosticId = "ARP-SCRIPT-SOURCE-CANCELLED";
            return false;
        }
    sealed_ = true;
    return true;
}

const ScriptSourceSnapshot::Entry* ScriptSourceSnapshot::find(
    core::StringView moduleId) const noexcept
{
    if (sealed_)
        for (const auto& entry : entries_)
            if (entry.moduleId == moduleId)
                return &entry;
    return nullptr;
}

} // namespace arp::scriptvm
