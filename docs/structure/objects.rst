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

Object Componenents
-------------------

All entities representing an object has a component that both stores
state and serves as a tag to determine type. Some objects also have
other components specific to their type.

-  ``Area``

   -  ``Common``
   -  ``AreaScripts``
   -  ``AreaWeather``

-  ``Creature``

   -  ``Common``
   -  ``Appearance``
   -  ``CombatInfo``
   -  ``CreatureScripts``
   -  ``CreatureStats``
   -  ``Equips``
   -  ``Inventory``
   -  ``LevelStats``

-  ``Door``

   -  ``Common``
   -  ``Lock``
   -  ``DoorScripts``
   -  ``Trap``

-  ``Encounter``

   -  ``Common``
   -  ``EncounterScripts``

-  ``Item``

   -  ``Common``
   -  ``Inventory``

-  ``Module``

   -  ``Common``
   -  ``ModuleScripts``

-  ``Placeable``

   -  ``Common``
   -  ``Inventory``
   -  ``Lock``
   -  ``PlaceableScripts``
   -  ``Trap``

-  ``Sound``

   -  ``Common``

-  ``Store``

   -  ``Common``
   -  ``StoreInventory``
   -  ``StoreScripts``

-  ``Trigger``

   -  ``Common``
   -  ``Trap``
   -  ``TriggerScripts``

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
