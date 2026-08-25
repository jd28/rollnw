# Client keybinding table

F9 preview dispatch currently joins the client's existing hard-coded SDL key chain.
`CommandSpec::default_binding` is metadata and is not the authoritative input-dispatch
table, so routing F9 through it would only create a second incomplete policy.

When remappable bindings are implemented, first enumerate the actual keyboard,
mouse, and controller actions and their conflict rules. Then replace the hard-coded
dispatch with one flat binding table consumed by both editor and runtime modes. Until
then, keep the single F9 branch rather than maintaining two sources of truth.
