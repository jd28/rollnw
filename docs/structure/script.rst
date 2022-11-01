script
======

A lexer and recursive decent parser, as is. It can create a parse tree, no type checking, no
real AST.. yet.

Currently it can parse almost all default nwscripts.

examples
--------

.. code:: python

    from rollnw.script import Nss, FunctionDecl

    # Load
    nss = Nss("nwscript.nss")
    # Parse
    script = nss.parse()
    # Iterate toplevel declarations and look for function declarations
    # This is all functions WITHOUT bodies.
    for decl in script:
    	if isinstance(decl, FunctionDecl):
    		# the identifier is token for now..
    		print(f"function '{decl.identifier.id}' has {len(decl)} parameter(s)")

performance
-----------

The parser currently parses at >100MBps on a 2015 MacBook Pro.

credits
-------

Robert Nystrom's wonderful book `Crafting Interpreters <https://craftinginterpreters.com/>`__
was huge source of inspiration.
