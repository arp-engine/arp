#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "core/text/utf8.h"
#include "editor/project/project_identity.h"
#include "editor/workspace/editor_recovery_session.h"
#include "resource/resource_manager.h"

#include <algorithm>
#include <thread>

namespace arp::editor {
namespace {
using Yaml = core::YamlContract;
using Node = Yaml::Node;
constexpr std::string_view regionFile = "region.arp.yaml";

bool fail(core::pmr::String& diagnostic, const char* code)
{
    diagnostic = code;
    return false;
}

Node budgetNode(const EditorRecoveryBudget& budget)
{
    auto node = Node::mapping();
    node["maxEnvelopeBytes"] = static_cast<std::uint64_t>(budget.maxEnvelopeBytes);
    node["maxPayloadBytes"] = static_cast<std::uint64_t>(budget.maxPayloadBytes);
    node["maxScripts"] = static_cast<std::uint64_t>(budget.maxScripts);
    node["maxPathBytes"] = static_cast<std::uint64_t>(budget.maxPathBytes);
    node["slotsPerSession"] = static_cast<std::uint64_t>(budget.slotsPerSession);
    node["maxSessions"] = static_cast<std::uint64_t>(budget.maxSessions);
    node["maxRegionBytes"] = static_cast<std::uint64_t>(budget.maxRegionBytes);
    node["snapshotIntervalSeconds"] = budget.snapshotIntervalSeconds;
    node["maxRegionPublishAttempts"] = budget.maxRegionPublishAttempts;
    node["regionPublishRetryMilliseconds"] = budget.regionPublishRetryMilliseconds;
    return node;
}

core::pmr::String metadata(const EditorRecoveryBudget& budget)
{
    auto root = Node::mapping();
    root["type"] = "arp.editor-recovery-region";
    root["contractVersion"] = 2;
    root["budget"] = budgetNode(budget);
    return Yaml::serialize(root);
}

bool validMetadata(std::string_view bytes, const EditorRecoveryBudget& budget)
{
    const auto node = Yaml::parseSingleDocument(bytes);
    Yaml::onlyKeys(node, {"type", "contractVersion", "budget"});
    if (Yaml::text(node, "type") != "arp.editor-recovery-region"
        || Yaml::uint32(node, "contractVersion") != 2)
        return false;
    const auto& configured = Yaml::required(node, "budget");
    // Compare parsed values, never patch missing fields or guess old versions.
    return configured == budgetNode(budget);
}

bool exactDirectory(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::symlink_status(path, error).type()
        == std::filesystem::file_type::directory
        && !error && std::filesystem::canonical(path, error) == path && !error;
}

bool temporaryName(std::string_view name, std::string_view slot)
{
    const auto prefix = core::pmr::String(slot) + ".arp-tmp-";
    if (name.substr(0, prefix.size()) != prefix)
        return false;
    const auto suffix = name.substr(prefix.size());
    const auto separator = suffix.find('-');
    const auto digits = [](std::string_view text) {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
            return c >= '0' && c <= '9';
        });
    };
    return separator != std::string_view::npos && digits(suffix.substr(0, separator))
        && digits(suffix.substr(separator + 1u));
}
} // namespace

std::string_view EditorRecoveryRegion::slotName(std::size_t slot) noexcept
{
    constexpr std::array<std::string_view, 2> names{"slot-a.arp.yaml", "slot-b.arp.yaml"};
    return slot < names.size() ? names[slot] : std::string_view{};
}

