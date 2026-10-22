#pragma once
#include "3d/vegetation/template.h"

#include <array>
#include <string_view>

namespace arp::resource::detail {
// Catalog-specific one-LOD family naming and count mapping. Shared by legacy
// offline preparation and the strict cooked metadata codec.
class CatalogFamilyContract final {
    using Config = vegetation::VegetationTemplateConfig;
    using Category = vegetation::TemplateCategory;

    struct Family {
        const char* name;
        Category category;
        std::uint16_t Config::* count;
    };

    static constexpr std::array<Family, 12> families_{{
        {"foliage_grass", Category::FoliageGrass, &Config::grassCardCount},
        {"foliage_weed", Category::FoliageWeed, &Config::weedCardCount},
        {"foliage_broadleaf", Category::FoliageBroadleaf, &Config::broadleafCardCount},
        {"foliage_fern", Category::FoliageFern, &Config::fernCardCount},
        {"foliage_bush", Category::FoliageBush, &Config::bushCardCount},
        {"foliage_flower", Category::FoliageFlower, &Config::flowerCardCount},
        {"foliage_mushroom", Category::FoliageMushroom, &Config::mushroomCardCount},
        {"tree_mature_pine", Category::TreeMaturePine, &Config::maturePineVariantCount},
        {"tree_young_fir", Category::TreeYoungFir, &Config::youngFirVariantCount},
        {"tree_young_pine", Category::TreeYoungPine, &Config::youngPineVariantCount},
        {"rock_mossy_01", Category::RockMossy01, &Config::mossyRock01VariantCount},
        {"rock_mossy_02", Category::RockMossy02, &Config::mossyRock02VariantCount},
    }};

public:
    static bool parse(std::string_view name, Category& category)
    {
        for (const auto& family : families_)
            if (name == family.name) {
                category = family.category;
                return true;
            }
        return false;
    }

    static const char* name(Category category)
    {
        for (const auto& family : families_)
            if (category == family.category)
                return family.name;
        return nullptr;
    }

    static bool matches(const Config& actual, const Config& expected) noexcept
    {
        if (!actual.hasEnvironmentAssets || actual.treeLodCount != 1u || actual.rockLodCount != 1u
            || actual.proceduralGrassTemplate || actual.proceduralTreeTemplate
            || actual.proceduralUnderstoryTemplate || actual.proceduralRockTemplate
            || actual.proceduralSlabRockTemplate)
            return false;
        for (const auto& family : families_)
            if (actual.*family.count != expected.*family.count)
                return false;
        return true;
    }

    static bool increment(Config& config, Category category)
    {
        for (const auto& family : families_)
            if (category == family.category) {
                if (config.*family.count == UINT16_MAX)
                    return false;
                ++(config.*family.count);
                return true;
            }
        return false;
    }
};
} // namespace arp::resource::detail
