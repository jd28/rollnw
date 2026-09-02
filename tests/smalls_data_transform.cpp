#include <gtest/gtest.h>

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/smalls/data_spec.hpp>
#include <nw/smalls/data_transform.hpp>

#include <algorithm>
#include <array>
#include <filesystem>

namespace {

constexpr std::string_view test_spec = R"json({
  "source": {
    "kind": "twoda",
    "resource": "test_rows",
    "valid_when": { "column": "LABEL", "on_missing": "omit_row" }
  },
  "output": {
    "config_path": "test.data.rows",
    "entry_type": "test.rules.Definition",
    "snapshot_filename": { "column": "LABEL" },
    "fields": [
      { "target": "id", "value": { "kind": "row_index" } }
    ],
    "field_groups": [
      {
        "target": "info",
        "owner": "native",
        "type": "test.native.Info",
        "fields": [
          { "target": "label", "value": { "kind": "column", "column": "LABEL", "type": "string" } },
          { "target": "kind", "value": { "kind": "enum", "column": "KIND", "values": { "F": 2, "P": 0 } } },
          { "target": "active", "value": { "kind": "column", "column": "ACTIVE", "type": "bool" } },
          { "target": "scale", "value": { "kind": "column", "column": "SCALE", "type": "float", "on_missing": "default", "default": 1.0 } }
        ]
      },
      {
        "target": "rules",
        "owner": "smalls",
        "type": "test.rules.Rules",
        "fields": [
          { "target": "enabled", "value": { "kind": "column", "column": "ACTIVE", "type": "bool" } }
        ]
      }
    ]
  }
})json";

const nw::smalls::MaterializedDataValue* find_value(
    const nw::smalls::MaterializedDataBatch& batch,
    const nw::smalls::MaterializedDataRow& row,
    nw::StringView target)
{
    const auto values = batch.row_values(row);
    const auto found = std::ranges::find(
        values, target, &nw::smalls::MaterializedDataValue::target);
    return found == values.end() ? nullptr : &*found;
}

} // namespace

TEST(SmallsDataTransform, MaterializesSparseMixedOwnerBatch)
{
    nw::smalls::DataSpec spec;
    nw::Vector<nw::smalls::DataDiagnostic> diagnostics;
    ASSERT_TRUE(nw::smalls::parse_data_spec(
        test_spec, "test/data_specs/rows.json", spec, diagnostics));
    EXPECT_EQ(spec.snapshot_filename_column, "LABEL");

    constexpr std::string_view source = R"2da(2DA V2.0

LABEL KIND ACTIVE SCALE
0 alpha F 1 1.25
1 **** **** **** ****
2 beta P 0 ****
)2da";
    nw::StaticTwoDA table{source};
    ASSERT_TRUE(table.is_valid());

    nw::smalls::MaterializedDataBatch batch;
    ASSERT_TRUE(nw::smalls::materialize_data_rows(
        spec, table, batch, diagnostics));
    ASSERT_EQ(batch.rows.size(), 2);
    EXPECT_EQ(batch.indexed_size, 3);
    EXPECT_EQ(batch.rows[0].id, 0);
    EXPECT_EQ(batch.rows[1].id, 2);

    const auto* label = find_value(batch, batch.rows[1], "info.label");
    const auto* kind = find_value(batch, batch.rows[1], "info.kind");
    const auto* scale = find_value(batch, batch.rows[1], "info.scale");
    const auto* enabled = find_value(batch, batch.rows[1], "rules.enabled");
    ASSERT_NE(label, nullptr);
    ASSERT_NE(kind, nullptr);
    ASSERT_NE(scale, nullptr);
    ASSERT_NE(enabled, nullptr);
    EXPECT_EQ(label->owner, nw::smalls::DataOwner::native);
    EXPECT_EQ(std::get<nw::String>(label->value), "beta");
    EXPECT_EQ(std::get<int32_t>(kind->value), 0);
    EXPECT_FLOAT_EQ(std::get<float>(scale->value), 1.0f);
    EXPECT_EQ(enabled->owner, nw::smalls::DataOwner::smalls);
    EXPECT_FALSE(std::get<bool>(enabled->value));
}

