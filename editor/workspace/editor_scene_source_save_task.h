#pragma once

#include "editor/project/project_file_save_task.h"
#include "editor/project/project_source_session.h"

namespace arp::editor {

// Acquires/validates a target without touching the live scene. Shell performs
// domain cold admission at ready(), before preparing the shared save operation.
class EditorSceneSourceSaveTask final {
public:
    EditorSceneSourceSaveTask(ProjectSourceContext,
        std::string_view relative,
        ProjectInputBudget,
        std::size_t maxBytes,
        bool bound,
        core::pmr::Vector<std::uint8_t> baseline,
        bool overwrite);
    ~EditorSceneSourceSaveTask();
    void advance() noexcept;
    bool write(core::pmr::Vector<std::uint8_t>) noexcept;
    void cancel() noexcept;
    void fail(const char*) noexcept;
    bool ready() const noexcept;
    bool terminal() const noexcept;
    bool submitted() const noexcept;
    bool cancelled() const noexcept;
    bool targetExisted() const noexcept;
    const char* diagnostic() const noexcept;
    const ProjectSourceContext& context() const noexcept;
    const core::pmr::Vector<std::uint8_t>& previousBytes() const noexcept;
    const core::io::FileSaveResult& result() const noexcept;
    ProjectFileReceiptObservation receiptObservation() const noexcept;
    core::io::FileVersionToken intendedVersion() const noexcept;

private:
    struct State;
    static void queried(void*, core::io::FileQueryResult) noexcept;
    core::SharedPtr<State> state_;
};
} // namespace arp::editor
