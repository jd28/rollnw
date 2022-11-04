objects
=======

rollNW is all about live objects and *not* serialized file formats.

definitions
-----------

:cpp:enum:`ObjectID`
   Unlike NWN an ObjectID does not provide a one-to-one mapping to an object.  Rather,
   it's an index into a structure containing objects.

:cpp:struct:`ObjectHandle`
   An object handle maps to a specific object it consists of an ObjectID, the objects type,
   and an unsigned 16 bit integer indicating the object's version.  To be valid, an object
   handle must match what is in the object array.

:cpp:struct:`ObjectBase`
   The base class of all objects

kernel service
--------------

Any object that is loaded via the Objects service, belongs to the service and must be deleted through it.

**Example - Loading and Deleting a Creature**

.. tabs::

   .. code-tab:: python

      import rollnw

      rollnw.kernel.start()
      obj = rollnw.kernel.objects().creature('nw_chicken.utc')
      rollnw.kernel.objects().destroy(obj.handle())
      # After this point accessing obj is undefined behavior and its handle if stored elsewhere
      # will no longer be valid

   .. code-tab:: cpp

      auto obj = nw::kernel::objects().load<nw::Creature>(fs::path("a/path/to/nw_chicken.utc"));
      nw::kernel::objects().destroy(obj->handle());
      // After this point accessing obj is undefined behavior and its handle if stored elsewhere
      // will no longer be valid

-------------------------------------------------------------------------------

area
----

creature
--------

.. tabs::

   .. code-tab:: python

      from rollnw import Creature

      # The file can also be rollnw JSON format, it doesn't matter.
      cre = Creature.from_file("a/path/to/nw_chicken.utc")
      if cre.scripts.on_attacked == "nw_c2_default5":
         cre.scripts.on_attacked = "nw_shakenbake"

   .. code-tab:: cpp

      // TODO

door
----

encounter
--------

item
----

module
------

placeable
---------

sound
-----

store
-----

trigger
-------

waypoint
--------
