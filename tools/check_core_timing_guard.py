#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import re
import sys
from typing import Dict

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCAN_DIRS = ("src", "include", "examples")
VALID_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}
CLEAN_CORE_HEADERS = (
    "include/AT21CS/AT21CS.h",
    "include/AT21CS/Bus.h",
    "include/AT21CS/Config.h",
    "include/AT21CS/Types.h",
    "include/AT21CS/Transport.h",
    "include/AT21CS/Status.h",
    "include/AT21CS/CommandTable.h",
)

ARDUINO_TRANSPORT_FILES = (
    "include/AT21CS/platform/esp32/Esp32Transport.h",
    "src/platform/esp32/Esp32Transport.cpp",
)

FORBIDDEN_NATIVE_IDF_BUILD_PATHS = (
    "CMakeLists.txt",
    "idf_component.yml",
    "test/consumer/phy_smoke/idf",
)

FORBIDDEN_CLEAN_HEADER_TOKENS = (
    "Arduino.h",
    "Wire.h",
    "ARDUINO",
    "ESP_PLATFORM",
    "AT21CS_PLATFORM",
    "FreeRTOS",
    "freertos",
    "portMUX",
    "IRAM_ATTR",
    "soc/gpio",
    "driver/gpio",
    "esp_",
)

FORBIDDEN_CALLS = {
    "millis": re.compile(r"\bmillis\s*\("),
    "micros": re.compile(r"\bmicros\s*\("),
    "delayMicroseconds": re.compile(r"\bdelayMicroseconds\s*\("),
    "yield": re.compile(r"\byield\s*\("),
}

INCLUDE_ARDUINO_RE = re.compile(r'^\s*#\s*include\s*[<\"]Arduino\.h[>\"]', re.MULTILINE)
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')

FRAMEWORK_TOKENS = FORBIDDEN_CLEAN_HEADER_TOKENS

FORBIDDEN_V1_PUBLIC_PATTERNS = {
    "include/AT21CS/AT21CS.h": (
        r"\btick\s*\(",
        r"\bwaitReady\s*\(",
        r"\breadCurrentAddress\s*\(",
        r"\bwriteEepromByte\s*\(",
        r"\bwriteSecurityUserByte\s*\(",
        r"\blockSecurityRegister\s*\(",
        r"\bisSecurityLocked\s*\(",
        r"\breadRomZoneRegister\s*\(",
        r"\bisZoneRom\s*\(",
        r"\bsetZoneRom\s*\(",
        r"\bfreezeRomZones\s*\(",
        r"\bareRomZonesFrozen\s*\(",
        r"\bdetectPart\s*\(",
        r"\bresetAndDiscover\s*\(",
        r"\bisPresent\s*\(",
        r"\bdriverState\s*\(",
        r"\bgetConfig\s*\(",
        r"\bgetSettings\s*\(",
        r"\bsetHighSpeed\s*\(",
        r"\bisHighSpeed\s*\(",
        r"\bsetStandardSpeed\s*\(",
        r"\bisStandardSpeed\s*\(",
        r"\bcrc8_31\s*\(",
    ),
    "include/AT21CS/Transport.h": (
        r"\bSingleWireTimingProfile\b",
        r"\bwriteByteReadAck\b",
        r"\breadByteSendAck\b",
    ),
    "include/AT21CS/Config.h": (
        r"\bsioPin\b",
        r"\bpresencePin\b",
        r"\bwriteTimeoutMs\b",
        r"\bdiscoveryRetries\b",
        r"\bnowMs\b",
        r"\bsleepUs\b",
        r"\btimeUser\b",
        r"\btransport\b",
    ),
    "include/AT21CS/Status.h": (r"\binProgress\s*\(",),
}


def framework_tokens_allowed(rel: str) -> bool:
    return (
        rel.startswith("src/platform/")
        or rel.startswith("src/backends/")
        or rel.startswith("include/AT21CS/platform/")
        or rel.startswith("examples/")
    )


def strip_non_code(text: str) -> str:
    text = BLOCK_COMMENT_RE.sub("", text)
    text = LINE_COMMENT_RE.sub("", text)
    return STRING_RE.sub('""', text)


