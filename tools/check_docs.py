#!/usr/bin/env python3
"""Validate current consumer documentation against the v2 public surface."""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]
VERSION = "2.0.0"
DATASHEET = ROOT / "docs" / "AT21CS01-AT21CS11-1-Kbit-Serial-EEPROM-Data-Sheet-DS20005857.pdf"
DATASHEET_SIZE = 2247216
DATASHEET_SHA256 = "704577264C3B6C60B2D14BE83A229F34C86433CC8951516641FB1DE9EC5DB1A5"
DATASHEET_URL = (
    "https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/"
    "ProductDocuments/DataSheets/AT21CS01-AT21CS11-1-Kbit-Serial-EEPROM-"
    "Data-Sheet-DS20005857.pdf"
)
PROTECTED_REPORT = ROOT / "docs" / "AT21CS01_AT21CS11_complete_driver_report.md"
# Canonical LF hash. Git may materialize CRLF on Windows, but the protected
# report's text must otherwise remain byte-for-byte identical across hosts.
PROTECTED_SHA256 = "B5803C866DB21CB33961DD6D482C9E6860043740A6E76B3DB43506BDB3F18E8E"

CONSUMER_DOCS = (
    ROOT / "README.md",
    ROOT / "CHANGELOG.md",
    ROOT / "CONTRIBUTING.md",
    ROOT / "SECURITY.md",
    ROOT / "docs" / "MIGRATION.md",
    ROOT / "docs" / "IRREVERSIBLE_OPERATIONS.md",
    ROOT / "docs" / "HARDWARE_VALIDATION.md",
)
PACKAGED_DOCS = {
    ROOT / "README.md",
    ROOT / "CHANGELOG.md",
    ROOT / "docs" / "MIGRATION.md",
    ROOT / "docs" / "IRREVERSIBLE_OPERATIONS.md",
    ROOT / "docs" / "HARDWARE_VALIDATION.md",
}
PACKAGE_ROOT_FILES = {
    "library.json",
    "LICENSE",
    "README.md",
    "CHANGELOG.md",
    "docs/MIGRATION.md",
    "docs/IRREVERSIBLE_OPERATIONS.md",
    "docs/HARDWARE_VALIDATION.md",
}
PACKAGE_PREFIXES = (
    "include/AT21CS/",
    "src/",
    "examples/01_basic_bringup_cli/",
    "examples/02_multi_device_cli/",
    "examples/common/",
)

EXAMPLES = (
    "examples/01_basic_bringup_cli",
    "examples/02_multi_device_cli",
)
ENVIRONMENTS = ("ex_cli_s3", "ex_cli_s2", "ex_multi_s3", "ex_multi_s2")
OVERRIDES = (
    "AT21CS_EXAMPLE_PRIMARY_SIO_PIN",
    "AT21CS_EXAMPLE_PRIMARY_PRESENCE_PIN",
    "AT21CS_EXAMPLE_PRIMARY_PRESENCE_ACTIVE_HIGH",
    "AT21CS_EXAMPLE_PRIMARY_ADDRESS_BITS",
    "AT21CS_EXAMPLE_PRIMARY_PART",
    "AT21CS_EXAMPLE_SECONDARY_SIO_PIN",
    "AT21CS_EXAMPLE_SECONDARY_PRESENCE_PIN",
    "AT21CS_EXAMPLE_SECONDARY_PRESENCE_ACTIVE_HIGH",
    "AT21CS_EXAMPLE_SECONDARY_ADDRESS_BITS",
    "AT21CS_EXAMPLE_SECONDARY_PART",
)

