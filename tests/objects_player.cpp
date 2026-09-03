#include <gtest/gtest.h>

#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Player.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/effects.hpp>
#include <nw/rules/feats.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>
#include <nw/smalls/GarbageCollector.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/propset_json.hpp>
#include <nw/smalls/runtime.hpp>

#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

namespace {

nw::smalls::Value script_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    const nw::smalls::StructDef* def = rt.get_struct_def(value.type_id);
    if (!def) { return {}; }

    const uint32_t index = def->field_index(field);
    if (index == std::numeric_limits<uint32_t>::max()) { return {}; }

    const auto& fd = def->fields[index];
    return rt.read_value_field_at_offset(value, fd.offset, fd.type_id);
}

int32_t script_int_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.int_type() ? field_value.data.ival : 0;
}

nw::Resref script_resref_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    nw::smalls::TypeID resref_type = rt.type_id("core.types.ResRef", false);
    if (field_value.type_id != resref_type) { return {}; }

    const auto* resref = static_cast<const nw::Resref*>(rt.get_value_data_ptr(field_value));
    return resref ? *resref : nw::Resref{};
}

nw::smalls::Value find_player_propset(nw::smalls::Runtime& rt, const nw::Player* player, const char* qname)
{
    if (!player) { return {}; }

    const auto tid = rt.type_id(qname, false);
    if (tid == nw::smalls::invalid_type_id) { return {}; }
    return rt.find_propset_ref(tid, player->handle());
}

int32_t player_base_attack_bonus_from_script(nw::Player* player)
{
    if (!player) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(player->handle()));
    return nwn1::bridge::call_nwn1_module_int("nwn1.combat", "compute_base_attack_bonus", args).value_or(0);
}

} // namespace

TEST(Player, LevelHistory)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto& rt = nwk::runtime();
    const auto nodes_before = rt.stats().at("object_component_array_nodes").get<size_t>();
    auto pl = nwk::objects().load_player("CDKEY", "testsorcpc1");
    ASSERT_TRUE(pl);

    auto tid = rt.type_id("nwn1.propsets.PlayerHistory", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto ref = rt.find_propset_ref(tid, pl->handle());
    ASSERT_EQ(ref.type_id, tid);
    auto def = rt.get_struct_def(tid);
    ASSERT_NE(def, nullptr);

    nw::smalls::PropsetJsonSerializer serializer{&rt};
    auto history = serializer.serialize(ref, def);
    ASSERT_TRUE(history.contains("entries"));
    ASSERT_EQ(history["entries"].size(), 15);
    const auto& first = history["entries"][0];
    EXPECT_EQ(first["ability"].size(), 2);
    EXPECT_EQ(first["known_spells"].size(), 6);
    EXPECT_LE(first["skills"].size(), first["skill_count"].get<size_t>());

    auto* script = rt.load_module_from_source("test.player_history", R"(
        import nwn1.player as P;

        fn main(obj: Player): int {
            return P.history_level_count(obj) * 100
                + P.history_known_spell_count(obj, 0);
        }
    )");
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto player_value = nw::smalls::Value::make_object(pl->handle());
    player_value.type_id = rt.object_subtype_for_tag(pl->handle().type);
    args.push_back(player_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1506);

    nw::smalls::Value entries_value = script_field(rt, ref, "entries");
    nw::smalls::IArray* entries = rt.resolve_array(entries_value);
    ASSERT_NE(entries, nullptr);
    const auto* entry_definition = rt.get_struct_def(entries->element_type());
    ASSERT_NE(entry_definition, nullptr);
    const uint32_t ability_field = entry_definition->field_index("ability");
    ASSERT_NE(ability_field, std::numeric_limits<uint32_t>::max());
    const auto* ability_type = rt.get_type(entry_definition->fields[ability_field].type_id);
    ASSERT_NE(ability_type, nullptr);
    ASSERT_TRUE(ability_type->type_params[0].is<nw::smalls::TupleID>());
    const auto* ability_definition = rt.type_table_.get(
        ability_type->type_params[0].as<nw::smalls::TupleID>());
    ASSERT_NE(ability_definition, nullptr);
    const auto* first_data = static_cast<const uint8_t*>(entries->element_data(0));
    ASSERT_NE(first_data, nullptr);
    int32_t stored_ability = 0;
    int32_t stored_ability_value = 0;
    std::memcpy(&stored_ability,
        first_data + entry_definition->fields[ability_field].offset
            + ability_definition->offsets[0],
        sizeof(stored_ability));
    std::memcpy(&stored_ability_value,
        first_data + entry_definition->fields[ability_field].offset
            + ability_definition->offsets[1],
        sizeof(stored_ability_value));
    EXPECT_EQ(stored_ability, first["ability"][0].get<int32_t>());
    EXPECT_EQ(stored_ability_value, first["ability"][1].get<int32_t>());
    nw::smalls::Value first_entry;
    ASSERT_TRUE(entries->get_value(0, first_entry, rt));
    nw::smalls::Value known_spells = script_field(rt, first_entry, "known_spells");
    ASSERT_NE(rt.resolve_array(known_spells), nullptr);

    rt.gc()->collect_minor();
    rt.gc()->collect_major();
    EXPECT_EQ(serializer.serialize(ref, def), history);

    nw::GffBuilder output = nw::serialize(pl);
    output.build();
    nw::ResourceData resource;
    resource.bytes = output.to_byte_array();
    nw::Gff roundtrip{std::move(resource)};
    ASSERT_TRUE(roundtrip.valid());
    auto first_level = roundtrip.toplevel()["LvlStatList"][0];
    size_t known_spell_count = 0;
    for (size_t level = 0; level < 10; ++level) {
        known_spell_count += first_level[fmt::format("KnownList{}", level)].size();
    }
    EXPECT_EQ(known_spell_count, 6);

    auto* restored = nwk::objects().make<nw::Player>();
    ASSERT_NE(restored, nullptr);
    ASSERT_TRUE(deserialize(restored, roundtrip.toplevel()));
    auto restored_ref = rt.find_propset_ref(tid, restored->handle());
    ASSERT_EQ(restored_ref.type_id, tid);
    EXPECT_EQ(serializer.serialize(restored_ref, def), history);
    nwk::objects().destroy(restored->handle());

    const auto nodes_loaded = rt.stats().at("object_component_array_nodes").get<size_t>();
    EXPECT_GT(nodes_loaded, nodes_before);
    const nw::ObjectHandle player_handle = pl->handle();
    nwk::objects().destroy(player_handle);
    EXPECT_EQ(rt.resolve_array(known_spells), nullptr);
    EXPECT_EQ(rt.stats().at("object_component_array_nodes").get<size_t>(), nodes_before);
}

