#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import posixpath
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any
from urllib.parse import quote, unquote, urlsplit, urlunsplit

try:
    import tomllib
except ModuleNotFoundError as exc:
    raise SystemExit("Python 3.11 or newer is required for tomllib") from exc

try:
    import markdown
    from markdown.extensions import Extension
    from markdown.treeprocessors import Treeprocessor
except ModuleNotFoundError as exc:
    raise SystemExit("Missing dependency: install tools/docs/requirements.txt") from exc


VALID_STATUSES = {"current", "transitional", "legacy"}
ASSET_OUTPUT_DIR = PurePosixPath("assets")
STYLE_TARGET = PurePosixPath("_static/site.css")


class SiteError(RuntimeError):
    pass


@dataclass(frozen=True)
class Page:
    source: Path
    source_rel: PurePosixPath
    target: PurePosixPath
    title: str
    section: str | None
    status: str | None
    notice: str | None


@dataclass
class SiteContext:
    repo_root: Path
    output_root: Path
    site_title: str
    repo_url: str
    source_branch: str
    pages: list[Page]
    pages_by_source: dict[PurePosixPath, Page]
    pages_by_target: dict[PurePosixPath, Page]
    copied_assets: set[PurePosixPath]


def repo_posix(path: Path, repo_root: Path) -> PurePosixPath:
    return PurePosixPath(path.relative_to(repo_root).as_posix())


def is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def fail(message: str) -> None:
    raise SiteError(message)


def load_manifest(path: Path, repo_root: Path) -> dict[str, Any]:
    if not path.exists():
        fail(f"manifest not found: {path}")
    with path.open("rb") as f:
        data = tomllib.load(f)
    if not isinstance(data, dict):
        fail("manifest root must be a table")
    return data


def parse_manifest(path: Path, repo_root: Path, output_root: Path) -> SiteContext:
    data = load_manifest(path, repo_root)
    site_title = require_string(data, "site_title", "rollNW")
    repo_url = require_string(data, "repo_url", "https://github.com/jd28/rollnw").rstrip("/")
    source_branch = require_string(data, "source_branch", "main")

    raw_pages = data.get("pages")
    if not isinstance(raw_pages, list) or not raw_pages:
        fail("manifest must contain at least one [[pages]] entry")

    pages: list[Page] = []
    sources: set[PurePosixPath] = set()
    targets: set[PurePosixPath] = set()

    for index, entry in enumerate(raw_pages):
        if not isinstance(entry, dict):
            fail(f"pages[{index}] must be a table")

        source_raw = require_string(entry, "source")
        target_raw = require_string(entry, "target")
        title = require_string(entry, "title")
        section = optional_string(entry, "section")
        status = optional_string(entry, "status")
        notice = optional_string(entry, "notice")

        if status is not None and status not in VALID_STATUSES:
            fail(f"{source_raw}: invalid status '{status}'")

        source_rel = clean_repo_relative(source_raw, f"{source_raw}: source")
        source = (repo_root / source_rel.as_posix()).resolve()
        if not is_relative_to(source, repo_root):
            fail(f"{source_raw}: source is outside repository")
        if not source.is_file():
            fail(f"{source_raw}: source file not found")
        if source_rel in sources:
            fail(f"{source_raw}: duplicate source")
        sources.add(source_rel)

        target = clean_output_target(target_raw, f"{source_raw}: target")
        target_abs = (output_root / target.as_posix()).resolve()
        if not is_relative_to(target_abs, output_root):
            fail(f"{source_raw}: target escapes output root")
        if target in targets:
            fail(f"{source_raw}: duplicate target {target}")
        targets.add(target)

        pages.append(
            Page(
                source=source,
                source_rel=source_rel,
                target=target,
                title=title,
                section=section,
                status=status,
                notice=notice,
            )
        )

    return SiteContext(
        repo_root=repo_root,
        output_root=output_root,
        site_title=site_title,
        repo_url=repo_url,
        source_branch=source_branch,
        pages=pages,
        pages_by_source={page.source_rel: page for page in pages},
        pages_by_target={page.target: page for page in pages},
        copied_assets=set(),
    )


def require_string(table: dict[str, Any], key: str, default: str | None = None) -> str:
    value = table.get(key, default)
    if not isinstance(value, str) or not value:
        fail(f"'{key}' must be a non-empty string")
    return value


