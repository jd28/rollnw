script
======

The script module provides a lexer, recursive decent parser, and type-checker for NWScript.

examples
--------

**Basic Loading**

.. tabs::

    .. code-tab:: python

        from rollnw.script import Nss, Context

        # Create a context and load a script
        ctx = Context()
        nss = Nss("path/to/myscript.nss", ctx)

        # Parse
        nss.parse()

        # Preprocessing
        nss.process_includes()
        # Now all dependencies are available
        deps = nss.dependencies()

        # Ast resolution and type-checking
        nss.resolve()

        # To simplify this process for assets in the resource manager,
        # the ``load`` function will call all the above
        raise_dead = rollnw.script.load("nw_s0_raisdead", Context())

    .. code-tab:: c++

        #include <nw/script/Nss.hpp>

        auto ctx = std::make_unique<nw::script::Context>();
        nw::script::Nss nss{nw::kerne::resman().demand({"nwscript"sv, nw::ResourceType::nss), ctx.get()};

        // Parse
        nss.parse();

        // Preprocessing
        nss.process_includes()
        // Now all dependencies are available
        auto deps = nss.dependencies()

        // Ast resolution and type-checking
        nss.resolve()


-------------------------------------------------------------------------------

**Iterating Top-Level Declarations**

.. code:: python

    from rollnw.script import Nss, FunctionDecl, Context

    # Load
    ctx = Context()
    nss = rollnw.script.load("nwscript", ctx)

    # Iterate toplevel declarations and look for function declarations
    # This is all functions WITHOUT bodies.
    for decl in nss.ast():
    	if isinstance(decl, FunctionDecl):
    		# the identifier is token for now..
    		print(f"function '{decl.identifier.loc.view()}' has {len(decl)} parameter(s)")

    # Or if you know what you're looking for..
    int_to_string = nss.locate_export("IntToString")

performance
-----------

The parser currently parses at >100MBps on a 2015 MacBook Pro.

TODOs
-----

- Decide how much to track NWN:EE NWScript changes
- Make the library more useful for NWScript successors (i.e Dragon Age or KoTOR)
- Whether to do optimizations or anything further than performance/usability improvements

credits
-------

- `Crafting Interpreters <https://craftinginterpreters.com/>`__
