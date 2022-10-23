building
========

rollNW uses cmake as its build system and more specifically
`CMakePresets.json <https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html>`__.

Install `vcpkg <https://github.com/microsoft/vcpkg>`__. All vcpkg packages required will be built
automatically.  Just export the vcpkg root path in an ENV var:

.. code:: bash

   $ export VCPKG_ROOT=path/to/vcpkg


In order to run all of the tests, you can help the library locate Neverwinter Nights installation
 paths by exporting the following ENV vars:

.. code:: bash

   $ export NWN_ROOT=path/to/game
   $ export NWN_USER=path/to/nwn-user-dir

.. tabs::

   .. tab:: linux

      .. code:: bash

         $ cd path/to/rollnw
         $ cmake --preset default-test
         $ cmake --build --preset default
         $ ctest --preset default

   .. tab:: macOS

      .. note::

         The deployment target is currently set to 10.15.  Only x86_64 builds are supported
         due to vcpkg.

      .. code:: bash

         $ cd path/to/rollnw
         $ cmake --preset macos
         $ cmake --build --preset default
         $ ctest --preset default

   .. tab:: windows

      The windows build currently only has a preset for Visual Studio 2019 (Community Edition), but mingw-64,
      later versions of Visual Studio will added.  For now it's probably easiest to open the Visual Studio
      Developer Command Prompt.

      .. code:: bash

         $ cd path/to/rollnw
         $ cmake --preset windows
         $ cmake --build --preset default
         $ ctest --preset default
