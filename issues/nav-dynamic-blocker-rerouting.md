# Dynamic blocker rerouting

Play preview bakes closed door and placeable walkmesh overlaps into a flat
owner-to-polygon CSR table. Changing a blocker updates Detour polygon flags without
rebuilding navigation, but the preview does not yet operate doors and therefore does
not invalidate an actor's current route.

When runtime door operation exists, measure the real change rate and actor count. The
minimum required transform is a batch of changed owner states followed by route
revalidation only for agents whose remaining polygon corridor intersects a changed
polygon. Do not add per-frame blocker scans or a general event system before that data
exists.
