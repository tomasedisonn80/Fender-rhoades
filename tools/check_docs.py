#!/usr/bin/env python3
"""Check repository Markdown integrity using only the Python standard library.

Subset: single-line inline/image links and reference definitions, ordinary fenced
blocks (0..3 leading spaces), and final newlines. Local destinations may use URL
escapes, Markdown punctuation escapes, angle brackets, and optional link titles.
Targets are resolved relative to their document and must stay inside the root,
including through symlinks. All repository .md/.markdown files are scanned, so
new files need not be staged and Git is not required.

This is not a CommonMark validator. It ignores code spans, indented/fenced code,
standalone HTML comments, external URI schemes/network URLs, queries and fragments.
Explicit template destinations containing <NAME>, {name}, or an ellipsis path
component are ignored; angle-wrapped <path>, <file>, <target>, and <url> are also
placeholders. It does not resolve reference labels, HTML links, multiline links,
or fences nested in lists/quotes. It never checks web availability or executes
examples. Archived .txt research is not scanned (links to it must still exist).
"""
from __future__ import annotations

import argparse
import html
import os
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {".git", "build", "out", "artifacts", "venv", ".venv", "__pycache__"}
FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})(.*)$")
REFERENCE = re.compile(r"^ {0,3}\[(?:\\.|[^\]\\])+\]:[ \t]*")
LINK_END = re.compile(r'''[ \t]*(?:(?:"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\((?:\\.|[^)\\])*\))[ \t]*)?\)''')
ESCAPE = re.compile(r"\\([!\"#$%&'()*+,\-./:;<=>?@\[\]^_`{|}~\\])")


def markdown_files(root: Path):
    """Walk source documents without following directory symlinks."""
    def fail(error):
        raise error
    for directory, folders, files in os.walk(root, onerror=fail, followlinks=False):
        folders[:] = sorted(name for name in folders if name.lower() not in SKIP_DIRS
                            and not (Path(directory) / name).is_symlink())
        for name in sorted(files):
            if Path(name).suffix.lower() in {".md", ".markdown"}:
                yield Path(directory) / name


def mask(text: str) -> str:
    return re.sub(r"[^\r\n]", " ", text)


def mask_code_spans(text: str) -> str:
    """Mask paired, equal-length backtick runs, preserving source line numbers."""
    runs = list(re.finditer(r"`+", text))
    result = list(text)
    index = 0
    while index < len(runs):
        start = runs[index]
        before = text[:start.start()]
        if (len(before) - len(before.rstrip("\\"))) % 2:
            index += 1
            continue
        end = next((j for j in range(index + 1, len(runs))
                    if len(runs[j].group()) == len(start.group())), None)
        if end is None:
            index += 1
            continue
        stop = runs[end].end()
        result[start.start():stop] = mask(text[start.start():stop])
        index = end + 1
    return "".join(result)


def destination(line: str, start: int) -> tuple[str, int] | None:
    """Read a destination, allowing escaped and balanced path parentheses."""
    while start < len(line) and line[start] in " \t":
        start += 1
    angle = start < len(line) and line[start] == "<"
    index, depth = start + int(angle), 0
    while index < len(line):
        character = line[index]
        if character == "\\":
            index += 2
            continue
        if angle:
            if character == ">":
                return line[start:index + 1], index + 1
        elif character.isspace() or character == ")" and depth == 0:
            return line[start:index], index
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        index += 1
    return None if angle or depth else (line[start:index], index)


def destinations(line: str):
    reference = REFERENCE.match(line)
    if reference:
        parsed = destination(line, reference.end())
        if parsed:
            yield parsed[0]
        return
    index, brackets = 0, 0
    while index < len(line):
        if line[index] == "\\":
            index += 2
            continue
        if line[index] == "[":
            brackets += 1
        elif line[index] == "]" and brackets:
            brackets -= 1
            if line[index:index + 2] == "](":
                parsed = destination(line, index + 2)
                if parsed:
                    target, end = parsed
                    closing = LINK_END.match(line, end)
                    if closing:
                        yield target
                        index = closing.end()
                        continue
        index += 1


