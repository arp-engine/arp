#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_scene_inspector_selection.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>

namespace {
using namespace arp;
using namespace arp::editor;
using Selection = EditorSceneInspectorSelection;
using Kind = SceneInspectorPropertyKind;
constexpr Selection::Budget budget{8, 16, 64, 128, 512, 256};

Selection::Object object(std::uint64_t id)
{
    Selection::Object result;
    result.inspector.objectId = id;
    result.inspector.objectName = "Same name";
    result.components.push_back({id + 100, core::pmr::String("FixtureComponent")});
    return result;
}

SceneInspectorProperty property(const Selection::Object& owner, Kind kind, std::string_view path)
{
    SceneInspectorProperty result;
    result.objectId = owner.inspector.objectId;
    result.componentId = owner.components.front().id;
    result.componentType = owner.components.front().type;
    result.name = path;
    result.typeName = "FixtureValue";
    result.kind = kind;
    result.editable = true;
    result.serializable = true;
    return result;
}

std::array<Selection::Object, 2> pair(Kind kind)
{
    std::array<Selection::Object, 2> objects{object(1), object(2)};
    for (auto& owner : objects)
        owner.inspector.properties.push_back(property(owner, kind, "nested.value"));
    return objects;
}

void everyValueKind()
{
    for (const auto kind : {Kind::Boolean,
             Kind::Integer,
             Kind::UnsignedInteger,
             Kind::Number,
             Kind::Enumeration,
             Kind::Vector2,
             Kind::Vector2List,
             Kind::Vector3,
             Kind::Color4,
             Kind::String,
             Kind::ResourceReference,
             Kind::SceneObjectReference,
             Kind::SceneObjectLinkList}) {
        auto objects = pair(kind);
        auto& first = objects[0].inspector.properties.front();
        auto& second = objects[1].inspector.properties.front();
        if (kind == Kind::Enumeration) {
            first.enumOptions = {
                {core::pmr::String("Zero"), {}, 0}, {core::pmr::String("One"), {}, 1}};
            second.enumOptions = first.enumOptions;
            std::reverse(second.enumOptions.begin(), second.enumOptions.end());
        }
        if (kind == Kind::Vector2List) {
            first.vector2ListValue = {{1, 2}, {3, 4}};
            second.vector2ListValue = first.vector2ListValue;
        }
        if (kind == Kind::SceneObjectLinkList) {
            first.sceneObjectLinkListValue = {{core::pmr::String("Target"), 42}};
            second.sceneObjectLinkListValue = first.sceneObjectLinkListValue;
        }
        Selection::Snapshot output;
        assert(!Selection::prepare(objects, budget, output));
        assert(output.fields.size() == 1 && !output.fields[0].mixed
            && output.fields[0].status == Selection::Status::Common);
        assert(output.fields[0].members[1].objectIndex == 1
            && output.objects[1].inspector.properties[0].componentId == 102);
        switch (kind) {
        case Kind::Boolean:
            second.booleanValue = true;
            break;
        case Kind::Integer:
        case Kind::Enumeration:
            second.integerValue = 1;
            break;
        case Kind::UnsignedInteger:
            second.unsignedIntegerValue = std::numeric_limits<std::uint64_t>::max();
            break;
        case Kind::Number:
            second.numberValue = std::nextafter(0.0, 1.0);
            break;
        case Kind::Vector2:
            second.vector2Value.x = 1;
            break;
        case Kind::Vector3:
            second.vector3Value.z = 1;
            break;
        case Kind::Color4:
            second.color4Value.w = 1;
            break;
        case Kind::String:
            second.stringValue = "Different";
            break;
        case Kind::ResourceReference:
            first.stringValue = "00000000-0000-4000-8000-000000000001";
            second.stringValue = "00000000-0000-4000-8000-000000000002";
            break;
        case Kind::SceneObjectReference:
            second.sceneObjectReferenceValue = std::numeric_limits<std::uint64_t>::max();
            break;
        case Kind::Vector2List:
            second.vector2ListValue[1].y = 5;
            break;
        case Kind::SceneObjectLinkList:
            second.sceneObjectLinkListValue[0].targetObjectId = 43;
            break;
        case Kind::Unsupported:
            assert(false);
            break;
        }
        assert(!Selection::prepare(objects, budget, output) && output.fields[0].mixed);
        // Output is a deep observation; subsequent source edits cannot alter it.
        second.name = "changed.path";
        assert(output.fields[0].path == "nested.value"
            && output.objects[1].inspector.properties[0].name == "nested.value");
    }
    auto objects = pair(Kind::ResourceReference);
    objects[0].inspector.properties[0].integerValue = 3;
    objects[1].inspector.properties[0].integerValue = 7;
    Selection::Snapshot output;
    assert(!Selection::prepare(objects, budget, output) && !output.fields[0].mixed);
    std::puts(
        "PASS exact values for all 13 supported kinds; reference identity ignores menu indices");
}

void metadataAndUnion()
{
    for (unsigned difference = 0; difference < 9; ++difference) {
        auto objects = pair(Kind::Number);
        auto& changed = objects[1].inspector.properties[0];
        switch (difference) {
        case 0:
            changed.kind = Kind::Integer;
            break;
        case 1:
            changed.typeName = "OtherValue";
            break;
        case 2:
            changed.editable = false;
            break;
        case 3:
            changed.serializable = false;
            break;
        case 4:
            changed.resourceType = "SpriteResource";
            break;
        case 5:
            changed.sceneObjectType = "RigidBody2DComponent";
            break;
        case 6:
            changed.resourceOptional = true;
            break;
        case 7:
            objects[0].inspector.properties[0].kind = Kind::Enumeration;
            objects[0].inspector.properties[0].enumOptions.push_back(
                {core::pmr::String("Zero"), {}, 0});
            changed.kind = Kind::Enumeration;
            changed.enumOptions = objects[0].inspector.properties[0].enumOptions;
            changed.enumOptions.push_back({core::pmr::String("Extra"), {}, 1});
            break;
        case 8:
            changed.name = "other.value";
            break;
        }
        Selection::Snapshot output;
        assert(!Selection::prepare(objects, budget, output));
        assert(output.fields[0].status
            == (difference == 8 ? Selection::Status::Missing : Selection::Status::Incompatible));
        assert(!output.fields[0].mixed);
    }
    auto objects = pair(Kind::Number);
    objects[1].components[0].type = "DifferentComponent";
    objects[1].inspector.properties[0].componentType = "DifferentComponent";
    Selection::Snapshot output;
    assert(!Selection::prepare(objects, budget, output) && output.fields.size() == 2);
    for (const auto& field : output.fields)
        assert(field.status == Selection::Status::Missing);
    objects = pair(Kind::Unsupported);
    assert(!Selection::prepare(objects, budget, output)
        && output.fields[0].status == Selection::Status::Unsupported);
    std::puts(
        "PASS exact type/path union, absent fields, incompatible metadata and unsupported observations");
}

void rejectedInputAndAllocation()
{
    auto objects = pair(Kind::Number);
    Selection::Snapshot output;
    assert(!Selection::prepare(objects, budget, output));
    const auto reject = [&](auto mutate, const char* expected) {
        auto invalid = objects;
        auto limits = budget;
        mutate(invalid, limits);
        const auto* diagnostic = Selection::prepare(invalid, limits, output);
        assert(diagnostic && std::string_view(diagnostic) == expected);
        assert(output.objects.size() == 2 && output.objects[0].inspector.objectId == 1
            && output.fields.size() == 1 && output.fields[0].status == Selection::Status::Common);
    };
    reject(
        [](auto& values, auto&) {
            values[1].inspector.objectId = 1;
        },
        "EDITOR-INSPECTOR-SELECTION-PROPERTY-INVALID");
    reject(
        [](auto& values, auto&) {
            values[1] = values[0];
        },
        "EDITOR-INSPECTOR-SELECTION-OBJECT-DUPLICATE");
    reject(
        [](auto& values, auto&) {
            values[0].inspector.properties[0].kind = static_cast<Kind>(255);
        },
        "EDITOR-INSPECTOR-SELECTION-PROPERTY-INVALID");
    reject(
        [](auto& values, auto&) {
            values[0].components.push_back({999, core::pmr::String("FixtureComponent")});
        },
        "EDITOR-INSPECTOR-SELECTION-COMPONENT-AMBIGUOUS");
    reject(
        [](auto& values, auto&) {
            values[0].inspector.properties.push_back(values[0].inspector.properties[0]);
        },
        "EDITOR-INSPECTOR-SELECTION-PATH-AMBIGUOUS");
    reject(
        [](auto& values, auto&) {
            values[0].inspector.properties[0].numberValue = std::numeric_limits<double>::infinity();
        },
        "EDITOR-INSPECTOR-SELECTION-NONFINITE");
    reject(
        [](auto&, auto& limits) {
            limits.maxObjects = 1;
        },
        "EDITOR-INSPECTOR-SELECTION-BUDGET");
    reject(
        [](auto&, auto& limits) {
            limits.maxStringBytes = 1;
        },
        "EDITOR-INSPECTOR-SELECTION-OBJECT-INVALID");
    reject(
        [](auto& values, auto& limits) {
            limits.maxFields = 1;
            values[1].inspector.properties[0].name = "other";
        },
        "EDITOR-INSPECTOR-SELECTION-FIELD-BUDGET");
    reject(
        [](auto& values, auto& limits) {
            limits.maxElementsPerValue = 1;
            values[1].inspector.properties[0].vector2ListValue.resize(2);
        },
        "EDITOR-INSPECTOR-SELECTION-VALUE-BUDGET");
    objects[1].inspector.properties[0].numberValue = 2;
    unsigned failures = 0, successes = 0;
    for (const std::uint64_t bytes : {1u, 1024u, 4096u, 16384u}) {
        const auto previous = output.objects[1].inspector.properties[0].numberValue;
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            core::CpuMemoryQuotaScope scope(quota);
            diagnostic = Selection::prepare(objects, budget, output);
        }
        if (diagnostic) {
            ++failures;
            assert(std::string_view(diagnostic) == "EDITOR-INSPECTOR-SELECTION-ALLOCATION-FAILED");
            assert(output.objects[1].inspector.properties[0].numberValue == previous);
        } else {
            ++successes;
            assert(output.objects[1].inspector.properties[0].numberValue == 2
                && output.fields[0].mixed);
        }
    }
    assert(failures && successes);
    assert(output.fields.size() == 1 && output.objects.size() == 2);
    assert(Selection::prepare({}, budget, output));
    std::puts("PASS ambiguous/invalid/budget/OOM rejection preserves previous output");
}

