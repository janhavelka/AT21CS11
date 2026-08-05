#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

EXPECTED_EXAMPLE_FILES = {
    "01_basic_bringup_cli/main.cpp",
    "02_multi_device_cli/main.cpp",
    "common/BoardConfig.h",
    "common/BoundedCli.h",
    "common/CommandContract.h",
    "common/StatusText.h",
    "common/WireInstance.h",
}

BOARD_DEFAULTS = {
    "AT21CS_EXAMPLE_PRIMARY_SIO_PIN": "6",
    "AT21CS_EXAMPLE_PRIMARY_PRESENCE_PIN": "-1",
    "AT21CS_EXAMPLE_PRIMARY_PRESENCE_ACTIVE_HIGH": "1",
    "AT21CS_EXAMPLE_PRIMARY_ADDRESS_BITS": "0",
    "AT21CS_EXAMPLE_PRIMARY_PART": "11",
    "AT21CS_EXAMPLE_SECONDARY_SIO_PIN": "10",
    "AT21CS_EXAMPLE_SECONDARY_PRESENCE_PIN": "-1",
    "AT21CS_EXAMPLE_SECONDARY_PRESENCE_ACTIVE_HIGH": "1",
    "AT21CS_EXAMPLE_SECONDARY_ADDRESS_BITS": "0",
    "AT21CS_EXAMPLE_SECONDARY_PART": "11",
}

SINGLE_REGISTRATIONS = [
    ("HELP", "handleHelp"),
    ("STATUS", "handleStatus"),
    ("PRESENCE", "handlePresence"),
    ("PROBE", "handleProbe"),
    ("MANUFACTURER", "handleManufacturer"),
    ("SERIAL_NUMBER", "handleSerial"),
    ("READ_EEPROM", "handleReadEeprom"),
    ("READ_SECURITY", "handleReadSecurity"),
    ("SECURITY_LOCKED", "handleSecurityLocked"),
    ("ROM_ZONE", "handleRomZone"),
    ("SPEED", "handleSpeed"),
    ("RECOVER", "handleRecover"),
    ("WRITE_PAGE", "handleWritePage"),
    ("SHUTDOWN", "handleShutdown"),
]

MULTI_REGISTRATIONS = [
    ("HELP", "handleHelp"),
    ("STATUS", "handleStatus"),
    ("PRESENCE", "handlePresence"),
    ("PROBE", "handleProbe"),
    ("SERIAL_NUMBER", "handleSerial"),
    ("READ_EEPROM", "handleReadEeprom"),
    ("RECOVER", "handleRecover"),
    ("SHUTDOWN", "handleShutdown"),
]

SINGLE_ACTION_MARKERS = {
    "handleHelp": "printHelp()",
    "handleStatus": "printInstanceStatus(",
    "handlePresence": "samplePresence(",
    "handleProbe": "driver.probe()",
    "handleManufacturer": "readManufacturerId(",
    "handleSerial": "readSerial()",
    "handleReadEeprom": "readEeprom(",
    "handleReadSecurity": "readSecurity(",
    "handleSecurityLocked": "readSecurityLockState(",
    "handleRomZone": "readRomZoneState(",
    "handleSpeed": "setSpeedMode(",
    "handleRecover": "recover(millis())",
    "handleWritePage": "writeEepromPage(",
    "handleShutdown": "wire.shutdown()",
}

MULTI_ACTION_MARKERS = {
    "handleHelp": "printHelp()",
    "handleStatus": "printInstanceStatus(",
    "handlePresence": "samplePresence(",
    "handleProbe": "driver.probe()",
    "handleSerial": "readSerial()",
    "handleReadEeprom": "readEeprom(",
    "handleRecover": "recover(millis())",
    "handleShutdown": ".shutdown()",
}

