#include <gtest/gtest.h>

#include <nw/kernel/Objects.hpp>
#include <nw/objects/Module.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Module, GffDeserialize)
{
    auto mod = new nw::Module;
    nw::Gff g{"test_data/user/scratch/module.ifo"};
    EXPECT_TRUE(g.valid());
    nw::deserialize(mod, g.toplevel());

    EXPECT_EQ(mod->haks.size(), 0);
    EXPECT_TRUE(!mod->is_save_game);
    EXPECT_EQ(mod->name.get(nw::LanguageID::english), "test_module");
    EXPECT_EQ(mod->scripts.on_load, "x2_mod_def_load");
}

TEST(Module, JsonRoundTrip)
{
    auto ent = new nw::Module;
    nw::Gff g{"test_data/user/scratch/module.ifo"};
    EXPECT_TRUE(g.valid());
    nw::deserialize(ent, g.toplevel());
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::Module::serialize(ent, j);

    auto ent2 = new nw::Module;
    nw::Module::deserialize(ent2, j);
    EXPECT_TRUE(ent2);

    nlohmann::json j2;
    nw::Module::serialize(ent2, j2);

    EXPECT_EQ(j, j2);

    std::ofstream f{"tmp/module.ifo.json"};
    f << std::setw(4) << j;
}

TEST(Module, GffRoundTrip)
{
    auto ent = new nw::Module;
    EXPECT_TRUE(ent);
    nw::Gff g{"test_data/user/scratch/module.ifo"};
    EXPECT_TRUE(g.valid());
    nw::deserialize(ent, g.toplevel());

    nw::GffBuilder oa = nw::serialize(ent);
    oa.write_to("tmp/module_2.ifo");

    nw::Gff g2("tmp/module_2.ifo");
    EXPECT_TRUE(g2.valid());
    EXPECT_EQ(nw::gff_to_gffjson(g), nw::gff_to_gffjson(g2));

    EXPECT_EQ(oa.header.struct_offset, g.head_->struct_offset);
    EXPECT_EQ(oa.header.struct_count, g.head_->struct_count);
    EXPECT_EQ(oa.header.field_offset, g.head_->field_offset);
    EXPECT_EQ(oa.header.field_count, g.head_->field_count);
    EXPECT_EQ(oa.header.label_offset, g.head_->label_offset);
    EXPECT_EQ(oa.header.label_count, g.head_->label_count);
    EXPECT_EQ(oa.header.field_data_offset, g.head_->field_data_offset);
    EXPECT_EQ(oa.header.field_data_count, g.head_->field_data_count);
    EXPECT_EQ(oa.header.field_idx_offset, g.head_->field_idx_offset);
    EXPECT_EQ(oa.header.field_idx_count, g.head_->field_idx_count);
    EXPECT_EQ(oa.header.list_idx_offset, g.head_->list_idx_offset);
    EXPECT_EQ(oa.header.list_idx_count, g.head_->list_idx_count);

    EXPECT_EQ(g2.head_->struct_offset, g.head_->struct_offset);
    EXPECT_EQ(g2.head_->struct_count, g.head_->struct_count);
    EXPECT_EQ(g2.head_->field_offset, g.head_->field_offset);
    EXPECT_EQ(g2.head_->field_count, g.head_->field_count);
    EXPECT_EQ(g2.head_->label_offset, g.head_->label_offset);
    EXPECT_EQ(g2.head_->label_count, g.head_->label_count);
    EXPECT_EQ(g2.head_->field_data_offset, g.head_->field_data_offset);
    EXPECT_EQ(g2.head_->field_data_count, g.head_->field_data_count);
    EXPECT_EQ(g2.head_->field_idx_offset, g.head_->field_idx_offset);
    EXPECT_EQ(g2.head_->field_idx_count, g.head_->field_idx_count);
    EXPECT_EQ(g2.head_->list_idx_offset, g.head_->list_idx_offset);
    EXPECT_EQ(g2.head_->list_idx_count, g.head_->list_idx_count);
}
