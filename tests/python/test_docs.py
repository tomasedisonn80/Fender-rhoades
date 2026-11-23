"""Behavioral checks for the deliberately limited Markdown integrity scanner."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
from pathlib import Path, PureWindowsPath
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("rhodes_docs", ROOT / "tools/check_docs.py")
assert SPEC and SPEC.loader
docs = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(docs)


class DocumentationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()

    def write(self, path, text):
        destination = self.root / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(text, encoding="utf-8")
        return destination

    def test_valid_paths_titles_and_reference_definitions(self):
        self.write("guide/read me (draft).md", "# Guide\n")
        self.write("asset.svg", "<svg/>\n")
        self.write("research.txt", "Archived prose without a final newline")
        self.write("README.md", "\n".join([
            '[Guide](guide/read%20me%20(draft).md#unknown-heading "A title")',
            r'[Escaped](<guide/read me \(draft\).md>)',
            '[![Image](asset.svg)](guide/read%20me%20(draft).md)',
            "[Research][source]", "[source]: research.txt 'Research archive'",
            "[Heading](#intentionally-unchecked)", "[Query](?view=plain)",
            "[Web](https://example.invalid/absent) [Mail](mailto:example@example.invalid)",
            "[Network](//example.invalid/absent)", "",
        ]))
        self.assertEqual(docs.check_repository(self.root), [])

    def test_missing_target_reports_document_and_line(self):
        self.write("docs/page.md", "# Page\n[Missing](absent.md)\n")
        issues = docs.check_repository(self.root)
        self.assertEqual(len(issues), 1)
        self.assertIn("docs/page.md:2: missing local link target: 'absent.md'", issues[0])

    def test_traversal_and_url_escaped_traversal_are_rejected(self):
        for target in ("../../outside.md", "%2e%2e/%2e%2e/outside.md", r"\.\./\.\./outside.md"):
            with self.subTest(target=target):
                self.write("docs/page.md", f"[Outside]({target})\n")
                issues = docs.check_repository(self.root)
                self.assertEqual(len(issues), 1)
                self.assertIn("escapes repository", issues[0])

    def test_absolute_and_encoded_absolute_paths_are_rejected(self):
        for target in ("/etc/passwd", "%2fetc/passwd", "C:/outside.md", "%43%3a/outside.md", "file:///outside.md"):
            with self.subTest(target=target):
                self.write("README.md", f"[Outside]({target})\n")
                self.assertIn("must be repository-relative", docs.check_repository(self.root)[0])

    def test_slash_rooted_links_have_the_same_policy_with_windows_path_semantics(self):
        # Exercise the Windows classification on every CI host. Resolution still
        # uses the temporary repository, so a missed root reports the wrong class.
        with patch.object(docs, "Path", PureWindowsPath):
            for target in ("/etc/passwd", "%2fetc/passwd", "%2Fetc/passwd",
                           "%2f%2fserver/share.md"):
                with self.subTest(target=target):
                    error = docs.target_error(self.root, self.root / "README.md", target)
                    self.assertIn("must be repository-relative", error)

    def test_symlink_target_cannot_escape_repository(self):
        with tempfile.TemporaryDirectory() as outside:
            try:
                (self.root / "linked").symlink_to(outside, target_is_directory=True)
            except (OSError, NotImplementedError):
                self.skipTest("Host does not permit symlinks")
            Path(outside, "outside.md").write_text("# External\n", encoding="utf-8")
            self.write("README.md", "[Outside](linked/outside.md)\n")
            self.assertIn("escapes repository", docs.check_repository(self.root)[0])

    def test_code_comments_and_explicit_placeholders_do_not_become_links(self):
        self.write("README.md", "\n".join([
            "`[Example](missing.md)` and ``literal `[Example](missing.md)` ``",
            "`a code span crossing", "[Example](missing.md)`",
            "    [Indented example](missing.md)",
            "<!-- [Comment](missing.md) -->",
            "<!--", "```", "[Commented example](missing.md)", "-->",
            "```markdown", "[Fenced example](missing.md)", "```",
            "```html", "<!-- literal comment marker", "```",
            "[Template](docs/<NAME>.md) [Template]({report})",
            "[Template](docs/.../report.md) [Template](<path>)", "",
        ]))
        self.assertEqual(docs.check_repository(self.root), [])

    def test_backticks_in_label_do_not_hide_real_link(self):
        self.write("README.md", "[`Missing API`](missing.md)\n")
        self.assertIn("missing local link target", docs.check_repository(self.root)[0])

    def test_unbalanced_fence_requires_matching_type_and_minimum_length(self):
        for ending in ("", "~~~\n", "``\n", "``` trailing text\n"):
            with self.subTest(ending=ending):
                self.write("README.md", "# Page\n```python\nprint('example')\n" + ending)
                self.assertEqual(docs.check_repository(self.root), ["README.md:2: unclosed code fence '```'"])
        self.write("README.md", "````markdown\n```\n[Example](missing.md)\n`````\n")
        self.assertEqual(docs.check_repository(self.root), [])

    def test_missing_final_newline(self):
        self.write("README.md", "# Page")
        self.assertEqual(docs.check_repository(self.root), ["README.md:1: missing final newline"])

    def test_generated_directories_and_text_archives_are_not_scanned(self):
        for directory in (".git", "build", "out", "artifacts", "venv", ".venv"):
            self.write(f"{directory}/broken.md", "```\n[Missing](absent)")
        self.write("docs/research/archive.txt", "```\n[Missing](absent)")
        self.write("README.md", "[Archive](docs/research/archive.txt)\n")
        self.assertEqual(docs.check_repository(self.root), [])

    def test_cli_returns_failure_with_diagnostic(self):
        self.write("README.md", "[Missing](missing.md)\n")
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            status = docs.main(["--root", str(self.root)])
        self.assertEqual(status, 1)
        self.assertEqual(output.getvalue(), "")
        self.assertIn("README.md:1: missing local link target", errors.getvalue())

    def test_current_repository_documentation(self):
        self.assertEqual(docs.check_repository(ROOT), [])


if __name__ == "__main__":
    unittest.main()
