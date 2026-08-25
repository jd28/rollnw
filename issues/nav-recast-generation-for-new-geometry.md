# Navigation generation for new geometry

The current navigation input is authored NWN `.wok`, `.pwk`, and `.dwk` geometry.
Completely new tiles or imported scene formats may eventually provide collision
geometry without an authored NWN walkmesh.

That is a different input transform. Decide whether to author a normalized walkmesh or
generate one with Recast only after representative new geometry and traversal rules
exist. Recast is not built today; only vendored Detour is used. Adding generation now
would increase build size and policy surface without an input corpus or acceptance
criteria.