bool EditorRecoveryRegion::open(const std::filesystem::path& directory,
    EditorRecoveryBudget budget,
    core::pmr::String& diagnostic)
{
    if (!directory_.empty())
        return fail(diagnostic, "EDITOR-RECOVERY-REGION-ALREADY-OPEN");
    if (!EditorRecoveryCodec::validateBudget(budget, diagnostic))
        return false;
    publicationAttempts_ = 0;
    try {
        const auto text = core::FilePath::utf8(directory);
        if (!directory.is_absolute() || directory != directory.lexically_normal()
            || text.size() > budget.maxPathBytes || core::Utf8Decoder::validate(text)
            || std::any_of(text.begin(), text.end(), [](unsigned char c) {
                   return c < 32u;
               }))
            return fail(diagnostic, "EDITOR-RECOVERY-REGION-PATH");
        std::error_code error;
        if (!std::filesystem::exists(directory, error) && !error) {
            core::io::StagedDirectory staging;
            if (!staging.begin(directory)) {
                diagnostic = staging.diagnostic();
                return false;
            }
            const auto yaml = metadata(budget);
            if (yaml.size() > EditorRecoveryBudget::maxRegionMetadataBytes)
                return fail(diagnostic, "EDITOR-RECOVERY-REGION-METADATA-LIMIT");
            const core::pmr::Vector<std::uint8_t> bytes(yaml.begin(), yaml.end());
            const auto saved = core::io::replaceFileAtomically(
                core::FilePath::utf8(staging.path() / std::filesystem::u8path(regionFile)), bytes);
            if (!saved.success || !saved.committed) {
                diagnostic = saved.diagnostic;
                return false;
            }
            bool published = false;
            for (std::uint32_t attempt = 0; attempt < budget.maxRegionPublishAttempts; ++attempt) {
                ++publicationAttempts_;
                if (staging.publish()) {
                    published = true;
                    break;
                }
                // Repeating the same exclusive publication never overwrites
                // a destination. No assumption that an OS error is transient.
                if (attempt + 1u < budget.maxRegionPublishAttempts)
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(budget.regionPublishRetryMilliseconds));
            }
            if (!published) {
                diagnostic = staging.diagnostic();
                return false;
            }
        }
        if (error || !exactDirectory(directory))
            return fail(diagnostic, "EDITOR-RECOVERY-REGION-PATH-CHANGED");
        const auto file = directory / std::filesystem::u8path(regionFile);
        if (std::filesystem::symlink_status(file, error).type()
                != std::filesystem::file_type::regular
            || error)
            return fail(diagnostic, "EDITOR-RECOVERY-REGION-METADATA-PATH");
        const auto read = core::readTextFile(file, EditorRecoveryBudget::maxRegionMetadataBytes);
        if (!read.success || !validMetadata(read.text, budget))
            return fail(diagnostic, "EDITOR-RECOVERY-REGION-METADATA-INVALID");
        directory_ = directory;
        budget_ = budget;
        metadataBytes_ = read.text;
        core::pmr::Vector<EditorRecoveryRegionEntry> entries;
        if (!inspect(entries, diagnostic)) {
            directory_.clear();
            metadataBytes_.clear();
            return false;
        }
        diagnostic.clear();
        return true;
    } catch (...) {
        directory_.clear();
        metadataBytes_.clear();
        return fail(diagnostic, "EDITOR-RECOVERY-REGION-OPEN-FAILED");
    }
}

