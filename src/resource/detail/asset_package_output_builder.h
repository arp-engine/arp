#pragma once

#include "core/io/filesystem_path.h"
#include "resource/asset_package_closure.h"

#include <algorithm>
#include <limits>
#include <new>

namespace arp::resource::detail {

struct PackageOutputFailure {
    const char* diagnostic;
};

inline void requirePackageOutput(bool value, const char* diagnostic)
{
    if (!value)
        throw PackageOutputFailure{diagnostic};
}

inline bool packageOutputPathsOverlap(core::StringView lhs, core::StringView rhs) noexcept
{
    const auto ancestor = [](core::StringView parent, core::StringView child) {
        return child.size() > parent.size() && child.substr(0, parent.size()) == parent
            && child[parent.size()] == '/';
    };
    return lhs == rhs || ancestor(lhs, rhs) || ancestor(rhs, lhs);
}

inline void validatePackageOutputBudget(const AssetPackageOutputBudget& budget)
{
    requirePackageOutput(budget.contractVersion == 1, "PACKAGE-OUTPUT-VERSION");
    requirePackageOutput(budget.maxResources <= AssetPackageClosureService::maxResources,
        "PACKAGE-OUTPUT-RESOURCE-BUDGET");
    for (const auto value : {budget.maxResources,
             budget.maxMetadataFileBytes,
             budget.maxMetadataTotalBytes,
             budget.maxOutputFiles,
             budget.maxOutputBytes,
             budget.maxPreparationBytes})
        requirePackageOutput(
            value && value < std::numeric_limits<std::size_t>::max(), "PACKAGE-OUTPUT-BUDGET");
}

// A retained allocation header aliases this state, so the caller's reservation
// survives the scope and follows even an individual file moved out of the result.
class PackageOutputPreparationScope final {
    struct State {
        State(std::size_t bytes, core::SharedPtr<core::CpuMemoryQuota> caller) : quota(bytes)
        {
            if (caller && !reservation.acquire(std::move(caller), bytes))
                throw PackageOutputFailure{"PACKAGE-OUTPUT-PREPARATION-BUDGET"};
        }

        core::CpuMemoryQuotaLease reservation;
        core::CpuMemoryQuota quota;
    };

public:
    explicit PackageOutputPreparationScope(std::size_t bytes)
        : state_(core::makeShared<State>(bytes, core::currentCpuMemoryQuota())),
          quota_(state_, &state_->quota), scope_(quota_)
    {}

private:
    core::SharedPtr<State> state_;
    core::SharedPtr<core::CpuMemoryQuota> quota_;
    core::CpuMemoryQuotaScope scope_;
};

class AssetPackageOutputBuilder final {
public:
    explicit AssetPackageOutputBuilder(const AssetPackageOutputBudget& budget) : budget_(budget) {}

    void append(AssetPackageOutputFile file)
    {
        const core::StringView path(file.path.data(), file.path.size());
        requirePackageOutput(
            path.find('\0') == core::StringView::npos && core::FilePath::isSafeRelativePath(path),
            "PACKAGE-OUTPUT-PATH");
        requirePackageOutput(file.role == AssetPackageOutputRole::Manifest
                || file.role == AssetPackageOutputRole::Envelope
                || file.role == AssetPackageOutputRole::Payload
                || file.role == AssetPackageOutputRole::Closure,
            "PACKAGE-OUTPUT-ROLE");
        requirePackageOutput(
            (path == "manifest.arpkg.yaml") == (file.role == AssetPackageOutputRole::Manifest),
            "PACKAGE-OUTPUT-PATH-COLLISION");
        for (const auto& previous : files_)
            requirePackageOutput(
                !packageOutputPathsOverlap(path, previous.path), "PACKAGE-OUTPUT-PATH-COLLISION");
        validateBytes(file.role, file.bytes.size());
        if (file.role != AssetPackageOutputRole::Payload) {
            metadataBytes_ += file.bytes.size();
        }
        totalBytes_ += file.bytes.size();
        files_.push_back(std::move(file));
    }

    void appendText(AssetPackageOutputRole role, core::StringView path, core::StringView text)
    {
        validateBytes(role, text.size());
        AssetPackageOutputFile file;
        file.role = role;
        file.path.assign(path.data(), path.size());
        file.bytes.assign(text.begin(), text.end());
        append(std::move(file));
    }

    void sort()
    {
        std::sort(files_.begin(), files_.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.path < rhs.path;
        });
    }

    const core::pmr::Vector<AssetPackageOutputFile>& files() const noexcept { return files_; }

    core::pmr::Vector<AssetPackageOutputFile> take() { return std::move(files_); }

private:
    void validateBytes(AssetPackageOutputRole role, std::size_t bytes) const
    {
        requirePackageOutput(files_.size() < budget_.maxOutputFiles, "PACKAGE-OUTPUT-FILE-COUNT");
        requirePackageOutput(
            bytes <= budget_.maxOutputBytes - totalBytes_, "PACKAGE-OUTPUT-TOTAL-BUDGET");
        if (role != AssetPackageOutputRole::Payload)
            requirePackageOutput(bytes <= budget_.maxMetadataFileBytes
                    && bytes <= budget_.maxMetadataTotalBytes - metadataBytes_,
                "PACKAGE-OUTPUT-METADATA-BUDGET");
    }

    const AssetPackageOutputBudget& budget_;
    std::size_t totalBytes_ = 0, metadataBytes_ = 0;
    core::pmr::Vector<AssetPackageOutputFile> files_{&core::defaultCpuMemoryResource()};
};

// Shares the enclosing preparation quota; never creates a nested budget scope.
AssetPackageOutputResult preparePackageMetadata(
    const InternalAssetPackageDesc&, const AssetPackageOutputBudget&);

} // namespace arp::resource::detail
