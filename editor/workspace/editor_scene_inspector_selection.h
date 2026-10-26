#pragma once

#include "core/container/array_view.h"
#include "editor/workspace/editor_scene_inspector.h"

namespace arp::editor {

// An owned observation, not a transaction. Matching never chooses a component
// by its name/order and never retains reflection objects or live Scene pointers.
class EditorSceneInspectorSelection final {
public:
    struct Component {
        std::uint64_t id = 0;
        core::pmr::String type{&core::defaultCpuMemoryResource()};
    };

    struct Object {
        EditorSceneInspectorSnapshot inspector;
        core::pmr::Vector<Component> components{&core::defaultCpuMemoryResource()};
    };

    struct Budget {
        std::size_t maxObjects = 0;
        std::size_t maxComponentsPerObject = 0;
        std::size_t maxPropertiesPerObject = 0;
        std::size_t maxFields = 0;
        std::size_t maxStringBytes = 0;
        std::size_t maxElementsPerValue = 0;
    };

    // Editor observation profile, independent of the larger Scene authoring budget.
    static constexpr Budget editorBudget{64, 64, 256, 256, 512, 256};
    static constexpr std::uint64_t editorObservationBytes = 16u * 1024u * 1024u;

    enum class Status : unsigned char { Common, Missing, Incompatible, Unsupported };

    struct Member {
        std::size_t objectIndex = 0;
        std::size_t propertyIndex = 0;
    };

    struct Field {
        core::pmr::String componentType{&core::defaultCpuMemoryResource()};
        core::pmr::String path{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<Member> members{&core::defaultCpuMemoryResource()};
        Status status = Status::Common;
        bool mixed = false;
    };

    struct Snapshot {
        std::uint64_t documentEpoch = 0;
        std::uint64_t resourceRevision = 0;
        std::size_t selectedCount = 0;
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<Object> objects{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<Field> fields{&core::defaultCpuMemoryResource()};

        void swap(Snapshot& other) noexcept
        {
            std::swap(documentEpoch, other.documentEpoch);
            std::swap(resourceRevision, other.resourceRevision);
            std::swap(selectedCount, other.selectedCount);
            diagnostic.swap(other.diagnostic);
            objects.swap(other.objects);
            fields.swap(other.fields);
        }
    };

    // nullptr means success. Every rejection (including OOM) preserves output.
    // Compatibility describes exposed metadata only, not native setter safety.
    static const char* prepare(
        core::ArrayView<Object> objects, Budget budget, Snapshot& output) noexcept;
};
} // namespace arp::editor
