# glTF Socket Authoring Contract

## Data

glTF supplies node hierarchies and arbitrary `extras`, but it does not define
equipment, VFX, or gameplay locator semantics. Current repository glTF fixtures
do not provide an observed socket annotation convention.

## Decision Needed

Define one explicit authoring annotation that lowers selected glTF nodes into
`ModelSocket` rows. The contract must state:

- the exact node or `extras` input shape
- whether the socket name is inline or references a name table
- how a node maps to runtime node or joint indices
- duplicate, missing-node, malformed-transform, and unknown-annotation behavior
- whether cross-rig attachments need data beyond the existing root attachment
  and source bind-offset policies

Do not infer sockets from subsets of node, bone, mesh, or model names.

## Done

- At least one real authored glTF asset exercises the proposed annotation.
- Import produces validated `ModelSocket` rows with stable indices.
- A round-trip or importer test proves the annotation shape and rejection rules.
- Runtime attachment tests use only the emitted socket indices; no glTF-specific
  renderer branch is introduced.
