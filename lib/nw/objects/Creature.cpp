#include "Creature.hpp"

#include "../functions.hpp"
#include "../kernel/Objects.hpp"
#include "../kernel/Rules.hpp"
#include "../kernel/Strings.hpp"
#include "../kernel/TwoDACache.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "../util/platform.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace nw {

bool CreatureScripts::deserialize(const GffStruct& archive)
{
    archive.get_to("ScriptAttacked", on_attacked);
    archive.get_to("ScriptDamaged", on_damaged);
    archive.get_to("ScriptDeath", on_death);
    archive.get_to("ScriptDialogue", on_conversation);
    archive.get_to("ScriptDisturbed", on_disturbed);
    archive.get_to("ScriptEndRound", on_endround);
    archive.get_to("ScriptHeartbeat", on_heartbeat);
    archive.get_to("ScriptOnBlocked", on_blocked);
    archive.get_to("ScriptOnNotice", on_perceived);
    archive.get_to("ScriptRested", on_rested);
    archive.get_to("ScriptSpawn", on_spawn);
    archive.get_to("ScriptSpellAt", on_spell_cast_at);
    archive.get_to("ScriptUserDefine", on_user_defined);

    return true;
}

bool CreatureScripts::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("on_attacked").get_to(on_attacked);
        archive.at("on_blocked").get_to(on_blocked);
        archive.at("on_conversation").get_to(on_conversation);
        archive.at("on_damaged").get_to(on_damaged);
        archive.at("on_death").get_to(on_death);
        archive.at("on_disturbed").get_to(on_disturbed);
        archive.at("on_endround").get_to(on_endround);
        archive.at("on_heartbeat").get_to(on_heartbeat);
        archive.at("on_perceived").get_to(on_perceived);
        archive.at("on_rested").get_to(on_rested);
        archive.at("on_spawn").get_to(on_spawn);
        archive.at("on_spell_cast_at").get_to(on_spell_cast_at);
        archive.at("on_user_defined").get_to(on_user_defined);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return true;
}

bool CreatureScripts::serialize(GffBuilderStruct& archive) const
{
    archive.add_field("ScriptAttacked", on_attacked)
        .add_field("ScriptDamaged", on_damaged)
        .add_field("ScriptDeath", on_death)
        .add_field("ScriptDialogue", on_conversation)
        .add_field("ScriptDisturbed", on_disturbed)
        .add_field("ScriptEndRound", on_endround)
        .add_field("ScriptHeartbeat", on_heartbeat)
        .add_field("ScriptOnBlocked", on_blocked)
        .add_field("ScriptOnNotice", on_perceived)
        .add_field("ScriptRested", on_rested)
        .add_field("ScriptSpawn", on_spawn)
        .add_field("ScriptSpellAt", on_spell_cast_at)
        .add_field("ScriptUserDefine", on_user_defined);

    return true;
}

nlohmann::json CreatureScripts::to_json() const
{
    nlohmann::json j;

    j["on_attacked"] = on_attacked;
    j["on_blocked"] = on_blocked;
    j["on_conversation"] = on_conversation;
    j["on_damaged"] = on_damaged;
    j["on_death"] = on_death;
    j["on_disturbed"] = on_disturbed;
    j["on_endround"] = on_endround;
    j["on_heartbeat"] = on_heartbeat;
    j["on_perceived"] = on_perceived;
    j["on_rested"] = on_rested;
    j["on_spawn"] = on_spawn;
    j["on_spell_cast_at"] = on_spell_cast_at;
    j["on_user_defined"] = on_user_defined;

    return j;
}

Creature::Creature()
    : Creature(nw::kernel::global_allocator())
{
}

Creature::Creature(nw::MemoryResource* allocator)
    : ObjectBase(allocator)
    , common(allocator)
    , equipment{this}
    , inventory(6, 6, 10, allocator)
{
    set_handle(ObjectHandle{object_invalid, ObjectType::creature});
    inventory.owner = this;
}

void Creature::clear()
{
    equipment.destroy();
    inventory.destroy();

    instantiated_ = false;
}

