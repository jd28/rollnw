#include "Item.hpp"

#include "../formats/Plt.hpp"
#include "../kernel/Resources.hpp"
#include "../kernel/Rules.hpp"
#include "../kernel/TwoDACache.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Item::Item()
    : Item(nw::kernel::global_allocator())
{
}

Item::Item(nw::MemoryResource* allocator)
    : ObjectBase(allocator)
    , common(allocator)
    , inventory(1, 6, 10, this, allocator) // [TODO] This needs to be dynamic based on container size??
{
    set_handle(ObjectHandle{object_invalid, ObjectType::item, 0});
    inventory.owner = this;

    model_colors.fill(0);
    model_parts.fill(0);
    for (auto& part_color : part_colors) {
        part_color.fill(255);
    }
}

void Item::clear()
{
    inventory.destroy();
    instantiated_ = false;
    inventory.owner = nullptr;

    model_colors.fill(0);
    model_parts.fill(0);
    for (auto& part_color : part_colors) {
        part_color.fill(255);
    }
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

Image Item::get_icon_by_part(ItemModelParts::type part, bool female) const
{
    String resref;
    bool is_plt = false;
    ResourceData rdata;
    Image result;

    auto bi = nw::kernel::rules().baseitems.get(baseitem);
    if (!bi) {
        LOG_F(ERROR, "[item] attempting to use invalid base item: {}", *baseitem);
        return result;
    }

    if (model_type == ItemModelType::simple && part == ItemModelParts::model1) {
        resref = fmt::format("i{}_{:03d}", bi->item_class.view(), model_parts[ItemModelParts::model1]);
    } else if (model_type == ItemModelType::layered && part == ItemModelParts::model1) {
        // In the layerd case, helmets, etc there is also only one part.
        if (baseitem == BaseItem::make(80)) { // Cloak
            resref = fmt::format("i{}_m_{:03d}", bi->item_class.view(), model_parts[ItemModelParts::model1]);
        } else { // helm
            resref = fmt::format("i{}_{:03d}", bi->item_class.view(), model_parts[ItemModelParts::model1]);
        }
        is_plt = true;
    } else if (model_type == ItemModelType::composite) {
        // In the composite case, weapons, etc there is also only one part.
        if (part == ItemModelParts::model1) {
            resref = fmt::format("i{}_b_{:03d}", bi->item_class.view(), model_parts[ItemModelParts::model1]);
        } else if (part == ItemModelParts::model2) {
            resref = fmt::format("i{}_m_{:03d}", bi->item_class.view(), model_parts[ItemModelParts::model2]);
        } else if (part == ItemModelParts::model3) {
            resref = fmt::format("i{}_t_{:03d}", bi->item_class.view(), model_parts[ItemModelParts::model3]);
        } else {
            LOG_F(ERROR, "[item] attempting to use invalid model part: {}", int(part));
            return result;
        }
    } else if (model_type == ItemModelType::armor) {
        if (model_parts[part] == 0) { return result; }
        // Only certain parts of the armor affect the icon.
        is_plt = true;
        if (part == ItemModelParts::armor_torso) {
            resref = fmt::format("ip{}_chest{:03d}", female ? 'f' : 'm', model_parts[part]);
        } else if (part == ItemModelParts::armor_robe) {
            resref = fmt::format("ip{}_robe{:03d}", female ? 'f' : 'm', model_parts[part]);
        } else if (part == ItemModelParts::armor_belt) {
            resref = fmt::format("ip{}_belt{:03d}", female ? 'f' : 'm', model_parts[part]);
        } else if (part == ItemModelParts::armor_pelvis) {
            resref = fmt::format("ip{}_pelvis{:03d}", female ? 'f' : 'm', model_parts[part]);
        } else if (part == ItemModelParts::armor_lshoul) {
            resref = fmt::format("ip{}_shol{:03d}", female ? 'f' : 'm', model_parts[part]);
        } else if (part == ItemModelParts::armor_rshoul) {
            resref = fmt::format("ip{}_shor{:03d}", female ? 'f' : 'm', model_parts[part]);
        } else {
            LOG_F(ERROR, "[item] attempting to use unnecessary model part: {}", int(part));
            return result;
        }
    }

    if (!is_plt) {
        rdata = nw::kernel::resman().demand_in_order(Resref(resref), {ResourceType::dds, ResourceType::tga});
        if (rdata.bytes.size() == 0) {
            rdata = nw::kernel::resman().demand_in_order(bi->default_icon, {ResourceType::dds, ResourceType::tga});
        }
        if (rdata.bytes.size() == 0) {
            LOG_F(ERROR, "[item] failed to load icon or default icon for base type", *baseitem);
            return result;
        }
        result = Image(std::move(rdata));
    } else {
        rdata = nw::kernel::resman().demand({Resref(resref), ResourceType::plt});
        Plt plt(std::move(rdata));
        if (!plt.valid()) {
            rdata = nw::kernel::resman().demand_in_order(bi->default_icon, {ResourceType::dds, ResourceType::tga});
            if (rdata.bytes.size() == 0) {
                LOG_F(ERROR, "[item] failed to load icon or default icon for base type", *baseitem);
                return result;
            }
            result = Image(std::move(rdata));
        } else {
            result = Image(plt, model_to_plt_colors());
        }
    }

    return result;
}

PltColors Item::model_to_plt_colors() const noexcept
{
    PltColors result{0};
    result.data[plt_layer_cloth1] = model_colors[ItemColors::cloth1];
    result.data[plt_layer_cloth2] = model_colors[ItemColors::cloth2];
    result.data[plt_layer_leather1] = model_colors[ItemColors::leather1];
    result.data[plt_layer_leather2] = model_colors[ItemColors::leather2];
    result.data[plt_layer_metal1] = model_colors[ItemColors::metal1];
    result.data[plt_layer_metal2] = model_colors[ItemColors::metal2];
    return result;
}

PltColors Item::part_to_plt_colors(ItemModelParts::type part) const noexcept
{
    PltColors result = model_to_plt_colors();
    if (part_colors[part][ItemColors::cloth1] != 255) {
        result.data[plt_layer_cloth1] = part_colors[part][ItemColors::cloth1];
    }
    if (part_colors[part][ItemColors::cloth2] != 255) {
        result.data[plt_layer_cloth2] = part_colors[part][ItemColors::cloth2];
    }
    if (part_colors[part][ItemColors::leather1] != 255) {
        result.data[plt_layer_leather1] = part_colors[part][ItemColors::leather1];
    }
    if (part_colors[part][ItemColors::leather2] != 255) {
        result.data[plt_layer_leather2] = part_colors[part][ItemColors::leather2];
    }
    if (part_colors[part][ItemColors::metal1] != 255) {
        result.data[plt_layer_metal1] = part_colors[part][ItemColors::metal1];
    }
    if (part_colors[part][ItemColors::metal2] != 255) {
        result.data[plt_layer_metal2] = part_colors[part][ItemColors::metal2];
    }
    return result;
}

String Item::get_name_from_file(const std::filesystem::path& path)
{
    String result;
    LocString l1;

    auto rdata = ResourceData::from_file(path);
    if (rdata.bytes.size() <= 8) { return result; }
    if (memcmp(rdata.bytes.data(), "UTI V3.2", 8) == 0) {
        Gff gff(std::move(rdata));
        if (!gff.valid()) { return result; }
        gff.toplevel().get_to("LocalizedName", l1);
    } else {
        try {
            nlohmann::json j = nlohmann::json::parse(rdata.bytes.string_view());
            j["common"].at("name").get_to(l1);
        } catch (...) {
            return result;
        }
    }

    result = nw::kernel::strings().get(l1);
    return result;
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
            if (ref[i].find("tag") != ref[i].end()) {
                ref[i].at("tag").get_to(ip.tag);
            }
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

        const auto& pc = archive.at("part_colors");
        for (size_t i = 0; i < pc.size(); ++i) {
            size_t part;
            pc[i].at("part").get_to(part);
            pc[i].at("colors").get_to(obj->part_colors[part]);
        }

        if (profile == SerializationProfile::instance) {
            auto it = archive.find("visual_transforms");
            if (it != std::end(archive)) {
                archive.at("visual_transforms").get_to(obj->visual_transform());
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Item::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool Item::serialize(const Item* obj, nlohmann::json& archive, SerializationProfile profile)
{
    CHECK_F(!!obj, "[item] unable to serialize null object");

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

    auto& pc = archive["part_colors"] = nlohmann::json::array();
    for (size_t i = 0; i < 19; ++i) {
        bool add = false;
        for (size_t j = 0; j < 6; ++j) {
            if (obj->part_colors[i][j] != 255) {
                add = true;
                break;
            }
        }
        if (add) {
            pc.push_back({
                {"part", i},
                {"colors", obj->part_colors[i]},
            });
        }
    }

    auto& ref = archive["properties"] = nlohmann::json::array();
    for (const auto& p : obj->properties) {
        ref.push_back({
            {"type", p.type},
            {"subtype", p.subtype},
            {"cost_table", p.cost_table},
            {"cost_value", p.cost_value},
            {"param_table", p.param_table},
            {"param_value", p.param_value},
            {"tag", p.tag},
        });
    }

    if (profile == SerializationProfile::instance) {
        archive["visual_transforms"] = nlohmann::json::array();
        for (const auto& vt : obj->visual_transform()) {
            // Don't add default constructed visual transforms
            if (vt != VisualTransform{}) {
                archive["visual_transforms"].push_back(vt);
            }
        }
    }

    return true;
}

// == Item - Serialization - Gff ==============================================
// ============================================================================

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
            list[i].get_to("CustomTag", ip.tag, false);
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
        if (!archive.get_to("xArmorPart_Belt", obj->model_parts[ItemModelParts::armor_belt], false)) {
            archive.get_to("ArmorPart_Belt", obj->model_parts[ItemModelParts::armor_belt]);
        }
        if (!archive.get_to("xArmorPart_LBice", obj->model_parts[ItemModelParts::armor_lbicep], false)) {
            archive.get_to("ArmorPart_LBicep", obj->model_parts[ItemModelParts::armor_lbicep]);
        }
        if (!archive.get_to("xArmorPart_LFArm", obj->model_parts[ItemModelParts::armor_lfarm], false)) {
            archive.get_to("ArmorPart_LFArm", obj->model_parts[ItemModelParts::armor_lfarm]);
        }
        if (!archive.get_to("xArmorPart_LFoot", obj->model_parts[ItemModelParts::armor_lfoot], false)) {
            archive.get_to("ArmorPart_LFoot", obj->model_parts[ItemModelParts::armor_lfoot]);
        }
        if (!archive.get_to("xArmorPart_LHand", obj->model_parts[ItemModelParts::armor_lhand], false)) {
            archive.get_to("ArmorPart_LHand", obj->model_parts[ItemModelParts::armor_lhand]);
        }
        if (!archive.get_to("xArmorPart_LShin", obj->model_parts[ItemModelParts::armor_lshin], false)) {
            archive.get_to("ArmorPart_LShin", obj->model_parts[ItemModelParts::armor_lshin]);
        }
        if (!archive.get_to("xArmorPart_LShou", obj->model_parts[ItemModelParts::armor_lshoul], false)) {
            archive.get_to("ArmorPart_LShoul", obj->model_parts[ItemModelParts::armor_lshoul]);
        }
        if (!archive.get_to("xArmorPart_LThig", obj->model_parts[ItemModelParts::armor_lthigh], false)) {
            archive.get_to("ArmorPart_LThigh", obj->model_parts[ItemModelParts::armor_lthigh]);
        }
        if (!archive.get_to("xArmorPart_Neck", obj->model_parts[ItemModelParts::armor_neck], false)) {
            archive.get_to("ArmorPart_Neck", obj->model_parts[ItemModelParts::armor_neck]);
        }
        if (!archive.get_to("xArmorPart_Pelvi", obj->model_parts[ItemModelParts::armor_pelvis], false)) {
            archive.get_to("ArmorPart_Pelvis", obj->model_parts[ItemModelParts::armor_pelvis]);
        }
        if (!archive.get_to("xArmorPart_RBice", obj->model_parts[ItemModelParts::armor_rbicep], false)) {
            archive.get_to("ArmorPart_RBicep", obj->model_parts[ItemModelParts::armor_rbicep]);
        }
        if (!archive.get_to("xArmorPart_RFArm", obj->model_parts[ItemModelParts::armor_rfarm], false)) {
            archive.get_to("ArmorPart_RFArm", obj->model_parts[ItemModelParts::armor_rfarm]);
        }
        if (!archive.get_to("xArmorPart_RFoot", obj->model_parts[ItemModelParts::armor_rfoot], false)) {
            archive.get_to("ArmorPart_RFoot", obj->model_parts[ItemModelParts::armor_rfoot]);
        }
        if (!archive.get_to("xArmorPart_RHand", obj->model_parts[ItemModelParts::armor_rhand], false)) {
            archive.get_to("ArmorPart_RHand", obj->model_parts[ItemModelParts::armor_rhand]);
        }
        if (!archive.get_to("xArmorPart_Robe", obj->model_parts[ItemModelParts::armor_robe], false)) {
            archive.get_to("ArmorPart_Robe", obj->model_parts[ItemModelParts::armor_robe], false);
        }
        if (!archive.get_to("xArmorPart_RShin", obj->model_parts[ItemModelParts::armor_rshin], false)) {
            archive.get_to("ArmorPart_RShin", obj->model_parts[ItemModelParts::armor_rshin]);
        }
        if (!archive.get_to("xArmorPart_RShou", obj->model_parts[ItemModelParts::armor_rshoul], false)) {
            archive.get_to("ArmorPart_RShoul", obj->model_parts[ItemModelParts::armor_rshoul]);
        }
        if (!archive.get_to("xArmorPart_RThig", obj->model_parts[ItemModelParts::armor_rthigh], false)) {
            archive.get_to("ArmorPart_RThigh", obj->model_parts[ItemModelParts::armor_rthigh]);
        }
        if (!archive.get_to("xArmorPart_Torso", obj->model_parts[ItemModelParts::armor_torso], false)) {
            archive.get_to("ArmorPart_Torso", obj->model_parts[ItemModelParts::armor_torso]);
        }
        for (size_t i = 0; i < 19; ++i) {
            for (size_t j = 0; j < 6; ++j) {
                auto field_name = fmt::format("APart_{}_Col_{}", i, j);
                archive.get_to(field_name, obj->part_colors[i][j], false);
            }
        }
    } else if (archive.has_field("ModelPart2")) {
        obj->model_type = ItemModelType::composite;
        if (!archive.get_to("xModelPart1", obj->model_parts[ItemModelParts::model1], false)) {
            archive.get_to("ModelPart1", obj->model_parts[ItemModelParts::model1]);
        }
        if (!archive.get_to("xModelPart2", obj->model_parts[ItemModelParts::model2], false)) {
            archive.get_to("ModelPart2", obj->model_parts[ItemModelParts::model2]);
        }
        if (!archive.get_to("xModelPart3", obj->model_parts[ItemModelParts::model3], false)) {
            archive.get_to("ModelPart3", obj->model_parts[ItemModelParts::model3]);
        }
    } else {
        if (archive.has_field("Cloth1Color")) {
            obj->model_type = ItemModelType::layered;
        } else {
            obj->model_type = ItemModelType::simple;
        }
        if (!archive.get_to("xModelPart1", obj->model_parts[ItemModelParts::model1], false)) {
            archive.get_to("ModelPart1", obj->model_parts[ItemModelParts::model1]);
        }
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

    if (profile == SerializationProfile::instance) {
        auto field = archive["VisTransformList"];
        if (field.valid()) {
            obj->visual_transform().reserve(field.size());
            for (size_t i = 0; i < field.size(); ++i) {
                VisualTransform vt;
                deserialize(field[i], vt);
                obj->add_visual_transform(vt);
            }
        } else {
            auto st = archive.get<GffStruct>("VisualTransform", false);
            if (st) {
                VisualTransform vt;
                deserialize(*st, vt);
                obj->add_visual_transform(vt);
            }
        }
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
        .add_field("Tag", String(obj->common.tag ? obj->common.tag.view() : StringView()));

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
        archive.add_field("ArmorPart_Belt", uint8_t(obj->model_parts[ItemModelParts::armor_belt]))
            .add_field("ArmorPart_LBicep", uint8_t(obj->model_parts[ItemModelParts::armor_lbicep]))
            .add_field("ArmorPart_LFArm", uint8_t(obj->model_parts[ItemModelParts::armor_lfarm]))
            .add_field("ArmorPart_LFoot", uint8_t(obj->model_parts[ItemModelParts::armor_lfoot]))
            .add_field("ArmorPart_LHand", uint8_t(obj->model_parts[ItemModelParts::armor_lhand]))
            .add_field("ArmorPart_LShin", uint8_t(obj->model_parts[ItemModelParts::armor_lshin]))
            .add_field("ArmorPart_LShoul", uint8_t(obj->model_parts[ItemModelParts::armor_lshoul]))
            .add_field("ArmorPart_LThigh", uint8_t(obj->model_parts[ItemModelParts::armor_lthigh]))
            .add_field("ArmorPart_Neck", uint8_t(obj->model_parts[ItemModelParts::armor_neck]))
            .add_field("ArmorPart_Pelvis", uint8_t(obj->model_parts[ItemModelParts::armor_pelvis]))
            .add_field("ArmorPart_RBicep", uint8_t(obj->model_parts[ItemModelParts::armor_rbicep]))
            .add_field("ArmorPart_RFArm", uint8_t(obj->model_parts[ItemModelParts::armor_rfarm]))
            .add_field("ArmorPart_RFoot", uint8_t(obj->model_parts[ItemModelParts::armor_rfoot]))
            .add_field("ArmorPart_RHand", uint8_t(obj->model_parts[ItemModelParts::armor_rhand]))
            .add_field("ArmorPart_Robe", uint8_t(obj->model_parts[ItemModelParts::armor_robe]))
            .add_field("ArmorPart_RShin", uint8_t(obj->model_parts[ItemModelParts::armor_rshin]))
            .add_field("ArmorPart_RShoul", uint8_t(obj->model_parts[ItemModelParts::armor_rshoul]))
            .add_field("ArmorPart_RThigh", uint8_t(obj->model_parts[ItemModelParts::armor_rthigh]))
            .add_field("ArmorPart_Torso", uint8_t(obj->model_parts[ItemModelParts::armor_torso]))
            .add_field("xArmorPart_Belt", obj->model_parts[ItemModelParts::armor_belt])
            .add_field("xArmorPart_LBice", obj->model_parts[ItemModelParts::armor_lbicep])
            .add_field("xArmorPart_LFArm", obj->model_parts[ItemModelParts::armor_lfarm])
            .add_field("xArmorPart_LFoot", obj->model_parts[ItemModelParts::armor_lfoot])
            .add_field("xArmorPart_LHand", obj->model_parts[ItemModelParts::armor_lhand])
            .add_field("xArmorPart_LShin", obj->model_parts[ItemModelParts::armor_lshin])
            .add_field("xArmorPart_LShou", obj->model_parts[ItemModelParts::armor_lshoul])
            .add_field("xArmorPart_LThig", obj->model_parts[ItemModelParts::armor_lthigh])
            .add_field("xArmorPart_Neck", obj->model_parts[ItemModelParts::armor_neck])
            .add_field("xArmorPart_Pelvi", obj->model_parts[ItemModelParts::armor_pelvis])
            .add_field("xArmorPart_RBice", obj->model_parts[ItemModelParts::armor_rbicep])
            .add_field("xArmorPart_RFArm", obj->model_parts[ItemModelParts::armor_rfarm])
            .add_field("xArmorPart_RFoot", obj->model_parts[ItemModelParts::armor_rfoot])
            .add_field("xArmorPart_RHand", obj->model_parts[ItemModelParts::armor_rhand])
            .add_field("xArmorPart_Robe", obj->model_parts[ItemModelParts::armor_robe])
            .add_field("xArmorPart_RShin", obj->model_parts[ItemModelParts::armor_rshin])
            .add_field("xArmorPart_RShou", obj->model_parts[ItemModelParts::armor_rshoul])
            .add_field("xArmorPart_RThig", obj->model_parts[ItemModelParts::armor_rthigh])
            .add_field("xArmorPart_Torso", obj->model_parts[ItemModelParts::armor_torso]);

        for (size_t i = 0; i < 19; ++i) {
            for (size_t j = 0; j < 6; ++j) {
                if (obj->part_colors[i][j] == 255) { continue; }
                auto field_name = fmt::format("APart_{}_Col_{}", i, j);
                archive.add_field(field_name, obj->part_colors[i][j]);
            }
        }
    } else if (obj->model_type == ItemModelType::composite) {
        archive.add_field("ModelPart1", uint8_t(obj->model_parts[ItemModelParts::model1]));
        archive.add_field("ModelPart2", uint8_t(obj->model_parts[ItemModelParts::model2]));
        archive.add_field("ModelPart3", uint8_t(obj->model_parts[ItemModelParts::model3]));
        archive.add_field("xModelPart1", obj->model_parts[ItemModelParts::model1]);
        archive.add_field("xModelPart2", obj->model_parts[ItemModelParts::model2]);
        archive.add_field("xModelPart3", obj->model_parts[ItemModelParts::model3]);
    } else {
        archive.add_field("ModelPart1", uint8_t(obj->model_parts[ItemModelParts::model1]));
        archive.add_field("xModelPart1", obj->model_parts[ItemModelParts::model1]);
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
            .add_field("ChanceAppear", chance)
            .add_field("CustomTag", ip.tag);
    }

    if (profile == SerializationProfile::instance) {
        // Don't add default constructed visual transforms (unlike the game).
        auto& vts = archive.add_list("VisTransformList");
        for (const auto& vt : obj->visual_transform()) {
            if (vt != VisualTransform{}) {
                auto& st = vts.push_back(6);
                serialize(st, vt);
            }
        }
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

} // namespace nw
