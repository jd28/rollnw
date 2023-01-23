#include "Item.hpp"

#include "../kernel/TwoDACache.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Item::Item()
{
    set_handle({object_invalid, ObjectType::item, 0});
    inventory.owner = this;
}

bool Item::instantiate()
{
    if (instantiated_) { return true; }
    instantiated_ = inventory.instantiate();

    if (model_type == nw::ItemModelType::armor) {
        auto& tda = nw::kernel::twodas();
        auto parts_chest = tda.get("parts_chest");
        if (parts_chest) {
            float temp = 0.0f;
            if (parts_chest->get_to(model_parts[nw::ItemModelParts::armor_torso], "ACBonus", temp)) {
                armor_id = static_cast<int>(temp);
            }
        } else {
            LOG_F(ERROR, "item: failed to load parts_chest.2da");
            instantiated_ = false;
        }
    }

    return instantiated_;
}

bool Item::deserialize(Item* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    try {
        obj->common.from_json(archive.at("common"), profile, ObjectType::item);
        obj->inventory.from_json(archive.at("inventory"), profile);

        archive.at("description").get_to(obj->description);
        archive.at("description_id").get_to(obj->description_id);
        const auto& ref = archive.at("properties");
        obj->properties.reserve(ref.size());
        for (size_t i = 0; i < ref.size(); ++i) {
            ItemProperty ip;
            ref[i].at("type").get_to(ip.type);
            ref[i].at("subtype").get_to(ip.subtype);
            ref[i].at("cost_table").get_to(ip.cost_table);
            ref[i].at("cost_value").get_to(ip.cost_value);
            ref[i].at("param_table").get_to(ip.param_table);
            ref[i].at("param_value").get_to(ip.param_value);
            obj->properties.push_back(ip);
        }

        archive.at("cost").get_to(obj->cost);
        archive.at("additional_cost").get_to(obj->additional_cost);
        archive.at("baseitem").get_to(obj->baseitem);

        archive.at("stacksize").get_to(obj->stacksize);

        archive.at("charges").get_to(obj->charges);
        archive.at("cursed").get_to(obj->cursed);
        archive.at("identified").get_to(obj->identified);
        archive.at("plot").get_to(obj->plot);
        archive.at("stolen").get_to(obj->stolen);
        archive.at("model_type").get_to(obj->model_type);
        archive.at("model_colors").get_to(obj->model_colors);
        archive.at("model_parts").get_to(obj->model_parts);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Item::from_json exception: {}", e.what());
        return false;
    }

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }

    return true;
}