bool Creature::instantiate()
{
    if (instantiated_) return true;
    update_appearance(appearance.id);

    instantiated_ = (inventory.instantiate() && equipment.instantiate());
    size_t i = 0;
    for (auto& equip : equipment.equips) {
        if (equip.is<Item*>()) {
            process_item_properties(this, equip.as<Item*>(),
                static_cast<EquipIndex>(i), false);
        }
        ++i;
    }

    nw::kernel::objects().run_instantiate_callback(this);
    return instantiated_;
}

Versus Creature::versus_me() const
{
    Versus result;
    result.align_flags = alignment_flags();
    result.race = race;
    return result;
}

String Creature::get_name_from_file(const std::filesystem::path& path)
{
    LocString l1, l2;
    auto rdata = ResourceData::from_file(path);
    String result;
    if (rdata.bytes.size() <= 8) { return result; }
    if (memcmp(rdata.bytes.data(), "UTC V3.2", 8) == 0) {
        Gff gff(std::move(rdata));
        if (!gff.valid()) { return result; }
        gff.toplevel().get_to("FirstName", l1);
        gff.toplevel().get_to("LastName", l2);
    } else {
        try {
            std::ifstream f{path, std::ifstream::binary};
            nlohmann::json j = nlohmann::json::parse(rdata.bytes.string_view());
            j.at("name_first").get_to(l1);
            j.at("name_last").get_to(l2);
        } catch (...) {
            return result;
        }
    }

    auto fn = nw::kernel::strings().get(l1);
    if (!fn.empty()) {
        result = fn;
        auto ln = nw::kernel::strings().get(l2);
        if (!ln.empty()) {
            result += " " + ln;
        }
    }
    return result;
}

AlignmentFlags Creature::alignment_flags() const noexcept
{
    AlignmentFlags result = AlignmentFlags::none;

    if (good_evil >= 70) {
        result |= AlignmentFlags::good;
    } else if (good_evil <= 30) {
        result |= AlignmentFlags::evil;
    } else {
        result |= AlignmentFlags::neutral;
    }

    if (lawful_chaotic >= 70) {
        result |= AlignmentFlags::lawful;
    } else if (lawful_chaotic <= 30) {
        result |= AlignmentFlags::chaotic;
    } else {
        result |= AlignmentFlags::neutral;
    }

    return result;
}

bool Creature::save(const std::filesystem::path& path, std::string_view format)
{
    bool result = false;
    if (format == "json") {
        nlohmann::json out;
        result = serialize(this, out, SerializationProfile::blueprint);
        if (result) {
            fs::path temp = fs::temp_directory_path() / path.filename();
            std::ofstream of{temp};
            of << std::setw(4) << out;
            of.close();
            result = move_file_safely(temp, path);
        }
    } else if (format == "gff") {
        GffBuilder out{serial_id};
        result = serialize(this, out.top, SerializationProfile::blueprint);
        if (result) {
            out.build();
            result = out.write_to(path);
        }
    } else {
        LOG_F(ERROR, "[objects] invalid format type: {}", format);
    }
    return result;
}

void Creature::update_appearance(Appearance id)
{
    auto app = kernel::rules().appearances.get(id);
    if (!app) { return; }

    size = app->size;
    auto cresize = kernel::twodas().get("creaturesize");
    if (cresize) {
        cresize->get_to(size, "ACATTACKMOD", combat_info.size_ab_modifier);
        cresize->get_to(size, "ACATTACKMOD", combat_info.size_ac_modifier);
    }

    if (walkrate == 7) {
        walkrate = app->walkrate;
    }
}

// == Creature - Serialization - Gff ==========================================
// ============================================================================

