#pragma once

#include "core/io/blocking_file_reader.h"
#include "core/io/file_io.h"
#include "core/io/file_read_contract.h"
#include "core/io/filesystem_path.h"
#include "scriptvm/plugin/cpp_plugin_importer.h"

#include <algorithm>

namespace arp::scriptvm::detail {

// Native compiler inputs share the portable source snapshot decoder. Each
// returned owner remains alive until the corresponding compilation attempt ends.
struct CppPluginSourceRead {
    bool success = false;
    core::UniquePtr<ScriptSourceSnapshot> snapshot;
    core::TextFileReadResult legacy;
    const char* diagnosticId = nullptr;

    const core::pmr::String& text() const
    {
        return snapshot ? snapshot->find("input")->text : legacy.text;
    }

    const char* diagnostics() const
    {
        return diagnosticId ? diagnosticId : legacy.diagnostics.c_str();
    }
};

class CppPluginSourceReader final {
public:
    explicit CppPluginSourceReader(const CppPluginImporterDesc& desc) : desc_(desc) {}

    CppPluginSourceRead read(const std::filesystem::path& path)
    {
        CppPluginSourceRead result;
        if (desc_.sourceProfile == CppPluginSourceProfile::LegacyDiscoveryV1) {
            result.legacy = core::readTextFile(path);
            result.success = result.legacy.success;
            return result;
        }
        const auto& budget = desc_.sourceBudget;
        if (!ScriptSourceSnapshot::validBudget(budget) || files_ == budget.maxFiles
            || raw_ == budget.maxSourceBytes) {
            result.diagnosticId = "ARP-CPP-PLUGIN-SOURCE-BUDGET";
            return result;
        }
        try {
            core::io::FileReadRequest binding;
            binding.contractVersion = 1;
            binding.pathScope = core::io::FilePathScope::HostFilesystem;
            binding.path = core::FilePath::utf8(path);
            binding.maxBytes = std::min(budget.maxFileBytes, budget.maxSourceBytes - raw_);
            result.snapshot = core::makeUnique<ScriptSourceSnapshot>(ScriptSourceBudget{
                1, budget.maxFileBytes, budget.maxSourceBytes - raw_, budget.maxTextBytes - text_});
            auto source = core::io::BlockingFileReader().read(binding);
            if (!result.snapshot->add("input", binding, std::move(source), result.diagnosticId)
                || !result.snapshot->seal(result.diagnosticId))
                return result;
            ++files_;
            raw_ += result.snapshot->sourceBytes();
            text_ += result.snapshot->textBytes();
            result.success = true;
        } catch (...) {
            result.diagnosticId = "ARP-CPP-PLUGIN-SOURCE-ALLOCATION";
        }
        return result;
    }

    bool canReplayInputs() const noexcept
    {
        return desc_.sourceProfile == CppPluginSourceProfile::LegacyDiscoveryV1
            || (files_ <= desc_.sourceBudget.maxFiles / 2
                && raw_ <= desc_.sourceBudget.maxSourceBytes / 2
                && text_ <= desc_.sourceBudget.maxTextBytes / 2);
    }

    static bool safeRelative(const std::filesystem::path& path)
    {
        if (path.empty() || path.has_root_path())
            return false;
        for (const auto& part : path)
            if (part == "..")
                return false;
        core::io::FileReadRequest request;
        request.contractVersion = 1;
        request.pathScope = core::io::FilePathScope::HostFilesystem;
        request.maxBytes = 1;
        request.path = core::FilePath::utf8(path);
        return core::io::FileReadContract::validate(request).status
            == core::io::FileIoStatus::Success;
    }

private:
    const CppPluginImporterDesc& desc_;
    std::size_t files_ = 0;
    std::uint64_t raw_ = 0, text_ = 0;
};

} // namespace arp::scriptvm::detail
