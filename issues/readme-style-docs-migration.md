# README-Style Docs Migration

## Data

The repository currently has two documentation styles:

- Local Markdown docs near the code, such as `README.md`,
  `lib/nw/gfx/README.md`, `lib/nw/render/README.md`, and `tools/mudl/README.md`.
- Older Sphinx/RST docs under `docs/`, including getting-started pages and
  structure notes for formats, i18n, kernel, objects, resources, rules, script,
  and serialization.

The old `docs/structure/model.rst` page was removed after its renderer-facing
content moved to `lib/nw/render/README.md` and `lib/nw/render/docs/*.md`.

## Direction

Prefer README-style Markdown docs located at the subsystem that owns the data or
tooling being described.

Each remaining RST page should be migrated only after it has a clear owner:

- build and usage notes: root `README.md` or a root-level `docs/*.md` Markdown
  page
- format notes: resource/content-format owner docs
- i18n, resources, objects, rules, script, and serialization: local subsystem
  README/docs files
- generated website index/navigation: remove only after all linked content has
  a replacement

## Migration Rule

Do not delete an RST page until the useful current content has a Markdown target.
Stale or obsolete text can be dropped instead of ported, but the deletion should
say where the live replacement is.

## Verification

For each migration slice:

- run a reference scan for removed page names and old ReadTheDocs URLs
- run `git diff --check`
- if the Sphinx tree still exists, make sure removed pages are also removed from
  `docs/index.rst` toctrees

## Generated Site Rendering Gaps

The README-style docs site still needs a rendering pass:

- Code blocks need readable syntax highlighting in generated HTML.
- Some Markdown tables are not formatting correctly; table rendering should be
  verified against the generated site, not only GitHub's Markdown preview.

## Do Not Carry Forward

- Renderer or runtime policy in the old Sphinx `structure/` pages.
- Duplicated docs that must be updated in both RST and Markdown.
- ReadTheDocs links as the only source of current subsystem truth.
