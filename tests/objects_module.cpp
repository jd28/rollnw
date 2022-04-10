#include <catch2/catch.hpp>

#include <nw/objects/Module.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("module: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/scratch/module.ifo"};
    REQUIRE(g.valid());
    nw::Module m{g.toplevel()};
    REQUIRE(m.haks.size() == 0);
    REQUIRE(!m.is_save_game);
    REQUIRE(m.name.get(nw::LanguageID::english) == "test_module");
    REQUIRE(m.scripts.on_load == "x2_mod_def_load");
}

TEST_CASE("module: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/scratch/module.ifo"};
    REQUIRE(g.valid());
    nw::Module m{g.toplevel()};

    nlohmann::json j = m.to_json();

    std::ofstream f{"tmp/module.ifo.json"};
    f << std::setw(4) << j;
}

TEST_CASE("module: json to and from", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/scratch/module.ifo"};
    REQUIRE(g.valid());
    nw::Module m{g.toplevel()};
    nlohmann::json j = m.to_json();
    nw::Module m2{j};
    nlohmann::json j2 = m2.to_json();
    REQUIRE(j == j2);
}

TEST_CASE("module: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/user/scratch/module.ifo");
    REQUIRE(g.valid());

    nw::Module mod{g.toplevel()};
    nw::GffOutputArchive oa = mod.to_gff();
    oa.write_to("tmp/module_2.ifo");

    nw::GffInputArchive g2("tmp/module_2.ifo");
    REQUIRE(g2.valid());

    std::ofstream f1{"tmp/module.ifo.gffjson", std::ios_base::binary};
    f1 << std::setw(4) << nw::gff_to_gffjson(g);
    std::ofstream f2{"tmp/module_2.ifo.gffjson", std::ios_base::binary};
    f2 << std::setw(4) << nw::gff_to_gffjson(g2);

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
}