bool Item::serialize(const Item* obj, nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive["$type"] = "UTI";
    archive["$version"] = json_archive_version;

    archive["common"] = obj->common.to_json(profile, ObjectType::item);
    archive["inventory"] = obj->inventory.to_json(profile);

    archive["description"] = obj->description;
    archive["description_id"] = obj->description_id;

    archive["cost"] = obj->cost;
    archive["additional_cost"] = obj->additional_cost;
    archive["baseitem"] = obj->baseitem;

    archive["stacksize"] = obj->stacksize;

    archive["charges"] = obj->charges;
    archive["cursed"] = obj->cursed;
    archive["identified"] = obj->identified;
    archive["plot"] = obj->plot;
    archive["stolen"] = obj->stolen;
    archive["model_type"] = obj->model_type;
    archive["model_colors"] = obj->model_colors;
    archive["model_parts"] = obj->model_parts;

    auto& ref = archive["properties"] = nlohmann::json::array();
    for (const auto& p : obj->properties) {
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

// == Item - Serialization - Gff ==============================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY

bool deserialize(Item* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }
    int temp_int = 0;
    deserialize(obj->common, archive, profile, ObjectType::item);
    deserialize(obj->inventory, archive, profile);

    archive.get_to("Description", obj->description);
    archive.get_to("DescIdentified", obj->description_id);

    auto prop_size = archive["PropertiesList"].size();
    obj->properties.reserve(prop_size);
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
            obj->properties.push_back(ip);
        }
    }

    archive.get_to("Cost", obj->cost);
    archive.get_to("AddCost", obj->additional_cost);
    if (archive.get_to("BaseItem", temp_int)) {
        obj->baseitem = nw::BaseItem::make(temp_int);
    }

    archive.get_to("StackSize", obj->stacksize);

    archive.get_to("Charges", obj->charges);
    archive.get_to("Cursed", obj->cursed, false);
    archive.get_to("Identified", obj->identified);
    archive.get_to("Plot", obj->plot);
    archive.get_to("Stolen", obj->stolen);

    // Guess model type from what's in the archive.
    if (archive.has_field("ArmorPart_Belt")) {
        obj->model_type = ItemModelType::armor;
        archive.get_to("ArmorPart_Belt", obj->model_parts[ItemModelParts::armor_belt]);
        archive.get_to("ArmorPart_LBicep", obj->model_parts[ItemModelParts::armor_lbicep]);
        archive.get_to("ArmorPart_LFArm", obj->model_parts[ItemModelParts::armor_lfarm]);
        archive.get_to("ArmorPart_LFoot", obj->model_parts[ItemModelParts::armor_lfoot]);
        archive.get_to("ArmorPart_LHand", obj->model_parts[ItemModelParts::armor_lhand]);
        archive.get_to("ArmorPart_LShin", obj->model_parts[ItemModelParts::armor_lshin]);
        archive.get_to("ArmorPart_LShoul", obj->model_parts[ItemModelParts::armor_lshoul]);
        archive.get_to("ArmorPart_LThigh", obj->model_parts[ItemModelParts::armor_lthigh]);
        archive.get_to("ArmorPart_Neck", obj->model_parts[ItemModelParts::armor_neck]);
        archive.get_to("ArmorPart_Pelvis", obj->model_parts[ItemModelParts::armor_pelvis]);
        archive.get_to("ArmorPart_RBicep", obj->model_parts[ItemModelParts::armor_rbicep]);
        archive.get_to("ArmorPart_RFArm", obj->model_parts[ItemModelParts::armor_rfarm]);
        archive.get_to("ArmorPart_RFoot", obj->model_parts[ItemModelParts::armor_rfoot]);
        archive.get_to("ArmorPart_RHand", obj->model_parts[ItemModelParts::armor_rhand]);
        archive.get_to("ArmorPart_Robe", obj->model_parts[ItemModelParts::armor_robe], false);
        archive.get_to("ArmorPart_RShin", obj->model_parts[ItemModelParts::armor_rshin]);
        archive.get_to("ArmorPart_RShoul", obj->model_parts[ItemModelParts::armor_rshoul]);
        archive.get_to("ArmorPart_RThigh", obj->model_parts[ItemModelParts::armor_rthigh]);
        archive.get_to("ArmorPart_Torso", obj->model_parts[ItemModelParts::armor_torso]);
    } else if (archive.has_field("ModelPart2")) {
        obj->model_type = ItemModelType::composite;
        archive.get_to("ModelPart1", obj->model_parts[ItemModelParts::model1]);
        archive.get_to("ModelPart2", obj->model_parts[ItemModelParts::model2]);
        archive.get_to("ModelPart3", obj->model_parts[ItemModelParts::model3]);
    } else {
        if (archive.has_field("Cloth1Color")) {
            obj->model_type = ItemModelType::layered;
        } else {
            obj->model_type = ItemModelType::simple;
        }
        archive.get_to("ModelPart1", obj->model_parts[ItemModelParts::model1]);
    }

    if (obj->model_type == ItemModelType::layered
        || obj->model_type == ItemModelType::armor) {
        archive.get_to("Cloth1Color", obj->model_colors[ItemColors::cloth1]);
        archive.get_to("Cloth2Color", obj->model_colors[ItemColors::cloth2]);
        archive.get_to("Leather1Color", obj->model_colors[ItemColors::leather1]);
        archive.get_to("Leather2Color", obj->model_colors[ItemColors::leather2]);
        archive.get_to("Metal1Color", obj->model_colors[ItemColors::metal1]);
        archive.get_to("Metal2Color", obj->model_colors[ItemColors::metal2]);
    }

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }

    return true;
}

