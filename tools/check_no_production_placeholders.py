#!/usr/bin/env python3
"""Reject deferred-implementation markers in production source trees."""

from __future__ import annotations

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (ROOT / "include", ROOT / "src")
FORBIDDEN = ("todo", "fixme", "placeholder", "not implemented")


def main() -> int:
    missing = [path for path in SOURCE_ROOTS if not path.is_dir()]
    if missing:
        for path in missing:
            print(f"missing source root: {path.relative_to(ROOT).as_posix()}")
        return 1

    hits: list[tuple[str, int, str]] = []
    for source_root in SOURCE_ROOTS:
        for path in sorted(item for item in source_root.rglob("*") if item.is_file()):
            relative = path.relative_to(ROOT).as_posix()
            for line_number, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(),
                start=1,
            ):
                folded = line.casefold()
                if any(term in folded for term in FORBIDDEN):
                    hits.append((relative, line_number, line))

    for relative, line_number, line in sorted(hits):
        print(f"{relative}:{line_number}:{line}")
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
