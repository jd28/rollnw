#include <catch2/catch.hpp>

#include <nw/components/Module.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("module: from_gff", "[objects]")
{
    auto ent = nw::kernel::objects().make(nw::ObjectType::module);
    nw::GffInputArchive g{"test_data/user/scratch/module.ifo"};
    REQUIRE(g.valid());
    nw::Module::deserialize(ent, g.toplevel());

    auto mod = ent.get<nw::Module>();
    auto scripts = ent.get<nw::ModuleScripts>();

    REQUIRE(mod->haks.size() == 0);
    REQUIRE(!mod->is_save_game);
    REQUIRE(mod->name.get(nw::LanguageID::english) == "test_module");
    REQUIRE(scripts->on_load == "x2_mod_def_load");
}

TEST_CASE("module: json round trip", "[objects]")
{
    auto ent = nw::kernel::objects().make(nw::ObjectType::module);
    nw::GffInputArchive g{"test_data/user/scratch/module.ifo"};
    REQUIRE(g.valid());
    nw::Module::deserialize(ent, g.toplevel());
    REQUIRE(ent.is_alive());

    nlohmann::json j;
    nw::kernel::objects().serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make(nw::ObjectType::module);
    nw::Module::deserialize(ent2, j);
    REQUIRE(ent2.is_alive());

    nlohmann::json j2;
    nw::kernel::objects().serialize(ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);

    std::ofstream f{"tmp/module.ifo.json"};
    f << std::setw(4) << j;
}

TEST_CASE("module: gff round trip", "[ojbects]")
{
    auto ent = nw::kernel::objects().make(nw::ObjectType::module);
    nw::GffInputArchive g{"test_data/user/scratch/module.ifo"};
    REQUIRE(g.valid());
    nw::Module::deserialize(ent, g.toplevel());
    REQUIRE(ent.is_alive());

    nw::GffOutputArchive oa = nw::Module::serialize(ent);
    oa.write_to("tmp/module_2.ifo");

    nw::GffInputArchive g2("tmp/module_2.ifo");
    REQUIRE(g2.valid());
    REQUIRE(nw::gff_to_gffjson(g) == nw::gff_to_gffjson(g2));

    REQUIRE(oa.header.struct_offset == g.head_->struct_offset);
    REQUIRE(oa.header.struct_count == g.head_->struct_count);
    REQUIRE(oa.header.field_offset == g.head_->field_offset);
    REQUIRE(oa.header.field_count == g.head_->field_count);
    REQUIRE(oa.header.label_offset == g.head_->label_offset);
    REQUIRE(oa.header.label_count == g.head_->label_count);
    REQUIRE(oa.header.field_data_offset == g.head_->field_data_offset);
    REQUIRE(oa.header.field_data_count == g.head_->field_data_count);
    REQUIRE(oa.header.field_idx_offset == g.head_->field_idx_offset);
    REQUIRE(oa.header.field_idx_count == g.head_->field_idx_count);
    REQUIRE(oa.header.list_idx_offset == g.head_->list_idx_offset);
    REQUIRE(oa.header.list_idx_count == g.head_->list_idx_count);

    REQUIRE(g2.head_->struct_offset == g.head_->struct_offset);
    REQUIRE(g2.head_->struct_count == g.head_->struct_count);
    REQUIRE(g2.head_->field_offset == g.head_->field_offset);
    REQUIRE(g2.head_->field_count == g.head_->field_count);
    REQUIRE(g2.head_->label_offset == g.head_->label_offset);
    REQUIRE(g2.head_->label_count == g.head_->label_count);
    REQUIRE(g2.head_->field_data_offset == g.head_->field_data_offset);
    REQUIRE(g2.head_->field_data_count == g.head_->field_data_count);
    REQUIRE(g2.head_->field_idx_offset == g.head_->field_idx_offset);
    REQUIRE(g2.head_->field_idx_count == g.head_->field_idx_count);
    REQUIRE(g2.head_->list_idx_offset == g.head_->list_idx_offset);
    REQUIRE(g2.head_->list_idx_count == g.head_->list_idx_count);
}
