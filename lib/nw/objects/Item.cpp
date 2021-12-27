#include "Item.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Item::Item()
    : common_{ObjectType::item}
{
}

Item::Item(const GffInputArchiveStruct& archive, SerializationProfile profile)
    : common_{ObjectType::item}
{
    this->from_gff(archive, profile);
}

Item::Item(const nlohmann::json& archive, SerializationProfile profile)
    : common_{ObjectType::item}
{
    this->from_json(archive, profile);
}

bool Item::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{

    common_.from_gff(archive, profile);
    inventory.from_gff(archive, profile);
    archive.get_to("Description", description);
    archive.get_to("DescIdentified", description_id);
    archive.get_to("Cost", cost);
    archive.get_to("AddCost", additional_cost);
    archive.get_to("BaseItem", baseitem);
    archive.get_to("StackSize", stacksize);
    archive.get_to("Charges", charges);
    archive.get_to("Cursed", cursed);
    archive.get_to("Identified", identified);
    archive.get_to("Plot", plot);
    archive.get_to("Stolen", stolen);

    // Guess model type from what's in the archive.
    if (archive.has_field("ArmorPart_Belt")) {
        model_type = ItemModelType::armor;
        archive.get_to("ArmorPart_Belt", model_parts[ItemModelParts::armor_belt]);
        archive.get_to("ArmorPart_LBicep", model_parts[ItemModelParts::armor_lbicep]);
        archive.get_to("ArmorPart_LFArm", model_parts[ItemModelParts::armor_lfarm]);
        archive.get_to("ArmorPart_LFoot", model_parts[ItemModelParts::armor_lfoot]);
        archive.get_to("ArmorPart_LHand", model_parts[ItemModelParts::armor_lhand]);
        archive.get_to("ArmorPart_LShin", model_parts[ItemModelParts::armor_lshin]);
        archive.get_to("ArmorPart_LShoul", model_parts[ItemModelParts::armor_lshoul]);
        archive.get_to("ArmorPart_LThigh", model_parts[ItemModelParts::armor_lthigh]);
        archive.get_to("ArmorPart_Neck", model_parts[ItemModelParts::armor_neck]);
        archive.get_to("ArmorPart_Pelvis", model_parts[ItemModelParts::armor_pelvis]);
        archive.get_to("ArmorPart_RBicep", model_parts[ItemModelParts::armor_rbicep]);
        archive.get_to("ArmorPart_RFArm", model_parts[ItemModelParts::armor_rfarm]);
        archive.get_to("ArmorPart_RFoot", model_parts[ItemModelParts::armor_rfoot]);
        archive.get_to("ArmorPart_RHand", model_parts[ItemModelParts::armor_rhand]);
        archive.get_to("ArmorPart_Robe", model_parts[ItemModelParts::armor_robe]);
        archive.get_to("ArmorPart_RShin", model_parts[ItemModelParts::armor_rshin]);
        archive.get_to("ArmorPart_RShoul", model_parts[ItemModelParts::armor_rshoul]);
        archive.get_to("ArmorPart_RThigh", model_parts[ItemModelParts::armor_rthigh]);
        archive.get_to("ArmorPart_Torso", model_parts[ItemModelParts::armor_torso]);
    } else if (archive.has_field("ModelPart2")) {
        model_type = ItemModelType::composite;
        archive.get_to("ModelPart1", model_parts[ItemModelParts::model1]);
        archive.get_to("ModelPart2", model_parts[ItemModelParts::model2]);
        archive.get_to("ModelPart3", model_parts[ItemModelParts::model3]);
    } else {
        if (archive.has_field("Cloth1Color")) {
            model_type = ItemModelType::layered;
        } else {
            model_type = ItemModelType::simple;
        }
        archive.get_to("ModelPart1", model_parts[ItemModelParts::model1]);
    }

    if (model_type == ItemModelType::layered
        || model_type == ItemModelType::armor) {
        archive.get_to("Cloth1Color", model_colors[ItemColors::cloth1]);
        archive.get_to("Cloth2Color", model_colors[ItemColors::cloth2]);
        archive.get_to("Leather1Color", model_colors[ItemColors::leather1]);
        archive.get_to("Leather2Color", model_colors[ItemColors::leather2]);
        archive.get_to("Metal1Color", model_colors[ItemColors::metal1]);
        archive.get_to("Metal2Color", model_colors[ItemColors::metal2]);
    }

    auto prop_size = archive["PropertiesList"].size();
    properties.reserve(prop_size);
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
            properties.push_back(ip);
        }
    }

    return true;
}

