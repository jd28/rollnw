from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import build_site


class BuildSiteTests(unittest.TestCase):
    def test_missing_manifest_source_fails(self) -> None:
        with Fixture() as fx:
            fx.write_manifest(
                """
site_title = "test"
repo_url = "https://example.test/repo"
source_branch = "main"

[[pages]]
source = "missing.md"
target = "index.html"
title = "Missing"
"""
            )

            with self.assertRaisesRegex(build_site.SiteError, "source file not found"):
                fx.build()

    def test_duplicate_target_fails(self) -> None:
        with Fixture() as fx:
            fx.write("a.md", "# A")
            fx.write("b.md", "# B")
            fx.write_manifest(
                """
site_title = "test"
repo_url = "https://example.test/repo"
source_branch = "main"

[[pages]]
source = "a.md"
target = "index.html"
title = "A"

[[pages]]
source = "b.md"
target = "index.html"
title = "B"
"""
            )

            with self.assertRaisesRegex(build_site.SiteError, "duplicate target"):
                fx.build()

    def test_unsafe_target_fails(self) -> None:
        with Fixture() as fx:
            fx.write("a.md", "# A")
            fx.write_manifest(
                """
site_title = "test"
repo_url = "https://example.test/repo"
source_branch = "main"

[[pages]]
source = "a.md"
target = "../index.html"
title = "A"
"""
            )

            with self.assertRaisesRegex(build_site.SiteError, "clean repository-relative"):
                fx.build()

    def test_invalid_status_fails(self) -> None:
        with Fixture() as fx:
            fx.write("a.md", "# A")
            fx.write_manifest(
                """
site_title = "test"
repo_url = "https://example.test/repo"
source_branch = "main"

[[pages]]
source = "a.md"
target = "index.html"
title = "A"
status = "stale"
"""
            )

            with self.assertRaisesRegex(build_site.SiteError, "invalid status"):
                fx.build()

    def test_transitional_banner_renders(self) -> None:
        with Fixture() as fx:
            fx.write("a.md", "# A")
            fx.write_manifest(
                """
site_title = "test"
repo_url = "https://example.test/repo"
source_branch = "main"

[[pages]]
source = "a.md"
target = "index.html"
title = "A"
status = "transitional"
notice = "Moving."
"""
            )

            fx.build()

            html = fx.read_output("index.html")
            self.assertIn("notice-transitional", html)
            self.assertIn("Moving.", html)

    def test_site_uses_sidebar_navigation(self) -> None:
        with Fixture() as fx:
            fx.write("a.md", "# A")
            fx.write_manifest(
                """
site_title = "test"
repo_url = "https://example.test/repo"
source_branch = "main"

[[pages]]
source = "a.md"
target = "index.html"
title = "A"
"""
            )

            fx.build()

            html = fx.read_output("index.html")
            css = fx.read_output("_static/site.css")
            self.assertIn('class="site-sidebar"', html)
            self.assertIn("grid-template-columns: 280px minmax(0, 1fr);", css)
            self.assertNotIn('class="site-header"', html)

    def test_sidebar_navigation_uses_manifest_sections(self) -> None:
        with Fixture() as fx:
            fx.write("a.md", "# A")
            fx.write("b.md", "# B")
            fx.write_manifest(
                """
site_title = "test"
repo_url = "https://example.test/repo"
source_branch = "main"

[[pages]]
source = "a.md"
target = "index.html"
title = "A"
section = "Alpha"

[[pages]]
source = "b.md"
target = "b.html"
title = "B"
section = "Beta"
"""
            )

            fx.build()

            html = fx.read_output("index.html")
            css = fx.read_output("_static/site.css")
            self.assertIn('<div class="site-nav-section">Alpha</div>', html)
            self.assertIn('<div class="site-nav-section">Beta</div>', html)
            self.assertIn(".site-nav-section", css)

    def test_manifest_markdown_link_rewrites_to_html(self) -> None:
        with Fixture() as fx:
            fx.write("a.md", "# A\n\n[Go](docs/b.md#part)\n")
            fx.write("docs/b.md", "# B\n\n## Part\n")
            fx.write_manifest(
                """
site_title = "test"
repo_url = "https://example.test/repo"
source_branch = "main"

[[pages]]
source = "a.md"
target = "index.html"
title = "A"

[[pages]]
source = "docs/b.md"
target = "docs/b.html"
title = "B"
"""
            )

            fx.build()

            self.assertIn('href="docs/b.html#part"', fx.read_output("index.html"))

    def test_missing_local_markdown_link_fails(self) -> None:
        with Fixture() as fx:
            fx.write("a.md", "# A\n\n[Missing](docs/missing.md)\n")
            fx.write_manifest(
                """
site_title = "test"
repo_url = "https://example.test/repo"
source_branch = "main"

[[pages]]
source = "a.md"
target = "index.html"
title = "A"
"""
            )

            with self.assertRaisesRegex(build_site.SiteError, "linked file does not exist"):
                fx.build()

    def test_non_manifest_markdown_link_rewrites_to_source_url(self) -> None:
        with Fixture() as fx:
            fx.write("a.md", "# A\n\n[Issue](issues/one.md)\n")
            fx.write("issues/one.md", "# One")
            fx.write_manifest(
                """
site_title = "test"
repo_url = "https://example.test/repo"
source_branch = "main"

[[pages]]
source = "a.md"
target = "index.html"
title = "A"
"""
            )

            fx.build()

            self.assertIn('href="https://example.test/repo/blob/main/issues/one.md"', fx.read_output("index.html"))

    def test_images_are_copied(self) -> None:
        with Fixture() as fx:
            fx.write("docs/a.md", "# A\n\n![Alt](img/pixel.gif)\n")
            fx.write_bytes("docs/img/pixel.gif", b"GIF89a")
            fx.write_manifest(
                """
site_title = "test"
repo_url = "https://example.test/repo"
source_branch = "main"

[[pages]]
source = "docs/a.md"
target = "docs/index.html"
title = "A"
"""
            )

            fx.build()

            self.assertIn('src="../assets/docs/img/pixel.gif"', fx.read_output("docs/index.html"))
            self.assertEqual(b"GIF89a", fx.read_output_bytes("assets/docs/img/pixel.gif"))


class Fixture:
    def __enter__(self) -> "Fixture":
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.output = self.root / "out"
        self.manifest = self.root / "docs.toml"
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.temp.cleanup()

    def write(self, rel: str, text: str) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def write_bytes(self, rel: str, data: bytes) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def write_manifest(self, text: str) -> None:
        self.manifest.write_text(text.lstrip(), encoding="utf-8")

    def build(self) -> build_site.SiteContext:
        return build_site.build_site(self.manifest, self.root, self.output)

    def read_output(self, rel: str) -> str:
        return (self.output / rel).read_text(encoding="utf-8")

    def read_output_bytes(self, rel: str) -> bytes:
        return (self.output / rel).read_bytes()


if __name__ == "__main__":
    unittest.main()
