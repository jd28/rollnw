#include "Item.hpp"

namespace nw {

Item::Item(const GffStruct gff, SerializationProfile profile)
    : loc{gff, profile}
    , data{gff, profile}
{

    gff.get_to("TemplateResRef", resref);
    gff.get_to("Tag", tag);
    gff.get_to("LocalizedName", name); // Bioware UTI doc is wrong
    gff.get_to("Description", description);
    gff.get_to("DescIdentified", description_id);
    gff.get_to("Cost", cost);
    gff.get_to("AddCost", additional_cost);
    gff.get_to("BaseItem", baseitem);
    gff.get_to("StackSize", stacksize);
    gff.get_to("Charges", charges);
    gff.get_to("Cursed", cursed);
    gff.get_to("Plot", plot);
    gff.get_to("Stolen", stolen);

    if (profile == SerializationProfile::blueprint) {
        gff.get_to("Comment", comment, false);
        gff.get_to("PaletteID", palette_id, false);
    }

    // Guess model type from what's in the gff.
    if (gff.has_field("ArmorPart_Belt")) {
        model_type = ItemModelType::armor;
        gff.get_to("ArmorPart_Belt", model_parts[ItemModelParts::armor_belt]);
        gff.get_to("ArmorPart_LBicep", model_parts[ItemModelParts::armor_lbicep]);
        gff.get_to("ArmorPart_LFArm", model_parts[ItemModelParts::armor_lfarm]);
        gff.get_to("ArmorPart_LFoot", model_parts[ItemModelParts::armor_lfoot]);
        gff.get_to("ArmorPart_LHand", model_parts[ItemModelParts::armor_lhand]);
        gff.get_to("ArmorPart_LShin", model_parts[ItemModelParts::armor_lshin]);
        gff.get_to("ArmorPart_LShoul", model_parts[ItemModelParts::armor_lshoul]);
        gff.get_to("ArmorPart_LThigh", model_parts[ItemModelParts::armor_lthigh]);
        gff.get_to("ArmorPart_Neck", model_parts[ItemModelParts::armor_neck]);
        gff.get_to("ArmorPart_Pelvis", model_parts[ItemModelParts::armor_pelvis]);
        gff.get_to("ArmorPart_RBicep", model_parts[ItemModelParts::armor_rbicep]);
        gff.get_to("ArmorPart_RFArm", model_parts[ItemModelParts::armor_rfarm]);
        gff.get_to("ArmorPart_RFoot", model_parts[ItemModelParts::armor_rfoot]);
        gff.get_to("ArmorPart_RHand", model_parts[ItemModelParts::armor_rhand]);
        gff.get_to("ArmorPart_Robe", model_parts[ItemModelParts::armor_robe]);
        gff.get_to("ArmorPart_RShin", model_parts[ItemModelParts::armor_rshin]);
        gff.get_to("ArmorPart_RShoul", model_parts[ItemModelParts::armor_rshoul]);
        gff.get_to("ArmorPart_RThigh", model_parts[ItemModelParts::armor_rthigh]);
        gff.get_to("ArmorPart_Torso", model_parts[ItemModelParts::armor_torso]);
    } else if (gff.has_field("ModelPart2")) {
        model_type = ItemModelType::composite;
        gff.get_to("ModelPart1", model_parts[ItemModelParts::model1]);
        gff.get_to("ModelPart2", model_parts[ItemModelParts::model2]);
        gff.get_to("ModelPart3", model_parts[ItemModelParts::model3]);
    } else {
        if (gff.has_field("Cloth1Color")) {
            model_type = ItemModelType::layered;
        } else {
            model_type = ItemModelType::simple;
        }
        gff.get_to("ModelPart1", model_parts[ItemModelParts::model1]);
    }

    if (model_type == ItemModelType::layered
        || model_type == ItemModelType::armor) {
        gff.get_to("Cloth1Color", model_colors[ItemColors::cloth1]);
        gff.get_to("Cloth2Color", model_colors[ItemColors::cloth2]);
        gff.get_to("Leather1Color", model_colors[ItemColors::leather1]);
        gff.get_to("Leather2Color", model_colors[ItemColors::leather2]);
        gff.get_to("Metal1Color", model_colors[ItemColors::metal1]);
        gff.get_to("Metal2Color", model_colors[ItemColors::metal2]);
    }

    auto prop_size = gff["PropertiesList"].size();
    properties.reserve(prop_size);
    auto list = gff["PropertiesList"];
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
}

} // namespace nw