PUBLIC_REQUIREMENTS = (
    ("include/AT21CS/platform/esp32/Esp32Transport.h", "struct Esp32TransportConfig", "Esp32TransportConfig"),
    ("include/AT21CS/platform/esp32/Esp32Transport.h", "Status begin(const Esp32TransportConfig& config);", "Esp32Transport::begin()"),
    ("include/AT21CS/Bus.h", "Status bind(const BusConfig& config);", "Bus::bind()"),
    ("include/AT21CS/Bus.h", "Status readPresenceIndicator(bool& present);", "Bus::readPresenceIndicator()"),
    ("include/AT21CS/AT21CS.h", "Status begin(Bus& bus, const Config& config);", "Driver::begin()"),
    ("include/AT21CS/AT21CS.h", "Status probe();", "Driver::probe()"),
    ("include/AT21CS/AT21CS.h", "Status recover();", "Driver::recover()"),
    ("include/AT21CS/AT21CS.h", "Status writeEepromPage(uint8_t address,", "Driver::writeEepromPage()"),
    ("include/AT21CS/AT21CS.h", "Status readSerialNumber(SerialNumberInfo& serial);", "Driver::readSerialNumber()"),
    ("include/AT21CS/AT21CS.h", "Status permanentlyLockSecurity(MutationResult& result);", "Driver::permanentlyLockSecurity()"),
    ("include/AT21CS/AT21CS.h", "Status permanentlyEnableRomZone(uint8_t zoneIndex, MutationResult& result);", "Driver::permanentlyEnableRomZone()"),
    ("include/AT21CS/AT21CS.h", "Status permanentlyFreezeRomZones(MutationResult& result);", "Driver::permanentlyFreezeRomZones()"),
)

FORBIDDEN_ACTIVE_TEXT = (
    "AT21CS/Core.h",
    "Driver::begin(const Config",
    "Config::sioPin",
    "Config::transport",
    "SingleWireTransport::presencePresent",
    "readCurrentAddress",
    "waitReady(",
    "driverState(",
    "getSettings(",
    "LoadCellMap",
    "framework = espidf",
    "ChannelRequest",
    "ChannelResult",
    "CachedChannelStatus",
    "firmware_owner",
)

OBSOLETE_PATHS = (
    "docs/IDF_PORT.md",
    "docs/IDF_PORT_IMPLEMENTATION.md",
    "docs/ARCHITECTURE_SPLIT_PLAN.md",
    "docs/AI_coder_prompt_AT21CS01_AT21CS11_driver.md",
    "docs/AT21CS01_AT21CS11_datasheet_reference.md",
    "docs/AT21CS11_datasheet.pdf",
    "docs/extracted-md",
    "docs/pdf-extracted-md",
    "examples/espidf_basic",
    "test/consumer/firmware_owner",
    "tools/check_idf_example_contract.py",
    "tools/run_firmware_owner_fixture.py",
    "docs/implementation-prompts",
    "docs/validation",
    "test/hil/destructive",
)

LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def sha256_lf_text(path: Path) -> str:
    content = path.read_text(encoding="utf-8")
    return hashlib.sha256(content.encode("utf-8")).hexdigest().upper()


def package_allowed(relative: str) -> bool:
    return relative in PACKAGE_ROOT_FILES or relative.startswith(PACKAGE_PREFIXES)


def check_links(failures: list[str]) -> None:
    root = ROOT.resolve()
    for document in CONSUMER_DOCS:
        if not document.is_file():
            failures.append(f"missing consumer document: {document.relative_to(ROOT)}")
            continue
        text = document.read_text(encoding="utf-8")
        for raw in LINK.findall(text):
            target = raw.strip().split(maxsplit=1)[0].strip("<>")
            if target.startswith(("https://", "mailto:", "#")):
                continue
            if target.startswith(("http://", "file://", "/")):
                failures.append(f"unsafe link in {document.relative_to(ROOT)}: {target}")
                continue
            target = unquote(target.split("#", 1)[0].split("?", 1)[0])
            resolved = (document.parent / target).resolve()
            if resolved != root and root not in resolved.parents:
                failures.append(f"link escapes checkout in {document.relative_to(ROOT)}: {target}")
                continue
            if not resolved.exists():
                failures.append(f"broken link in {document.relative_to(ROOT)}: {target}")
                continue
            if document in PACKAGED_DOCS:
                relative = resolved.relative_to(root).as_posix()
                if not package_allowed(relative):
                    failures.append(
                        f"packaged document links outside package: "
                        f"{document.relative_to(ROOT)} -> {relative}"
                    )


