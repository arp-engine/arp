#include "tools/asset_importer/tool_script_sources.h"

#include "core/io/file_read_contract.h"
#include "core/io/filesystem_path.h"
#include "scriptvm/compile/parser.h"
#include "tools/asset_importer/script_diagnostics.h"

#include <algorithm>

namespace arp::tools::asset_importer {
namespace {
using Limits = ToolScriptSourceLimits;
using namespace core::io;

bool validModuleName(core::StringView name)
{
    if (name.empty() || name.size() > Limits::maxModuleBytes)
        return false;
    return std::all_of(name.begin(), name.end(), [](char ch) {
        return ch == '_' || ch == '-' || ch == '.' || (ch >= 'a' && ch <= 'z')
            || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    });
}

bool validLabel(core::StringView name)
{
    return name.size() <= Limits::maxPathBytes && name.find('\0') == core::StringView::npos
        && !core::Utf8Decoder::validate(name);
}
} // namespace

const ToolScriptSources::Module* ToolScriptSources::find(core::StringView name) const noexcept
{
    for (const auto& module : modules_)
        if (module.name == name)
            return &module;
    return nullptr;
}

struct ToolScriptSourceTask::State {
    core::io::FileService* files = nullptr;
    ToolScriptSourceDesc desc;
    core::SharedPtr<ToolScriptSources> candidate;
    core::runtime::CancellationSource cancellation;
    core::io::FileReadRequest binding;
    core::io::FileReadResult result;
    std::size_t index = 0, reads = 0;
    ToolScriptSourcePhase phase = ToolScriptSourcePhase::Pending;
    const char* diagnosticId = nullptr;
    core::pmr::String message{&core::defaultCpuMemoryResource()};
    bool submitted = false, delivered = false;
};

ToolScriptSourceTask::ToolScriptSourceTask(FileService& files, const ToolScriptSourceDesc& desc)
    : state_(core::makeShared<State>())
{
    auto& s = *state_;
    s.files = &files;
    // Member assignment preserves the explicitly selected core resources;
    // PMR copy construction would select the process default resource.
    s.desc = desc;
    if (s.desc.contractVersion != 1 || !scriptvm::ScriptSourceSnapshot::validBudget(s.desc.budget)
        || !s.desc.maxNames || s.desc.maxNames > std::max(Limits::maxNames, s.desc.budget.maxFiles)
        || s.desc.budget.maxFiles > s.desc.maxNames || s.desc.entry.contractVersion != 1
        || s.desc.entry.range.mode != FileReadRangeMode::WholeFile
        || s.desc.entry.partialPolicy != FilePartialReadPolicy::DenyPartial
        || !s.desc.entry.maxBytes || s.desc.entry.maxBytes > s.desc.budget.maxFileBytes
        || s.desc.entry.path.size() > Limits::maxPathBytes
        || FileReadContract::validate(s.desc.entry).status != FileIoStatus::Success
        || s.desc.moduleScope != FilePathScope::MountedResource || !s.desc.moduleMount
        || (!s.desc.modulePrefix.empty()
            && !core::FilePath::isSafeRelativePath(s.desc.modulePrefix, Limits::maxPathBytes))
        || !validLabel(s.desc.entryDisplayName) || !validLabel(s.desc.moduleDisplayPrefix)) {
        fail(s, "TOOL-SOURCE-DESCRIPTOR");
        return;
    }
    s.candidate = core::makeShared<ToolScriptSources>();
    s.candidate->modules_.reserve(s.desc.maxNames);
    s.candidate->modules_.push_back({Limits::entryModule,
        s.desc.entry.path,
        s.desc.entryDisplayName.empty() ? s.desc.entry.path : s.desc.entryDisplayName,
        {},
        false});
}

ToolScriptSourceTask::~ToolScriptSourceTask()
{
    cancel();
}

void ToolScriptSourceTask::fail(State& s, const char* diagnostic)
{
    s.diagnosticId = diagnostic;
    s.phase = ToolScriptSourcePhase::Failed;
    s.candidate.reset();
    s.result = {};
}

bool ToolScriptSourceTask::consume(State& s)
{
    auto& output = *s.candidate;
    auto& module = output.modules_[s.index];
    if (s.result.status == FileIoStatus::MissingFile && s.index != 0) {
        module.absent = true;
        return true;
    }
    if (s.result.status != FileIoStatus::Success) {
        s.message = std::move(s.result.diagnostics);
        fail(s, s.result.diagnosticId ? s.result.diagnosticId : "TOOL-SOURCE-READ-FAILED");
        return false;
    }
    if (output.fileCount_ >= s.desc.budget.maxFiles) {
        fail(s, "TOOL-SOURCE-FILE-BUDGET");
        return false;
    }
    const auto remainingRaw = s.desc.budget.maxSourceBytes - output.sourceBytes_;
    const auto remainingText = s.desc.budget.maxTextBytes - output.textBytes_;
    auto snapshot = core::makeShared<scriptvm::ScriptSourceSnapshot>(
        scriptvm::ScriptSourceBudget{1, s.desc.budget.maxFileBytes, remainingRaw, remainingText});
    const char* diagnostic = nullptr;
    if (!snapshot->add(module.name, s.binding, std::move(s.result), diagnostic)
        || !snapshot->seal(diagnostic)) {
        fail(s, diagnostic);
        return false;
    }
    const auto* source = snapshot->find(module.name);
    const auto parsed = scriptvm::parseSource(source->text, module.displayName);
    if (!parsed.ok() || !parsed.module) {
        s.message = detail::diagnosticText(parsed.error);
        fail(s, "TOOL-SOURCE-PARSE");
        return false;
    }
    output.sourceBytes_ += snapshot->sourceBytes();
    output.textBytes_ += snapshot->textBytes();
    ++output.fileCount_;
    module.snapshot = std::move(snapshot);
    // Discovery only. Preserve the original top-level import/class-expansion
    // grammar; do not run sema, user initialization or retain an analyzed AST.
    for (const auto& declaration : parsed.module->decls) {
        if (declaration->kind != scriptvm::StmtKind::ImportDecl)
            continue;
        const auto& name = declaration->importModule;
        if (!validModuleName(name)) {
            fail(s, "TOOL-SOURCE-MODULE-NAME");
            return false;
        }
        if (output.find(name))
            continue;
        if (output.modules_.size() >= s.desc.maxNames) {
            fail(s, "TOOL-SOURCE-NAME-BUDGET");
            return false;
        }
        core::pmr::String path(s.desc.modulePrefix, &core::defaultCpuMemoryResource());
        if (!path.empty())
            path += '/';
        path += name;
        path += ".arp";
        if (!core::FilePath::isSafeRelativePath(path, Limits::maxPathBytes)) {
            fail(s, "TOOL-SOURCE-MODULE-PATH");
            return false;
        }
        core::pmr::String display(s.desc.moduleDisplayPrefix, &core::defaultCpuMemoryResource());
        if (!display.empty())
            display += '/';
        display += path;
        if (!validLabel(display)) {
            fail(s, "TOOL-SOURCE-DISPLAY-PATH");
            return false;
        }
        output.modules_.push_back({name, std::move(path), std::move(display), {}, false});
    }
    return true;
}

void ToolScriptSourceTask::advance() noexcept
{
    auto& s = *state_;
    if (s.phase != ToolScriptSourcePhase::Pending)
        return;
    if (s.desc.entry.cancellation.cancelled()) {
        cancel();
        return;
    }
    try {
        if (s.submitted) {
            if (!s.delivered)
                return;
            if (!consume(s))
                return;
            ++s.index;
            s.submitted = s.delivered = false;
            s.result = {};
        }
        if (s.index == s.candidate->modules_.size()) {
            s.candidate->ready_ = true;
            s.phase = ToolScriptSourcePhase::Ready;
            return;
        }
        if (s.candidate->sourceBytes_ == s.desc.budget.maxSourceBytes
            || s.candidate->textBytes_ == s.desc.budget.maxTextBytes) {
            fail(s, "TOOL-SOURCE-BYTE-BUDGET");
            return;
        }
        s.binding = s.index == 0 ? s.desc.entry : FileReadRequest{};
        if (s.index != 0) {
            s.binding.contractVersion = 1;
            s.binding.path = s.candidate->modules_[s.index].path;
            s.binding.pathScope = s.desc.moduleScope;
            s.binding.mount = s.desc.moduleMount;
            s.binding.maxBytes = s.desc.budget.maxFileBytes;
        }
        s.binding.maxBytes =
            std::min(s.binding.maxBytes, s.desc.budget.maxSourceBytes - s.candidate->sourceBytes_);
        s.binding.cancellation = s.cancellation.token();
        auto admission = s.files->validateRead(s.binding);
        if (admission.status != FileIoStatus::Success) {
            s.message = std::move(admission.diagnostics);
            fail(s, admission.diagnosticId ? admission.diagnosticId : "TOOL-SOURCE-READ-ADMISSION");
            return;
        }
        using Retained = core::SharedPtr<State>;
        auto retained = core::makeUnique<Retained>(state_);
        const auto accepted = s.files->read(s.binding,
            {[](void* context, FileReadResult result) noexcept {
                 core::UniquePtr<Retained> owner{static_cast<Retained*>(context),
                     core::CpuAllocatedDeleter<Retained>{
                         context, &core::destroyCpuAllocatedUnique<Retained>}};
                 auto& state = **owner;
                 if (state.phase == ToolScriptSourcePhase::Pending) {
                     state.result = std::move(result);
                     state.delivered = true;
                 }
             },
                retained.get()});
        if (accepted != FileIoStatus::Accepted) {
            fail(s, "TOOL-SOURCE-NOT-ACCEPTED");
            return;
        }
        retained.release();
        s.submitted = true;
        ++s.reads;
    } catch (...) {
        fail(s, "TOOL-SOURCE-ALLOCATION");
    }
}

void ToolScriptSourceTask::cancel() noexcept
{
    if (state_->phase == ToolScriptSourcePhase::Pending) {
        state_->cancellation.cancel();
        state_->phase = ToolScriptSourcePhase::Cancelled;
        state_->diagnosticId = "TOOL-SOURCE-CANCELLED";
        state_->candidate.reset();
    }
}

ToolScriptSourcePhase ToolScriptSourceTask::phase() const noexcept
{
    return state_->phase;
}

const char* ToolScriptSourceTask::diagnosticId() const noexcept
{
    return state_->diagnosticId;
}

core::StringView ToolScriptSourceTask::diagnostic() const noexcept
{
    return !state_->message.empty() ? core::StringView(state_->message)
        : state_->diagnosticId      ? core::StringView(state_->diagnosticId)
                                    : core::StringView{};
}

core::SharedPtr<const ToolScriptSources> ToolScriptSourceTask::sources() const noexcept
{
    return state_->phase == ToolScriptSourcePhase::Ready ? state_->candidate : nullptr;
}

std::size_t ToolScriptSourceTask::readCount() const noexcept
{
    return state_->reads;
}
} // namespace arp::tools::asset_importer
