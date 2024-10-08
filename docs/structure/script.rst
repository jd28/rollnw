script
======

The script module provides a lexer, recursive decent parser, and type-checker for NWScript.

.. note::

    In the case of the `Python API <https://rollnw.readthedocs.io/en/latest/api_python/rollnw_script.html>`__,
    the interface to the AST is read only.

examples
--------

**Basic Loading**

.. tabs::

    .. code-tab:: python

        import rollnw
        from rollnw.script import Nss, Context

        # Start kernel, if you want to load game assets
        rollnw.kernel.start()

        # Create a context and to add include path, pass them into the Context constructor
        ctx = Context(["includes/"])

        # Load the script from a file
        nss = Nss("path/to/myscript.nss", ctx)

        # Parse
        nss.parse()

        # Preprocessing
        nss.process_includes()

        # Now all dependencies are available
        deps = nss.dependencies()

        # Ast resolution and type-checking
        nss.resolve()

        # Load a script from string
        nss2 = Nss.from_string("void test_function(string s, int b);", ctx)

        # To get any old script in the the context's resman use ``get``.  Note this
        # parses and resolves the script, nothing further processing is needed.
        raise_dead = ctx.get("nw_s0_raisdead")

    .. code-tab:: c++

        #include <nw/kernel/Kernel.hpp>
        #include <nw/script/Nss.hpp>

        // Start the kernel, if you want to load game assets
        nw::kernel::config().initialize();
        nw::kernel::services().start();

        auto ctx = std::make_unique<nw::script::Context>();
        nw::script::Nss nss{nw::kernel::resman().demand({"nwscript"sv, nw::ResourceType::nss), ctx.get(), true};

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

    import rollnw
    from rollnw.script import Nss, FunctionDecl, Context

    # Start kernel, if you want to load game assets
    rollnw.kernel.start()

    # Create a context..
    ctx = Context()

    # The default command script is "nwsscript"
    nss = ctx.command_script()

    # Iterate toplevel declarations and look for function declarations
    # This is all functions WITHOUT bodies.
    for decl in nss.ast():
        if isinstance(decl, FunctionDecl):
            # the identifier is token for now..
            print(f"function '{decl.identifier()}' has {len(decl)} parameter(s)")

    # Or if you know what you're looking for.. the result is a rollnw.script.Symbol
    int_to_string = nss.locate_export("IntToString", False)

performance
-----------

The parser currently parses at >100MBps on a 2015 MacBook Pro.

TODOs
-----

- Decide how much to track NWN:EE NWScript changes, only raw strings isn't already done.
- Make the library more useful for NWScript successors (i.e Dragon Age or KoTOR)
- Whether to do optimizations or anything further than performance/usability improvements

credits
-------

- `Crafting Interpreters <https://craftinginterpreters.com/>`__