def optional_string(table: dict[str, Any], key: str) -> str | None:
    value = table.get(key)
    if value is None:
        return None
    if not isinstance(value, str):
        fail(f"'{key}' must be a string")
    return value


def clean_repo_relative(value: str, label: str) -> PurePosixPath:
    if "\\" in value:
        fail(f"{label} must use forward slashes")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts):
        fail(f"{label} must be a clean repository-relative path")
    return path


def clean_output_target(value: str, label: str) -> PurePosixPath:
    path = clean_repo_relative(value, label)
    if path.suffix != ".html":
        fail(f"{label} must end in .html")
    return path


def source_url(ctx: SiteContext, source_rel: PurePosixPath, fragment: str = "") -> str:
    encoded = quote(source_rel.as_posix())
    url = f"{ctx.repo_url}/blob/{ctx.source_branch}/{encoded}"
    if fragment:
        url += f"#{fragment}"
    return url


def relative_url(from_target: PurePosixPath, to_target: PurePosixPath, fragment: str = "") -> str:
    from_dir = from_target.parent.as_posix()
    rel = posixpath.relpath(to_target.as_posix(), "." if from_dir == "." else from_dir)
    if fragment:
        rel += f"#{fragment}"
    return rel


def is_external_url(value: str) -> bool:
    split = urlsplit(value)
    return bool(split.scheme or split.netloc) or value.startswith(("mailto:", "tel:", "data:"))


def resolve_local_link(page: Page, ctx: SiteContext, raw_url: str) -> tuple[Path, PurePosixPath, str]:
    split = urlsplit(raw_url)
    link_path = unquote(split.path)
    if not link_path:
        fail(f"{page.source_rel}: empty local link path in '{raw_url}'")
    if "\\" in link_path:
        fail(f"{page.source_rel}: local link uses backslashes: {raw_url}")

    base = page.source.parent
    resolved = (base / link_path).resolve()
    if not is_relative_to(resolved, ctx.repo_root):
        fail(f"{page.source_rel}: local link escapes repository: {raw_url}")
    rel = repo_posix(resolved, ctx.repo_root)
    return resolved, rel, split.fragment


def rewrite_anchor_href(page: Page, ctx: SiteContext, raw_url: str) -> str:
    if not raw_url or raw_url.startswith("#") or is_external_url(raw_url):
        return raw_url
    split = urlsplit(raw_url)
    if split.query:
        return raw_url

    resolved, rel, fragment = resolve_local_link(page, ctx, raw_url)
    if not resolved.exists():
        fail(f"{page.source_rel}: linked file does not exist: {raw_url}")

    if rel.suffix.lower() == ".md":
        target_page = ctx.pages_by_source.get(rel)
        if target_page is not None:
            return relative_url(page.target, target_page.target, fragment)
        return source_url(ctx, rel, fragment)

    return source_url(ctx, rel, fragment)


def rewrite_image_src(page: Page, ctx: SiteContext, raw_url: str) -> str:
    if not raw_url or raw_url.startswith("#") or is_external_url(raw_url):
        return raw_url
    split = urlsplit(raw_url)
    if split.query:
        return raw_url

    resolved, rel, fragment = resolve_local_link(page, ctx, raw_url)
    if not resolved.is_file():
        fail(f"{page.source_rel}: linked image does not exist: {raw_url}")
    if rel.parts and rel.parts[0] == "external":
        return source_url(ctx, rel, fragment)

    asset_target = ASSET_OUTPUT_DIR / rel
    copy_asset(resolved, asset_target, ctx)
    url = relative_url(page.target, asset_target, fragment)
    return url


def copy_asset(source: Path, target: PurePosixPath, ctx: SiteContext) -> None:
    target_abs = (ctx.output_root / target.as_posix()).resolve()
    if not is_relative_to(target_abs, ctx.output_root):
        fail(f"asset target escapes output root: {target}")
    if target in ctx.copied_assets:
        return
    target_abs.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target_abs)
    ctx.copied_assets.add(target)


