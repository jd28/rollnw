resources
=========

kernel service
--------------

The resource services provides access the file system and resources stored in NWN container files.
The main goals mostly satisfied: the ability to read all NWN(:EE) containers. The ability to
add new container types is limited in utility due to `NWNX:EE <https://github.com/nwnxee/unified>`__'s
lack of a ResMan plugin, even so the ability to load files from a Zip file is included.

**Example - Demanding a resource from resman**

.. tabs::

    .. code-tab:: python

        import rollnw
        from rollnw.kernel import resman

        rollnw.kernel.start()
        assert resman().contains('nw_chicken.utc')
        data = resman().demand('nw_chicken.utc')
        data2 = resman().demand(rollnw.Resource('nw_chicken', rollnw.ResourceType.utc))
        assert data == data2


    .. code-tab:: cpp

        // TODO

-------------------------------------------------------------------------------

containers
----------

Directory
~~~~~~~~~

**Status**: Read, Write (file format dependant)


Erf
~~~

**Status**: Read

**Example - Load an Erf and Print Contents**

.. tabs::

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

    .. code-tab:: python

        import rollnw

        erf = rollnw.Erf("tests/test_data/user/hak/hak_with_description.hak")
        print(erf.name(), erf.size())
        for rd in erf.all():
            print(rd.name.filename(), rd.size)


Key/Bif
~~~~~~~

**Status**: Read

NWSync
~~~~~~

**Status**: Read

**Example - Loading and Reading From NWSync Manifest**

.. tabs::

    .. code-tab:: cpp

        #include <nw/resources/NWSync.hpp>
        #include <nw/kernel/Kernel.hpp>

        auto path = nw::kernel::config().alias_path(nw::PathAlias::nwsync);
        auto n = nw::NWSync(path);
        if(!n.is_loaded()) {
            throw std::runtime_error("a fit");
        }

        auto manifests = n.manifests();
        if (manifests.size() > 0) {
            auto manifest = n.get(manifests[0]);
            auto resource = manifest->all();
            if(resource.size() > 0) {
                // Extract the first resource found
                manifest->extract(std::regex(resource[0].name.filename()), "tmp/");
            }
        }

    .. code-tab:: python

        # TODO

Zip
~~~

**Status**: Read
