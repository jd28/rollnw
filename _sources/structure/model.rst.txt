model
=====

Loading NWN models still matters for conversion and asset-pipeline work, but
renderer-backed validation is now an active part of rollNW through ``nw::gfx``,
``lib/nw/render``, and ``mudl``.

See ``tools/mudl`` for the current NWN model, spell, VFX, particle, and
glTF/PBR viewer and headless capture path.

ASCII Models
------------

Most default NWN models can be parsed without errors.

The parser can parse at about 100mb/s and can read pretty much `all ascii models <https://neverwintervault.org/project/nwn1/model/neverwinter-nights-ee-ascii-source-models>`__
in ~20s on 2015 MacBook Pro.

Binary Models
-------------

The beginnings of binary model parsing is in the library.
It's hard to tell what's right and what's wrong until there is more rendering experience.

Examples
--------

.. code-block:: c++

    // TODO
