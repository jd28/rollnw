# Release publication follow-up

The build identity and package validation pipeline does not publish the VS Code
extension to Marketplace. Before enabling that, choose the publisher credential
and approval policy, and decide how development VSIX artifacts should be offered.
Keep publication limited to explicitly approved extension tags and publish the
already-verified VSIX rather than rebuilding after approval.

The distribution workflow starts on `release: published`, so the release entry
can be visible before its assets pass CI. Consider draft-to-published promotion
if atomic visibility becomes a release requirement. Artifact signing/provenance
also needs an explicit credential/trust policy; SHA256SUMS is integrity metadata,
not a signature. No LTS/backport support is promised by dated snapshots.