def check_public_symbols(failures: list[str]) -> None:
    active = (ROOT / "README.md").read_text(encoding="utf-8")
    migration = (ROOT / "docs" / "MIGRATION.md").read_text(encoding="utf-8")
    documented = active + "\n" + migration
    for header_name, declaration, documentation_name in PUBLIC_REQUIREMENTS:
        header = (ROOT / header_name).read_text(encoding="utf-8")
        if declaration not in header:
            failures.append(f"expected public declaration missing: {declaration}")
        if documentation_name not in documented:
            failures.append(f"public symbol is not documented: {documentation_name}")

    active_docs = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (ROOT / "README.md", ROOT / "CONTRIBUTING.md", ROOT / "SECURITY.md")
    )
    for forbidden in FORBIDDEN_ACTIVE_TEXT:
        if forbidden in active_docs:
            failures.append(f"obsolete active documentation token: {forbidden}")


def check_safety_guidance(failures: list[str]) -> None:
    guide_path = ROOT / "docs" / "IRREVERSIBLE_OPERATIONS.md"
    header_path = ROOT / "include" / "AT21CS" / "AT21CS.h"
    if not guide_path.is_file():
        failures.append("irreversible-operation guide is missing")
        return

    guide = guide_path.read_text(encoding="utf-8")
    normalized_guide = re.sub(r"\s+", " ", guide)
    header = header_path.read_text(encoding="utf-8")
    for expected in (
        "Security-user bytes `0x10..0x1F`",
        "0 | `0x00..0x1F`",
        "1 | `0x20..0x3F`",
        "2 | `0x40..0x5F`",
        "3 | `0x60..0x7F`",
        "It does not freeze EEPROM data",
        "can never later be converted to ROM",
        "Do not call `permanentlyFreezeRomZones()` merely to inspect",
        "Never automatically replay",
        "MutationEffect::VERIFIED",
    ):
        if expected not in normalized_guide:
            failures.append(f"irreversible guidance missing: {expected}")

    for expected in (
        "Lock affects Security bytes",
        "Permanently makes one 32-byte EEPROM zone read-only",
        "This freezes configuration, not EEPROM data",
        "it is not a read-only query",
        "Do not automatically retry ambiguous evidence",
    ):
        if expected not in header:
            failures.append(f"public irreversible API warning missing: {expected}")

    esp32_header = (
        ROOT / "include" / "AT21CS" / "platform" / "esp32" / "Esp32Transport.h"
    ).read_text(encoding="utf-8")
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    normalized_readme = re.sub(r"\s+", " ", readme)
    for expected in (
        "exactly -1 disables it",
        "disables internal pulls",
        "ignored when `presencePin == -1`",
    ):
        if expected not in esp32_header:
            failures.append(f"public presence-pin contract missing: {expected}")
    for expected in (
        "For a fixed, non-hot-plugged device",
        "no polling is required",
        "Do not call `Bus::readPresenceIndicator()` when detection is disabled",
    ):
        if expected not in normalized_readme:
            failures.append(f"no-detect guidance missing: {expected}")


