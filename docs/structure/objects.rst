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

.. code-block:: c++

   auto obj = nw::kernel::objects().load<nw::Creature>(fs::path("a/path/to/nw_chicken.utc"));
   nw::kernel::objects().destroy(obj->handle());
   // After this point accessing obj is undefined behavior and its handle if stored elsewhere
   // will no longer be valid

-------------------------------------------------------------------------------

area
----

creature
--------

.. code-block:: c++

   // TODO

door
----

encounter
---------

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
