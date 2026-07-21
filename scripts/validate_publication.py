#!/usr/bin/env python3
"""Validate the portable OKF bundle and reject workstation-specific paths."""

from __future__ import annotations

import re
import sys
from pathlib import Path

import yaml

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
OKF = ROOT / ".okf"

# Construct the prior local username so the checker does not flag its own source.
PRIOR_LOCAL_USER = "g" + "thom"
FORBIDDEN = {
    "prior local username": re.compile(re.escape(PRIOR_LOCAL_USER), re.IGNORECASE),
    "absolute Windows user profile": re.compile(r"[A-Za-z]:\\Users\\[^<%\\\s]+", re.IGNORECASE),
    "prior workspace drive": re.compile(r"F:\\ai\\", re.IGNORECASE),
    "absolute Unix home": re.compile(r"/home/(?!<)[^/\s]+/", re.IGNORECASE),
    "WSL-mounted local drive": re.compile(r"/mnt/[a-z]/", re.IGNORECASE),
}

TEXT_SUFFIXES = {
    ".bat", ".cmake", ".cpp", ".h", ".hpp", ".ini", ".json", ".lua",
    ".md", ".ps1", ".py", ".sh", ".toml", ".txt", ".yml", ".yaml",
}

errors: list[str] = []

for path in ROOT.rglob("*"):
    if (
        not path.is_file()
        or path.resolve() == Path(__file__).resolve()
        or ".git" in path.parts
        or path.suffix.lower() not in TEXT_SUFFIXES
    ):
        continue
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        continue
    rel = path.relative_to(ROOT)
    for label, pattern in FORBIDDEN.items():
        for match in pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            errors.append(f"{rel}:{line}: {label}")

if not OKF.is_dir():
    errors.append(".okf: bundle is missing")
else:
    for path in OKF.rglob("*.md"):
        if path.name in {"index.md", "log.md"}:
            continue
        text = path.read_text(encoding="utf-8")
        if not text.startswith("---\n"):
            errors.append(f"{path.relative_to(ROOT)}: missing YAML frontmatter")
            continue
        try:
            end = text.index("\n---\n", 4)
            frontmatter = yaml.safe_load(text[4:end])
        except (ValueError, yaml.YAMLError) as exc:
            errors.append(f"{path.relative_to(ROOT)}: invalid YAML frontmatter: {exc}")
            continue
        if not isinstance(frontmatter, dict) or not str(frontmatter.get("type", "")).strip():
            errors.append(f"{path.relative_to(ROOT)}: missing non-empty frontmatter type")

if errors:
    print("Publication validation failed:")
    for error in errors:
        print(f"  - {error}")
    raise SystemExit(1)

print("Publication validation passed: portable paths and OKF frontmatter are clean.")
