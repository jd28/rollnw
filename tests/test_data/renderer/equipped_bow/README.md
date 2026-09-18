# Equipped bow encounter regression

The five MDLs are unchanged files extracted with `mudl dump --module` from
The Awakening on 2026-09-17. The reported encounter was `vj_enc_dim_arc`, spawning
`vj_dim_bow` (appearance 1769), which holds `vj_diml_bow` in its right hand.
The resolved item parts are `wbwln_b_071`, `wbwln_m_044`, and `wbwln_t_064`;
middle and top inherit `bowshot` from the included `_011` supermodels. Bottom
has no animation. No texture or custom appearance dependencies are needed.

The native JSON blueprints are adapted from those three reported blueprints.
They use fixture resrefs, remove other equipment/inventory and item properties,
and use the existing bodak appearance 23 without wings/tail. The encounter has
two identical spawn rows to exercise repeated instance transfers.

`EncounterSpawnsPreserveEquippedAnimationPolicy` compares standalone and
encounter equipment, advances the creature's animation, and rebuilds the live
encounter. Equipment retains its disabled scene animation gate and bind node
transforms. This catches starting the inherited `bowshot` clip when moving an
equipped instance into a composite scene.
