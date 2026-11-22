#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import re
import shlex
import sys
import textwrap
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

ESP32_PREDICATE = "defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)"
TRANSPORT_GUARDS = {
    "include/AT21CS/platform/esp32/Esp32Transport.h": (
        (f"!({ESP32_PREDICATE}) && !defined(AT21CS_TESTING)", "#error"),
        (ESP32_PREDICATE, "#include <esp_attr.h>"),
        (ESP32_PREDICATE, "portMUX_TYPE"),
        (f"defined(AT21CS_TESTING) && !({ESP32_PREDICATE})", "struct TestLineEvent"),
    ),
    "src/platform/esp32/Esp32Transport.cpp": (
        (ESP32_PREDICATE, "#include <driver/gpio.h>"),
    ),
}

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


def check_transport_guards(errors: list[str]) -> None:
    for rel, sites in TRANSPORT_GUARDS.items():
        path = ROOT / rel
        if not path.is_file():
            errors.append(f"missing ESP32 transport file: {rel}")
            continue
        raw = path.read_text(encoding="utf-8")
        lines = [line.strip() for line in strip_non_code(raw).splitlines() if line.strip()]
        for condition, first_line in sites:
            if not any(
                guard == f"#if {condition}" and body.startswith(first_line)
                for guard, body in zip(lines, lines[1:])
            ):
                errors.append(f"missing ESP32 guard for {first_line} in {rel}")
        code = "\n".join(lines)
        unpaired = code.replace(ESP32_PREDICATE, "")
        if re.search(r"\b(?:ARDUINO_ARCH_ESP32|ESP_PLATFORM)\b", unpaired):
            errors.append(f"unpaired ESP32 framework predicate in {rel}")
        if "UNSUPPORTED_COMMAND" in code:
            errors.append(f"obsolete runtime stub in ESP32 transport: {rel}")
        if path.suffix == ".h" and not re.search(
            r'^#error "[^"\n]*Arduino-ESP32[^"\n]*ESP-IDF[^"\n]*"$', raw, re.M
        ):
            errors.append("ESP32 transport rejection must name Arduino-ESP32 and ESP-IDF")


def check_idf_component(version: str, errors: list[str]) -> None:
    if not (ROOT / "test/consumer/phy_smoke/idf/CMakeLists.txt").is_file():
        errors.append("missing native ESP-IDF smoke project")

    try:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        manifest = (ROOT / "idf_component.yml").read_text(encoding="utf-8")
    except OSError as exc:
        errors.append(f"cannot read ESP-IDF component: {exc}")
        return

    cmake = re.sub(r"#[^\n]*", "", cmake)
    registrations = re.findall(r"\bidf_component_register\s*\(([^()]*)\)", cmake)
    if len(registrations) != 1:
        errors.append("expected one idf_component_register at the repository root")
    else:
        prefix = "${CMAKE_CURRENT_LIST_DIR}/"
        expected = {
            "SRCS": [prefix + path for path in (
                "src/AT21CS.cpp", "src/Bus.cpp", "src/platform/esp32/Esp32Transport.cpp",
            )],
            "INCLUDE_DIRS": [prefix + "include"],
            "PRIV_REQUIRES": ["esp_driver_gpio", "esp_hal_gpio", "esp_pm", "esp_timer"],
            "LDFRAGMENTS": ["${CMAKE_CURRENT_BINARY_DIR}/at21cs_pm.lf"],
        }
        actual: dict[str, list[str]] = {}
        key = ""
        try:
            for token in shlex.split(registrations[0]):
                if token.isupper():
                    key = token
                    actual.setdefault(key, [])
                else:
                    actual.setdefault(key, []).append(token)
        except ValueError as exc:
            errors.append(f"invalid idf_component_register arguments: {exc}")
        if {key: sorted(values) for key, values in actual.items()} != expected:
            errors.append(f"unexpected ESP-IDF component registration: {actual}")

    # Read the manifest's small scalar and list fields without a YAML dependency.
    manifest = re.sub(r"(?m)^[ \t]*#[^\n]*\n|[ \t]+#[^\n]*", "", manifest)

    def field(key: str) -> str:
        # A top-level sequence can start in column one and contain blank lines.
        matches = re.findall(rf"^{key}:(.*?)(?=^[^\s-]|\Z)", manifest, re.M | re.S)
        return textwrap.dedent(matches[0]).strip() if len(matches) == 1 else ""

    if not isinstance(version, str) or not version or field("version") not in (
        version, f'"{version}"', f"'{version}'",
    ):
        errors.append("ESP-IDF manifest version must match library.json")
    targets = field("targets")
    if targets.startswith("[") and targets.endswith("]"):
        entries = targets[1:-1].split(",")
    else:
        entries = [
            line[2:] if line.startswith("- ") else ""
            for line in targets.splitlines() if line.strip()
        ]
    if sorted(entry.strip().strip("'\"") for entry in entries) != ["esp32s2", "esp32s3"]:
        errors.append("ESP-IDF manifest targets must be esp32s2 and esp32s3")
    sdk = re.findall(r"^idf:[ \t]*(.*)$", field("dependencies"), re.M)
    if sdk not in (['">=6.0.1"'], ["'>=6.0.1'"]):
        errors.append('ESP-IDF manifest must require idf: ">=6.0.1"')


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

    check_transport_guards(errors)

    library_json = ROOT / "library.json"
    try:
        metadata = json.loads(library_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"cannot parse library.json: {exc}")
    else:
        check_idf_component(metadata.get("version", ""), errors)
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