class LinkRewriteProcessor(Treeprocessor):
    def __init__(self, md: markdown.Markdown, page: Page, ctx: SiteContext):
        super().__init__(md)
        self.page = page
        self.ctx = ctx

    def run(self, root):
        for element in root.iter():
            href = element.get("href")
            if href is not None:
                element.set("href", rewrite_anchor_href(self.page, self.ctx, href))
            src = element.get("src")
            if src is not None:
                element.set("src", rewrite_image_src(self.page, self.ctx, src))
        return root


class LinkRewriteExtension(Extension):
    def __init__(self, page: Page, ctx: SiteContext):
        super().__init__()
        self.page = page
        self.ctx = ctx

    def extendMarkdown(self, md: markdown.Markdown) -> None:
        md.treeprocessors.register(LinkRewriteProcessor(md, self.page, self.ctx), "rollnw_links", 15)


def render_markdown(page: Page, ctx: SiteContext) -> str:
    text = page.source.read_text(encoding="utf-8")
    md = markdown.Markdown(
        extensions=[
            "extra",
            "fenced_code",
            "tables",
            "toc",
            LinkRewriteExtension(page, ctx),
        ],
        output_format="html5",
    )
    return md.convert(text)


def page_banner(page: Page) -> str:
    if page.status is None and not page.notice:
        return ""
    status = page.status or "notice"
    notice = page.notice or f"Status: {status}."
    return (
        f'<aside class="notice notice-{html.escape(status)}">'
        f'<span class="notice-label">{html.escape(status)}</span>'
        f"<p>{html.escape(notice)}</p>"
        "</aside>"
    )


def render_nav(ctx: SiteContext, current: Page) -> str:
    links = []
    last_section: str | None = None
    for page in ctx.pages:
        if page.section != last_section:
            last_section = page.section
            if page.section:
                links.append(f'<div class="site-nav-section">{html.escape(page.section)}</div>')
        cls = ' class="active"' if page == current else ""
        href = html.escape(relative_url(current.target, page.target))
        links.append(f'<a{cls} href="{href}">{html.escape(page.title)}</a>')
    return "\n".join(links)


def render_page(page: Page, ctx: SiteContext) -> str:
    body = render_markdown(page, ctx)
    css = html.escape(relative_url(page.target, STYLE_TARGET))
    source = html.escape(source_url(ctx, page.source_rel))
    nav = render_nav(ctx, page)
    banner = page_banner(page)
    title = html.escape(f"{page.title} - {ctx.site_title}")
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{title}</title>
  <link rel="stylesheet" href="{css}">
</head>
<body>
  <div class="site-shell">
    <aside class="site-sidebar">
      <a class="site-title" href="{html.escape(relative_url(page.target, ctx.pages[0].target))}">{html.escape(ctx.site_title)}</a>
      <nav class="site-nav" aria-label="Documentation">
{nav}
      </nav>
    </aside>
    <main class="page">
      <div class="page-meta"><a href="{source}">Source</a></div>
      {banner}
      <article class="content">
{body}
      </article>
    </main>
  </div>
</body>
</html>
"""


def write_css(ctx: SiteContext) -> None:
    target = ctx.output_root / STYLE_TARGET.as_posix()
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(
        """* {
  box-sizing: border-box;
}

:root {
  color-scheme: light dark;
  --bg: #f7f7f4;
  --fg: #1d1f21;
  --muted: #62666d;
  --line: #d9d9d2;
  --panel: #ffffff;
  --link: #005f87;
  --code-bg: #ecece6;
  --notice-current: #e8f4ef;
  --notice-transitional: #fff2cf;
  --notice-legacy: #f4e1df;
}

@media (prefers-color-scheme: dark) {
  :root {
    --bg: #151617;
    --fg: #e7e7e1;
    --muted: #a6abb3;
    --line: #383a3d;
    --panel: #202225;
    --link: #7cc8e8;
    --code-bg: #2b2d30;
    --notice-current: #163729;
    --notice-transitional: #433618;
    --notice-legacy: #432421;
  }
}

body {
  margin: 0;
  background: var(--bg);
  color: var(--fg);
  font: 16px/1.55 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
}

a {
  color: var(--link);
}

.site-shell {
  display: grid;
  grid-template-columns: 280px minmax(0, 1fr);
  min-height: 100vh;
}

.site-sidebar {
  background: var(--panel);
  border-right: 1px solid var(--line);
  height: 100vh;
  overflow-y: auto;
  padding: 24px 18px;
  position: sticky;
  top: 0;
}

