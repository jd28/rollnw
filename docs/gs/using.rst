using
=====

While the library is far from done, basic usage would be as follows.

.. warning::

    These examples show the stable kernel/module-loading cross section.
    The broader project is moving toward infrastructure for an authored
    RPG toolset/game. Graphics validation and tool-facing rendering live
    around ``nw::gfx``, ``lib/nw/render``, and ``mudl``, while rule/script
    authoring is moving toward Smalls. Networking and runtime services
    are part of the long-term foundation. Treat older examples as
    orientation rather than an API stability promise.

.. code:: cpp

    #include <nw/kernel/Kernel.hpp>
    #include <nw/log.hpp>

    int main(int argc, char* argv[])
    {
        // Initialize logger
        nw::init_logger(argc, argv);

        // Say this application is specific to 1.69.
        // This must be set before the initialize call below. The default is NWN:EE, so in that case,
        // ``set_version`` need not be called. This also controls which profile is loaded.
        nw::kernel::config().set_version(GameVersion::v1_69);

        // Sets config for the system, paths, version, etc.
        nw::kernel::config().initialize();

        // Initializes all systems
        nw::kernel::services().start();

        auto mod = nw::kernel::load_module("path/to/modules/your_module.mod");

        // Do neat stuff

        nw::kernel::unload_module();

        return 0;
    }
