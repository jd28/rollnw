# NWN Dump Fixture User Root

This directory is an explicit NWN user root for renderer smoke tests. `development`
contains resource closures generated with `mudl dump` from a local NWN install;
`manifests` keeps the per-root dump reports.

Use it with:

```bash
mudl stats c_aribeth --user ./tests/test_data/renderer/nwn_dump_user
mudl screenshot c_drgshad ./out/c_drgshad.png --user ./tests/test_data/renderer/nwn_dump_user
```

The install root is still required for bootstrap data such as TLK/rules, but the
listed model, texture, and TXI resources are served from this committed fixture
root.