TEST(Player, LevelHistoryJsonRoundTrip)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* player = nwk::objects().load_player("CDKEY", "testsorcpc1");
    ASSERT_NE(player, nullptr);

    nlohmann::json json;
    ASSERT_TRUE(nw::serialize(player, json));
    ASSERT_TRUE(json.contains("nwn1.propsets.PlayerHistory"));
    ASSERT_EQ(json["nwn1.propsets.PlayerHistory"]["entries"].size(), 15);

    auto* restored = nwk::objects().make<nw::Player>();
    ASSERT_NE(restored, nullptr);
    ASSERT_TRUE(nw::deserialize(restored, json));

    nlohmann::json roundtrip;
    ASSERT_TRUE(nw::serialize(restored, roundtrip));
    EXPECT_EQ(roundtrip, json);
}

TEST(Player, LevelHistoryRejectsInvalidGffValues)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* player = nwk::objects().load_player("CDKEY", "testsorcpc1");
    ASSERT_NE(player, nullptr);

    auto& rt = nwk::runtime();
    const auto type = rt.type_id("nwn1.propsets.PlayerHistory", false);
    ASSERT_NE(type, nw::smalls::invalid_type_id);
    const auto ref = rt.find_propset_ref(type, player->handle());
    ASSERT_EQ(ref.type_id, type);
    const auto* definition = rt.get_struct_def(type);
    ASSERT_NE(definition, nullptr);

    nw::smalls::PropsetJsonSerializer serializer{&rt};
    const nlohmann::json valid = serializer.serialize(ref, definition);
    ASSERT_FALSE(valid["entries"].empty());

    const auto expect_rejected = [&](const char* label, const nlohmann::json& invalid) {
        SCOPED_TRACE(label);
        ASSERT_TRUE(serializer.deserialize(invalid, ref, definition));
        nw::GffBuilder output{"BIC"};
        EXPECT_FALSE(nw::serialize(player, output.top));
        ASSERT_TRUE(serializer.deserialize(valid, ref, definition));
        EXPECT_EQ(serializer.serialize(ref, definition), valid);
    };

    auto invalid = valid;
    invalid["entries"][0]["ability"] = nlohmann::json::array({-1, 1});
    expect_rejected("invalid absent ability tuple", invalid);

    invalid = valid;
    invalid["entries"][0]["skill_count"] = 2;
    invalid["entries"][0]["skills"] = nlohmann::json::array({
        nlohmann::json::array({0, 1}),
        nlohmann::json::array({0, 2}),
    });
    expect_rejected("duplicate skill IDs", invalid);

    invalid = valid;
    invalid["entries"][0]["skill_count"] = 1;
    invalid["entries"][0]["skills"] = nlohmann::json::array({
        nlohmann::json::array({1, 1}),
    });
    expect_rejected("skill ID outside dense width", invalid);

    invalid = valid;
    invalid["entries"][0]["skill_count"] = 1;
    invalid["entries"][0]["skills"] = nlohmann::json::array({
        nlohmann::json::array({0, 256}),
    });
    expect_rejected("skill rank outside GFF byte range", invalid);

    invalid = valid;
    invalid["entries"][0]["known_spells"] = nlohmann::json::array({
        nlohmann::json::array({10, 0}),
    });
    expect_rejected("spell level outside KnownList range", invalid);
}

