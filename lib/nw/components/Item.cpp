#include "Item.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool Item::deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    auto item = ent.get_mut<Item>();
    auto common = ent.get_mut<Common>();
    auto inventory = ent.get_mut<Inventory>();

    common->from_gff(archive, profile);
    inventory->from_gff(archive, profile);

    archive.get_to("Description", item->description);
    archive.get_to("DescIdentified", item->description_id);

    auto prop_size = archive["PropertiesList"].size();
    item->properties.reserve(prop_size);
    auto list = archive["PropertiesList"];
    for (size_t i = 0; i < prop_size; ++i) {
        ItemProperty ip;
        if (!list[i].get_to("PropertyName", ip.type)
            || !list[i].get_to("Subtype", ip.subtype)
            || !list[i].get_to("CostTable", ip.cost_table)
            || !list[i].get_to("CostValue", ip.cost_value)
            || !list[i].get_to("Param1", ip.param_table)
            || !list[i].get_to("Param1Value", ip.param_value)) {
            LOG_F(WARNING, "item invalid property at index {}", i);
        } else {
            item->properties.push_back(ip);
        }
    }

    archive.get_to("Cost", item->cost);
    archive.get_to("AddCost", item->additional_cost);
    archive.get_to("BaseItem", item->baseitem);

    archive.get_to("StackSize", item->stacksize);

    archive.get_to("Charges", item->charges);
    archive.get_to("Cursed", item->cursed);
    archive.get_to("Identified", item->identified);
    archive.get_to("Plot", item->plot);
    archive.get_to("Stolen", item->stolen);

    // Guess model type from what's in the archive.
    if (archive.has_field("ArmorPart_Belt")) {
        item->model_type = ItemModelType::armor;
        archive.get_to("ArmorPart_Belt", item->model_parts[ItemModelParts::armor_belt]);
        archive.get_to("ArmorPart_LBicep", item->model_parts[ItemModelParts::armor_lbicep]);
        archive.get_to("ArmorPart_LFArm", item->model_parts[ItemModelParts::armor_lfarm]);
        archive.get_to("ArmorPart_LFoot", item->model_parts[ItemModelParts::armor_lfoot]);
        archive.get_to("ArmorPart_LHand", item->model_parts[ItemModelParts::armor_lhand]);
        archive.get_to("ArmorPart_LShin", item->model_parts[ItemModelParts::armor_lshin]);
        archive.get_to("ArmorPart_LShoul", item->model_parts[ItemModelParts::armor_lshoul]);
        archive.get_to("ArmorPart_LThigh", item->model_parts[ItemModelParts::armor_lthigh]);
        archive.get_to("ArmorPart_Neck", item->model_parts[ItemModelParts::armor_neck]);
        archive.get_to("ArmorPart_Pelvis", item->model_parts[ItemModelParts::armor_pelvis]);
        archive.get_to("ArmorPart_RBicep", item->model_parts[ItemModelParts::armor_rbicep]);
        archive.get_to("ArmorPart_RFArm", item->model_parts[ItemModelParts::armor_rfarm]);
        archive.get_to("ArmorPart_RFoot", item->model_parts[ItemModelParts::armor_rfoot]);
        archive.get_to("ArmorPart_RHand", item->model_parts[ItemModelParts::armor_rhand]);
        archive.get_to("ArmorPart_Robe", item->model_parts[ItemModelParts::armor_robe]);
        archive.get_to("ArmorPart_RShin", item->model_parts[ItemModelParts::armor_rshin]);
        archive.get_to("ArmorPart_RShoul", item->model_parts[ItemModelParts::armor_rshoul]);
        archive.get_to("ArmorPart_RThigh", item->model_parts[ItemModelParts::armor_rthigh]);
        archive.get_to("ArmorPart_Torso", item->model_parts[ItemModelParts::armor_torso]);
    } else if (archive.has_field("ModelPart2")) {
        item->model_type = ItemModelType::composite;
        archive.get_to("ModelPart1", item->model_parts[ItemModelParts::model1]);
        archive.get_to("ModelPart2", item->model_parts[ItemModelParts::model2]);
        archive.get_to("ModelPart3", item->model_parts[ItemModelParts::model3]);
    } else {
        if (archive.has_field("Cloth1Color")) {
            item->model_type = ItemModelType::layered;
        } else {
            item->model_type = ItemModelType::simple;
        }
        archive.get_to("ModelPart1", item->model_parts[ItemModelParts::model1]);
    }

    if (item->model_type == ItemModelType::layered
        || item->model_type == ItemModelType::armor) {
        archive.get_to("Cloth1Color", item->model_colors[ItemColors::cloth1]);
        archive.get_to("Cloth2Color", item->model_colors[ItemColors::cloth2]);
        archive.get_to("Leather1Color", item->model_colors[ItemColors::leather1]);
        archive.get_to("Leather2Color", item->model_colors[ItemColors::leather2]);
        archive.get_to("Metal1Color", item->model_colors[ItemColors::metal1]);
        archive.get_to("Metal2Color", item->model_colors[ItemColors::metal2]);
    }

    return true;
}

