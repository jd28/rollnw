|License: MIT| |linux| |macos| |windows| |Language grade: C/C++|
|CodeQL| |codecov|

libnw
=====

::

   We shall not cease from exploration
   And the end of all our exploring
   Will be to arrive where we started
   And know the place for the first time.
      --T.S. Eliot, Little Gidding, The Four Quartets.

libnw is a simple modern static C++ library for Neverwinter Nights (and
some Enhanced Edition) file formats and objects, that

-  aims to implement an RPG engine inspired by NWN, excluding graphics
   and networking.
-  focuses on usage, instead doing things the Aurora Engine Way.
-  follows `utf8 everywhere <https://utf8everywhere.org/>`__.
-  hews as close to `C++ Core
   Guidelines <https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines>`__
   as possible.
-  aims to be as easily bindable as possible to other languages. I.e.
   only library specific or STL types at API boundaries.

The library will be renamed as there was already an Open Knights project
named
`libnw <https://sourceforge.net/projects/openknights/files/libnw/>`__.

-----------------------------------------

Quickstart - Open VS Code in your Browser
-----------------------------------------

|Open in Gitpod|

`Github Codespaces <https://github.com/features/codespaces>`__ is
available to those in the beta.

-----------------------------------------

Building
--------

libnw uses cmake as its build system and more specifically
`CMakePresets.json <https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html>`__.

Install `vcpkg <https://github.com/microsoft/vcpkg>`__. Note that
windows build requires VS Studio 2019 (Community Edition), but it
doesn’t have to. All vcpkg packages required will be built
automatically.

The example below is building on Linux, but it’s virtually the same for
all platforms. The only difference is the configuration preset.

.. code:: bash

   $ export VCPKG_ROOT=path/to/vcpkg
   $ export NWN_ROOT=path/to/game
   $ export NWN_USER=path/to/nwn-user-dir
   $ cd path/to/libnw
   $ cmake --preset default-test
   $ cmake --build --preset default
   $ ctest --preset default

Using
-----

While the library is far from done, basic usage would be as follows.

.. code:: cpp

   #include <nw/kernel/Kernel.hpp>
   #include <nw/log.hpp>
   #include <nw/profiles/nwn1/Profile.hpp>

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

      // Do neat stuff

      return 0;
   }

-----------------------------------------

History
-------

A lot of what’s here was written in the 2011-2015 range as part of
personal minimalist toolset, modernized and with new EE stuff added. In
some sense, it’s a work of historical fiction – it’s what I’d have
suggested at the start of NWN:EE: get the game and the community on the
same set of libraries. Similarly to an older project that asked `“what
if Bioware had stuck with
Lua?” <https://solstice.readthedocs.io/en/latest/>`__. The awnser to
that was pretty positive: a decade ahead, at least, of nwscript.

-----------------------------------------

General Warning
---------------

This library is a work-in-progress. There will be serious refactorings
and until there is a real release, it should be assumed the library is
in flux.

If you’ve somehow found your way here by accident, you should head to
`neverwinter.nim <https://github.com/niv/neverwinter.nim>`__, those are
the official tools of NWN:EE and can handle basic workflows out of the
box.

-----------------------------------------

Moonshots
---------

You make ask yourself, why? But, to paraphrase Tennyson, ours isn’t to
question why, it’s but to do and die and learn and maybe make neat
things. In that spirit, here is a list of crazy projects that this
library hopes to facilitate and that all fly in the face of “WHY?”.

-  A nwscript `Language
   Server <https://en.wikipedia.org/wiki/Language_Server_Protocol>`__
-  A modern, cross-platform nwexplorer.
-  And, of course, the ever elusive open source NWN Toolset, with
   plugins, scripting, and a built-in console.

-----------------------------------------

Credits
-------

-  `Bioware <https://bioware.com>`__, `Beamdog <https://beamdog.com>`__
   - The game itself
