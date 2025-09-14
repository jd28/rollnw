#pragma once

#include "../rules/Versus.hpp"
#include "../rules/effects.hpp"
#include "ObjectHandle.hpp"
#include "VisualTransform.hpp"

namespace nw {

// -- ObjectBase --------------------------------------------------------------
//-----------------------------------------------------------------------------

struct Area;
struct Common;
struct Creature;
struct Door;
struct Encounter;
struct Item;
struct Module;
struct Placeable;
struct Player;
struct Sound;
struct Store;
struct Trigger;
struct Waypoint;

struct ObjectBase {
    ObjectBase(nw::MemoryResource* allocator = nw::kernel::global_allocator());
    virtual ~ObjectBase() { }

    nw::MemoryResource* allocator() const noexcept { return allocator_; };
    ObjectHandle handle() const noexcept { return handle_; }
    void set_handle(ObjectHandle handle) { handle_ = handle; }
    const EffectArray& effects() const;
    EffectArray& effects();
    Vector<VisualTransform>& visual_transform() noexcept { return visual_transform_; }
    const Vector<VisualTransform>& visual_transform() const noexcept { return visual_transform_; }
    void add_visual_transform(VisualTransform value);

    virtual void clear();
    virtual bool instantiate() = 0;
    virtual InternedString tag() const;
    virtual Versus versus_me() const { return Versus{}; }

    virtual Area* as_area() { return nullptr; }
    virtual const Area* as_area() const { return nullptr; }
    virtual Common* as_common() { return nullptr; }
    virtual const Common* as_common() const { return nullptr; }
    virtual Creature* as_creature() { return nullptr; }
    virtual const Creature* as_creature() const { return nullptr; }
    virtual Door* as_door() { return nullptr; }
    virtual const Door* as_door() const { return nullptr; }
    virtual Encounter* as_encounter() { return nullptr; }
    virtual const Encounter* as_encounter() const { return nullptr; }
    virtual Item* as_item() { return nullptr; }
    virtual const Item* as_item() const { return nullptr; }
    virtual Module* as_module() { return nullptr; }
    virtual const Module* as_module() const { return nullptr; }
    virtual Placeable* as_placeable() { return nullptr; }
    virtual const Placeable* as_placeable() const { return nullptr; }
    virtual Player* as_player() { return nullptr; }
    virtual const Player* as_player() const { return nullptr; }
    virtual Sound* as_sound() { return nullptr; }
    virtual const Sound* as_sound() const { return nullptr; }
    virtual Store* as_store() { return nullptr; }
    virtual const Store* as_store() const { return nullptr; }
    virtual Trigger* as_trigger() { return nullptr; }
    virtual const Trigger* as_trigger() const { return nullptr; }
    virtual Waypoint* as_waypoint() { return nullptr; }
    virtual const Waypoint* as_waypoint() const { return nullptr; }

private:
    nw::MemoryResource* allocator_;
    ObjectHandle handle_;
    EffectArray effects_;
    Vector<VisualTransform> visual_transform_;
};

} // namespace nw
