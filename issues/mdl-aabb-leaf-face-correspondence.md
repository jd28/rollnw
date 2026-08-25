# MDL AABB leaf-to-face correspondence

The navigation fallback consumes the flat triangles and surface materials from an
MDL AABB node. It deliberately does not consume `AABBEntry::leaf_face`: text-model
cleanup can drop faces and split vertices, while the binary loader currently does
not materialize the AABB tree. The stored leaf indices therefore are not a reliable
index into the normalized triangle batch.

Resolve this only when a consumer needs the authored AABB tree. That work must define
one accepted-face remap for both text and binary MDLs and verify it against real NWN
models. Detour builds its own query structure, so repairing this now would add unused
state and work.
