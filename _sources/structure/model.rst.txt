model
=====

As mentioned elsewhere, the goal of this library is not to render graphics, but maybe?  Loading NWN models is
for the purpose of conversion or in some other asset pipeline.

See the `mudl <https://github.com/jd28/mudl>`__ model viewer side project for work on a NWN model viewer.

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

.. tabs::

    .. code-tab:: python

        #! /usr/bin/env python

        from rollnw.model import Mdl
        import os

        mdl = Mdl.from_file("my_ascii_model.mdl")
        if not mdl.valid():
            print(f"failed parsing: {f}")
        print(mdl.supermodel_name)

    .. code-tab:: c++

        // TODO
