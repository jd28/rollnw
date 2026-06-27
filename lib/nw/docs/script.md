# NWScript Parser

The script module provides NWScript compatibility tooling: a lexer, recursive
descent parser, include processing, AST resolution, and type checking.

New scripting and rules work for authored RPG toolsets should start with
[Smalls](../smalls/docs/index.md). The NWScript parser remains useful for
reading, analyzing, and migrating existing NWN content.

## Basic Loading

```cpp
#include <nw/kernel/Kernel.hpp>
#include <nw/script/Nss.hpp>

nw::kernel::config().initialize();
nw::kernel::services().start();

auto ctx = std::make_unique<nw::script::Context>();
nw::script::Nss nss{fs::path("path/to/myscript.nss"), ctx.get()};

nss.parse();
nss.process_includes();
auto deps = nss.dependencies();
nss.resolve();
```

## Top-Level Declarations

Tooling can inspect declarations through the resolved AST exposed by
`nw::script::Nss`. That path is intended for compatibility analysis, dependency
tracking, migration tools, and other uses that need to understand NWScript
source without executing it.

## Notes

The older docs recorded parser throughput observations from a specific 2015
MacBook Pro. Those numbers are not repeated here as current performance claims;
measure with the current parser and input corpus before using throughput as a
design constraint.
