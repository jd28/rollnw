# Smalls Docs

Smalls is the active rules/script authoring path in rollnw. Older docs may focus
on NWScript parsing or C++ rule modifiers; those systems still exist, but Smalls
is where new language, embedding, and authored RPG toolset/game rules work
should be expected to land.

Read the architecture documents in this order:

1. [`conventions.md`](conventions.md) — normative ownership, dependency, data
   protocol, failure, and persistence rules.
2. [`profile-packages.md`](profile-packages.md) — selected profile root, module
   conventions, package resolution, and bootstrap ordering.
3. [`propset-architecture.md`](propset-architecture.md) — detailed propset,
   native component, ABI bridge, and object-domain storage rules.
4. [`load-config.md`](load-config.md) — typed shared rules/config batches and
   their relationship to live propsets and native values.
5. [`spec.md`](spec.md) — SmallS language and virtual-machine design.

The normative documents describe the target architecture. Current deviations
and their migration sequence are tracked in
[`issues/smalls-module-ownership-and-profile-boundaries.md`](../../../../issues/smalls-module-ownership-and-profile-boundaries.md).
