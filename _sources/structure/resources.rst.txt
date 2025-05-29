resources
=========

The resources module provides access to the file system and resources stored in NWN container files.

:cpp:struct:`nw::Resref`
--------------------------

Unlike Neverwinter Nights 1 or 2 a resref is not a fixed character array. It's simply a wrapper around an interned string.
The constraint on its length is 4096.

This is not exposed to Python, in Python a resref maps directly to a ``str``, i.e. "nw_chicken".

:cpp:struct:`nw::ResourceType`
------------------------------

ResourceType has only one difference from NWN(:EE)'s implementation: it contains some categories, i.e. ``nw::ResourceType::texture``
that aligns with the available image types.

**Example - Checking a ResourceType category**

.. code:: cpp

    if(nw::ResourceType::check_category(nw::ResourceType::texture, some_res_type)) {
        // Proceed with some image resource type agnostic function.
    }


:cpp:struct:`nw::Resource`
--------------------------

Resources are referred to by a :cpp:struct:`nw::Resource`, which is just a
:cpp:struct:`nw::Resref` and :cpp:struct:`nw::ResourceType` pair.

This is not exposed to Python, in Python one can referrer to an asset as a filename, i.e. "nw_chicken.utc".

-------------------------------------------------------------------------------

:cpp:struct:`nw::Container`
---------------------------

Containers in rollnw are designed around the performance of the resource manager and are not for direct use. Classes that inherit the ``nw::Container`` interface are expected to be thread-safe.

The built in containers ``nw::StaticDirectory``, ``nw::StaticErf``, ``nw::StaticKey``, and ``nw::StaticZip`` are all static and immutable and after construction proceed with the assumption that the underlying files **do not** change. They are not exposed to Python.

Support for nwsync was removed since it is not applicable to module/persistant world development, nor do I see it as having any
'future tense', i.e., a hypothetical NWN3 *would not* use nwsync.

The ability to add new container types is limited in utility due to `NWNX:EE <https://github.com/nwnxee/unified>`__'s lack
of a ResMan plugin.

There is experimental support for a ``package.json`` file (currently empty) that will serve to signal to containers that
they are 'modern' containers. So, in the case of ``nw::StaticDirectory`` if ``package.json`` is present at the toplevel then
path is preseved, i,e::

    ├── hak_with_package_json
    │   ├── package.json
    │   └── test
    │       └── cloth028.UTI

one resulting resref in this container is ``hak_with_package_json/test/cloth028``. This is ultimately the future of parts of the system
that needn't be compatible with NWN(:EE).

-------------------------------------------------------------------------------

:cpp:struct:`nw::Erf`
---------------------

A dynamic implementation of the Erf file format, which is also exposed to Python:

**Example - Load an Erf and Print Contents**

.. tabs::

    .. code-tab:: python

        import rollnw

        erf = rollnw.Erf("tests/test_data/user/hak/hak_with_description.hak")
        print(erf.name(), erf.size())
        for rd in erf.all():
            print(rd.name.filename(), rd.size)

    .. code-tab:: cpp

        #include <nw/resources/Erf.hpp>
        // ...
        Erf e("MyModule.mod");
        if (e.valid()) {
            std::cout << fmt::format("{} has {} resources", e.name(), e.size()) << "\n";
            for (const auto& rd : e.all()) {
                std::cout << fmt::format("File: {}, Size: {}", rd.name.filename(), rd.size) << "\n";
            }
        }



:cpp:struct:`nw::ResourceManager`
---------------------------------

The global resource mananger is available by calling ``nw::kernel::resman()``.

**Example - Demanding a resource from resman**

.. tabs::

    .. code-tab:: python

        import rollnw

        rollnw.kernel.start()
        assert rollnw.kernel.resman().contains('nw_chicken.utc')
        data = rollnw.kernel.resman().demand('nw_chicken.utc')
        assert len(data.bytes)


    .. code-tab:: cpp

        nw::kernel::start();
        // Assumes that NWN root directory was found.
        if (nw::kernel::resman().contains({"nw_chicken"sv, nw::ResourceType::utc})) {
            auto utc = nw::kernel::resman().demand({"nw_chicken"sv, nw::ResourceType::utc});
            // Do something with this chicken.
        }

-------------------------------------------------------------------------------