bool EditorRecoveryRegion::inspect(
    core::pmr::Vector<EditorRecoveryRegionEntry>& destination, core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    if (directory_.empty() || !exactDirectory(directory_))
        return fail(diagnostic, "EDITOR-RECOVERY-REGION-NOT-OPEN-OR-CHANGED");
    try {
        const auto metadataPath = directory_ / std::filesystem::u8path(regionFile);
        if (std::filesystem::symlink_status(metadataPath).type()
            != std::filesystem::file_type::regular)
            return fail(diagnostic, "EDITOR-RECOVERY-REGION-METADATA-PATH");
        const auto current =
            core::readTextFile(metadataPath, EditorRecoveryBudget::maxRegionMetadataBytes);
        if (!current.success || current.text != metadataBytes_)
            return fail(diagnostic, "EDITOR-RECOVERY-REGION-METADATA-CHANGED");
        core::pmr::Vector<EditorRecoveryRegionEntry> candidate;
        const auto perSession = budget_.maxEnvelopeBytes * (budget_.slotsPerSession + 1u);
        std::size_t regionBytes = metadataBytes_.size();
        for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
            if (entry.path() == metadataPath)
                continue;
            if (candidate.size() >= budget_.maxSessions)
                return fail(diagnostic, "EDITOR-RECOVERY-SESSION-CAPACITY");
            EditorRecoveryRegionEntry session;
            session.directory = entry.path();
            session.sessionId = core::FilePath::utf8(entry.path().filename());
            if (!resource::isResourceUuid(session.sessionId) || !exactDirectory(session.directory))
                return fail(diagnostic, "EDITOR-RECOVERY-REGION-UNKNOWN-ENTRY");
            std::size_t files = 0;
            for (const auto& file : std::filesystem::directory_iterator(session.directory)) {
                if (++files > budget_.slotsPerSession + 1u
                    || file.symlink_status().type() != std::filesystem::file_type::regular
                    || std::filesystem::hard_link_count(file.path()) != 1u)
                    return fail(diagnostic, "EDITOR-RECOVERY-SESSION-UNKNOWN-ENTRY");
                const auto name = core::FilePath::utf8(file.path().filename());
                bool known = false;
                for (std::size_t i = 0; i < budget_.slotsPerSession; ++i) {
                    const auto slot = slotName(i);
                    if (name == slot)
                        known = true;
                    else if (temporaryName(name, slot)) {
                        known = true;
                        ++session.temporaryFiles;
                    }
                }
                if (!known)
                    return fail(diagnostic, "EDITOR-RECOVERY-SESSION-UNKNOWN-FILE");
                const auto size = file.file_size();
                if (size > budget_.maxEnvelopeBytes || size > perSession - session.usedBytes)
                    return fail(diagnostic, "EDITOR-RECOVERY-SESSION-DISK-LIMIT");
                session.usedBytes += static_cast<std::size_t>(size);
            }
            if (session.temporaryFiles > 1u)
                return fail(diagnostic, "EDITOR-RECOVERY-SESSION-TEMPORARY-LIMIT");
            if (session.usedBytes > budget_.maxRegionBytes - regionBytes)
                return fail(diagnostic, "EDITOR-RECOVERY-REGION-DISK-LIMIT");
            regionBytes += session.usedBytes;
            candidate.push_back(std::move(session));
        }
        std::sort(candidate.begin(), candidate.end(), [](const auto& a, const auto& b) {
            return a.sessionId < b.sessionId;
        });
        destination = std::move(candidate);
        return true;
    } catch (...) {
        return fail(diagnostic, "EDITOR-RECOVERY-REGION-INSPECT-FAILED");
    }
}

bool EditorRecoveryRegion::claim(
    EditorRecoveryRegionEntry& destination, core::pmr::String& diagnostic) const
{
    core::pmr::Vector<EditorRecoveryRegionEntry> entries;
    if (!inspect(entries, diagnostic))
        return false;
    if (entries.size() >= budget_.maxSessions)
        return fail(diagnostic, "EDITOR-RECOVERY-SESSION-CAPACITY");
    EditorRecoveryRegionEntry candidate;
    if (!ProjectIdentity::create("editor-recovery-session", candidate.sessionId, diagnostic))
        return false;
    candidate.directory = directory_ / std::filesystem::u8path(candidate.sessionId);
    if (core::FilePath::utf8(candidate.directory / std::filesystem::u8path(slotName(0))).size()
        > budget_.maxPathBytes)
        return fail(diagnostic, "EDITOR-RECOVERY-SESSION-PATH-LIMIT");
    std::error_code error;
    if (!std::filesystem::create_directory(candidate.directory, error) || error)
        return fail(diagnostic, "EDITOR-RECOVERY-SESSION-CLAIM-FAILED");
    // Recount after exclusive creation. Simultaneous claimers may both fail
    // conservatively, but neither may publish files beyond the reserved count.
    if (!inspect(entries, diagnostic)) {
        if (exactDirectory(candidate.directory) && candidate.directory.parent_path() == directory_)
            std::filesystem::remove(candidate.directory, error); // Empty owned directory only.
        return false;
    }
    destination = std::move(candidate);
    return true;
}

} // namespace arp::editor
