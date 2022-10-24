script
======

A lexer and recursive decent parser, as is. It can create a parse tree, no type checking, no
real AST.. yet.

Currently it can parse almost all default nwscripts.

examples
--------

.. code:: python

    #! /usr/bin/env python

    from rollnw import Nss
    import os


    for f in os.listdir():
        if os.path.isfile(f) and os.path.splitext(f)[1].lower() == '.nss':
            print(f)
            nss = Nss(f)
            try:
                script = nss.parse()
            except BaseException as e:
                print(f"************** failed {f} because {e}")
                pass


performance
-----------

The parser currently parses at >100MBps on a 2015 MacBook Pro.

credits
-------

Robert Nystrom's wonderful book `Crafting Interpreters <https://craftinginterpreters.com/>`__
was huge source of inspiration.
