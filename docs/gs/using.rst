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

                // Sets config for the system, paths, version, etc.
                auto info = nw::probe_nwn_install();
                nw::kernel::config().initialize({
                    info.version,
                    info.install,
                    info.user,
                });

                // Initializes all systems
                nw::kernel::services().start();

                // Loads a game profile, in this case something vaguely like NWN1
                nw::kernel::load_profile(new nwn1::Profile);

                auto mod = nw::kernel::load_module("path/to/modules/your_module.mod");

                // Do neat stuff

                nw::kernel::unload_module();

                return 0;
            }
