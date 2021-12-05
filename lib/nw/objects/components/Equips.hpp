#pragma once

#include "../../serialization/Serialization.hpp"

#include <memory>
#include <variant>

namespace nw {

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

enum struct EquipIndex {
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
};

using EquipItem = std::variant<Resref, std::unique_ptr<Item>>;

struct Equips {
    Equips(const GffStruct gff, SerializationProfile profile);

    EquipItem head;
    EquipItem chest;
    EquipItem boots;
    EquipItem arms;
    EquipItem righthand;
    EquipItem lefthand;
    EquipItem cloak;
    EquipItem leftring;
    EquipItem rightring;
    EquipItem neck;
    EquipItem belt;
    EquipItem arrows;
    EquipItem bullets;
    EquipItem bolts;
    EquipItem creature_left;
    EquipItem creature_right;
    EquipItem creature_bite;
    EquipItem creature_skin;
};

} // namespace nw