bool Item::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    try {
        common_.from_json(archive.at("common"), profile);
        inventory.from_json(archive.at("inventory"), profile);

        archive.at("description").get_to(description);
        archive.at("description_id").get_to(description_id);
        archive.at("cost").get_to(cost);
        archive.at("additional_cost").get_to(additional_cost);
        archive.at("baseitem").get_to(baseitem);
        archive.at("stacksize").get_to(stacksize);
        archive.at("charges").get_to(charges);
        archive.at("cursed").get_to(cursed);
        archive.at("identified").get_to(identified);
        archive.at("plot").get_to(plot);
        archive.at("stolen").get_to(stolen);
        archive.at("model_type").get_to(model_type);
        archive.at("model_colors").get_to(model_colors);
        archive.at("model_parts").get_to(model_parts);

        const auto& ref = archive.at("properties");
        properties.reserve(ref.size());
        for (size_t i = 0; i < ref.size(); ++i) {
            ItemProperty ip;
            ref[i].at("type").get_to(ip.type);
            ref[i].at("subtype").get_to(ip.subtype);
            ref[i].at("cost_table").get_to(ip.cost_table);
            ref[i].at("cost_value").get_to(ip.cost_value);
            ref[i].at("param_table").get_to(ip.param_table);
            ref[i].at("param_value").get_to(ip.param_value);
            properties.push_back(ip);
        }

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Item::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool Item::to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const
{
    archive.add_fields({
        {"TemplateResRef", common_.resref},
        {"LocalizedName", common_.name},
        {"Tag", common_.tag},
    });

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common_.comment);
        archive.add_field("PaletteID", common_.palette_id);
    } else {
        archive.add_fields({
            {"PositionX", common_.location.position.x},
            {"PositionY", common_.location.position.y},
            {"PositionZ", common_.location.position.z},
            {"OrientationX", common_.location.orientation.x},
            {"OrientationY", common_.location.orientation.y},
        });
    }

    common_.local_data.to_gff(archive);

    if (inventory.items.size()) {
        inventory.to_gff(archive, profile);
    }
    archive.add_fields({
        {"Description", description},
        {"DescIdentified", description_id},
        {"Cost", cost},
        {"AddCost", additional_cost},
        {"BaseItem", baseitem},
        {"StackSize", stacksize},
        {"Charges", charges},
        {"Cursed", cursed},
        {"Identified", identified},
        {"Plot", plot},
        {"Stolen", stolen},
    });

    if (model_type == ItemModelType::armor) {
        archive.add_fields({
            {"ArmorPart_Belt", model_parts[ItemModelParts::armor_belt]},
            {"ArmorPart_LBicep", model_parts[ItemModelParts::armor_lbicep]},
            {"ArmorPart_LFArm", model_parts[ItemModelParts::armor_lfarm]},
            {"ArmorPart_LFoot", model_parts[ItemModelParts::armor_lfoot]},
            {"ArmorPart_LHand", model_parts[ItemModelParts::armor_lhand]},
            {"ArmorPart_LShin", model_parts[ItemModelParts::armor_lshin]},
            {"ArmorPart_LShoul", model_parts[ItemModelParts::armor_lshoul]},
            {"ArmorPart_LThigh", model_parts[ItemModelParts::armor_lthigh]},
            {"ArmorPart_Neck", model_parts[ItemModelParts::armor_neck]},
            {"ArmorPart_Pelvis", model_parts[ItemModelParts::armor_pelvis]},
            {"ArmorPart_RBicep", model_parts[ItemModelParts::armor_rbicep]},
            {"ArmorPart_RFArm", model_parts[ItemModelParts::armor_rfarm]},
            {"ArmorPart_RFoot", model_parts[ItemModelParts::armor_rfoot]},
            {"ArmorPart_RHand", model_parts[ItemModelParts::armor_rhand]},
            {"ArmorPart_Robe", model_parts[ItemModelParts::armor_robe]},
            {"ArmorPart_RShin", model_parts[ItemModelParts::armor_rshin]},
            {"ArmorPart_RShoul", model_parts[ItemModelParts::armor_rshoul]},
            {"ArmorPart_RThigh", model_parts[ItemModelParts::armor_rthigh]},
            {"ArmorPart_Torso", model_parts[ItemModelParts::armor_torso]},
        });
    } else if (model_type == ItemModelType::composite) {
        archive.add_field("ModelPart1", model_parts[ItemModelParts::model1]);
        archive.add_field("ModelPart2", model_parts[ItemModelParts::model2]);
        archive.add_field("ModelPart3", model_parts[ItemModelParts::model3]);
    } else {
        archive.add_field("ModelPart1", model_parts[ItemModelParts::model1]);
    }

    if (model_type == ItemModelType::layered
        || model_type == ItemModelType::armor) {
        archive.add_field("Cloth1Color", model_colors[ItemColors::cloth1]);
        archive.add_field("Cloth2Color", model_colors[ItemColors::cloth2]);
        archive.add_field("Leather1Color", model_colors[ItemColors::leather1]);
        archive.add_field("Leather2Color", model_colors[ItemColors::leather2]);
        archive.add_field("Metal1Color", model_colors[ItemColors::metal1]);
        archive.add_field("Metal2Color", model_colors[ItemColors::metal2]);
    }

    auto& property_list = archive.add_list("PropertiesList");
    uint8_t chance = 100;
    for (const auto& ip : properties) {
        property_list.push_back(0, {
                                       {"PropertyName", ip.type},
                                       {"Subtype", ip.subtype},
                                       {"CostTable", ip.cost_table},
                                       {"CostValue", ip.cost_value},
                                       {"Param1", ip.param_table},
                                       {"Param1Value", ip.param_value},
                                       {"ChanceAppear", chance},
                                   });
    }

    return true;
}

nlohmann::json Item::to_json(SerializationProfile profile) const
{
    nlohmann::json j;
    j["$type"] = "UTI";
    j["$version"] = LIBNW_JSON_ARCHIVE_VERSION;

    j["common"] = common_.to_json(profile);
    j["inventory"] = inventory.to_json(profile);

    j["description"] = description;
    j["description_id"] = description_id;
    j["cost"] = cost;
    j["additional_cost"] = additional_cost;
    j["baseitem"] = baseitem;
    j["stacksize"] = stacksize;
    j["charges"] = charges;
    j["cursed"] = cursed;
    j["identified"] = identified;
    j["plot"] = plot;
    j["stolen"] = stolen;
    j["model_type"] = model_type;
    j["model_colors"] = model_colors;
    j["model_parts"] = model_parts;

    auto& ref = j["properties"] = nlohmann::json::array();
    for (const auto& p : properties) {
        ref.push_back({
            {"type", p.type},
            {"subtype", p.subtype},
            {"cost_table", p.cost_table},
            {"cost_value", p.cost_value},
            {"param_table", p.param_table},
            {"param_value", p.param_value},
        });
    }

    return j;
}

} // namespace nw
