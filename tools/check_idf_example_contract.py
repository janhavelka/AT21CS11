#!/usr/bin/env python3
"""ESP-IDF wrapper contract check.

AT21CS keeps the shared Arduino/ESP-IDF CLI checks in
`check_cli_contract.py`. This dedicated entry point exists so CI and agents can
run the same IDF-specific contract name used by the other driver repositories.
"""

from __future__ import annotations

import pathlib
import runpy
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main() -> int:
    script = ROOT / "tools" / "check_cli_contract.py"
    runpy.run_path(str(script), run_name="__main__")
    return 0


if __name__ == "__main__":
    sys.exit(main())
