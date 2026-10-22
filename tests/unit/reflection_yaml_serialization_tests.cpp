#include "reflection/reflection.h"
#include "reflection/serialization.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>

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
    hlist tags = hlist(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
};

struct YamlStats {
    YamlStats() = default;

    int level = 0;
    arp::core::pmr::String title{&arp::core::defaultCpuMemoryResource()};
};

struct YamlNestedPlayer {
    YamlNestedPlayer() = default;

    YamlNestedPlayer(const YamlNestedPlayer& other)
        : stats(other.stats), statsPtr(other.statsPtr), sharedStats(other.sharedStats),
          uniqueStats(
              other.uniqueStats ? arp::core::makeUnique<YamlStats>(*other.uniqueStats) : nullptr)
    {}

    YamlNestedPlayer(YamlNestedPlayer&&) = default;

    YamlStats stats;
    YamlStats* statsPtr = nullptr;
    std::shared_ptr<YamlStats> sharedStats;
    arp::core::UniquePtr<YamlStats> uniqueStats;
};

ReflectionObjectResult legacyRead(NameView text)
{
    return deserialize_from_yaml(text, {SerializationProfile::LegacyAllPropertiesV1});
}

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

enum class Choice { A = 1, B = 7 };

struct ArchiveState {
    std::uint64_t full = UINT64_MAX;
    std::int64_t signedFull = INT64_MIN;
    Choice choice = Choice::B;
    double finite = 1;
    int editorOnly = 44;
    arp::core::pmr::Vector<YamlStats> nested{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Map<arp::core::pmr::String, std::uint64_t> map{
        &arp::core::defaultCpuMemoryResource()};
    std::optional<int> optional;
};

void archiveContracts()
{
    using Node = arp::core::YamlContract::Node;
    constexpr auto flag = static_cast<std::uint8_t>(PropertyHint::Serialize);
    const auto* enumType = type_of<Choice>();
    const bool originalMembership = enumType->containsEnumValue(99);
    {
        RegistrationTransaction transaction("serialization-metadata-rollback");
        RegistrationScope scope(transaction);
        arp::core::pmr::Map<arp::core::pmr::String, int> values{
            &arp::core::defaultCpuMemoryResource()};
        values.emplace("temporary", 99);
        enumType->setEnumValues(values);
        assert(enumType->containsEnumValue(99));
    }
    assert(enumType->containsEnumValue(99) == originalMembership);
    registration::class_<YamlStats>()
        .property("level", &YamlStats::level, {}, flag)
        .property("title", &YamlStats::title, {}, flag);
    registration::class_<ArchiveState>()
        .construct<>()
        .constant<Choice>("Choice", {{"A", Choice::A}, {"B", Choice::B}})
        .property("full", &ArchiveState::full, {}, flag)
        .property("signedFull", &ArchiveState::signedFull, {}, flag)
        .property("choice", &ArchiveState::choice, {}, flag)
        .property("finite", &ArchiveState::finite, {}, flag)
        .property("editorOnly",
            &ArchiveState::editorOnly,
            {},
            static_cast<std::uint8_t>(PropertyHint::Editor))
        .property("nested", &ArchiveState::nested, {}, flag)
        .property("map", &ArchiveState::map, {}, flag)
        .property("optional", &ArchiveState::optional, {}, flag);
    ArchiveState state;
    state.nested.emplace_back();
    state.nested.back().level = 12;
    state.map.emplace(text("wide"), UINT64_MAX);
    state.optional = 7;
    object source(state);
    SerializationArchive writer;
    Node node;
    if (!writer.writeFields(source, node)) {
        std::cerr << writer.diagnostics() << "\n";
        std::abort();
    }
    assert(!node.contains("editorOnly"));
    assert(node["full"].get_value<std::string>() == "18446744073709551615");
    ArchiveState destination;
    object target(&destination);
    SerializationArchive reader;
    assert(reader.readFields(node, target));
    assert(destination.full == UINT64_MAX && destination.signedFull == INT64_MIN);
    assert(destination.optional == 7 && destination.nested[0].level == 12);
    assert(destination.map.at(text("wide")) == UINT64_MAX && destination.editorOnly == 44);
    auto bad = node;
    bad["choice"] = 9;
    SerializationArchive enumReader;
    assert(!enumReader.readFields(bad, target));
    assert(enumReader.diagnostic().path == "$.choice");
    bad = node;
    bad["full"] = "18446744073709551616";
    SerializationArchive overflow;
    assert(!overflow.readFields(bad, target));
    bad["full"] = "01";
    SerializationArchive noncanonical;
    assert(!noncanonical.readFields(bad, target));
    SerializationArchive underflow;
    object narrow;
    assert(!underflow.readValue(Node(1e-100), type_of<float>(), narrow));
    bad = node;
    bad["finite"] = std::numeric_limits<double>::infinity();
    SerializationArchive finite;
    assert(!finite.readFields(bad, target));
    bad = node;
    bad["editorOnly"] = 3;
    SerializationArchive hidden;
    assert(!hidden.readFields(bad, target));
    bad = node;
    bad["nested"][0]["fields"]["level"] = "wrong";
    destination.full = 42;
    SerializationArchive staged;
    assert(!staged.readFields(bad, target));
    assert(destination.full == 42); // conversion failure does not partially assign
    SerializationArchive budget({SerializationProfile::Authoring, 1, 3, 1});
    assert(!budget.readFields(node, target));
    for (const double number : {1.23456789012345,
             std::numeric_limits<double>::max(),
             std::numeric_limits<double>::min(),
             std::numeric_limits<double>::denorm_min(),
             -0.0}) {
        const auto text = arp::core::YamlContract::serialize(Node(number));
        const auto parsed = arp::core::YamlContract::parseSingleDocument(text);
        assert(parsed.is_float_number() && parsed.get_value<double>() == number);
        assert(std::signbit(parsed.get_value<double>()) == std::signbit(number));
    }
    state.optional.reset();
    auto yaml = serialize_to_yaml(object(state));
    assert(yaml.success);
    auto roundtrip = deserialize_from_yaml(yaml.yaml);
    if (!roundtrip.success)
        std::cerr << roundtrip.diagnostics << "\n" << yaml.yaml << "\n";
    assert(roundtrip.success && !roundtrip.value.cast<ArchiveState&>().optional);
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

    auto yamlResult =
        serialize_to_yaml(object(source), {SerializationProfile::LegacyAllPropertiesV1});
    if (!yamlResult.success)
        std::cerr << yamlResult.diagnostics << "\n";
    assert(yamlResult.success);
    assertContains(yamlResult.yaml, "type: YamlPlayer");
    assertContains(yamlResult.yaml, "fields:");
    assertContains(yamlResult.yaml, "health: 77");
    assertContains(yamlResult.yaml, "active: true");
    assertContains(yamlResult.yaml, "speed:");
    assertContains(yamlResult.yaml, "name: mika");
    assertContains(yamlResult.yaml, "tags:");
    assertContains(yamlResult.yaml, "- scout");

    auto objectResult = legacyRead(yamlResult.yaml);
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

    auto missingType = legacyRead("fields:\n  health: 1\n");
    assert(!missingType.success);
    assertContains(missingType.diagnostics, "missing required type");

    auto unknownType = legacyRead("type: MissingType\nfields:\n  health: 1\n");
    assert(!unknownType.success);
    assertContains(unknownType.diagnostics, "unregistered type");

    auto wrongType = legacyRead("type: YamlPlayer\nfields:\n  active: not-bool\n");
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

    auto nestedYamlResult = serialize_to_yaml(
        object(std::move(nestedSource)), {SerializationProfile::LegacyAllPropertiesV1});
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

    auto nestedObjectResult = legacyRead(nestedYamlResult.yaml);
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

    auto nullPointerResult = legacyRead("type: YamlNestedPlayer\n"
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

    archiveContracts();
    return 0;
}
