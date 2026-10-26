#pragma once

#include "editor/viewport/editor_scene_transform_batch.h"
#include "editor/viewport/editor_scene_transform_snap.h"

#include <initializer_list>
#include <string_view>

namespace arp::editor {

// Shell preferences only. A complete value has explicit units and no Scene owner.
class EditorSceneTransformSettings final {
public:
    EditorSceneTransformTool tool = EditorSceneTransformTool::Select;
    EditorSceneTransformSpace space = EditorSceneTransformSpace::World;
    EditorSceneTransformPivot pivot = EditorSceneTransformPivot::Active;
    EditorSceneTransformSnap snap;

    bool valid() const noexcept
    {
        return !toolName(tool).empty() && !spaceName(space).empty() && !pivotName(pivot).empty()
            && snap.valid();
    }

    bool same(const EditorSceneTransformSettings& other) const noexcept
    {
        return tool == other.tool && space == other.space && pivot == other.pivot
            && snap.same(other.snap);
    }

    static constexpr std::string_view toolName(EditorSceneTransformTool value) noexcept
    {
        switch (value) {
        case EditorSceneTransformTool::Select:
            return "select";
        case EditorSceneTransformTool::Move:
            return "move";
        case EditorSceneTransformTool::Rotate:
            return "rotate";
        case EditorSceneTransformTool::Scale:
            return "scale";
        }
        return {};
    }

    static constexpr std::string_view spaceName(EditorSceneTransformSpace value) noexcept
    {
        switch (value) {
        case EditorSceneTransformSpace::World:
            return "world";
        case EditorSceneTransformSpace::Local:
            return "local";
        }
        return {};
    }

    static constexpr std::string_view pivotName(EditorSceneTransformPivot value) noexcept
    {
        switch (value) {
        case EditorSceneTransformPivot::Active:
            return "active";
        case EditorSceneTransformPivot::Center:
            return "center";
        case EditorSceneTransformPivot::Individual:
            return "individual";
        }
        return {};
    }

    static bool parseTool(std::string_view name, EditorSceneTransformTool& result) noexcept
    {
        for (const auto value : {EditorSceneTransformTool::Select,
                 EditorSceneTransformTool::Move,
                 EditorSceneTransformTool::Rotate,
                 EditorSceneTransformTool::Scale})
            if (toolName(value) == name) {
                result = value;
                return true;
            }
        return false;
    }

    static bool parseSpace(std::string_view name, EditorSceneTransformSpace& result) noexcept
    {
        for (const auto value :
            {EditorSceneTransformSpace::World, EditorSceneTransformSpace::Local})
            if (spaceName(value) == name) {
                result = value;
                return true;
            }
        return false;
    }

    static bool parsePivot(std::string_view name, EditorSceneTransformPivot& result) noexcept
    {
        for (const auto value : {EditorSceneTransformPivot::Active,
                 EditorSceneTransformPivot::Center,
                 EditorSceneTransformPivot::Individual})
            if (pivotName(value) == name) {
                result = value;
                return true;
            }
        return false;
    }
};
} // namespace arp::editor
