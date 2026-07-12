#include <gtest/gtest.h>

#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Player.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/effects.hpp>
#include <nw/rules/feats.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/propset_json.hpp>
#include <nw/smalls/runtime.hpp>

#include <nlohmann/json.hpp>

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
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "testsorcpc1");
    EXPECT_TRUE(pl);
    EXPECT_EQ(pl->history.entries.size(), 15);
    EXPECT_EQ(pl->history.entries[0].known_spells.size(), 6);

    auto& rt = nwk::runtime();
    auto tid = rt.type_id("core.creature.CreatureLevels", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    auto ref = rt.get_or_create_propset_ref(tid, pl->handle());
    ASSERT_EQ(ref.type_id, tid);
    auto def = rt.get_struct_def(tid);
    ASSERT_NE(def, nullptr);

    nw::smalls::PropsetJsonSerializer serializer{&rt};
    auto levels = serializer.serialize(ref, def);
    ASSERT_TRUE(levels.contains("levelup_classes"));
    ASSERT_EQ(levels["levelup_classes"].size(), pl->history.entries.size());
    ASSERT_TRUE(levels.contains("classes"));

    int32_t first_slot = -1;
    for (size_t i = 0; i < levels["classes"].size(); ++i) {
        if (levels["classes"][i].get<int32_t>() == *pl->history.entries[0].class_) {
            first_slot = static_cast<int32_t>(i);
            break;
        }
    }
    ASSERT_NE(first_slot, -1);
    EXPECT_EQ(levels["levelup_classes"][0].get<int32_t>(), first_slot);

    auto* script = rt.load_module_from_source("test.player_levelup_classes", R"(
        import core.creature as C;

        fn main(obj: Creature): int {
            return C.get_levelup_class_position(obj, 0);
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
    EXPECT_EQ(result.value.data.ival, static_cast<int32_t>(first_slot));
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
    std::ofstream out{"tmp/testwizardpc1.bic.gffjson"};
    out << std::setw(4) << j;
}

TEST(Player, Colors)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);
    auto& rt = nwk::runtime();
    auto appearance = find_player_propset(rt, pl, "core.creature.CreatureAppearance");
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
    std::ofstream out{"tmp/damienoneknife.bic.gffjson"};
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
    auto appearance = find_player_propset(rt, pl, "core.creature.CreatureAppearance");
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
    EXPECT_TRUE(pl->save("tmp/daeris1.bic", "gff"));
}

TEST(Player, JsonSerialization)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);
    EXPECT_TRUE(pl->save("tmp/daeris1.bic", "gff"));
    EXPECT_TRUE(pl->save("tmp/daeris1.bic.json", "json"));
}
