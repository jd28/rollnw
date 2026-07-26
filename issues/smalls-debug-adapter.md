# Smalls Debug Adapter

## Problem

There is no way to debug a Smalls script. No breakpoints, no stepping, no
variable inspection, no call stack. The only diagnostic tool is `print`.

This is the largest single gap in the tooling and the one with the clearest
advantage: the VM, the bytecode, the compiler that emits it, and the garbage
collector are all in-repo. A debug adapter is not an integration with a
third-party runtime — it is exposing state the runtime already has. Nothing
else in the roadmap is as visible to a script author, and nothing else is as
hard for anyone outside this repository to replicate.

It matters more than usual here because the toolset is an edit-in-play
runtime: scripts run against live game state, which is exactly the situation
where reading the code is not enough to explain behavior.

## Direction

Implement the Debug Adapter Protocol as a separate executable from the
language server. DAP and LSP are different protocols with different lifetimes
— a debug session starts and stops many times within one editor session — and
merging them couples an unstable session to a stable one.

The prerequisite is a source map. The compiler must emit, and the bytecode
must retain, a mapping from instruction offset to source position, plus local
variable names and their live ranges. Without live ranges, variable inspection
reports stale slots after a scope ends and reports them confidently, which is
worse than reporting nothing. This is a compiler and bytecode change, and it
should be scoped before any adapter work starts.

Breakpoints are set on source positions and lowered to instruction offsets.
Verify them: a breakpoint on a line with no instruction must be reported
unverified or moved to the next line that has one, not silently ignored.

Stepping needs the same map. Step-over and step-out require frame identity
from the VM's call stack, not line-number heuristics.

Variable inspection must cooperate with the garbage collector. A debugger that
holds raw references into the script heap across a suspension will read freed
or moved memory. Inspection roots must be visible to the collector, which
connects this work to the collector architecture already documented for the
runtime.

Attaching to a live Arclight session is the case that makes this worth
building, and it should be designed for from the start rather than retrofitted
onto a launch-only adapter. Launch-only is the simpler first milestone; the
protocol boundary should not assume it.

`textDocument/inlineValue` in the language server displays values inline
during a debug session and should follow once the adapter reports frames.

## Required decision

Whether the first milestone is launch-only or attach-capable. Launch-only
ships sooner and exercises the source map, breakpoints, and stepping without
touching the running toolset. Attach is where the actual value is. Decide
based on whether the source-map work can land independently of the session
transport.

## Done

- The compiler emits an instruction-offset-to-source-position map and local
  variable live ranges, and the bytecode retains them. Bytecode size impact is
  measured and recorded here.
- A `smalls-dap` executable implements launch, breakpoints, step in/over/out,
  call stack, and variable inspection for locals, parameters, and globals.
- A breakpoint on a line with no instruction is reported unverified or moved,
  never silently dropped.
- Variable inspection is safe across a garbage collection, verified under ASan
  with a collection forced at a suspension point.
- Stepping through a recursive function reports correct frame identity.
- The VS Code extension contributes a debug configuration and launches a
  script from the editor.
- Attach to a running Arclight session is either implemented or explicitly
  scoped as the next milestone with the transport decided.
- `textDocument/inlineValue` displays locals during a session.