-  `vcpkg <https://github.com/microsoft/vcpkg>`__ - Package Management
-  `abseil <https://abseil.io/>`__ - Foundational
-  `Catch2 <https://github.com/catchorg/Catch2>`__ - Testing
-  `flecs <https://github.com/SanderMertens/flecs>`__ -
   Entity-Component-System
-  `glm <https://www.opengl.org/sdk/libs/GLM/>`__ - Mathematics
-  `loguru <https://github.com/emilk/loguru>`__,
   `fmt <https://github.com/fmtlib/fmt>`__ - Logging
-  `stbi_image <https://github.com/nothings/stb>`__,
   `NWNExplorer <https://github.com/virusman/nwnexplorer>`__,
   `SOIL2 <https://github.com/SpartanJ/SOIL2/>`__ - Image/Texture
   loading.
-  `inih <https://github.com/benhoyt/inih>`__ - INI, SET parsing
-  `nholmann_json <https://github.com/nlohmann/json>`__ - JSON
-  `toml++ <https://github.com/marzer/tomlplusplus/>`__ - For
   settings.tml
-  `libiconv <https://www.gnu.org/software/libiconv/>`__,
   `boost::nowide <https://github.com/boostorg/nowide>`__ - i18n, string
   conversion
- `doxygen <https://doxygen.nl/>`__, `sphinx <https://www.sphinx-doc.org/en/master/>`__,
  `breathe <https://breathe.readthedocs.io/en/latest/>`__ - documentation


.. |License: MIT| image:: https://img.shields.io/badge/License-MIT-yellow.svg
   :target: https://opensource.org/licenses/MIT
.. |linux| image:: https://github.com/jd28/libnw/actions/workflows/linux.yml/badge.svg
   :target: https://github.com/jd28/libnw/actions?query=workflow%3Alinux
.. |macos| image:: https://github.com/jd28/libnw/actions/workflows/macos.yml/badge.svg
   :target: https://github.com/jd28/libnw/actions?query=workflow%3Amacos
.. |windows| image:: https://github.com/jd28/libnw/actions/workflows/windows.yml/badge.svg
   :target: https://github.com/jd28/libnw/actions?query=workflow%3Awindows
.. |Language grade: C/C++| image:: https://img.shields.io/lgtm/grade/cpp/g/jd28/libnw.svg?logo=lgtm&logoWidth=18
   :target: https://lgtm.com/projects/g/jd28/libnw/context:cpp
.. |CodeQL| image:: https://github.com/jd28/libnw/actions/workflows/codeql-analysis.yml/badge.svg
   :target: https://github.com/jd28/libnw/actions/workflows/codeql-analysis.yml
.. |codecov| image:: https://codecov.io/gh/jd28/libnw/branch/main/graph/badge.svg?token=79PNROEEUU
   :target: https://codecov.io/gh/jd28/libnw
.. |Open in Gitpod| image:: https://gitpod.io/button/open-in-gitpod.svg
   :target: https://gitpod.io/#https://github.com/jd28/libnw

.. toctree::
  :caption: structure
  :maxdepth: 1

  structure/formats
  structure/i18n
  structure/kernel
  structure/objects
  structure/profiles
  structure/resources
  structure/rules
  structure/serialization
  structure/util

.. toctree::
  :caption: c++ api
  :maxdepth: 1

  api/classlist
  api/definelist
  api/enumlist
  api/functionlist
  api/typedeflist

.. toctree::
  :caption: gff schemas
  :maxdepth: 1

  gff_schema/are
  gff_schema/dlg
  gff_schema/fac
  gff_schema/gic
  gff_schema/git
  gff_schema/ifo
  gff_schema/itp
  gff_schema/jrl
  gff_schema/utc
  gff_schema/utd
  gff_schema/ute
  gff_schema/uti
  gff_schema/utm
  gff_schema/utp
  gff_schema/uts
  gff_schema/utt
  gff_schema/utw
  gff_schema/vartable