def collect_sources() -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for dirname in SCAN_DIRS:
        root = ROOT / dirname
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in VALID_SUFFIXES:
                files.append(path)
    return files


def main() -> int:
    observed_calls: Dict[str, Dict[str, int]] = {}
    observed_includes: Dict[str, int] = {}
    observed_framework_tokens: Dict[str, list[str]] = {}

    for path in collect_sources():
        rel = path.relative_to(ROOT).as_posix()
        raw = path.read_text(encoding="utf-8", errors="replace")
        code = strip_non_code(raw)

        call_counts: Dict[str, int] = {}
        for call_name, pattern in FORBIDDEN_CALLS.items():
            count = len(pattern.findall(code))
            if count > 0:
                call_counts[call_name] = count
        if call_counts:
            observed_calls[rel] = call_counts

        include_count = len(INCLUDE_ARDUINO_RE.findall(raw))
        if include_count > 0:
            observed_includes[rel] = include_count

        if not framework_tokens_allowed(rel):
            token_hits = [token for token in FRAMEWORK_TOKENS if token in code]
            if token_hits:
                observed_framework_tokens[rel] = token_hits

    errors: list[str] = []

    for rel, counts in observed_calls.items():
        if not framework_tokens_allowed(rel):
            errors.append(f"forbidden timing calls in core/public file: {rel} -> {counts}")

    for rel, count in observed_includes.items():
        if not framework_tokens_allowed(rel):
            errors.append(f"forbidden Arduino include in core/public file: {rel} -> {count}")

    for rel, tokens in observed_framework_tokens.items():
        errors.append(f"forbidden framework tokens in core/public file: {rel} -> {tokens}")

    for rel in CLEAN_CORE_HEADERS:
        path = ROOT / rel
        if not path.exists():
            errors.append(f"missing clean core header: {rel}")
            continue
        raw = path.read_text(encoding="utf-8", errors="replace")
        for token in FORBIDDEN_CLEAN_HEADER_TOKENS:
            if token in raw:
                errors.append(f"forbidden framework token in clean core header {rel}: {token}")

    for rel, patterns in FORBIDDEN_V1_PUBLIC_PATTERNS.items():
        path = ROOT / rel
        if not path.exists():
            errors.append(f"missing public contract header: {rel}")
            continue
        code = strip_non_code(path.read_text(encoding="utf-8", errors="replace"))
        for pattern in patterns:
            if re.search(pattern, code):
                errors.append(f"obsolete v1 public symbol in {rel}: {pattern}")

    for rel in ARDUINO_TRANSPORT_FILES:
        path = ROOT / rel
        if not path.exists():
            errors.append(f"missing Arduino ESP32 transport file: {rel}")
            continue
        code = strip_non_code(path.read_text(encoding="utf-8", errors="replace"))
        if "ESP_PLATFORM" in code:
            errors.append(f"native-IDF ESP_PLATFORM branch in Arduino transport: {rel}")
        if "UNSUPPORTED_COMMAND" in code:
            errors.append(f"obsolete non-Arduino runtime stub in ESP32 transport: {rel}")

    transport_header = ROOT / ARDUINO_TRANSPORT_FILES[0]
    transport_header_text = transport_header.read_text(
        encoding="utf-8", errors="replace"
    )
    if (
        "!defined(ARDUINO_ARCH_ESP32) && !defined(AT21CS_TESTING)"
        not in transport_header_text
        or '#error "Esp32Transport is available only for Arduino-ESP32"'
        not in transport_header_text
    ):
        errors.append(
            "ESP32 transport must reject non-Arduino production compilation"
        )

    for rel in FORBIDDEN_NATIVE_IDF_BUILD_PATHS:
        if (ROOT / rel).exists():
            errors.append(f"unsupported native-IDF build path remains: {rel}")

    library_json = ROOT / "library.json"
    try:
        metadata = json.loads(library_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"cannot parse library.json: {exc}")
    else:
        frameworks = metadata.get("frameworks", [])
        if frameworks != ["arduino"]:
            errors.append(
                "library.json frameworks must contain only arduino: "
                f"{frameworks!r}"
            )

    if errors:
        print("Core timing guard FAILED:")
        for err in errors:
            print(f"- {err}")
        return 1

    print("Core timing guard PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())