EXPECTED_CATALOG = {
    "HELP": ("help", "help", 0, "help", 0, "SAFE"),
    "STATUS": ("status", "status", 0, "status <wire>", 1, "SAFE"),
    "PRESENCE": (
        "presence", "presence", 0, "presence <wire>", 1, "SAFE"
    ),
    "PROBE": ("probe", "probe", 0, "probe <wire>", 1, "SAFE"),
    "MANUFACTURER": (
        "manufacturer", "manufacturer", 0, None, 0, "SAFE"
    ),
    "SERIAL_NUMBER": (
        "serial", "serial", 0, "serial <wire>", 1, "SAFE"
    ),
    "READ_EEPROM": (
        "read-eeprom",
        "read-eeprom <address> <length>",
        2,
        "read-eeprom <wire> <address> <length>",
        3,
        "SAFE",
    ),
    "READ_SECURITY": (
        "read-security", "read-security <address> <length>", 2, None, 0, "SAFE"
    ),
    "SECURITY_LOCKED": (
        "security-locked", "security-locked", 0, None, 0, "SAFE"
    ),
    "ROM_ZONE": ("rom-zone", "rom-zone <0..3>", 1, None, 0, "SAFE"),
    "SPEED": (
        "speed", "speed <high|standard>", 1, None, 0, "SAFE"
    ),
    "RECOVER": ("recover", "recover", 0, "recover <wire>", 1, "SAFE"),
    "WRITE_PAGE": (
        "write-page",
        "write-page <address> <2..16 hexadecimal digits> "
        "CONFIRM_EEPROM_OVERWRITE",
        3,
        None,
        0,
        "DESTRUCTIVE",
    ),
    "SHUTDOWN": (
        "shutdown", "shutdown", 0, "shutdown <wire|all>", 1, "SAFE"
    ),
}


def fail(message: str) -> None:
    print(f"CLI contract FAILED: {message}")
    raise SystemExit(1)


def read_text(path: pathlib.Path) -> str:
    if not path.is_file():
        fail(f"missing file: {path.relative_to(ROOT).as_posix()}")
    return path.read_text(encoding="utf-8", errors="strict")


def check_layout() -> None:
    examples = ROOT / "examples"
    actual = {
        path.relative_to(examples).as_posix()
        for path in examples.rglob("*")
        if path.is_file()
    }
    if actual != EXPECTED_EXAMPLE_FILES:
        fail(
            "example layout mismatch; "
            f"missing={sorted(EXPECTED_EXAMPLE_FILES - actual)}, "
            f"extra={sorted(actual - EXPECTED_EXAMPLE_FILES)}"
        )
    for obsolete in (
        ROOT / "tools" / "check_idf_example_contract.py",
        ROOT / "test" / "consumer" / "firmware_owner",
        ROOT / "tools" / "run_firmware_owner_fixture.py",
    ):
        if obsolete.exists():
            fail(f"forbidden artifact remains: {obsolete.relative_to(ROOT)}")


def check_board_config() -> None:
    board = read_text(ROOT / "examples" / "common" / "BoardConfig.h")
    for name, expected in BOARD_DEFAULTS.items():
        match = re.search(rf"^#define\s+{re.escape(name)}\s+([^\s]+)\s*$", board, re.M)
        if match is None or match.group(1) != expected:
            fail(f"{name} must default to {expected}")
        if f"#ifndef {name}" not in board:
            fail(f"{name} must remain a build-time override")
    for setting in (
        "OFFLINE_THRESHOLD_PRIMARY = 5",
        "OFFLINE_THRESHOLD_SECONDARY = 5",
        "DETECT_SAMPLE_MS = 20",
        "DETECT_DEBOUNCE_MS = 100",
        "HOTPLUG_POLL_MS = 1000",
        "PRESENCE_PRIMARY_ACTIVE_HIGH",
        "PRESENCE_SECONDARY_ACTIVE_HIGH",
        "EXPECTED_PART_PRIMARY",
        "EXPECTED_PART_SECONDARY",
    ):
        if setting not in board:
            fail(f"BoardConfig missing frozen setting {setting!r}")
    if board.count("static_assert(") < 8:
        fail("BoardConfig must enforce the cross-pin and bounded-value checks")


