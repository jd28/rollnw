|License: MIT| |ci| |CodeQL| |codecov| |Documentation Status|

rollNW
======

rollNW is an homage to Neverwinter Nights in C++. It started
as reusable NWN infrastructure and is broadening toward support for a
modern authored RPG toolset/game: content formats, rules, scripting,
rendering validation, networking foundations, and the runtime services
needed to make those pieces authorable and playable.

See the `docs <https://rollnw.readthedocs.io/en/latest/>`__ and
`tests <https://github.com/jd28/rollnw/tree/main/tests>`__ for more info,
or open an IDE in browser in the quickstart section below.

.. warning::

   rollNW is in active transition. Older docs and APIs may show a
   narrower NWN library cross section of the project. Current work is
   reframing the codebase as a foundation for an authored RPG
   toolset/game,
   with ``nw::gfx`` as the low-level graphics layer, ``lib/nw/render``
   and ``mudl`` for renderer-backed asset validation, and Smalls as the
   rule/script authoring path. Networking and runtime services are part
   of the long-term foundation. Until there is a real release, assume
   APIs and subsystem boundaries can move.

-----------------------------------------

features
--------

- The beginnings of a novel `Rules System <https://rollnw.readthedocs.io/en/latest/structure/rules.html>`__ designed for
  easily adding, overriding, expanding, or removing any rule and reasonable performance
- A `combat engine <https://github.com/jd28/rollnw/blob/main/api/combat.cpp>`__ that's very
  nearing being able to simulate melee battles.
- Objects (i.e. Creatures, Waypoints, etc) are implemented at a toolset level. In other words, their features cover blueprints and area instances, with support for effects and item properties. Loading objects from resman or the filesystem is transparent whether the source is GFF or JSON.
- A recursive descent `NWScript Parser <https://rollnw.readthedocs.io/en/latest/structure/script.html>`__. This remains useful for NWN compatibility, but new rules and scripting experiments are moving toward Smalls.
- Implementations of pretty much every `NWN File Format <https://rollnw.readthedocs.io/en/latest/structure/formats.html>`__
- NWN and glTF model rendering docs now live with the renderer code in
  `lib/nw/render <https://github.com/jd28/rollnw/tree/main/lib/nw/render>`__.
- ``nw::gfx``, ``lib/nw/render``, and ``mudl`` form the current renderer validation path for NWN model, area, spell, VFX, particle, and glTF/PBR work.
- A Resource Manager that can load all NWN containers (e.g. erf, key, nwsync) and also Zip files.
- An implementation of NWN's `Localization System <https://rollnw.readthedocs.io/en/latest/structure/i18n.html>`__ focused
  on utf8 everywhere.
- Smalls is the active rules/script transition path for modern embedded tooling and rule experiments.

-----------------------------------------

goals
-----

-  aims to implement the reusable RPG, toolset, resource, rendering, networking, and runtime foundations for a modern authored game inspired by NWN.
-  keeps current implementation focused on practical foundations first: rules, content formats, renderer validation, authoring workflows, and the services a playable toolset/game will need.
-  focuses on authoring and usage, instead of doing things the Aurora Engine Way.
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
-  `abseil <https://abseil.io/>`__ - Foundational
-  `Catch2 <https://github.com/catchorg/Catch2>`__ - Testing
-  `glm <https://www.opengl.org/sdk/libs/GLM/>`__ - Mathematics
-  `Vulkan <https://www.vulkan.org/>`__,
   `Dear ImGui <https://github.com/ocornut/imgui>`__ - Graphics and tool UI
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
- `sphinx <https://www.sphinx-doc.org/en/master/>`__ - documentation


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
  gs/differences

.. toctree::
  :caption: structure
  :maxdepth: 1

  structure/formats
  structure/i18n
  structure/kernel
  structure/objects
  structure/resources
  structure/rules
  structure/script
  structure/serialization
