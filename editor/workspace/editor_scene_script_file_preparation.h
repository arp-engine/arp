#pragma once

#include "editor/workspace/editor_scene_script_codec.h"

namespace arp::editor {
// Owner-thread preparation only. It carries ordinary byte snapshots plus weak
// freshness evidence, never a Scene, receiver, module or borrowed service owner.
// The persistence service consumes these snapshots through conditional IO.
class EditorSceneScriptFilePreparation final {
public:
    struct Request {
        std::string_view relativePath;
        bool targetExisted;
        std::string_view previousBytes;
    };

    class Prepared final {
    public:
        Prepared() = default;

        const core::pmr::String& path() const noexcept { return path_; }

        bool targetExisted() const noexcept { return targetExisted_; }

        const core::pmr::Vector<std::uint8_t>& previousBytes() const noexcept { return previous_; }

        const core::pmr::Vector<std::uint8_t>& replacementBytes() const noexcept
        {
            return replacement_;
        }

        const core::pmr::String& contentHash() const noexcept { return hash_; }

        // Synchronous cold revalidation, never a frame/poll hot path. Requires
        // the same owner thread and rechecks full encode admission. It does not compare disk contents;
        // conditional IO must check the exact previous byte snapshot.
        bool current(const EditorSceneDocument&,
            const EditorSceneScriptInputs&,
            core::pmr::String& diagnostic) const;

    private:
        friend class EditorSceneScriptFilePreparation;
        core::pmr::String path_, relativePath_, project_, hash_;
        core::pmr::Vector<std::uint8_t> previous_, replacement_;
        core::LifetimeEpoch::Ticket document_;
        bool targetExisted_ = false, ready_ = false;
    };

    // Existing bytes must be from an explicit v4 raw-read preflight. Unknown,
    // corrupt, stale-source or v3 input fails without publishing a token.
    // Neither this call nor Prepared::current executes user code or writes files.
    static bool prepare(const EditorSceneDocument&,
        const EditorSceneScriptInputs&,
        Request,
        core::UniquePtr<Prepared>& output,
        core::pmr::String& diagnostic);
};
} // namespace arp::editor