def catalog_rows(
    contract: str,
) -> dict[str, tuple[str, str, int, str | None, int, str]]:
    match = re.search(
        r"COMMAND_CATALOG\[\]\s*=\s*\{(.*?)\n\};", contract, re.S
    )
    if match is None:
        fail("missing authoritative command catalog")
    entry_pattern = re.compile(
        r'\{CommandId::([A-Z_]+),\s*"([a-z-]+)",\s*'
        r'((?:"[^"]*"\s*)+),\s*([0-9]+),\s*'
        r'(nullptr|(?:"[^"]*"\s*)+),\s*([0-9]+),\s*'
        r"CommandRisk::([A-Z_]+)\}",
        re.S,
    )
    rows: dict[str, tuple[str, str, int, str | None, int, str]] = {}
    for entry in entry_pattern.finditer(match.group(1)):
        command_id, name, single_source, single_count, multi_source, multi_count, risk = (
            entry.groups()
        )
        if command_id in rows:
            fail(f"duplicate command catalog ID {command_id}")
        single_usage = "".join(re.findall(r'"([^"]*)"', single_source))
        multi_usage = None
        if multi_source != "nullptr":
            multi_usage = "".join(re.findall(r'"([^"]*)"', multi_source))
        rows[command_id] = (
            name,
            single_usage,
            int(single_count),
            multi_usage,
            int(multi_count),
            risk,
        )
    return rows


def function_body(source: str, function_name: str) -> str:
    signature = re.search(rf"\bvoid\s+{function_name}\s*\([^)]*\)\s*\{{", source)
    if signature is None:
        fail(f"handler {function_name} has no definition")
    start = signature.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1 : index]
    fail(f"handler {function_name} has an unterminated body")
    return ""


def registrations(main: str, form: str) -> list[tuple[str, str]]:
    match = re.search(
        r"static const CommandRegistration REGISTRATIONS\[\]\s*=\s*\{(.*?)\n\};",
        main,
        re.S,
    )
    if match is None:
        fail(f"missing {form.lower()} registration table")
    rows = re.findall(
        rf"\{{CommandId::([A-Z_]+),\s*CommandForm::{form},\s*(handle[A-Za-z0-9_]+)\}}",
        match.group(1),
    )
    if len(rows) != len(set(rows)):
        fail(f"duplicate {form.lower()} registration")
    for _, handler in rows:
        if re.search(rf"\bvoid\s+{handler}\s*\(", main) is None:
            fail(f"registered handler {handler} has no definition")
    definitions = set(re.findall(r"\bvoid\s+(handle[A-Za-z0-9_]+)\s*\(", main))
    registered = {handler for _, handler in rows}
    if definitions != registered:
        fail(
            f"{form.lower()} handler/catalog mismatch; "
            f"unregistered={sorted(definitions - registered)}, "
            f"undefined={sorted(registered - definitions)}"
        )
    markers = SINGLE_ACTION_MARKERS if form == "SINGLE" else MULTI_ACTION_MARKERS
    for handler in registered:
        marker = markers.get(handler)
        if marker is None or marker not in function_body(main, handler):
            fail(f"{form.lower()} handler {handler} has no expected action")
    return rows


