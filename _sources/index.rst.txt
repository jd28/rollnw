|License: MIT| |ci| |CodeQL| |codecov| |Documentation Status|

rollNW
======

rollNW is an homage to Neverwinter Nights in C++ and Python.

See the `docs <https://rollnw.readthedocs.io/en/latest/>`__ and
`tests <https://github.com/jd28/rollnw/tree/main/tests>`__ for more info,
or open an IDE in browser in the quickstart section below.

**This library is a work-in-progress. There will be serious refactoring
and until there is a real release, it should be assumed the library is
in flux.**

-----------------------------------------

features
--------

- The beginnings of a novel `Rules System <https://rollnw.readthedocs.io/en/latest/structure/rules.html>`__ designed for
  easily adding, overriding, expanding, or removing any rule and reasonable performance
- A `combat engine <https://github.com/jd28/rollnw/blob/main/profiles/nwn1/combat.cpp>`__ that's very
  nearing being able to simulate melee battles.
- Objects (i.e. Creatures, Waypoints, etc) are implemented at a toolset level.  Or in other words their features cover blueprints, area instances, with support for effects and item properties.  They are still missing some new EE things.  Player Characters are read only, for now.
- A recursive decent `NWScript Parser <https://rollnw.readthedocs.io/en/latest/structure/script.html>`__
- Implementations of pretty much every `NWN File Format <https://rollnw.readthedocs.io/en/latest/structure/formats.html>`__
- An binary and ASCII `Model Parser <https://rollnw.readthedocs.io/en/latest/structure/model.html>`__.  See also the `mudl <https://github.com/jd28/mudl>`__ model viewer side project.
- A Resource Manager that can load all NWN containers (e.g. erf, key, nwsync) and also Zip files.
- An implementation of NWN's `Localization System <https://rollnw.readthedocs.io/en/latest/structure/i18n.html>`__ focused
  on utf8 everywhere.
- The beginnings of runtime scripting with `Luau <https://luau-lang.org/>`__ the scripting language of Roblox.

-----------------------------------------

goals
-----

-  aims to implement an RPG engine inspired by NWN, excluding graphics and networking.
-  focuses on usage, instead of doing things the Aurora Engine Way.
-  follows `utf8 everywhere <https://utf8everywhere.org/>`__.
-  hews as close to `C++ Core
   Guidelines <https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines>`__
   as possible.
-  aims to be as easily bindable as possible to other languages. I.e.
   only library specific or STL types at API boundaries.

-----------------------------------------

quickstart - Open VS Code in your Browser
-----------------------------------------

|Open in Gitpod|

`Github Codespaces <https://github.com/features/codespaces>`__ is
available to those in the beta.

-----------------------------------------

History
-------

A lot of what's here was written in the 2011-2015 range as part of
personal minimalist toolset, modernized and with new EE stuff added. In
some sense, it's a work of historical fiction - it's what I'd have
suggested at the start of NWN:EE: get the game and the community on the
same set of libraries. Similarly to an older project that asked `“what
if Bioware had stuck with Lua?” <https://solstice.readthedocs.io/en/latest/>`__. The answer to
that was pretty positive: a decade ahead, at least, of nwscript.

-----------------------------------------

Moonshots
---------

You make ask yourself, why? But, to paraphrase Tennyson, ours isn't to
question why, it's but to do and die and learn and maybe make neat
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

-  `Bioware <https://bioware.com>`__, `Beamdog <https://beamdog.com>`__ - The game itself
-  `vcpkg <https://github.com/microsoft/vcpkg>`__ - Package Management
-  `abseil <https://abseil.io/>`__ - Foundational
-  `Catch2 <https://github.com/catchorg/Catch2>`__ - Testing
-  `Roblox <https://luau-lang.org/>`__ - The Luau scripting language
-  `glm <https://www.opengl.org/sdk/libs/GLM/>`__ - Mathematics
-  `loguru <https://github.com/emilk/loguru>`__,
   `fmt <https://github.com/fmtlib/fmt>`__ - Logging
-  `stbi_image <https://github.com/nothings/stb>`__,
   `NWNExplorer <https://github.com/virusman/nwnexplorer>`__,
   `SOIL2 <https://github.com/SpartanJ/SOIL2/>`__ - Image/Texture loading.
-  `inih <https://github.com/benhoyt/inih>`__ - INI, SET parsing
-  `nholmann_json <https://github.com/nlohmann/json>`__ - JSON
-  `toml++ <https://github.com/marzer/tomlplusplus/>`__ - For settings.tml
-  `libiconv <https://www.gnu.org/software/libiconv/>`__,
   `boost::nowide <https://github.com/boostorg/nowide>`__ - i18n, string conversion
- `doxygen <https://doxygen.nl/>`__, `sphinx <https://www.sphinx-doc.org/en/master/>`__,
  `breathe <https://breathe.readthedocs.io/en/latest/>`__ - documentation


.. |License: MIT| image:: https://img.shields.io/badge/License-MIT-yellow.svg
   :target: https://opensource.org/licenses/MIT
.. |ci| image:: https://github.com/jd28/rollnw/actions/workflows/ci.yml/badge.svg
   :target: https://github.com/jd28/rollnw/actions?query=workflow%3Aci
.. |CodeQL| image:: https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml/badge.svg
   :target: https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml
.. |codecov| image:: https://codecov.io/gh/jd28/rollnw/branch/main/graph/badge.svg?token=79PNROEEUU
   :target: https://codecov.io/gh/jd28/rollnw
.. |Open in Gitpod| image:: https://gitpod.io/button/open-in-gitpod.svg
   :target: https://gitpod.io/#https://github.com/jd28/rollnw
.. |Documentation Status| image:: https://readthedocs.org/projects/rollnw/badge/?version=latest
    :target: https://rollnw.readthedocs.io/en/latest/?badge=latest

.. toctree::
  :caption: getting started
  :maxdepth: 1

  gs/building
  gs/using

.. toctree::
  :caption: structure
  :maxdepth: 1

  structure/formats
  structure/i18n
  structure/kernel
  structure/model
  structure/objects
  structure/resources
  structure/rules
  structure/script
  structure/serialization

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
