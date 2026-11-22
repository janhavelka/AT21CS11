#!/usr/bin/env python3
"""Verify that every emitted ESP32 timing helper uses an IRAM section."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


REQUIRED_METHODS = (
    "Esp32Transport::_transfer(",
    "Esp32Transport::_resetAndDiscover(",
    "Esp32Transport::_nowUs(",
    "Esp32Transport::_beginSegment(",
    "Esp32Transport::_spinFrom(",
    "Esp32Transport::_cyclesForNs(",
    "Esp32Transport::_segmentExpired(",
    "Esp32Transport::_cycleCount(",
    "Esp32Transport::_enterCritical(",
    "Esp32Transport::_exitCritical(",
    "Esp32Transport::_setLine(",
    "Esp32Transport::_readLine(",
    "Esp32Transport::_writeBit(",
    "Esp32Transport::_readBit(",
    "Esp32Transport::_writeEightBits(",
    "Esp32Transport::_readEightBits(",
    "Esp32Transport::_readAck(",
)

PM_LOCK_METHODS = (
    "AT21CS::Esp32Transport::_acquireTimingLock()",
    "AT21CS::Esp32Transport::_releaseTimingLock()",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--objdump", required=True, type=Path)
    parser.add_argument("--object", required=True, type=Path)
    parser.add_argument(
        "--elf", type=Path, metavar="PM_ELF",
        help="check PM timing lock wrappers in the linked ESP-IDF image",
    )
    parser.add_argument(
        "--require-undefined", nargs="+", action="extend", default=[], metavar="SYMBOL",
        help="require each named external reference in the object symbol table",
    )
    return parser.parse_args()


def symbol_table(objdump: Path, path: Path, *, demangle: bool = False) -> str:
    command = [str(objdump), "-t"]
    if demangle:
        command.append("-C")
    return subprocess.check_output(
        [*command, str(path)], text=True, stderr=subprocess.STDOUT,
    )


def function_symbols(table: str) -> list[tuple[str, str]]:
    symbols: list[tuple[str, str]] = []
    for line in table.splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) == 6 and fields[2] == "F":
            symbols.append((fields[3], fields[5]))
    return symbols


def main() -> int:
    args = parse_args()
    if not args.objdump.is_file() or not args.object.is_file():
        print("IRAM section check FAILED: objdump or object is missing")
        return 2
    failures: list[str] = []
    try:
        symbols = function_symbols(symbol_table(args.objdump, args.object, demangle=True))
        raw_table = symbol_table(args.objdump, args.object)
        if args.elf:
            linked = function_symbols(symbol_table(args.objdump, args.elf, demangle=True))
            for method in PM_LOCK_METHODS:
                sections = {section for section, name in linked if method in name}
                if sections != {".iram0.text"}:
                    failures.append(f"linked {method}: expected .iram0.text, found {sorted(sections)}")
    except subprocess.CalledProcessError as exc:
        print(exc.output, end="")
        return exc.returncode

    for method in REQUIRED_METHODS:
        matches = [entry for entry in symbols if method in entry[1]]
        if not matches:
            failures.append(f"missing emitted method: {method}")
        for section, name in matches:
            if not section.startswith(".iram1"):
                failures.append(f"{section}: {name}")

    undefined: set[str] = set()
    for line in raw_table.splitlines():
        fields = line.split(maxsplit=5)
        if "*UND*" in fields:
            undefined.add(fields[-1])

    for name in args.require_undefined:
        if name not in undefined:
            failures.append(f"missing undefined reference: {name}")

    transfer_callable_prefix = "_ZZN6AT21CS14Esp32Transport9_transfer"
    timing_lambdas = [
        entry for entry in function_symbols(raw_table)
        if entry[1].startswith(transfer_callable_prefix)
    ]
    for section, name in timing_lambdas:
        if not section.startswith(".iram1"):
            failures.append(f"{section}: {name}")

    for optional_helper in ("phaseAfterAck(", "failure("):
        for section, name in symbols:
            if optional_helper in name and not section.startswith(".iram1"):
                failures.append(f"{section}: {name}")

    if failures:
        print("IRAM section check FAILED")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print(
        "IRAM section check PASSED: "
        f"{len(REQUIRED_METHODS)} timing methods and "
        f"{len(timing_lambdas)} emitted timing lambdas"
    )
    if args.require_undefined:
        print("Required undefined references: " + ", ".join(args.require_undefined))
    if args.elf:
        print("Linked PM timing lock wrappers: .iram0.text")
    return 0


if __name__ == "__main__":
    sys.exit(main())