TEST(SmallsDataTransform, DropsInvalidEnumRowAndRetainsValidRows)
{
    nw::smalls::DataSpec spec;
    nw::Vector<nw::smalls::DataDiagnostic> diagnostics;
    ASSERT_TRUE(nw::smalls::parse_data_spec(
        test_spec, "test/data_specs/rows.json", spec, diagnostics));

    nw::StaticTwoDA source{std::string_view{R"2da(2DA V2.0

LABEL KIND ACTIVE SCALE
0 alpha F 1 1.0
1 beta P 0 1.0
2 malformed UNKNOWN 1 1.0
)2da"}};
    nw::smalls::MaterializedDataBatch batch;
    diagnostics.clear();
    EXPECT_TRUE(nw::smalls::materialize_data_rows(
        spec, source, batch, diagnostics));
    ASSERT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(diagnostics.front().row, 2);
    EXPECT_EQ(diagnostics.front().target, "info.kind");
    EXPECT_EQ(diagnostics.front().severity,
        nw::smalls::DiagnosticSeverity::error);
    ASSERT_EQ(batch.rows.size(), 2);
    EXPECT_EQ(batch.indexed_size, 3);
    EXPECT_EQ(batch.rows[0].id, 0);
    EXPECT_EQ(batch.rows[1].id, 1);
}

TEST(SmallsDataTransform, CoercesOutOfRangeBoolToTrueWithWarning)
{
    nw::smalls::DataSpec spec;
    nw::Vector<nw::smalls::DataDiagnostic> diagnostics;
    ASSERT_TRUE(nw::smalls::parse_data_spec(
        test_spec, "test/data_specs/rows.json", spec, diagnostics));

    nw::StaticTwoDA valid{std::string_view{R"2da(2DA V2.0

LABEL KIND ACTIVE SCALE
0 alpha F 1 1.0
)2da"}};
    nw::smalls::MaterializedDataBatch batch;
    ASSERT_TRUE(nw::smalls::materialize_data_rows(
        spec, valid, batch, diagnostics));

    nw::StaticTwoDA malformed{std::string_view{R"2da(2DA V2.0

LABEL KIND ACTIVE SCALE
0 alpha F 2 1.0
)2da"}};
    diagnostics.clear();
    EXPECT_TRUE(nw::smalls::materialize_data_rows(
        spec, malformed, batch, diagnostics));
    ASSERT_EQ(diagnostics.size(), 2);
    EXPECT_EQ(diagnostics.front().row, 0);
    EXPECT_EQ(diagnostics.front().target, "info.active");
    EXPECT_EQ(diagnostics.front().severity,
        nw::smalls::DiagnosticSeverity::warning);
    ASSERT_EQ(batch.rows.size(), 1);
    EXPECT_EQ(batch.rows.front().id, 0);
    const auto* active = find_value(batch, batch.rows.front(), "info.active");
    const auto* enabled = find_value(batch, batch.rows.front(), "rules.enabled");
    ASSERT_NE(active, nullptr);
    ASSERT_NE(enabled, nullptr);
    EXPECT_TRUE(std::get<bool>(active->value));
    EXPECT_TRUE(std::get<bool>(enabled->value));
}

TEST(SmallsDataTransform, RejectsMissingRequiredColumnWithoutReplacingOutput)
{
    nw::smalls::DataSpec spec;
    nw::Vector<nw::smalls::DataDiagnostic> diagnostics;
    ASSERT_TRUE(nw::smalls::parse_data_spec(
        test_spec, "test/data_specs/rows.json", spec, diagnostics));

    nw::StaticTwoDA valid{std::string_view{R"2da(2DA V2.0

LABEL KIND ACTIVE SCALE
0 alpha F 1 1.0
)2da"}};
    nw::smalls::MaterializedDataBatch batch;
    ASSERT_TRUE(nw::smalls::materialize_data_rows(
        spec, valid, batch, diagnostics));

    nw::StaticTwoDA invalid{std::string_view{R"2da(2DA V2.0

LABEL KIND SCALE
0 alpha F 1.0
)2da"}};
    diagnostics.clear();
    EXPECT_FALSE(nw::smalls::materialize_data_rows(
        spec, invalid, batch, diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.front().row, -1);
    EXPECT_EQ(diagnostics.front().target, "active");
    ASSERT_EQ(batch.rows.size(), 1);
    EXPECT_EQ(batch.rows.front().id, 0);
}

TEST(SmallsDataTransform, SnapshotFilenameIsOptionalSinkMetadata)
{
    constexpr std::string_view source = R"json({
  "source": { "kind": "twoda", "resource": "test_rows" },
  "output": {
    "config_path": "test.data.rows",
    "entry_type": "test.rules.Definition",
    "fields": [
      { "target": "id", "value": { "kind": "row_index" } }
    ]
  }
})json";
    nw::smalls::DataSpec spec;
    nw::Vector<nw::smalls::DataDiagnostic> diagnostics;
    EXPECT_TRUE(nw::smalls::parse_data_spec(
        source, "test/data_specs/rows.json", spec, diagnostics));
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_TRUE(spec.snapshot_filename_column.empty());
}

TEST(SmallsDataTransform, AppearanceSpecAcceptsAllModelTypes)
{
    struct ExpectedModelType {
        const char* source;
        int32_t type;
        int32_t flags;
    };
    constexpr std::array model_types{
        ExpectedModelType{"P", 0, 3},
        ExpectedModelType{"S", 1, 0},
        ExpectedModelType{"F", 2, 0},
        ExpectedModelType{"L", 3, 0},
        ExpectedModelType{"SW", 1, 1},
        ExpectedModelType{"ST", 1, 2},
        ExpectedModelType{"SWT", 1, 3},
        ExpectedModelType{"FW", 2, 1},
        ExpectedModelType{"FT", 2, 2},
        ExpectedModelType{"FWT", 2, 3},
        ExpectedModelType{"LW", 3, 1},
        ExpectedModelType{"LT", 3, 2},
        ExpectedModelType{"LWT", 3, 3},
    };

    const auto spec_path = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR}
        / "lib/nw/smalls/scripts/nwn1/data_specs/appearance.json";
    const std::array spec_paths{spec_path};
    nw::Vector<nw::smalls::DataSpec> specs;
    nw::Vector<nw::smalls::DataDiagnostic> diagnostics;
    ASSERT_TRUE(nw::smalls::parse_data_specs(
        spec_paths, specs, diagnostics));
    ASSERT_EQ(specs.size(), 1);

    std::string source = "2DA V2.0\n\nLABEL MODELTYPE MOVERATE\n";
    for (size_t index = 0; index < model_types.size(); ++index) {
        source += std::to_string(index) + " row_" + std::to_string(index)
            + " " + model_types[index].source + " NORM\n";
    }
    nw::StaticTwoDA table{std::string_view{source}};
    ASSERT_TRUE(table.is_valid());

    nw::smalls::MaterializedDataBatch batch;
    ASSERT_TRUE(nw::smalls::materialize_data_rows(
        specs.front(), table, batch, diagnostics));
    ASSERT_EQ(batch.rows.size(), model_types.size());
    for (size_t index = 0; index < model_types.size(); ++index) {
        const auto* model_type = find_value(
            batch, batch.rows[index], "info.model_type");
        ASSERT_NE(model_type, nullptr);
        EXPECT_EQ(std::get<int32_t>(model_type->value),
            model_types[index].type);
        const auto* model_flags = find_value(
            batch, batch.rows[index], "info.model_flags");
        ASSERT_NE(model_flags, nullptr);
        EXPECT_EQ(std::get<int32_t>(model_flags->value),
            model_types[index].flags);
    }
}

TEST(SmallsDataTransform, AppearanceSpecAcceptsCreatureSpeedNames)
{
    struct ExpectedMovementRate {
        const char* source;
        int32_t row;
    };
    constexpr std::array movement_rates{
        ExpectedMovementRate{"PLAYER", 0},
        ExpectedMovementRate{"PC_Movement", 0},
        ExpectedMovementRate{"NOMOVE", 1},
        ExpectedMovementRate{"Immobile", 1},
        ExpectedMovementRate{"VSLOW", 2},
        ExpectedMovementRate{"Very_Slow", 2},
        ExpectedMovementRate{"SLOW", 3},
        ExpectedMovementRate{"NORM", 4},
        ExpectedMovementRate{"Normal", 4},
        ExpectedMovementRate{"FAST", 5},
        ExpectedMovementRate{"VFAST", 6},
        ExpectedMovementRate{"Very_Fast", 6},
        ExpectedMovementRate{"DEFAULT", 7},
        ExpectedMovementRate{"DFAST", 8},
        ExpectedMovementRate{"DM_Fast", 8},
    };

    const auto spec_path = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR}
        / "lib/nw/smalls/scripts/nwn1/data_specs/appearance.json";
    const std::array spec_paths{spec_path};
    nw::Vector<nw::smalls::DataSpec> specs;
    nw::Vector<nw::smalls::DataDiagnostic> diagnostics;
    ASSERT_TRUE(nw::smalls::parse_data_specs(
        spec_paths, specs, diagnostics));
    ASSERT_EQ(specs.size(), 1);

    std::string source = "2DA V2.0\n\nLABEL MODELTYPE MOVERATE\n";
    for (size_t index = 0; index < movement_rates.size(); ++index) {
        source += std::to_string(index) + " row_" + std::to_string(index)
            + " S " + movement_rates[index].source + "\n";
    }
    nw::StaticTwoDA table{std::string_view{source}};
    ASSERT_TRUE(table.is_valid());

    nw::smalls::MaterializedDataBatch batch;
    ASSERT_TRUE(nw::smalls::materialize_data_rows(
        specs.front(), table, batch, diagnostics));
    ASSERT_EQ(batch.rows.size(), movement_rates.size());
    for (size_t index = 0; index < movement_rates.size(); ++index) {
        const auto* movement_rate = find_value(
            batch, batch.rows[index], "rules.movement_rate");
        ASSERT_NE(movement_rate, nullptr);
        EXPECT_EQ(std::get<int32_t>(movement_rate->value),
            movement_rates[index].row);
    }
}

TEST(SmallsDataTransform, AppearanceSpecWarnsAndCoercesMalformedHasArms)
{
    const auto spec_path = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR}
        / "lib/nw/smalls/scripts/nwn1/data_specs/appearance.json";
    const std::array spec_paths{spec_path};
    nw::Vector<nw::smalls::DataSpec> specs;
    nw::Vector<nw::smalls::DataDiagnostic> diagnostics;
    ASSERT_TRUE(nw::smalls::parse_data_specs(
        spec_paths, specs, diagnostics));
    ASSERT_EQ(specs.size(), 1);

    nw::StaticTwoDA table{std::string_view{R"2da(2DA V2.0

LABEL MODELTYPE MOVERATE HASARMS
0 no_arms  S NORM 0
1 arms     S NORM 1
2 arms_bit S NORM 8
)2da"}};
    ASSERT_TRUE(table.is_valid());

    nw::smalls::MaterializedDataBatch batch;
    ASSERT_TRUE(nw::smalls::materialize_data_rows(
        specs.front(), table, batch, diagnostics));
    ASSERT_EQ(batch.rows.size(), 3);
    ASSERT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(diagnostics.front().row, 2);
    EXPECT_EQ(diagnostics.front().target, "info.has_arms");
    EXPECT_EQ(diagnostics.front().severity,
        nw::smalls::DiagnosticSeverity::warning);
    EXPECT_NE(diagnostics.front().message.find("coerced to true"),
        nw::String::npos);
    constexpr std::array expected{false, true, true};
    for (size_t index = 0; index < expected.size(); ++index) {
        const auto* has_arms = find_value(
            batch, batch.rows[index], "info.has_arms");
        ASSERT_NE(has_arms, nullptr);
        EXPECT_EQ(std::get<bool>(has_arms->value), expected[index]);
    }
}