void nativeObservation()
{
    EditorSceneDocument document(false);
    const auto a = document.createObject("Same name");
    const auto b = document.createObject("Same name");
    assert(document.addComponent("CameraComponent", a));
    assert(document.addComponent("CameraComponent", b));
    std::array<Selection::Object, 2> objects;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto id = i == 0 ? a : b;
        auto& observation = objects[i];
        const auto* native = document.scene().findObject(id);
        observation.inspector.objectId = id.value();
        observation.inspector.objectName = native->name();
        observation.inspector.properties = document.inspectorProperties(id);
        for (const auto& component : native->components())
            observation.components.push_back(
                {component->id().value(), core::pmr::String(component->scriptTypeName())});
    }
    Selection::Snapshot result;
    const auto* diagnostic = Selection::prepare(objects, budget, result);
    if (diagnostic)
        std::fprintf(stderr, "%s\n", diagnostic);
    assert(!diagnostic && !result.fields.empty());
    assert(document.selectedObject() == b);
    assert(std::all_of(result.fields.begin(), result.fields.end(), [](const auto& field) {
        return field.status == Selection::Status::Common && !field.mixed;
    }));
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        nativeObservation();
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned cycle = 0; cycle < 20; ++cycle) {
            nativeObservation();
            const auto current = core::defaultCpuAllocator().stats();
            assert(current.liveBytes == baseline.liveBytes
                && current.allocationCount - current.deallocationCount
                    == baseline.allocationCount - baseline.deallocationCount);
        }
        std::puts(
            "PASS 20 ordinary warmed read-only native observation cycles: core live bytes/count delta=0");
    } else {
        everyValueKind();
        metadataAndUnion();
        rejectedInputAndAllocation();
        nativeObservation();
    }
}