def check_examples(failures: list[str]) -> None:
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    sections = set(re.findall(r"^\[env:([^\]]+)\]$", platformio, flags=re.MULTILINE))
    for example in EXAMPLES:
        if not (ROOT / example).is_dir() or example not in readme:
            failures.append(f"example missing or undocumented: {example}")
    for environment in ENVIRONMENTS:
        if environment not in sections or environment not in readme:
            failures.append(f"PlatformIO environment missing or undocumented: {environment}")

    board = (ROOT / "examples" / "common" / "BoardConfig.h").read_text(encoding="utf-8")
    expected_defines = {
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
    for name, value in expected_defines.items():
        if not re.search(rf"^#define\s+{re.escape(name)}\s+{re.escape(value)}$", board, re.MULTILINE):
            failures.append(f"unexpected example default: {name}")
        if name not in readme:
            failures.append(f"example override is undocumented: {name}")
    for declaration in (
        "DETECT_SAMPLE_MS = 20",
        "DETECT_DEBOUNCE_MS = 100",
        "HOTPLUG_POLL_MS = 1000",
    ):
        if declaration not in board:
            failures.append(f"unexpected example hot-plug constant: {declaration}")
    for text in ("20 ms", "100 ms", "1,000 ms"):
        if text not in readme:
            failures.append(f"hot-plug cadence is undocumented: {text}")


def check_versions(failures: list[str]) -> None:
    manifest = json.loads((ROOT / "library.json").read_text(encoding="utf-8"))
    if manifest.get("version") != VERSION:
        failures.append(f"library.json version is not {VERSION}")
    version_header = (ROOT / "include" / "AT21CS" / "Version.h").read_text(encoding="utf-8")
    for expected in (
        "VERSION_MAJOR = 2;",
        "VERSION_MINOR = 0;",
        "VERSION_PATCH = 0;",
        "VERSION_CODE = 20000;",
        f'VERSION = "{VERSION}";',
    ):
        if expected not in version_header:
            failures.append(f"Version.h mismatch: {expected}")
    doxyfile = (ROOT / "Doxyfile").read_text(encoding="utf-8")
    if f'PROJECT_NUMBER         = "{VERSION}"' not in doxyfile:
        failures.append("Doxyfile version mismatch")
    if "WARN_AS_ERROR          = YES" not in doxyfile:
        failures.append("Doxygen warnings are not fatal")
    if "HAVE_DOT               = NO" not in doxyfile:
        failures.append("Doxygen unexpectedly requires Graphviz")
    for document in (
        "docs/IRREVERSIBLE_OPERATIONS.md",
        "docs/HARDWARE_VALIDATION.md",
    ):
        if document not in doxyfile:
            failures.append(f"Doxygen input missing: {document}")
    attributes = (ROOT / ".gitattributes").read_text(encoding="utf-8")
    for expected in (
        "include/AT21CS/Version.h text eol=lf",
        "scripts/generate_version.py text eol=lf",
    ):
        if expected not in attributes:
            failures.append(f"deterministic line-ending rule missing: {expected}")
    for document in (ROOT / "README.md", ROOT / "CHANGELOG.md", ROOT / "SECURITY.md"):
        if VERSION not in document.read_text(encoding="utf-8"):
            failures.append(f"version missing from {document.relative_to(ROOT)}")


def check_artifacts(failures: list[str]) -> None:
    for relative in OBSOLETE_PATHS:
        path = ROOT / relative
        if path.is_file() or (
            path.is_dir() and any(entry.is_file() for entry in path.rglob("*"))
        ):
            failures.append(f"obsolete artifact remains: {relative}")

    if not DATASHEET.is_file():
        failures.append("authoritative datasheet is missing")
    else:
        if DATASHEET.stat().st_size != DATASHEET_SIZE:
            failures.append("authoritative datasheet size mismatch")
        if sha256(DATASHEET) != DATASHEET_SHA256:
            failures.append("authoritative datasheet hash mismatch")
    if (
        not PROTECTED_REPORT.is_file()
        or sha256_lf_text(PROTECTED_REPORT) != PROTECTED_SHA256
    ):
        failures.append("protected complete-driver report hash mismatch")

    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    for expected in (DATASHEET_URL, str(DATASHEET_SIZE), DATASHEET_SHA256):
        if expected not in readme:
            failures.append(f"README authoritative datasheet record mismatch: {expected}")


def check_doxygen(failures: list[str]) -> None:
    executable = shutil.which("doxygen")
    if executable is None:
        failures.append("doxygen executable is unavailable")
        return
    with tempfile.TemporaryDirectory(prefix="at21cs-doxygen-") as temporary:
        config = (ROOT / "Doxyfile").read_text(encoding="utf-8")
        config += f"\nOUTPUT_DIRECTORY = {Path(temporary).as_posix()}\n"
        result = subprocess.run(
            [executable, "-"],
            cwd=ROOT,
            input=config,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if result.returncode != 0:
            failures.append("Doxygen failed:\n" + result.stdout.strip())


def main() -> int:
    failures: list[str] = []
    try:
        check_links(failures)
        check_public_symbols(failures)
        check_safety_guidance(failures)
        check_examples(failures)
        check_versions(failures)
        check_artifacts(failures)
        check_doxygen(failures)
    except (OSError, ValueError, json.JSONDecodeError, AttributeError) as error:
        failures.append(f"documentation checker error: {error}")

    if failures:
        print("Documentation check FAILED", file=sys.stderr)
        for failure in sorted(failures):
            print(f"- {failure}", file=sys.stderr)
        return 1
    print("Documentation check PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
