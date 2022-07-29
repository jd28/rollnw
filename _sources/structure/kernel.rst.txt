kernel
======

The ``kernel`` module provides submodules for handling global resources
and services. It’s designed around some explicit goals:

-  Every service should be easily overrideable to allow for `parallel
   implementation <http://sevangelatos.com/john-carmack-on-parallel-implementations/>`__.
-  Every service should be decoupled from the kernel itself.
-  Any function or object that can modify global state must be contained
   in this module for easy search/grepability.

Services
--------

Config
~~~~~~

The ``Config`` service provides access to installation info, path
aliases, Ini and Toml settings.

**Example**

.. code:: cpp

    if (config.options().version == nw::GameVersion::vEE) {
        REQUIRE_FALSE(config.settings_tml().empty());
        REQUIRE(*config.settings_tml()["game"]["gore"].as<int64_t>() == 1);
    }

-------------------------------------------------------------------------------

Objects
~~~~~~~

rollNW is all about live objects and *not* serialized file formats.

**Definitions**

:cpp:enum:`ObjectID`
   Unlike NWN an ObjectID does not provide a one-to-one mapping to an object.  Rather,
   it's an index into a structure containing objects.

:cpp:struct:`ObjectHandle`
   An object handle maps to a specific object it consists of an ObjectID, the objects type,
   and an unsigned 16 bit integer indicating the object's version.  To be valid, an object
   handle must match what is in the object array.

:cpp:struct:`ObjectBase`
   The base class of all objects

**Example**

.. code:: cpp

     // Loading an object into the system directly from the file system.
     auto obj = nw::kernel::objects().load<nw::Creature>(fs::path("a/path/to/nw_chicken.utc"));
     if(ent->scripts.on_attacked == "nw_c2_default5") {
         ent->scripts.on_attacked = "nw_shakenbake";
     }

-------------------------------------------------------------------------------

Resources
~~~~~~~~~

The resource services provides access the file system and resources stored in NWN container files.
The main goals mostly satisfied: the ability to read all NWN(:EE) containers. The ability to
add new container types is limited in utility due to `NWNX:EE <https://github.com/nwnxee/unified>`__'s
lack of a ResMan plugin, even so the ability to load files from a Zip file is included.

+------------------+---------+------------------+------------------+
| Type             | Read    | Write            | In Situ          |
|                  |         |                  | Modifications    |
+==================+=========+==================+==================+
| Key/Bif          | Yes     | Never            | Never            |
+------------------+---------+------------------+------------------+
| ERF              | Yes     | Maybe            | Maybe            |
+------------------+---------+------------------+------------------+
| NWSync           | Yes     | Never            | Never            |
+------------------+---------+------------------+------------------+
| NWSync Server    | Not Yet | Never            | Never            |
| Directory        |         |                  |                  |
+------------------+---------+------------------+------------------+
| Directories      | Yes     | Yes (file format | N/A              |
|                  |         | dependant)       |                  |
+------------------+---------+------------------+------------------+
| Zip              | Yes     | Maybe            | Maybe            |
+------------------+---------+------------------+------------------+


Rules
~~~~~

See `rules <https://jd28.github.io/rollnw/structure/rules.html>`__ sfor the module
supporting this service.

-------------------------------------------------------------------------------

Strings
~~~~~~~

Provides access to dialog/custom TLKs and localized strings. It also provides a
mechanism for interning commonly used strings.

See `i18n <https://jd28.github.io/rollnw/structure/i18n.html>`__ for the module
supporting this service.

**Example**

.. code:: cpp

    auto str = nw::kernel::strings().intern("This is a Test");
    if(str == "This is a Test") {
       // This will occur
    }
