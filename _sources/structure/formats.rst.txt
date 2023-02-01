formats
=======

One thing that makes NWN(:EE) so great is that it's a local optima of
power and simplicity. It's file formats are no different many of them -
at the reader level - can be implemented as a thin wrapper over a
handful of casts.

Where necessary the reading will be implemented separately from the
writing and likewise from the 'rithmatic, unless it doesn't affect the
usage or performance characteristics of one or the other. E,g. tlk can
easily be made read/write/modifiable with the exact same performance
characteristics, Gff cannot. It's better to separate them than convolute
the implementations of all of them.

-------------------------------------------------------------------------------

2da
---

**Status**: read/write

The 2da parser is one of the more optimized parts of the library.  It can parse ~300MBps and all
default 2das in under half of a second.

**Example - Load a TwoDA**:

.. tabs::

    .. code-tab:: python

        #! /usr/bin/env python

        from rollnw import TwoDA
        import os


        for f in os.listdir():
            if os.path.isfile(f) and os.path.splitext(f)[1].lower() == '.2da':
                tda = TwoDA(f)
                if not tda.valid():
                    print(f"failed parsing: {f}")
                    continue
                print(tda[0][0])

    .. code-tab:: c++

        nw::TwoDA feat("feat.2da");
        if(feat.is_valid()) {
            std::cout << fmt::format("feat.2da has {} rows", feat.rows()) << "\n";
            std::cout << *feat.get<std::string_view>(4, 0) << "\n";
            std::cout << *feat.get<int32_t>(0, "FEAT") << "\n";
        }

-------------------------------------------------------------------------------

gff
---

See `serialization docs <https://rollnw.readthedocs.io/en/latest/structure/serialization.html>`__

-------------------------------------------------------------------------------

images formats
--------------

**Status**:

- bmp: read/write
- dds (standard): read/write
- dds (bioware): read/write
- jpg: read/write
- gif: read/write
- plt: unsupported
- png: read/write
- tga: read/write

The library can do conversions between image formats and can do anything that stbi_image can, however,
this has no goal of being any kind of useful conversion library.

bmp, gif, jpg, png, tga are supported thanks to `stbi_image <https://github.com/nothings/stb>`__.
dds (standard & bioware) is supported thanks to `SOIL2 <https://github.com/SpartanJ/SOIL2/>`__.

.. tabs::

    .. code-tab:: python

        from rollnw import Image
        img = Image("my_texture.dds") # Can be Bioware DDS or standard
        img.write_to("my_texture.png")

    .. code-tab:: c++

        // TODO

-------------------------------------------------------------------------------

ini
---

**Status**: read

Supported thanks to `inih <https://github.com/benhoyt/inih>`__

.. tabs::

    .. code-tab:: python

        from rollnw import Ini
        ini = Ini("userpatch.ini")
        if ini.get_str("Patch/PatchFile000"):
            # User has patch files defined
            pass

    .. code-tab:: c++

        // TODO

-------------------------------------------------------------------------------

json
----

**Status**: read/write

Supported thanks to `nholmann_json <https://github.com/nlohmann/json>`__

-------------------------------------------------------------------------------

mdl
---

See `model docs <https://rollnw.readthedocs.io/en/latest/structure/model.html>`__

-------------------------------------------------------------------------------

mtr
---

**Status**: unsupported

.. tabs::

    .. code-tab:: python

        # TODO

    .. code-tab:: c++

        // TODO

-------------------------------------------------------------------------------

set
---

**Status**: read

Supported thanks to `inih <https://github.com/benhoyt/inih>`__

.. tabs::

    .. code-tab:: python

        # TODO

    .. code-tab:: c++

        // TODO

-------------------------------------------------------------------------------

ssf
---

**Status**: unsupported

.. tabs::

    .. code-tab:: python

        # TODO

    .. code-tab:: c++

        // TODO

-------------------------------------------------------------------------------

tml
---

**Status**: read/write (c++), unsupported (python)

Supported thanks to `toml++ <https://github.com/marzer/tomlplusplus/>`__.

txi
---

**Status**: unsupported

.. tabs::

    .. code-tab:: python

        # TODO

    .. code-tab:: c++

        // TODO
