using
=====

While the library is far from done, basic usage would be as follows.

.. tabs::

    .. tab:: Python

        .. code:: bash

            pip install rollnw

        .. code:: python

            import rollnw

            rollnw.kernel.start()
            mod = rollnw.kernel.load_module("mymodule.mod")
            for area in mod:
                # Do neat things

    .. tab:: C++

        .. code:: cpp

            #include <nw/kernel/Kernel.hpp>
            #include <nw/log.hpp>
            #include <nwn1/Profile.hpp>

            int main(int argc, char* argv[])
            {
                // Initialize logger
                nw::init_logger(argc, argv);

                // Say this application is specific to 1.69.
                // This must be set before the initialize call below.  The default is NWN:EE, so in that case,
                // ``set_version`` need not be called
                nw::kernel::config().set_version(GameVersion::v1_69);

                // Sets config for the system, paths, version, etc.
                nw::kernel::config().initialize();

                // Initializes all systems
                nw::kernel::services().start();

                // Loads a game profile, in this case something vaguely like NWN1
                nw::kernel::load_profile(new nwn1::Profile);

                auto mod = nw::kernel::load_module("path/to/modules/your_module.mod");

                // Do neat stuff

                nw::kernel::unload_module();

                return 0;
            }
