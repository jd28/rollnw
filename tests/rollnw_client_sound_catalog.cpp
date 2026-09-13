#include <gtest/gtest.h>

#include "../tools/client/sound_catalog.hpp"

#include <nw/kernel/Kernel.hpp>

#include <algorithm>
#include <string_view>
#include <vector>

namespace {

TEST(ClientSoundCatalog, BuildsAndFiltersActiveAmbientSoundRows)
{
    auto module = nw::kernel::load_module(
        "test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);

    nw::toolset::SoundCatalog catalog;
    ASSERT_TRUE(nw::toolset::build_sound_catalog(catalog))
        << catalog.diagnostic;
    ASSERT_EQ(catalog.status, nw::toolset::SoundCatalogStatus::ready);
    EXPECT_GT(catalog.table_row_count, 0u);
    ASSERT_FALSE(catalog.rows.empty());
    EXPECT_TRUE(std::is_sorted(catalog.rows.begin(), catalog.rows.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.sort_key < rhs.sort_key
                || (lhs.sort_key == rhs.sort_key
                    && lhs.resource < rhs.resource);
        }));

    constexpr std::string_view target = "al_pl_whispersm";
    const auto row = std::ranges::find_if(catalog.rows,
        [target](const auto& value) { return value.resource.view() == target; });
    ASSERT_NE(row, catalog.rows.end());
    EXPECT_FALSE(row->name.empty());
    EXPECT_FALSE(row->name.starts_with("Bad Strref"));

    std::vector<uint32_t> matches;
    nw::toolset::filter_sound_catalog(catalog, "  AL_PL_WHISPERSM\t", matches);
    ASSERT_EQ(matches.size(), 1u);
    ASSERT_LT(matches.front(), catalog.rows.size());
    EXPECT_EQ(catalog.rows[matches.front()].resource.view(), target);

    nw::toolset::filter_sound_catalog(catalog, {}, matches);
    EXPECT_EQ(matches.size(), catalog.rows.size());
}

} // namespace