bool Item::deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile)
{
    auto item = ent.get_mut<Item>();
    auto common = ent.get_mut<Common>();
    auto inventory = ent.get_mut<Inventory>();

    try {
        common->from_json(archive.at("common"), profile);
        inventory->from_json(archive.at("inventory"), profile);

        archive.at("description").get_to(item->description);
        archive.at("description_id").get_to(item->description_id);
        const auto& ref = archive.at("properties");
        item->properties.reserve(ref.size());
        for (size_t i = 0; i < ref.size(); ++i) {
            ItemProperty ip;
            ref[i].at("type").get_to(ip.type);
            ref[i].at("subtype").get_to(ip.subtype);
            ref[i].at("cost_table").get_to(ip.cost_table);
            ref[i].at("cost_value").get_to(ip.cost_value);
            ref[i].at("param_table").get_to(ip.param_table);
            ref[i].at("param_value").get_to(ip.param_value);
            item->properties.push_back(ip);
        }

        archive.at("cost").get_to(item->cost);
        archive.at("additional_cost").get_to(item->additional_cost);
        archive.at("baseitem").get_to(item->baseitem);

        archive.at("stacksize").get_to(item->stacksize);

        archive.at("charges").get_to(item->charges);
        archive.at("cursed").get_to(item->cursed);
        archive.at("identified").get_to(item->identified);
        archive.at("plot").get_to(item->plot);
        archive.at("stolen").get_to(item->stolen);
        archive.at("model_type").get_to(item->model_type);
        archive.at("model_colors").get_to(item->model_colors);
        archive.at("model_parts").get_to(item->model_parts);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Item::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool Item::serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile)
{
    auto item = ent.get<Item>();
    auto common = ent.get<Common>();
    auto inventory = ent.get<Inventory>();

    archive.add_field("TemplateResRef", common->resref)
        .add_field("LocalizedName", common->name)
        .add_field("Tag", common->tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common->comment);
        archive.add_field("PaletteID", common->palette_id);
    } else {
        archive.add_field("PositionX", common->location.position.x)
            .add_field("PositionY", common->location.position.y)
            .add_field("PositionZ", common->location.position.z)
            .add_field("OrientationX", common->location.orientation.x)
            .add_field("OrientationY", common->location.orientation.y);
    }

    common->locals.to_gff(archive);
    inventory->to_gff(archive, profile);

    archive.add_field("Description", item->description);
    archive.add_field("DescIdentified", item->description_id);

    archive.add_field("Cost", item->cost)
        .add_field("AddCost", item->additional_cost)
        .add_field("BaseItem", item->baseitem);

    archive.add_field("StackSize", item->stacksize);

    archive.add_field("Charges", item->charges)
        .add_field("Cursed", item->cursed)
        .add_field("Identified", item->identified)
        .add_field("Plot", item->plot)
        .add_field("Stolen", item->stolen);

    if (item->model_type == ItemModelType::armor) {
        archive.add_field("ArmorPart_Belt", item->model_parts[ItemModelParts::armor_belt])
            .add_field("ArmorPart_LBicep", item->model_parts[ItemModelParts::armor_lbicep])
            .add_field("ArmorPart_LFArm", item->model_parts[ItemModelParts::armor_lfarm])
            .add_field("ArmorPart_LFoot", item->model_parts[ItemModelParts::armor_lfoot])
            .add_field("ArmorPart_LHand", item->model_parts[ItemModelParts::armor_lhand])
            .add_field("ArmorPart_LShin", item->model_parts[ItemModelParts::armor_lshin])
            .add_field("ArmorPart_LShoul", item->model_parts[ItemModelParts::armor_lshoul])
            .add_field("ArmorPart_LThigh", item->model_parts[ItemModelParts::armor_lthigh])
            .add_field("ArmorPart_Neck", item->model_parts[ItemModelParts::armor_neck])
            .add_field("ArmorPart_Pelvis", item->model_parts[ItemModelParts::armor_pelvis])
            .add_field("ArmorPart_RBicep", item->model_parts[ItemModelParts::armor_rbicep])
            .add_field("ArmorPart_RFArm", item->model_parts[ItemModelParts::armor_rfarm])
            .add_field("ArmorPart_RFoot", item->model_parts[ItemModelParts::armor_rfoot])
            .add_field("ArmorPart_RHand", item->model_parts[ItemModelParts::armor_rhand])
            .add_field("ArmorPart_Robe", item->model_parts[ItemModelParts::armor_robe])
            .add_field("ArmorPart_RShin", item->model_parts[ItemModelParts::armor_rshin])
            .add_field("ArmorPart_RShoul", item->model_parts[ItemModelParts::armor_rshoul])
            .add_field("ArmorPart_RThigh", item->model_parts[ItemModelParts::armor_rthigh])
            .add_field("ArmorPart_Torso", item->model_parts[ItemModelParts::armor_torso]);
    } else if (item->model_type == ItemModelType::composite) {
        archive.add_field("ModelPart1", item->model_parts[ItemModelParts::model1]);
        archive.add_field("ModelPart2", item->model_parts[ItemModelParts::model2]);
        archive.add_field("ModelPart3", item->model_parts[ItemModelParts::model3]);
    } else {
        archive.add_field("ModelPart1", item->model_parts[ItemModelParts::model1]);
    }

    if (item->model_type == ItemModelType::layered
        || item->model_type == ItemModelType::armor) {
        archive.add_field("Cloth1Color", item->model_colors[ItemColors::cloth1]);
        archive.add_field("Cloth2Color", item->model_colors[ItemColors::cloth2]);
        archive.add_field("Leather1Color", item->model_colors[ItemColors::leather1]);
        archive.add_field("Leather2Color", item->model_colors[ItemColors::leather2]);
        archive.add_field("Metal1Color", item->model_colors[ItemColors::metal1]);
        archive.add_field("Metal2Color", item->model_colors[ItemColors::metal2]);
    }

    auto& property_list = archive.add_list("PropertiesList");
    uint8_t chance = 100;
    for (const auto& ip : item->properties) {
        property_list.push_back(0)
            .add_field("PropertyName", ip.type)
            .add_field("Subtype", ip.subtype)
            .add_field("CostTable", ip.cost_table)
            .add_field("CostValue", ip.cost_value)
            .add_field("Param1", ip.param_table)
            .add_field("Param1Value", ip.param_value)
            .add_field("ChanceAppear", chance);
    }

    return true;
}