def check_command_contract() -> None:
    contract = read_text(ROOT / "examples" / "common" / "CommandContract.h")
    rows = catalog_rows(contract)
    if rows != EXPECTED_CATALOG:
        fail(f"command catalog mismatch: {rows}")
    if len({spec[0] for spec in rows.values()}) != len(rows):
        fail("command catalog names must be unique")
    for literal in (
        "read-eeprom <address> <length>",
        "read-eeprom <wire> <address> <length>",
        "write-page <address> <2..16 hexadecimal digits>",
        "CONFIRM_EEPROM_OVERWRITE",
        "shutdown <wire|all>",
        "READ_BUFFER_BYTES = 32",
        "CommandRisk::DESTRUCTIVE",
    ):
        if literal not in contract:
            fail(f"command contract missing {literal!r}")

    single = read_text(ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp")
    multi = read_text(ROOT / "examples" / "02_multi_device_cli" / "main.cpp")
    if registrations(single, "SINGLE") != SINGLE_REGISTRATIONS:
        fail("single-device registration is not the frozen command surface")
    if registrations(multi, "MULTI") != MULTI_REGISTRATIONS:
        fail("multi-device registration is not the frozen command surface")
    for token in ("CommandId::WRITE_PAGE", "writeEepromPage", "EEPROM_CONFIRMATION"):
        if token in multi:
            fail(f"multi-device CLI contains destructive token {token!r}")
    for token in ("writeEepromPage", "AT21CS::WriteResult", "printWriteResult"):
        if token not in single:
            fail(f"single-device page write missing {token!r}")


def check_sources() -> None:
    paths = [
        ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp",
        ROOT / "examples" / "02_multi_device_cli" / "main.cpp",
    ]
    texts = [read_text(path) for path in paths]
    for path, source in zip(paths, texts):
        for header in (
            "BoardConfig.h",
            "BoundedCli.h",
            "CommandContract.h",
            "StatusText.h",
            "WireInstance.h",
        ):
            if f'#include "{header}"' not in source:
                fail(f"{path.relative_to(ROOT)} must include {header} locally")
    combined = "\n".join(
        read_text(path)
        for path in (ROOT / "examples").rglob("*")
        if path.is_file()
    )
    for token in (
        "String ",
        "String<",
        "LoadCell",
        "readCurrentAddress",
        "waitReady",
        "writeEepromByte",
        "permanentlyLockSecurity",
        "permanentlyEnableRomZone",
        "permanentlyFreezeRomZones",
        "writeSecurityUser",
        "gpio_set_level",
        "freertos/task.h",
        "app_main",
        "At21csOwner",
        "ChannelRequest",
        "ChannelResult",
        "CachedChannelStatus",
    ):
        if token in combined:
            fail(f"forbidden example token remains: {token!r}")
    for obsolete_command in ("scan", "stress", "rawtx", "full erase"):
        if re.search(rf"\b{re.escape(obsolete_command)}\b", combined, re.I):
            fail(f"forbidden example command remains: {obsolete_command!r}")

    wire = read_text(ROOT / "examples" / "common" / "WireInstance.h")
    for token in (
        "backend.begin(transportConfig)",
        "bus.bind(busConfig)",
        "driver.begin(bus, driverConfig)",
        "driver.end()",
        "bus.end()",
        "backend.end()",
        "static_cast<uint32_t>(nowMs - sinceMs)",
        "bus.readPresenceIndicator(present)",
        "driver.probe()",
        "driver.recover()",
    ):
        if token not in wire:
            fail(f"wire helper missing production-path token {token!r}")


def check_platformio() -> None:
    ini = read_text(ROOT / "platformio.ini")
    envs = set(re.findall(r"^\[env:(ex_[A-Za-z0-9_]+)\]$", ini, re.M))
    expected = {"ex_cli_s3", "ex_cli_s2", "ex_multi_s3", "ex_multi_s2"}
    if envs != expected:
        fail(f"example environments mismatch: {sorted(envs)}")
    if "-Iexamples/common" not in ini:
        fail("example and native builds need the common-header include path")
    for env, source in (
        ("ex_cli_s3", "examples/01_basic_bringup_cli/**"),
        ("ex_cli_s2", "examples/01_basic_bringup_cli/**"),
        ("ex_multi_s3", "examples/02_multi_device_cli/**"),
        ("ex_multi_s2", "examples/02_multi_device_cli/**"),
    ):
        block = re.search(rf"^\[env:{env}\]\n(.*?)(?=^\[|\Z)", ini, re.M | re.S)
        if block is None or f"+<{source}>" not in block.group(1):
            fail(f"{env} does not select {source}")
        other = (
            "examples/02_multi_device_cli/**"
            if "01_basic" in source
            else "examples/01_basic_bringup_cli/**"
        )
        if f"+<{other}>" in block.group(1):
            fail(f"{env} selects both Arduino mains")
    pin = (
        "https://github.com/pioarduino/platform-espressif32/releases/download/"
        "55.03.311/platform-espressif32.zip"
    )
    if pin not in ini or "framework = arduino" not in ini:
        fail("the exact supported Arduino/PioArduino build contract is missing")


def main() -> int:
    check_layout()
    check_board_config()
    check_command_contract()
    check_sources()
    check_platformio()
    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
