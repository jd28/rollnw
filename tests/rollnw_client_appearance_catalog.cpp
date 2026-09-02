#include <gtest/gtest.h>

#include "../tools/client/appearance_catalog.hpp"

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <vector>

namespace {

TEST(ClientAppearanceCatalog, BuildsSortedNativeCreatureRowsAndFiltersStableIndices)
{
    auto module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);

    nw::toolset::AppearanceCatalog catalog;
    ASSERT_TRUE(nw::toolset::build_appearance_catalog(
        nw::toolset::AppearanceCatalogKind::creature, catalog))
        << catalog.diagnostic;
    ASSERT_EQ(catalog.status, nw::toolset::AppearanceCatalogStatus::ready);
    EXPECT_GT(catalog.source_row_count, catalog.rows.size());
    EXPECT_TRUE(std::is_sorted(catalog.rows.begin(), catalog.rows.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.sort_key < rhs.sort_key || (lhs.sort_key == rhs.sort_key && lhs.id < rhs.id);
    }));

    const auto* bodak = nw::toolset::find_appearance_catalog_row(catalog, 23);
    ASSERT_NE(bodak, nullptr);
    EXPECT_EQ(bodak->label, "Bodak");
    EXPECT_EQ(bodak->model, "c_bodak");

    const auto* current_fixture_appearance = nw::toolset::find_appearance_catalog_row(catalog, 6);
    ASSERT_NE(current_fixture_appearance, nullptr);
    EXPECT_TRUE(std::none_of(catalog.rows.begin(), catalog.rows.end(), [](const auto& row) {
        return row.name.starts_with("Bad Strref");
    }));

    nw::StaticTwoDA source{nw::kernel::resman().demand(
        nw::Resource{nw::StringView{"appearance"}, nw::ResourceType::twoda})};
    ASSERT_TRUE(source.is_valid());
    const nw::toolset::AppearanceCatalogRow* missing_strref = nullptr;
    std::string expected_name;
    for (size_t index = 0; index < source.rows(); ++index) {
        int32_t strref = -1;
        if (source.get_to(index, "STRING_REF", strref, false)) {
            continue;
        }

        nw::StringView label;
        if (!source.get_to(index, "LABEL", label, false)) {
            continue;
        }
        missing_strref = nw::toolset::find_appearance_catalog_row(
            catalog, static_cast<int32_t>(index));
        if (missing_strref) {
            expected_name = label;
            std::replace(expected_name.begin(), expected_name.end(), '_', ' ');
            break;
        }
    }
    ASSERT_NE(missing_strref, nullptr);
    EXPECT_EQ(missing_strref->name, expected_name);

    std::vector<uint32_t> matches;
    nw::toolset::filter_appearance_catalog(catalog, "c_bodak", matches);
    ASSERT_FALSE(matches.empty());
    EXPECT_TRUE(std::any_of(matches.begin(), matches.end(), [&](uint32_t index) {
        return catalog.rows[index].id == 23;
    }));
}

TEST(ClientAppearanceCatalog, BuildsAndFiltersNativePlaceableRows)
{
    auto module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);

    nw::toolset::AppearanceCatalog catalog;
    ASSERT_TRUE(nw::toolset::build_appearance_catalog(
        nw::toolset::AppearanceCatalogKind::placeable, catalog))
        << catalog.diagnostic;

    const auto* corpse = nw::toolset::find_appearance_catalog_row(catalog, 109);
    ASSERT_NE(corpse, nullptr);
    EXPECT_EQ(corpse->label, "ArrowCorpse");
    EXPECT_EQ(corpse->model, "plc_o01");

    std::vector<uint32_t> matches;
    nw::toolset::filter_appearance_catalog(catalog, "arrowcorpse", matches);
    ASSERT_FALSE(matches.empty());
    EXPECT_TRUE(std::any_of(matches.begin(), matches.end(), [&](uint32_t index) {
        return catalog.rows[index].id == 109;
    }));
}

TEST(ClientAppearanceCatalog, BuildsSparseNativeWingAndTailRows)
{
    auto module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);

    const auto check_catalog = [](nw::toolset::AppearanceCatalogKind kind,
                                   std::string_view resource) {
        nw::toolset::AppearanceCatalog catalog;
        ASSERT_TRUE(nw::toolset::build_appearance_catalog(kind, catalog))
            << catalog.diagnostic;
        ASSERT_EQ(catalog.status, nw::toolset::AppearanceCatalogStatus::ready);
        ASSERT_FALSE(catalog.rows.empty());
        EXPECT_TRUE(std::is_sorted(
            catalog.rows.begin(), catalog.rows.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.sort_key < rhs.sort_key
                    || (lhs.sort_key == rhs.sort_key && lhs.id < rhs.id);
            }));

        const auto* none = nw::toolset::find_appearance_catalog_row(catalog, 0);
        ASSERT_NE(none, nullptr);
        EXPECT_EQ(none->name, "None");

        const auto source_row = std::find_if(catalog.rows.begin(), catalog.rows.end(), [](const auto& row) {
            return row.id > 0 && !row.label.empty() && !row.model.empty();
        });
        ASSERT_NE(source_row, catalog.rows.end());
        ASSERT_LT(static_cast<size_t>(source_row->id), catalog.source_row_count);

        nw::StaticTwoDA source{nw::kernel::resman().demand(
            nw::Resource{nw::StringView{resource}, nw::ResourceType::twoda})};
        ASSERT_TRUE(source.is_valid());
        nw::StringView label;
        ASSERT_TRUE(source.get_to(static_cast<size_t>(source_row->id), "LABEL", label, false));
        EXPECT_EQ(source_row->label, label);

        std::string expected_name{label};
        std::replace(expected_name.begin(), expected_name.end(), '_', ' ');
        EXPECT_EQ(source_row->name, expected_name);

        const nw::CreatureAccessoryModelInfo* native_row = nullptr;
        if (kind == nw::toolset::AppearanceCatalogKind::wing) {
            native_row = nw::kernel::rules().wingmodels.get(
                nw::WingModel::make(source_row->id));
        } else {
            native_row = nw::kernel::rules().tailmodels.get(
                nw::TailModel::make(source_row->id));
        }
        ASSERT_NE(native_row, nullptr);
        EXPECT_EQ(native_row->label, source_row->label);
        EXPECT_EQ(native_row->model.view(), source_row->model);

        std::vector<uint32_t> matches;
        nw::toolset::filter_appearance_catalog(catalog, source_row->label, matches);
        EXPECT_TRUE(std::any_of(matches.begin(), matches.end(), [&](uint32_t index) {
            return catalog.rows[index].id == source_row->id;
        }));
    };

    check_catalog(nw::toolset::AppearanceCatalogKind::wing, "wingmodel");
    check_catalog(nw::toolset::AppearanceCatalogKind::tail, "tailmodel");
}

} // namespace