.site-title {
  color: var(--fg);
  display: block;
  font-size: 20px;
  font-weight: 700;
  margin: 0 0 20px;
  text-decoration: none;
}

.site-nav {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.site-nav-section {
  color: var(--muted);
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0;
  margin: 18px 10px 4px;
  text-transform: uppercase;
}

.site-nav-section:first-child {
  margin-top: 0;
}

.site-nav a {
  border-left: 3px solid transparent;
  color: var(--muted);
  display: block;
  padding: 6px 10px;
  text-decoration: none;
}

.site-nav a.active {
  border-color: var(--link);
  color: var(--fg);
  background: var(--code-bg);
}

.page {
  margin: 0;
  max-width: 960px;
  padding: 36px clamp(24px, 5vw, 64px) 72px;
}

.page-meta {
  font-size: 14px;
  margin-bottom: 14px;
  text-align: right;
}

.notice {
  border: 1px solid var(--line);
  margin: 0 0 28px;
  padding: 12px 14px;
}

.notice-current {
  background: var(--notice-current);
}

.notice-transitional {
  background: var(--notice-transitional);
}

.notice-legacy {
  background: var(--notice-legacy);
}

.notice-label {
  display: inline-block;
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0;
  margin-bottom: 4px;
  text-transform: uppercase;
}

.notice p {
  margin: 0;
}

.content {
  background: transparent;
}

.content h1,
.content h2,
.content h3,
.content h4 {
  line-height: 1.2;
  margin: 1.6em 0 0.55em;
}

.content h1 {
  font-size: 36px;
  margin-top: 0;
}

.content h2 {
  border-bottom: 1px solid var(--line);
  font-size: 26px;
  padding-bottom: 4px;
}

.content p,
.content ul,
.content ol,
.content pre,
.content table {
  margin: 0 0 1em;
}

.content code {
  background: var(--code-bg);
  border-radius: 3px;
  padding: 0.1em 0.25em;
}

.content pre {
  background: var(--code-bg);
  overflow-x: auto;
  padding: 14px;
}

.content pre code {
  background: transparent;
  padding: 0;
}

.content img {
  height: auto;
  max-width: 100%;
}

.content table {
  border-collapse: collapse;
  display: block;
  overflow-x: auto;
}

.content th,
.content td {
  border: 1px solid var(--line);
  padding: 6px 10px;
}

@media (max-width: 860px) {
  .site-shell {
    display: block;
  }

  .site-sidebar {
    border-bottom: 1px solid var(--line);
    border-right: 0;
    height: auto;
    overflow: visible;
    padding: 16px clamp(16px, 4vw, 28px);
    position: static;
  }

  .site-title {
    margin-bottom: 12px;
  }

  .site-nav {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 4px 8px;
  }

  .page {
    padding: 28px clamp(16px, 4vw, 28px) 56px;
  }
}
""",
        encoding="utf-8",
    )


def render_site(ctx: SiteContext) -> None:
    if ctx.output_root.exists():
        shutil.rmtree(ctx.output_root)
    ctx.output_root.mkdir(parents=True, exist_ok=True)
    write_css(ctx)

    for page in ctx.pages:
        html_text = render_page(page, ctx)
        target = (ctx.output_root / page.target.as_posix()).resolve()
        if not is_relative_to(target, ctx.output_root):
            fail(f"{page.source_rel}: target escapes output root")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(html_text, encoding="utf-8")


def build_site(manifest: Path, repo_root: Path, output_root: Path) -> SiteContext:
    repo_root = repo_root.resolve()
    output_root = output_root.resolve()
    ctx = parse_manifest(manifest.resolve(), repo_root, output_root)
    render_site(ctx)
    return ctx


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build the rollNW inline Markdown docs site")
    parser.add_argument("--manifest", type=Path, default=Path("docs.toml"))
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, default=Path("build/docs/site"))
    args = parser.parse_args(argv)

    try:
        ctx = build_site(args.manifest, args.repo_root, args.output)
    except SiteError as exc:
        print(f"docs build failed: {exc}", file=sys.stderr)
        return 1

    print(f"wrote {len(ctx.pages)} pages to {ctx.output_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
