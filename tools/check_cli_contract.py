#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

REQUIRED_COMMON = [
    "BoardConfig.h",
    "BuildConfig.h",
    "Log.h",
    "TransportAdapter.h",
    "BusDiag.h",
    "CliShell.h",
    "HealthView.h",
]

MANDATORY_COMMANDS = ["help", "scan", "probe", "recover", "drv", "read", "verbose", "stress"]
IDF_EXAMPLE_MACRO = "AT21CS_EXAMPLE_PLATFORM_IDF"
IDF_REQUIRED_COMPONENTS = [
    "AT21CS11",
    "esp_driver_gpio",
    "esp_timer",
    "esp_hw_support",
    "freertos",
    "vfs",
]
IDF_NATIVE_TOKENS = [
    "extern \"C\" void app_main(void)",
    "#include <driver/gpio.h>",
    "#include <esp_timer.h>",
    "#include <freertos/task.h>",
    "std::fgets",
    "char line[LINE_LEN]",
    "gDevice.tick",
]
IDF_FORBIDDEN_TOKENS = [
    "IdfArduinoCompat",
    "Arduino.h",
    "Wire.h",
    "String ",
    "Serial.",
    "Serial.begin",
    "TwoWire",
    "HardwareSerial",
    '#include "examples/01_basic_bringup_cli/main.cpp"',
]


def fail(msg: str) -> None:
    print(f"CLI contract FAILED: {msg}")
    raise SystemExit(1)


def ensure_exists(path: pathlib.Path, label: str) -> None:
    if not path.exists():
        fail(f"missing {label}: {path.as_posix()}")


def ensure_missing(path: pathlib.Path, label: str) -> None:
    if path.exists():
        fail(f"forbidden {label} still present: {path.as_posix()}")


def main() -> int:
    common_dir = ROOT / "examples" / "common"
    bringup_main = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
    idf_main = ROOT / "examples" / "espidf_basic" / "main" / "main.cpp"
    idf_cmake = ROOT / "examples" / "espidf_basic" / "main" / "CMakeLists.txt"

    ensure_exists(common_dir, "common example directory")
    ensure_exists(bringup_main, "bringup CLI example")
    ensure_exists(idf_main, "ESP-IDF bringup entry point")
    ensure_exists(idf_cmake, "ESP-IDF bringup CMake file")

    ensure_missing(ROOT / "examples" / "00_smoke_boot", "deprecated example 00_smoke_boot")
    ensure_missing(
        ROOT / "examples" / "03_feature_walkthrough",
        "deprecated example 03_feature_walkthrough",
    )

    for name in REQUIRED_COMMON:
        ensure_exists(common_dir / name, f"common helper {name}")

    text = bringup_main.read_text(encoding="utf-8", errors="replace")

    for cmd in MANDATORY_COMMANDS:
        if re.search(rf"\b{re.escape(cmd)}\b", text) is None:
            fail(f"mandatory command '{cmd}' missing in {bringup_main.as_posix()}")

    if re.search(r"\bcfg\b", text) is None and re.search(r"\bsettings\b", text) is None:
        fail("either 'cfg' or 'settings' command must be present")

    idf_text = idf_main.read_text(encoding="utf-8", errors="replace")
    for token in IDF_NATIVE_TOKENS:
        if token not in idf_text:
            fail(f"native ESP-IDF entry point missing token {token!r}")
    for token in IDF_FORBIDDEN_TOKENS:
        if token in idf_text:
            fail(f"native ESP-IDF entry point must not contain {token!r}")

    cmake_text = idf_cmake.read_text(encoding="utf-8", errors="replace")
    for component in IDF_REQUIRED_COMPONENTS:
        if re.search(rf"\b{re.escape(component)}\b", cmake_text) is None:
            fail(f"ESP-IDF CMake file missing required component '{component}'")

    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
