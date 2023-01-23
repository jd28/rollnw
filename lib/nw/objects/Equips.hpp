#pragma once

#include "../serialization/Archives.hpp"

#include <memory>
#include <variant>

namespace nw {

struct Creature;
struct Item;

enum struct EquipSlot {
    head = (1 << 0),
    chest = (1 << 1),
    boots = (1 << 2),
    arms = (1 << 3),
    righthand = (1 << 4),
    lefthand = (1 << 5),
    cloak = (1 << 6),
    leftring = (1 << 7),
    rightring = (1 << 8),
    neck = (1 << 9),
    belt = (1 << 10),
    arrows = (1 << 11),
    bullets = (1 << 12),
    bolts = (1 << 13),
    creature_left = (1 << 14),
    creature_right = (1 << 15),
    creature_bite = (1 << 16),
    creature_skin = (1 << 17),
};

enum struct EquipIndex : uint32_t {
    head = 0,
    chest = 1,
    boots = 2,
    arms = 3,
    righthand = 4,
    lefthand = 5,
    cloak = 6,
    leftring = 7,
    rightring = 8,
    neck = 9,
    belt = 10,
    arrows = 11,
    bullets = 12,
    bolts = 13,
    creature_left = 14,
    creature_right = 15,
    creature_bite = 16,
    creature_skin = 17,

    invalid = 0xffffffff
};

constexpr std::string_view equip_index_to_string(EquipIndex idx)
{
    switch (idx) {
    default:
        return "EQUIP_INVALID";
    case EquipIndex::head:
        return "head";
    case EquipIndex::chest:
        return "chest";
    case EquipIndex::boots:
        return "boots";
    case EquipIndex::arms:
        return "arms";
    case EquipIndex::righthand:
        return "righthand";
    case EquipIndex::lefthand:
        return "lefthand";
    case EquipIndex::cloak:
        return "cloak";
    case EquipIndex::leftring:
        return "leftring";
    case EquipIndex::rightring:
        return "rightring";
    case EquipIndex::neck:
        return "neck";
    case EquipIndex::belt:
        return "belt";
    case EquipIndex::arrows:
        return "arrows";
    case EquipIndex::bullets:
        return "bullets";
    case EquipIndex::bolts:
        return "bolts";
    case EquipIndex::creature_left:
        return "creature_left";
    case EquipIndex::creature_right:
        return "creature_right";
    case EquipIndex::creature_bite:
        return "creature_bite";
    case EquipIndex::creature_skin:
        return "creature_skin";
    }
}

constexpr EquipIndex equip_slot_to_index(EquipSlot slot)
{
    switch (slot) {
    default:
        return EquipIndex::invalid;
    case EquipSlot::head:
        return EquipIndex::head;
    case EquipSlot::chest:
        return EquipIndex::chest;
    case EquipSlot::boots:
        return EquipIndex::boots;
    case EquipSlot::arms:
        return EquipIndex::arms;
    case EquipSlot::righthand:
        return EquipIndex::righthand;
    case EquipSlot::lefthand:
        return EquipIndex::lefthand;
    case EquipSlot::cloak:
        return EquipIndex::cloak;
    case EquipSlot::leftring:
        return EquipIndex::leftring;
    case EquipSlot::rightring:
        return EquipIndex::rightring;
    case EquipSlot::neck:
        return EquipIndex::neck;
    case EquipSlot::belt:
        return EquipIndex::belt;
    case EquipSlot::arrows:
        return EquipIndex::arrows;
    case EquipSlot::bullets:
        return EquipIndex::bullets;
    case EquipSlot::bolts:
        return EquipIndex::bolts;
    case EquipSlot::creature_left:
        return EquipIndex::creature_left;
    case EquipSlot::creature_right:
        return EquipIndex::creature_right;
    case EquipSlot::creature_bite:
        return EquipIndex::creature_bite;
    case EquipSlot::creature_skin:
        return EquipIndex::creature_skin;
    }
}

using EquipItem = std::variant<Resref, Item*>;

struct Equips {
    Equips(Creature* owner);
    Equips(const Equips&) = delete;
    Equips(Equips&&) = default;
    Equips& operator=(const Equips&) = delete;
    Equips& operator=(Equips&&) = default;
    ~Equips() = default;

    bool instantiate();
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    nlohmann::json to_json(SerializationProfile profile) const;

    nw::Creature* owner_ = nullptr;
    std::array<EquipItem, 18> equips;
};

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Equips& self, const GffStruct& archive, SerializationProfile profile);
bool serialize(const Equips& self, GffBuilderStruct& archive, SerializationProfile profile);
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
