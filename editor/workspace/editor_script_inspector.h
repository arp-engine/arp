#pragma once

#include "core/memory/cpu_allocator.h"

#include <string_view>

namespace arp::editor {
class EditorShell;
struct EditorCommandOutcome;
struct SceneInspectorEnumOption;

// Editor-owned field drafts. Script values are published only by the existing
// typed command; widgets and transient reflection values never enter history.
class EditorScriptInspector final {
public:
    EditorScriptInspector();
    ~EditorScriptInspector();
    static bool handles(std::string_view) noexcept;
    void prepare(EditorShell&);
    void append(EditorShell&, float& rowY);
    // Cold popup preparation. Menu values are opaque presentation bindings to
    // exact catalog identities, never bare script names or guessed ordinals.
    void addMenuOptions(EditorShell&, core::pmr::Vector<SceneInspectorEnumOption>&);
    EditorCommandOutcome edit(EditorShell&, std::string_view source, std::string_view value);
    EditorCommandOutcome activate(EditorShell&, std::string_view source);

    bool applying() const noexcept { return applying_; }

    // Explicit initial presentation limits, independent of the archive format.
    static constexpr std::size_t maxNodes = 256, maxTextBytes = 4096, maxReferenceCandidates = 512,
                                 maxBindings = 1024;

private:
    struct Session;
    core::UniquePtr<Session> session_;
    std::uint64_t generation_ = 0;
    bool applying_ = false;
};
} // namespace arp::editor
