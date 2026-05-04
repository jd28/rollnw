script
======

The script module provides a lexer, recursive descent parser, and type-checker for NWScript.

.. warning::

    This page documents the NWScript compatibility parser. That parser is
    still useful for reading and analyzing existing NWN content, but it is
    not the only scripting direction in rollNW anymore. New rule and
    scripting work for authored RPG toolsets/games is moving through Smalls under
    ``lib/nw/smalls``.

examples
--------

**Basic Loading**

.. code:: cpp

    #include <nw/kernel/Kernel.hpp>
    #include <nw/script/Nss.hpp>

    // Start the kernel, if you want to load game assets
    nw::kernel::config().initialize();
    nw::kernel::services().start();

    auto ctx = std::make_unique<nw::script::Context>();
    nw::script::Nss nss{fs::path("path/to/myscript.nss"), ctx.get()};

    // Parse
    nss.parse();

    // Preprocessing
    nss.process_includes();
    // Now all dependencies are available
    auto deps = nss.dependencies();

    // Ast resolution and type-checking
    nss.resolve();


-------------------------------------------------------------------------------

**Iterating Top-Level Declarations**

The parser exposes top-level declarations through the C++ AST. Use
``nw::script::Nss`` and its resolved AST when tooling needs to inspect
function declarations, includes, or dependencies.

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
