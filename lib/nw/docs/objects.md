# Objects

This page preserves the still-useful runtime object context from the older
Sphinx docs. It is not the future game-data model. New authored game data should
move through [Smalls propsets](../smalls/docs/propset-architecture.md), with
engine systems owning only the native arrays they need for their hot paths.

## Runtime Handles

`ObjectID` is an index into the object storage, not a one-to-one permanent
identity for a game object.

`ObjectHandle` identifies a specific live object slot. It combines the object
id, object type, and a 16-bit version. A handle is valid only when all of those
fields still match the object array.

`ObjectBase` is the shared base for loaded runtime objects.

## Kernel Service

Objects loaded through the kernel object service belong to that service and must
be destroyed through it.

```cpp
auto obj = nw::kernel::objects().load<nw::Creature>(fs::path("a/path/to/nw_chicken.utc"));
nw::kernel::objects().destroy(obj->handle());
```

After destroy, direct access to the object pointer is invalid and any stored
handle no longer resolves to that object version.

## Current Boundary

The compatibility object layer still loads NWN blueprints and area instances
from GFF or JSON through resman or the filesystem. That is useful for toolset
loading, renderer previews, and migration. Rules and game-data ownership should
not grow around these object classes when a Smalls propset or native subsystem
array is the simpler explicit data owner.