bool serialize(const Item* obj, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive.add_field("TemplateResRef", obj->common.resref)
        .add_field("LocalizedName", obj->common.name)
        .add_field("Tag", obj->common.tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", obj->common.comment);
        archive.add_field("PaletteID", obj->common.palette_id);
    } else {
        archive.add_field("PositionX", obj->common.location.position.x)
            .add_field("PositionY", obj->common.location.position.y)
            .add_field("PositionZ", obj->common.location.position.z)
            .add_field("OrientationX", obj->common.location.orientation.x)
            .add_field("OrientationY", obj->common.location.orientation.y);
    }

    serialize(obj->common.locals, archive, profile);
    serialize(obj->inventory, archive, profile);

    archive.add_field("Description", obj->description);
    archive.add_field("DescIdentified", obj->description_id);

    archive.add_field("Cost", obj->cost)
        .add_field("AddCost", obj->additional_cost)
        .add_field("BaseItem", *obj->baseitem);

    archive.add_field("StackSize", obj->stacksize);

    archive.add_field("Charges", obj->charges)
        .add_field("Cursed", obj->cursed)
        .add_field("Identified", obj->identified)
        .add_field("Plot", obj->plot)
        .add_field("Stolen", obj->stolen);

    if (obj->model_type == ItemModelType::armor) {
        archive.add_field("ArmorPart_Belt", obj->model_parts[ItemModelParts::armor_belt])
            .add_field("ArmorPart_LBicep", obj->model_parts[ItemModelParts::armor_lbicep])
            .add_field("ArmorPart_LFArm", obj->model_parts[ItemModelParts::armor_lfarm])
            .add_field("ArmorPart_LFoot", obj->model_parts[ItemModelParts::armor_lfoot])
            .add_field("ArmorPart_LHand", obj->model_parts[ItemModelParts::armor_lhand])
            .add_field("ArmorPart_LShin", obj->model_parts[ItemModelParts::armor_lshin])
            .add_field("ArmorPart_LShoul", obj->model_parts[ItemModelParts::armor_lshoul])
            .add_field("ArmorPart_LThigh", obj->model_parts[ItemModelParts::armor_lthigh])
            .add_field("ArmorPart_Neck", obj->model_parts[ItemModelParts::armor_neck])
            .add_field("ArmorPart_Pelvis", obj->model_parts[ItemModelParts::armor_pelvis])
            .add_field("ArmorPart_RBicep", obj->model_parts[ItemModelParts::armor_rbicep])
            .add_field("ArmorPart_RFArm", obj->model_parts[ItemModelParts::armor_rfarm])
            .add_field("ArmorPart_RFoot", obj->model_parts[ItemModelParts::armor_rfoot])
            .add_field("ArmorPart_RHand", obj->model_parts[ItemModelParts::armor_rhand])
            .add_field("ArmorPart_Robe", obj->model_parts[ItemModelParts::armor_robe])
            .add_field("ArmorPart_RShin", obj->model_parts[ItemModelParts::armor_rshin])
            .add_field("ArmorPart_RShoul", obj->model_parts[ItemModelParts::armor_rshoul])
            .add_field("ArmorPart_RThigh", obj->model_parts[ItemModelParts::armor_rthigh])
            .add_field("ArmorPart_Torso", obj->model_parts[ItemModelParts::armor_torso]);
    } else if (obj->model_type == ItemModelType::composite) {
        archive.add_field("ModelPart1", obj->model_parts[ItemModelParts::model1]);
        archive.add_field("ModelPart2", obj->model_parts[ItemModelParts::model2]);
        archive.add_field("ModelPart3", obj->model_parts[ItemModelParts::model3]);
    } else {
        archive.add_field("ModelPart1", obj->model_parts[ItemModelParts::model1]);
    }

    if (obj->model_type == ItemModelType::layered
        || obj->model_type == ItemModelType::armor) {
        archive.add_field("Cloth1Color", obj->model_colors[ItemColors::cloth1]);
        archive.add_field("Cloth2Color", obj->model_colors[ItemColors::cloth2]);
        archive.add_field("Leather1Color", obj->model_colors[ItemColors::leather1]);
        archive.add_field("Leather2Color", obj->model_colors[ItemColors::leather2]);
        archive.add_field("Metal1Color", obj->model_colors[ItemColors::metal1]);
        archive.add_field("Metal2Color", obj->model_colors[ItemColors::metal2]);
    }

    auto& property_list = archive.add_list("PropertiesList");
    uint8_t chance = 100;
    for (const auto& ip : obj->properties) {
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

GffBuilder serialize(const Item* obj, SerializationProfile profile)
{
    GffBuilder out{"UTI"};
    if (!obj) return out;

    serialize(obj, out.top, profile);
    out.build();
    return out;
}

#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