GffOutputArchive Item::serialize(const flecs::entity ent, SerializationProfile profile)
{
    GffOutputArchive out{"UTI"};
    Item::serialize(ent, out.top, profile);
    out.build();
    return out;
}

bool Item::serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile)
{
    auto item = ent.get<Item>();
    auto common = ent.get<Common>();
    auto inventory = ent.get<Inventory>();

    archive["$type"] = "UTI";
    archive["$version"] = json_archive_version;

    archive["common"] = common->to_json(profile);
    archive["inventory"] = inventory->to_json(profile);

    archive["description"] = item->description;
    archive["description_id"] = item->description_id;

    archive["cost"] = item->cost;
    archive["additional_cost"] = item->additional_cost;
    archive["baseitem"] = item->baseitem;

    archive["stacksize"] = item->stacksize;

    archive["charges"] = item->charges;
    archive["cursed"] = item->cursed;
    archive["identified"] = item->identified;
    archive["plot"] = item->plot;
    archive["stolen"] = item->stolen;
    archive["model_type"] = item->model_type;
    archive["model_colors"] = item->model_colors;
    archive["model_parts"] = item->model_parts;

    auto& ref = archive["properties"] = nlohmann::json::array();
    for (const auto& p : item->properties) {
        ref.push_back({
            {"type", p.type},
            {"subtype", p.subtype},
            {"cost_table", p.cost_table},
            {"cost_value", p.cost_value},
            {"param_table", p.param_table},
            {"param_value", p.param_value},
        });
    }

    return true;
}

} // namespace nw
