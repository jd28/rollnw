# Navigation blocker triangle granularity

The first play-preview pass marks complete navigation triangles overlapped by door and placeable walkmeshes. This is simple and keeps runtime door updates to flat poly-index ranges, but small blockers may exclude more floor than their geometry occupies. Record affected-poly counts and visible over-blocking before introducing finer subdivision.
