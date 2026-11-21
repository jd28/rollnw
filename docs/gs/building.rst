building
========

rollnw uses cmake as its build system and more specifically
`CMakePresets.json <https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html>`__.

To build the library, all one needs to do is use the following cmake commands.  This example
also builds tests which are not enabled by default.

.. tabs::

   .. tab:: linux

      .. code:: bash

         $ cd path/to/rollnw
         $ cmake --preset linux -DROLLNW_BUILD_TESTS=ON
         $ cmake --build --preset default
         $ ctest --preset default

   .. tab:: macOS

      .. note::

         The deployment target is currently set to 12.

      .. code:: bash

         $ cd path/to/rollnw
         $ cmake --preset macos -DROLLNW_BUILD_TESTS=ON
         $ cmake --build --preset default
         $ ctest --preset default

   .. tab:: windows

      The main windows builds are for Visual Studio 2022 (Community Edition), but mingw-64,
      later versions of Visual Studio will added.

      For now it's probably easiest to open the x64 Visual Studio Developer Command Prompt.
      If you have ninja installed, it's highly recommended to use the ``windows-ninja`` configuration
      preset.

      .. code:: bash

         $ cd path/to/rollnw
         $ cmake --preset windows -DROLLNW_BUILD_TESTS=ON
         $ cmake --build --preset default
         $ ctest --preset default

The test suite **requires** a Neverwinter Nights installation. This repository
distributes no game assets, so the tests read game data from an install rather
than from a copy checked in here, and the suite refuses to start without one.

Dedicated-server data is sufficient for everything except renderer and asset
work, and ``scripts/ci/ensure_nwn.sh`` fetches it — this is what CI uses:

.. code:: bash

   $ ./scripts/ci/ensure_nwn.sh          # downloads to ./nwn
   $ NWN_ROOT=./nwn ctest --preset default

``NWN_ROOT`` may point at any installation containing ``data/nwn_base.key``:
dedicated server, GOG, Steam or Beamdog client. Note that the dedicated-server
data *indexes* models, textures and PLT layers but ships zero bytes for them,
so tests needing those assets skip unless ``NWN_ROOT`` is a full game install.

Export the following if your install is in a non-standard location, or to point
at a full install:

.. tabs::

   .. tab:: Linux / MacOS

      .. code:: bash

         $ export NWN_ROOT=path/to/game
         $ export NWN_HOME=path/to/nwn-user-dir

   .. tab:: Windows

      .. code:: batch

         set "NWN_ROOT=C:\GOG Games\Neverwinter Nights Enhanced Edition"
         set "NWN_HOME=C:\Users\m\OneDrive\MyDocuments\Neverwinter Nights"

``NWN_HOME`` is the game user-data directory itself (containing folders such as
``modules``, ``hak``, and ``override``), not its Documents parent. Use your actual
path; valid overrides take precedence over automatic discovery. Windows discovery
reads GOG's registered installation path and the current Windows Documents known
folder, including OneDrive redirection. Restart an already running application
after changing its environment variables.
