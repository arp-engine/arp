#include "reflection/reflection.h"
#include "reflection/serialization.h"

#include <cassert>
#include <iostream>
#include <memory>

using namespace arp::reflection;

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

struct YamlPlayer {
    YamlPlayer() = default;

    int health = 0;
    bool active = false;
    double speed = 0.0;
    arp::core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
    hlist tags(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
};

struct YamlStats {
    YamlStats() = default;

    int level = 0;
    arp::core::pmr::String title{&arp::core::defaultCpuMemoryResource()};
};

struct YamlNestedPlayer {
    YamlNestedPlayer() = default;

    YamlStats stats;
    YamlStats* statsPtr = nullptr;
    std::shared_ptr<YamlStats> sharedStats;
    arp::core::UniquePtr<YamlStats> uniqueStats;
};

void registerYamlPlayer()
{
    registration::class_<YamlStats>("YamlStats")
        .construct<>()
        .property("level", &YamlStats::level)
        .property("title", &YamlStats::title);

    registration::class_<YamlPlayer>("YamlPlayer")
        .construct<>()
        .property("health", &YamlPlayer::health)
        .property("active", &YamlPlayer::active)
        .property("speed", &YamlPlayer::speed)
        .property("name", &YamlPlayer::name)
        .property("tags", &YamlPlayer::tags);

    registration::class_<YamlNestedPlayer>("YamlNestedPlayer")
        .construct<>()
        .property("stats", &YamlNestedPlayer::stats)
        .property("statsPtr", &YamlNestedPlayer::statsPtr)
        .property("sharedStats", &YamlNestedPlayer::sharedStats)
        .property("uniqueStats", &YamlNestedPlayer::uniqueStats);
}

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    registerYamlPlayer();

    YamlPlayer source;
    source.health = 77;
    source.active = true;
    source.speed = 3.5;
    source.name = text("mika");
    source.tags.emplace_back(text("scout"));
    source.tags.emplace_back(12);

    auto yamlResult = serialize_to_yaml(object(source));
    assert(yamlResult.success);
    assertContains(yamlResult.yaml, "type: YamlPlayer");
    assertContains(yamlResult.yaml, "fields:");
    assertContains(yamlResult.yaml, "health: 77");
    assertContains(yamlResult.yaml, "active: true");
    assertContains(yamlResult.yaml, "speed:");
    assertContains(yamlResult.yaml, "name: mika");
    assertContains(yamlResult.yaml, "tags:");
    assertContains(yamlResult.yaml, "- scout");

    auto objectResult = deserialize_from_yaml(yamlResult.yaml);
    if (!objectResult.success) {
        std::cerr << objectResult.diagnostics << "\n";
        std::cerr << yamlResult.yaml << "\n";
    }
    assert(objectResult.success);
    auto* restored = objectResult.value.cast<YamlPlayer*>();
    assert(restored != nullptr);
    assert(restored->health == 77);
    assert(restored->active);
    assert(restored->speed > 3.49 && restored->speed < 3.51);
    assert(restored->name == text("mika"));
    assert(restored->tags.size() == 2u);
    assert(restored->tags[0].cast<arp::core::pmr::String>() == text("scout"));
    assert(restored->tags[1].cast<int>() == 12);

    auto missingType = deserialize_from_yaml("fields:\n  health: 1\n");
    assert(!missingType.success);
    assertContains(missingType.diagnostics, "missing required type");

    auto unknownType = deserialize_from_yaml("type: MissingType\nfields:\n  health: 1\n");
    assert(!unknownType.success);
    assertContains(unknownType.diagnostics, "unregistered type");

    auto wrongType = deserialize_from_yaml("type: YamlPlayer\nfields:\n  active: not-bool\n");
    assert(!wrongType.success);
    assertContains(wrongType.diagnostics, "active");
    assertContains(wrongType.diagnostics, "bool");

    YamlNestedPlayer nestedSource;
    nestedSource.stats.level = 9;
    nestedSource.stats.title = text("captain");
    YamlStats pointedStats;
    pointedStats.level = 12;
    pointedStats.title = text("pointer");
    nestedSource.statsPtr = &pointedStats;
    nestedSource.sharedStats = arp::core::makeShared<YamlStats>();
    nestedSource.sharedStats->level = 14;
    nestedSource.sharedStats->title = text("shared");
    nestedSource.uniqueStats = arp::core::makeUnique<YamlStats>();
    nestedSource.uniqueStats->level = 16;
    nestedSource.uniqueStats->title = text("unique");

    auto nestedYamlResult = serialize_to_yaml(object(std::move(nestedSource)));
    assert(nestedYamlResult.success);
    assertContains(nestedYamlResult.yaml, "type: YamlNestedPlayer");
    assertContains(nestedYamlResult.yaml, "stats:");
    assertContains(nestedYamlResult.yaml, "type: YamlStats");
    assertContains(nestedYamlResult.yaml, "level: 9");
    assertContains(nestedYamlResult.yaml, "title: captain");
    assertContains(nestedYamlResult.yaml, "statsPtr:");
    assertContains(nestedYamlResult.yaml, "level: 12");
    assertContains(nestedYamlResult.yaml, "title: pointer");
    assertContains(nestedYamlResult.yaml, "sharedStats:");
    assertContains(nestedYamlResult.yaml, "level: 14");
    assertContains(nestedYamlResult.yaml, "title: shared");
    assertContains(nestedYamlResult.yaml, "uniqueStats:");
    assertContains(nestedYamlResult.yaml, "level: 16");
    assertContains(nestedYamlResult.yaml, "title: unique");

    auto nestedObjectResult = deserialize_from_yaml(nestedYamlResult.yaml);
    assert(nestedObjectResult.success);
    assert(nestedObjectResult.ownedObjects.size() == 1u);
    auto* restoredNested = nestedObjectResult.value.cast<YamlNestedPlayer*>();
    assert(restoredNested != nullptr);
    assert(restoredNested->stats.level == 9);
    assert(restoredNested->stats.title == text("captain"));
    assert(restoredNested->statsPtr != nullptr);
    assert(restoredNested->statsPtr->level == 12);
    assert(restoredNested->statsPtr->title == text("pointer"));
    assert(restoredNested->sharedStats != nullptr);
    assert(restoredNested->sharedStats->level == 14);
    assert(restoredNested->sharedStats->title == text("shared"));
    assert(restoredNested->uniqueStats != nullptr);
    assert(restoredNested->uniqueStats->level == 16);
    assert(restoredNested->uniqueStats->title == text("unique"));

    auto nullPointerResult = deserialize_from_yaml("type: YamlNestedPlayer\n"
                                                   "fields:\n"
                                                   "  stats:\n"
                                                   "    type: YamlStats\n"
                                                   "    fields:\n"
                                                   "      level: 3\n"
                                                   "      title: local\n"
                                                   "  statsPtr: null\n"
                                                   "  sharedStats: null\n"
                                                   "  uniqueStats: null\n");
    assert(nullPointerResult.success);
    assert(nullPointerResult.ownedObjects.empty());
    auto* nullPointerPlayer = nullPointerResult.value.cast<YamlNestedPlayer*>();
    assert(nullPointerPlayer != nullptr);
    assert(nullPointerPlayer->stats.level == 3);
    assert(nullPointerPlayer->statsPtr == nullptr);
    assert(nullPointerPlayer->sharedStats == nullptr);
    assert(nullPointerPlayer->uniqueStats == nullptr);

    return 0;
}
