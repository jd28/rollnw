objects
=======

The System
----------

The object system is largely handled by the Entity-Component-System
`flecs <https://github.com/SanderMertens/flecs>`__. This is a departure
from the architecture of NWN which use a more traditional inheritance
based approach. The main advantage is that entities can be treated maps
the keys to which are types and the values are instantiations of those
types. The performance implications have not been considered.

Object Components
-----------------

All entities representing an object has a component that both stores
state and serves as a tag to determine type. Some objects also have
other components specific to their type.

-  :cpp:struct:`Area`

   -  :cpp:struct:`nw::Common`
   -  :cpp:struct:`AreaScripts`
   -  :cpp:struct:`AreaWeather`

-  :cpp:struct:`Creature`

   -  :cpp:struct:`nw::Common`
   -  :cpp:struct:`Appearance`
   -  :cpp:struct:`CombatInfo`
   -  :cpp:struct:`CreatureScripts`
   -  :cpp:struct:`CreatureStats`
   -  :cpp:struct:`Equips`
   -  :cpp:struct:`Inventory`
   -  :cpp:struct:`LevelStats`

-  :cpp:struct:`Door`

   -  :cpp:struct:`nw::Common`
   -  :cpp:struct:`Lock`
   -  :cpp:struct:`DoorScripts`
   -  :cpp:struct:`Trap`

-  :cpp:struct:`Encounter`

   -  :cpp:struct:`nw::Common`
   -  :cpp:struct:`EncounterScripts`

-  :cpp:struct:`Item`

   -  :cpp:struct:`nw::Common`
   -  :cpp:struct:`Inventory`

-  :cpp:struct:`Module`

   -  :cpp:struct:`nw::Common`
   -  :cpp:struct:`ModuleScripts`

-  :cpp:struct:`Placeable`

   -  :cpp:struct:`nw::Common`
   -  :cpp:struct:`Inventory`
   -  :cpp:struct:`Lock`
   -  :cpp:struct:`PlaceableScripts`
   -  :cpp:struct:`Trap`

-  :cpp:struct:`Sound`

   -  :cpp:struct:`nw::Common`

-  :cpp:struct:`Store`

   -  :cpp:struct:`nw::Common`
   -  :cpp:struct:`StoreInventory`
   -  :cpp:struct:`StoreScripts`

-  :cpp:struct:`Trigger`

   -  :cpp:struct:`nw::Common`
   -  :cpp:struct:`Trap`
   -  :cpp:struct:`TriggerScripts`

In some cases an entity could have multiple object components, e.g. both
a ``Player`` and a ``Creature``.

**Example**:

.. code:: cpp

   if(entity.has<Store>()) {
       // This entity is a store.
   } else if(auto it = entity.get<Item>()) {
       // This entity is an item.
   }

**Example**:

.. code:: cpp

   if(auto common = entity.get<Common>()) {
       std::cout << "This is my resref: " << common->resref << std::endl;
   }
