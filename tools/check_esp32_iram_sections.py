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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--objdump", required=True, type=Path)
    parser.add_argument("--object", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.objdump.is_file() or not args.object.is_file():
        print("IRAM section check FAILED: objdump or object is missing")
        return 2

    completed = subprocess.run(
        [str(args.objdump), "-t", "-C", str(args.object)],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        print(completed.stderr, end="")
        return completed.returncode

    raw_completed = subprocess.run(
        [str(args.objdump), "-t", str(args.object)],
        check=False,
        capture_output=True,
        text=True,
    )
    if raw_completed.returncode != 0:
        print(raw_completed.stderr, end="")
        return raw_completed.returncode

    symbols: list[tuple[str, str]] = []
    for line in completed.stdout.splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) == 6 and fields[2] == "F":
            symbols.append((fields[3], fields[5]))

    failures: list[str] = []
    for method in REQUIRED_METHODS:
        matches = [entry for entry in symbols if method in entry[1]]
        if not matches:
            failures.append(f"missing emitted method: {method}")
        for section, name in matches:
            if not section.startswith(".iram1"):
                failures.append(f"{section}: {name}")

    raw_symbols: list[tuple[str, str]] = []
    for line in raw_completed.stdout.splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) == 6 and fields[2] == "F":
            raw_symbols.append((fields[3], fields[5]))

    transfer_callable_prefix = "_ZZN6AT21CS14Esp32Transport9_transfer"
    timing_lambdas = [
        entry for entry in raw_symbols
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