def target_error(root: Path, document: Path, raw: str) -> str | None:
    target = raw
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
        if re.fullmatch(r"[A-Z][A-Z0-9_]*|path|file|target|url|\.\.\.", target):
            return None
    if re.search(r"<[^<>]+>|\{[^{}]+\}|(?:^|/)(?:\.\.\.|…)(?:/|$)", target):
        return None
    target = html.unescape(ESCAPE.sub(r"\1", target))
    if re.match(r"^[A-Za-z]:[\\/]", target) or target.lower().startswith("file:"):
        return f"local link must be repository-relative: {raw!r}"
    if target.startswith("//") or re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", target):
        return None
    try:
        path = unquote(urlsplit(target).path, errors="strict")
        if not path:
            return None
        if "\\" in path or any(ord(char) < 32 for char in path):
            return f"invalid local link path: {raw!r}"
        if Path(path).is_absolute() or re.match(r"^[A-Za-z]:[\\/]", path):
            return f"local link must be repository-relative: {raw!r}"
        resolved = (document.parent / path).resolve()
        if not resolved.is_relative_to(root):
            return f"local link escapes repository: {raw!r}"
        if not resolved.exists():
            return f"missing local link target: {raw!r}"
    except (OSError, ValueError, RuntimeError) as error:
        return f"invalid local link {raw!r}: {error}"
    return None


def check_document(root: Path, document: Path) -> list[str]:
    label = document.relative_to(root).as_posix()
    try:
        if not document.resolve().is_relative_to(root):
            return [f"{label}:1: Markdown file escapes repository"]
        text = document.read_text(encoding="utf-8")
    except (OSError, UnicodeError, RuntimeError) as error:
        return [f"{label}:1: cannot read Markdown: {error}"]
    issues, visible = [], []
    fence = None
    comment = False
    for number, line in enumerate(text.splitlines(keepends=True), 1):
        match = FENCE.match(line.rstrip("\r\n"))
        if fence:
            delimiter = fence[0]
            if (match and match[1][0] == delimiter[0]
                    and len(match[1]) >= len(delimiter) and not match[2].strip()):
                fence = None
            visible.append(mask(line))
            continue
        # Only standalone comment blocks belong to this scanner's subset.
        # A literal comment marker in a fenced example must not consume prose.
        if comment or (not line.startswith(("    ", "\t"))
                       and line.lstrip(" ").startswith("<!--")):
            closing = line.find("-->")
            if closing < 0:
                comment = True
                visible.append(mask(line))
                continue
            comment = False
            line = mask(line[:closing + 3]) + line[closing + 3:]
            match = FENCE.match(line.rstrip("\r\n"))
        if match and not (match[1][0] == "`" and "`" in match[2]):
            fence = (match[1], number)
            visible.append(mask(line))
        elif line.startswith(("    ", "\t")):
            visible.append(mask(line))
        else:
            visible.append(line)
    if fence:
        issues.append(f"{label}:{fence[1]}: unclosed code fence {fence[0]!r}")
    if text and not text.endswith("\n"):
        issues.append(f"{label}:{len(text.splitlines())}: missing final newline")
    for number, line in enumerate(mask_code_spans("".join(visible)).splitlines(), 1):
        for target in destinations(line):
            error = target_error(root, document, target)
            if error:
                issues.append(f"{label}:{number}: {error}")
    return issues


def check_repository(root: Path) -> list[str]:
    root = root.resolve()
    return [issue for path in markdown_files(root) for issue in check_document(root, path)]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (default: script's repository)")
    arguments = parser.parse_args(argv)
    if not arguments.root.is_dir():
        parser.error("--root must be an existing directory")
    try:
        issues = check_repository(arguments.root)
    except OSError as error:
        print(f"Documentation check failed: {error}", file=sys.stderr)
        return 2
    if issues:
        print("\n".join(issues), file=sys.stderr)
        return 1
    print("Documentation integrity checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