bool deserialize(Creature* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    deserialize(obj->common, archive, profile, ObjectType::creature);
    deserialize(obj->appearance, archive);
    deserialize(obj->combat_info, archive);
    deserialize(obj->equipment, archive, profile);
    deserialize(obj->inventory, archive, profile);
    deserialize(obj->levels, archive);
    obj->scripts.deserialize(archive);
    deserialize(obj->stats, archive);

    archive.get_to("Conversation", obj->conversation);
    archive.get_to("Deity", obj->deity);
    archive.get_to("Description", obj->description);
    archive.get_to("FirstName", obj->name_first);
    archive.get_to("LastName", obj->name_last);
    archive.get_to("Subrace", obj->subrace);

    archive.get_to("ChallengeRating", obj->cr);
    archive.get_to("CRAdjust", obj->cr_adjust);
    archive.get_to("DecayTime", obj->decay_time, false);
    archive.get_to("WalkRate", obj->walkrate);

    archive.get_to("FactionID", obj->faction_id);
    archive.get_to("CurrentHitPoints", obj->hp_current);
    archive.get_to("HitPoints", obj->hp);
    archive.get_to("MaxHitPoints", obj->hp_max);
    archive.get_to("SoundSetFile", obj->soundset);

    archive.get_to("BodyBag", obj->bodybag);
    archive.get_to("NoPermDeath", obj->chunk_death);
    archive.get_to("Disarmable", obj->disarmable);
    archive.get_to("Gender", obj->gender);
    archive.get_to("GoodEvil", obj->good_evil);
    archive.get_to("IsImmortal", obj->immortal, false);
    archive.get_to("Interruptable", obj->interruptable);
    archive.get_to("LawfulChaotic", obj->lawful_chaotic);
    archive.get_to("Lootable", obj->lootable, false);
    archive.get_to("IsPC", obj->pc);
    archive.get_to("PerceptionRange", obj->perception_range);
    archive.get_to("Plot", obj->plot);

    uint8_t temp;
    if (archive.get_to("Race", temp)) {
        obj->race = Race::make(temp);
    }

    if (!archive.get_to("xStartingPackage", obj->starting_package, false)) {
        archive.get_to("StartingPackage", obj->starting_package, false);
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

bool serialize(const Creature* obj, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive.add_field("TemplateResRef", obj->common.resref)
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
    serialize(obj->appearance, archive);
    serialize(obj->combat_info, archive);
    serialize(obj->equipment, archive, profile);
    serialize(obj->inventory, archive, profile);
    serialize(obj->levels, archive);
    obj->scripts.serialize(archive);
    serialize(obj->stats, archive);

    archive.add_field("Conversation", obj->conversation);
    archive.add_field("Deity", obj->deity);
    archive.add_field("Description", obj->description);
    archive.add_field("FirstName", obj->name_first);
    archive.add_field("LastName", obj->name_last);
    archive.add_field("Subrace", obj->subrace);

    archive.add_list("TemplateList"); // Not sure if this is obsolete?

    archive.add_field("ChallengeRating", obj->cr)
        .add_field("CRAdjust", obj->cr_adjust)
        .add_field("DecayTime", obj->decay_time)
        .add_field("WalkRate", obj->walkrate);

    archive.add_field("HitPoints", obj->hp)
        .add_field("CurrentHitPoints", obj->hp_current)
        .add_field("MaxHitPoints", obj->hp_max)
        .add_field("FactionID", obj->faction_id)
        .add_field("SoundSetFile", obj->soundset);

    archive.add_field("BodyBag", obj->bodybag)
        .add_field("Disarmable", obj->disarmable)
        .add_field("Gender", obj->gender)
        .add_field("GoodEvil", obj->good_evil)
        .add_field("IsImmortal", obj->immortal)
        .add_field("Interruptable", obj->interruptable)
        .add_field("LawfulChaotic", obj->lawful_chaotic)
        .add_field("Lootable", obj->lootable)
        .add_field("IsPC", obj->pc)
        .add_field("NoPermDeath", obj->chunk_death)
        .add_field("PerceptionRange", obj->perception_range)
        .add_field("Plot", obj->plot)
        .add_field("Race", uint8_t(*obj->race))
        .add_field("StartingPackage", uint8_t(obj->starting_package))
        .add_field("xStartingPackage", obj->starting_package);

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

GffBuilder serialize(const Creature* obj, SerializationProfile profile)
{
    GffBuilder out{Creature::serial_id};
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }
    serialize(obj, out.top, profile);
    out.build();
    return out;
}

// == Creature - Serialization - JSON =========================================
// ============================================================================

bool deserialize(Creature* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    try {
        obj->common.from_json(archive.at("common"), profile, ObjectType::creature);
        obj->appearance.from_json(archive.at("appearance"));
        obj->combat_info.from_json(archive.at("combat_info"));
        obj->equipment.from_json(archive.at("equipment"), profile);
        obj->inventory.from_json(archive.at("inventory"), profile);
        obj->levels.from_json(archive.at("levels"));
        obj->scripts.from_json(archive.at("scripts"));
        obj->stats.from_json(archive.at("stats"));

        archive.at("conversation").get_to(obj->conversation);
        archive.at("description").get_to(obj->description);
        archive.at("name_first").get_to(obj->name_first);
        archive.at("name_last").get_to(obj->name_last);
        archive.at("subrace").get_to(obj->subrace);

        archive.at("cr").get_to(obj->cr);
        archive.at("cr_adjust").get_to(obj->cr_adjust);
        archive.at("decay_time").get_to(obj->decay_time);
        archive.at("walkrate").get_to(obj->walkrate);

        archive.at("faction_id").get_to(obj->faction_id);
        archive.at("hp").get_to(obj->hp);
        archive.at("hp_current").get_to(obj->hp_current);
        archive.at("hp_max").get_to(obj->hp_max);

        archive.at("bodybag").get_to(obj->bodybag);
        archive.at("chunk_death").get_to(obj->chunk_death);
        archive.at("deity").get_to(obj->deity);
        archive.at("disarmable").get_to(obj->disarmable);
        archive.at("gender").get_to(obj->gender);
        archive.at("good_evil").get_to(obj->good_evil);
        archive.at("immortal").get_to(obj->immortal);
        archive.at("interruptable").get_to(obj->interruptable);
        archive.at("lawful_chaotic").get_to(obj->lawful_chaotic);
        archive.at("lootable").get_to(obj->lootable);
        archive.at("pc").get_to(obj->pc);
        archive.at("perception_range").get_to(obj->perception_range);
        archive.at("plot").get_to(obj->plot);
        archive.at("race").get_to(obj->race);
        archive.at("soundset").get_to(obj->soundset);
        archive.at("starting_package").get_to(obj->starting_package);

        if (profile == SerializationProfile::instance) {
            auto it = archive.find("visual_transforms");
            if (it != std::end(archive)) {
                archive.at("visual_transforms").get_to(obj->visual_transform());
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool serialize(const Creature* obj, nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive["$type"] = Creature::serial_id;
    archive["$version"] = Creature::json_archive_version;

    archive["appearance"] = obj->appearance.to_json();
    archive["combat_info"] = obj->combat_info.to_json();
    archive["common"] = obj->common.to_json(profile, ObjectType::creature);
    archive["equipment"] = obj->equipment.to_json(profile);
    archive["inventory"] = obj->inventory.to_json(profile);
    archive["levels"] = obj->levels.to_json();
    archive["scripts"] = obj->scripts.to_json();
    archive["stats"] = obj->stats.to_json();

    archive["bodybag"] = obj->bodybag;
    archive["chunk_death"] = obj->chunk_death;
    archive["conversation"] = obj->conversation;
    archive["cr_adjust"] = obj->cr_adjust;
    archive["cr"] = obj->cr;
    archive["decay_time"] = obj->decay_time;
    archive["deity"] = obj->deity;
    archive["description"] = obj->description;
    archive["disarmable"] = obj->disarmable;
    archive["faction_id"] = obj->faction_id;
    archive["gender"] = obj->gender;
    archive["good_evil"] = obj->good_evil;
    archive["hp_current"] = obj->hp_current;
    archive["hp_max"] = obj->hp_max;
    archive["hp"] = obj->hp;
    archive["immortal"] = obj->immortal;
    archive["interruptable"] = obj->interruptable;
    archive["lawful_chaotic"] = obj->lawful_chaotic;
    archive["lootable"] = obj->lootable;
    archive["name_first"] = obj->name_first;
    archive["name_last"] = obj->name_last;
    archive["pc"] = obj->pc;
    archive["perception_range"] = obj->perception_range;
    archive["plot"] = obj->plot;
    archive["race"] = obj->race;
    archive["soundset"] = obj->soundset;
    archive["starting_package"] = obj->starting_package;
    archive["subrace"] = obj->subrace;
    archive["walkrate"] = obj->walkrate;

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

} // namespace nw