TEST(Player, AttackBonus)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "testbardrddpc1");
    EXPECT_TRUE(pl);
    EXPECT_EQ(player_base_attack_bonus_from_script(pl), 10);
}

TEST(Player, GffJsonSerialize)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ba = nwk::resman().demand_server_vault("CDKEY", "testwizardpc1");
    nw::Gff g{std::move(ba)};
    EXPECT_TRUE(g.valid());

    auto j = nw::gff_to_gffjson(g);
    std::ofstream out{"tmp/testwizardpc1.bic.gffjson", std::ios::binary};
    out << std::setw(4) << j;
}

TEST(Player, Colors)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);
    auto& rt = nwk::runtime();
    auto appearance = find_player_propset(rt, pl, "nwn1.propsets.CreatureAppearance");
    ASSERT_NE(appearance.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_int_field(rt, appearance, "color_hair"), 20);
    EXPECT_EQ(script_int_field(rt, appearance, "color_skin"), 2);
}

TEST(Player, Inventory)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);

    EXPECT_EQ(pl->inventory().size(), 4);
    auto slot1 = pl->inventory().find_slot(1, 1);
    auto [x1, y1] = pl->inventory().slot_to_xy(slot1);
    EXPECT_EQ(x1, 1);
    EXPECT_EQ(y1, 0);

    auto slot2 = pl->inventory().find_slot(3, 4);
    auto [x2, y2] = pl->inventory().slot_to_xy(slot2);
    EXPECT_EQ(x2, 5);
    EXPECT_EQ(y2, 0);

    auto it = nw::inventory_item_ptr(pl->inventory().items[0]);
    ASSERT_NE(it, nullptr);
    EXPECT_TRUE(pl->inventory().remove_item(it));
    EXPECT_FALSE(pl->inventory().has_item(it));
    EXPECT_EQ(pl->inventory().size(), 3);
    EXPECT_TRUE(pl->inventory().can_add_item(it));
    EXPECT_TRUE(pl->inventory().add_item(it));
    EXPECT_TRUE(pl->inventory().has_item(it));
    EXPECT_EQ(pl->inventory().size(), 4);
}

TEST(Player, PerPartColor)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ba = nwk::resman().demand_server_vault("CDKEY", "damienoneknife");
    nw::Gff g{std::move(ba)};
    EXPECT_TRUE(g.valid());

    auto j = nw::gff_to_gffjson(g);
    std::ofstream out{"tmp/damienoneknife.bic.gffjson", std::ios::binary};
    out << std::setw(4) << j;

    auto pl = nwk::objects().load_player("CDKEY", "damienoneknife");
    EXPECT_TRUE(pl);
}

TEST(Player, Portrait)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);

    auto& rt = nwk::runtime();
    auto appearance = find_player_propset(rt, pl, "nwn1.propsets.CreatureAppearance");
    ASSERT_NE(appearance.type_id, nw::smalls::invalid_type_id);
    const auto portrait = script_resref_field(rt, appearance, "portrait");
    EXPECT_EQ(portrait.view(), "po_dw_m_02_");
}

TEST(Player, GffSerialization)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);
    EXPECT_TRUE(pl->save("tmp/daeris1_gff.bic", "gff"));
}

TEST(Player, JsonSerialization)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);
    EXPECT_TRUE(pl->save("tmp/daeris1_json_source.bic", "gff"));
    EXPECT_TRUE(pl->save("tmp/daeris1.bic.json", "json"));
}
