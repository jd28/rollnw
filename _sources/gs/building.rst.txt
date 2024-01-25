building
========

rollnw uses cmake as its build system and more specifically
`CMakePresets.json <https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html>`__.

Install `vcpkg <https://github.com/microsoft/vcpkg>`__. All vcpkg packages required will be built
automatically.  Just export the vcpkg root path in an ENV var:

.. tabs::

   .. tab:: Linux / MacOS

      .. code:: bash

         $ export VCPKG_ROOT=path/to/vcpkg

   .. tab:: Linux / MacOS

      .. code:: batch

         set VCPKG_ROOT=C:\path\to\vcpkg

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

         The deployment target is currently set to 10.15.  Only x86_64 builds are supported
         due to vcpkg.

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

In order to run all of the tests, you can help the library locate Neverwinter Nights installation
 paths by exporting the following ENV vars, if your install is in a non-standard location:

.. tabs::

   .. tab:: Linux / MacOS

      .. code:: bash

         $ export NWN_ROOT=path/to/game
         $ export NWN_USER=path/to/nwn-user-dir

   .. tab:: Linux / MacOS

      .. code:: batch

         set NWN_ROOT=path\to\game
         set NWN_USER=path\to\nwn-user-dir
