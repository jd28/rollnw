#pragma once

#include "../../serialization/Serialization.hpp"

namespace nw {
struct Creature;
struct Door;
struct Encounter;
struct GffBuilderStruct;
struct GffStruct;
struct Item;
struct Placeable;
struct Sound;
struct Store;
struct Trigger;
struct Waypoint;
} // namespace nw

namespace nw::smalls {
struct Runtime;
} // namespace nw::smalls

namespace nwn1 {

void import_creature_propsets_from_gff(nw::smalls::Runtime* rt, nw::Creature* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile);
void import_item_propsets_from_gff(nw::smalls::Runtime* rt, nw::Item* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile);
void import_door_propsets_from_gff(nw::smalls::Runtime* rt, nw::Door* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile);
void import_encounter_propsets_from_gff(nw::smalls::Runtime* rt, nw::Encounter* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile);
void import_placeable_propsets_from_gff(nw::smalls::Runtime* rt, nw::Placeable* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile);
void import_sound_propsets_from_gff(nw::smalls::Runtime* rt, nw::Sound* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile);
void import_store_propsets_from_gff(nw::smalls::Runtime* rt, nw::Store* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile);
void import_trigger_propsets_from_gff(nw::smalls::Runtime* rt, nw::Trigger* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile);
void import_waypoint_propsets_from_gff(nw::smalls::Runtime* rt, nw::Waypoint* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile);

void export_creature_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Creature* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile);
void export_item_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Item* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile);
void export_door_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Door* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile);
void export_encounter_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Encounter* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile);
void export_placeable_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Placeable* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile);
void export_sound_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Sound* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile);
void export_store_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Store* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile);
void export_trigger_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Trigger* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile);
void export_waypoint_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Waypoint* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile);

} // namespace nwn1
