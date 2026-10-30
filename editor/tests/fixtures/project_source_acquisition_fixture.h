#pragma once

#include "editor/project/project_source_session.h"
#include "editor/workspace/editor_project_resource_planner.h"
#include "tests/fixtures/unified_io_fixture.h"

namespace arp::editor::tests {

// Real FileService acquisition driven exclusively by the production project
// planner. Fixture-only strong versions may pin known native producer bytes.
class ProjectSourceAcquisitionFixture final {
public:
    using VersionProvider = core::Callback<core::io::FileVersionToken(std::string_view)>;

    static core::SharedPtr<const ProjectSourceSnapshot> acquire(core::io::FileService& files,
        core::runtime::CompletionQueue& queue,
        ProjectSourceLocation location,
        ProjectInputBudget budget,
        std::size_t maxDecodedBytes,
        VersionProvider version = {})
    {
        ProjectSourceSession session(files, location, budget);
        resource::ResourceSourceSet acquired;
        resource::AssetPackageSourceContext context;
        context.origin = {core::io::FilePathScope::MountedResource, location.mount};
        context.resources.requirePreloadedSources = true;
        context.resources.sources = &acquired;
        context.resources.maxDecodedBytes = maxDecodedBytes;
        std::size_t count = 0;
        for (;;) {
            auto plan = EditorProjectResourcePlanner::plan(
                std::filesystem::u8path(location.root), context, budget, maxDecodedBytes);
            if (!plan.success)
                std::fprintf(stderr, "plan: %s\n", plan.diagnostics.c_str());
            assert(plan.success);
            if (plan.reads.empty())
                break;
            const auto& request = plan.reads.front();
            const auto relative = core::FilePath::utf8(std::filesystem::u8path(request.path)
                    .lexically_relative(std::filesystem::u8path(location.root)));
            const auto expected = version ? version(relative) : core::io::FileVersionToken{};
            assert(session.read(relative,
                       request.maxBytes,
                       expected,
                       expected.kind == core::io::FileVersionKind::None ? 0 : budget.maxFileBytes)
                == core::io::FileIoStatus::Accepted);
            unified_io_fixture::wait(queue, [&] {
                return session.state() != ProjectSourceState::Pending;
            });
            assert(session.state() == ProjectSourceState::Ready && ++count <= budget.maxFiles);
            const auto* entry = session.acquired(relative);
            assert(entry);
            resource::ResourceSourceBuffer source;
            source.path = entry->binding.path;
            source.pathScope = entry->binding.pathScope;
            source.mount = entry->binding.mount;
            source.version = entry->receipt.version;
            source.bytes = entry->receipt.bytes;
            source.role = "project-read-plan-test/v1";
            assert(acquired.add(std::move(source)));
        }
        const auto snapshot = session.finish(location.epoch);
        assert(snapshot && snapshot->size() == count);
        return snapshot;
    }
};

} // namespace arp::editor::tests
