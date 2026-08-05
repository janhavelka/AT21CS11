#!/usr/bin/env python3
"""Inspect and build the curated PlatformIO package outside the checkout."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[1]
CORE_CONSUMER = ROOT / "test" / "consumer" / "core_only" / "main.cpp"
PLATFORM_URL = (
    "https://github.com/pioarduino/platform-espressif32/releases/download/"
    "55.03.311/platform-espressif32.zip"
)

ROOT_FILES = {
    "library.json",
    "LICENSE",
    "README.md",
    "CHANGELOG.md",
    "docs/MIGRATION.md",
    "docs/IRREVERSIBLE_OPERATIONS.md",
    "docs/HARDWARE_VALIDATION.md",
}
ALLOWED_PREFIXES = (
    "include/AT21CS/",
    "src/",
    "examples/01_basic_bringup_cli/",
    "examples/02_multi_device_cli/",
    "examples/common/",
)
DENIED_PARTS = {
    ".git",
    ".github",
    ".pio",
    ".vscode",
    "test",
    "tests",
    "tools",
    "scripts",
    "implementation-prompts",
    "validation",
    "firmware_owner",
    "captures",
    "espidf_basic",
}
DENIED_NAMES = {
    "agents.md",
    "codeowners",
    "contributing.md",
    "security.md",
    "doxyfile",
    "platformio.ini",
    "cmakelists.txt",
    "idf_component.yml",
}
DENIED_SUFFIXES = {
    ".o",
    ".obj",
    ".a",
    ".so",
    ".dll",
    ".dylib",
    ".elf",
    ".exe",
    ".hex",
    ".bin",
    ".map",
    ".d",
    ".pyc",
    ".tmp",
    ".bak",
    ".log",
}


class CheckFailure(RuntimeError):
    pass


def run(
    command: list[str],
    cwd: Path,
    capture: bool = False,
    env: dict[str, str] | None = None,
) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
        env=env,
    )
    output = result.stdout or ""
    if result.returncode != 0:
        if output:
            print(output, end="")
        raise CheckFailure(
            f"command failed ({result.returncode}): {shlex.join(command)}"
        )
    return output


def pio_command(*arguments: str) -> list[str]:
    if os.name == "nt":
        wrapper = ROOT / "scripts" / "pio.cmd"
        if not wrapper.is_file():
            raise CheckFailure(f"PlatformIO wrapper is missing: {wrapper}")
        command_line = subprocess.list2cmdline([str(wrapper), *arguments])
        return [os.environ.get("COMSPEC", "cmd.exe"), "/d", "/s", "/c", command_line]
    return [sys.executable, "-m", "platformio", *arguments]


def git_output(*arguments: str) -> bytes:
    result = subprocess.run(
        ["git", *arguments],
        cwd=ROOT,
        check=False,
        stdout=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise CheckFailure(f"cannot inspect repository: git {shlex.join(arguments)}")
    return result.stdout


def file_fingerprint(path: Path) -> str:
    if path.is_symlink():
        return f"symlink:{os.readlink(path)}"
    if not path.exists():
        return "missing"
    if not path.is_file():
        return "non-file"
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return f"file:{digest.hexdigest()}"


def repository_snapshot() -> tuple[bytes, bytes, dict[str, str]]:
    status = git_output("status", "--porcelain=v1", "-z", "--untracked-files=all")
    index = git_output("ls-files", "--stage", "-z")
    listed = git_output(
        "ls-files", "--cached", "--others", "--exclude-standard", "-z"
    )
    paths = {
        value.decode("utf-8", errors="surrogateescape")
        for value in listed.rstrip(b"\0").split(b"\0")
        if value
    }
    fingerprints = {path: file_fingerprint(ROOT / path) for path in sorted(paths)}
    return status, index, fingerprints


def describe_repository_change(
    before: tuple[bytes, bytes, dict[str, str]],
    after: tuple[bytes, bytes, dict[str, str]],
) -> str:
    before_status, before_index, before_files = before
    after_status, after_index, after_files = after
    details = ["package check changed the repository"]
    if before_status != after_status:
        details.append("Git status changed")
    if before_index != after_index:
        details.append("Git index changed")
    all_paths = sorted(set(before_files) | set(after_files))
    details.extend(
        f"content changed: {path}"
        for path in all_paths
        if before_files.get(path) != after_files.get(path)
    )
    return "\n".join(details)


def expected_files() -> set[str]:
    expected = set(ROOT_FILES)
    for prefix in ALLOWED_PREFIXES:
        directory = ROOT / prefix
        if not directory.is_dir():
            raise CheckFailure(f"required package directory is missing: {prefix}")
        for source in directory.rglob("*"):
            if source.is_symlink():
                raise CheckFailure(f"package source must not be a symlink: {source}")
            if source.is_file():
                expected.add(source.relative_to(ROOT).as_posix())
    for relative in ROOT_FILES:
        if not (ROOT / relative).is_file():
            raise CheckFailure(f"required package file is missing: {relative}")
    return expected


def normalized_member(member: tarfile.TarInfo) -> str:
    raw = member.name
    if "\\" in raw:
        raise CheckFailure(f"archive member uses a backslash: {raw}")
    path = PurePosixPath(raw)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise CheckFailure(f"unsafe archive member path: {raw}")
    if len(path.parts) > 0 and path.parts[0].endswith(":"):
        raise CheckFailure(f"archive member has a drive prefix: {raw}")
    return path.as_posix()


def validate_archive(archive: Path) -> tuple[tarfile.TarFile, dict[str, tarfile.TarInfo]]:
    handle = tarfile.open(archive, mode="r:gz")
    members: dict[str, tarfile.TarInfo] = {}
    try:
        for member in handle.getmembers():
            name = normalized_member(member)
            if member.isdir():
                continue
            if not member.isfile():
                raise CheckFailure(f"archive contains a non-regular file: {name}")
            if name in members:
                raise CheckFailure(f"archive contains a duplicate member: {name}")
            members[name] = member

        actual = set(members)
        expected = expected_files()
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        if missing or extra:
            lines = ["package contents do not match the explicit allowlist"]
            lines.extend(f"missing: {name}" for name in missing)
            lines.extend(f"extra: {name}" for name in extra)
            raise CheckFailure("\n".join(lines))

        for name in sorted(actual):
            path = PurePosixPath(name)
            lower = name.lower()
            if any(part.lower() in DENIED_PARTS for part in path.parts):
                raise CheckFailure(f"denied package path: {name}")
            if path.name.lower() in DENIED_NAMES or path.suffix.lower() in DENIED_SUFFIXES:
                raise CheckFailure(f"denied package file: {name}")
            if "esp-idf" in lower or "framework=espidf" in lower:
                raise CheckFailure(f"native framework artifact in package: {name}")
        return handle, members
    except Exception:
        handle.close()
        raise


def safe_extract(
    handle: tarfile.TarFile, members: dict[str, tarfile.TarInfo], destination: Path
) -> None:
    destination = destination.resolve()
    for name, member in members.items():
        target = (destination / name).resolve()
        if destination not in target.parents:
            raise CheckFailure(f"archive extraction escaped destination: {name}")
        target.parent.mkdir(parents=True, exist_ok=True)
        source = handle.extractfile(member)
        if source is None:
            raise CheckFailure(f"cannot read archive member: {name}")
        with source, target.open("wb") as output:
            shutil.copyfileobj(source, output)


LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")


def validate_package_links(package: Path, members: set[str]) -> None:
    for markdown in package.rglob("*.md"):
        relative_doc = markdown.relative_to(package)
        text = markdown.read_text(encoding="utf-8")
        for raw in LINK.findall(text):
            target = raw.strip().split(maxsplit=1)[0].strip("<>")
            if target.startswith(("https://", "mailto:", "#")):
                continue
            if target.startswith(("http://", "file://", "/")):
                raise CheckFailure(f"unsafe package link in {relative_doc}: {target}")
            target = target.split("#", 1)[0].split("?", 1)[0]
            resolved = (relative_doc.parent / target).as_posix()
            normalized = PurePosixPath(resolved)
            if ".." in normalized.parts or normalized.as_posix() not in members:
                raise CheckFailure(f"broken package link in {relative_doc}: {target}")


def reject_checkout_text(package: Path) -> None:
    checkout = str(ROOT.resolve())
    variants = {checkout, checkout.replace("\\", "/")}
    for source in package.rglob("*"):
        if not source.is_file() or source.suffix.lower() in {".pdf"}:
            continue
        try:
            text = source.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if any(value in text for value in variants):
            raise CheckFailure(f"package embeds the checkout path: {source}")
        if re.search(r"framework\s*=\s*espidf", text, flags=re.IGNORECASE):
            raise CheckFailure(f"package advertises a native framework path: {source}")


def create_and_extract(temp: Path) -> Path:
    archive = temp / "AT21CS01_AT21CS11-2.0.0.tar.gz"
    run(pio_command("pkg", "pack", str(ROOT), "--output", str(archive)), ROOT)
    if not archive.is_file():
        raise CheckFailure("PlatformIO did not create the requested archive")
    package = temp / "package"
    package.mkdir()
    handle, members = validate_archive(archive)
    try:
        safe_extract(handle, members, package)
    finally:
        handle.close()
    validate_package_links(package, set(members))
    reject_checkout_text(package)
    return package


def compiler_command() -> list[str]:
    configured = os.environ.get("CXX")
    if configured:
        command = shlex.split(configured, posix=os.name != "nt")
        if command:
            return command
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return [found]
    raise CheckFailure("no C++ compiler found for the platform-neutral consumer")


def contains_checkout_path(value: str) -> bool:
    checkout = str(ROOT.resolve())
    normalized = value.replace("\\", "/").casefold()
    return checkout.replace("\\", "/").casefold() in normalized


def clean_compiler_environment() -> dict[str, str]:
    environment = os.environ.copy()
    for name in (
        "CPATH",
        "CPLUS_INCLUDE_PATH",
        "C_INCLUDE_PATH",
        "OBJC_INCLUDE_PATH",
        "INCLUDE",
    ):
        environment.pop(name, None)
    return environment


def build_platform_neutral(package: Path, temp: Path) -> None:
    consumer = temp / "core-consumer.cpp"
    shutil.copyfile(CORE_CONSUMER, consumer)
    output = temp / ("core-consumer.exe" if os.name == "nt" else "core-consumer")
    dependencies = temp / "core-consumer.d"
    inputs = [
        consumer,
        package / "src" / "AT21CS.cpp",
        package / "src" / "Bus.cpp",
    ]
    for source in inputs:
        if not source.is_file():
            raise CheckFailure(f"core consumer input is missing: {source}")
        if ROOT.resolve() in source.resolve().parents:
            raise CheckFailure(f"core consumer reached into checkout: {source}")
    compiler = compiler_command()
    if any(contains_checkout_path(argument) for argument in compiler):
        raise CheckFailure("CXX contains a path into the checkout")
    command = [
        *compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Wconversion",
        "-Wsign-conversion",
        "-Wshadow",
        "-Wundef",
        "-Werror",
        "-MMD",
        "-MF",
        str(dependencies),
        f"-I{package / 'include'}",
        *(str(source) for source in inputs),
        "-o",
        str(output),
    ]
    run(command, temp, env=clean_compiler_environment())
    if not dependencies.is_file():
        raise CheckFailure("compiler did not produce the dependency trace")
    dependency_text = dependencies.read_text(encoding="utf-8", errors="replace")
    if contains_checkout_path(dependency_text):
        raise CheckFailure("core consumer dependency trace reached into checkout")
    run([str(output)], temp)


def arduino_ini() -> str:
    return f"""[platformio]
src_dir = .
default_envs = ex_cli_s3

[env]
platform = {PLATFORM_URL}
framework = arduino
build_flags =
  -std=c++17
  -Wall
  -Wextra
  -Werror=return-type
  -Iinclude
  -Iexamples/common
  -DCORE_DEBUG_LEVEL=0

[env:ex_cli_s3]
extends = env
board = esp32-s3-devkitc-1
build_flags =
  ${{env.build_flags}}
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
build_src_filter =
  -<*>
  +<examples/01_basic_bringup_cli/**>
  +<examples/common/**>
  +<src/**>
  +<include/**>

[env:ex_cli_s2]
extends = env
board = esp32-s2-saola-1
board_upload.after_reset = no-reset-stub
build_flags =
  ${{env.build_flags}}
  -DARDUINO_USB_MODE=0
  -DARDUINO_USB_CDC_ON_BOOT=1
build_src_filter =
  -<*>
  +<examples/01_basic_bringup_cli/**>
  +<examples/common/**>
  +<src/**>
  +<include/**>

[env:ex_multi_s3]
extends = env
board = esp32-s3-devkitc-1
build_flags =
  ${{env.build_flags}}
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
build_src_filter =
  -<*>
  +<examples/02_multi_device_cli/**>
  +<examples/common/**>
  +<src/**>
  +<include/**>

[env:ex_multi_s2]
extends = env
board = esp32-s2-saola-1
board_upload.after_reset = no-reset-stub
build_flags =
  ${{env.build_flags}}
  -DARDUINO_USB_MODE=0
  -DARDUINO_USB_CDC_ON_BOOT=1
build_src_filter =
  -<*>
  +<examples/02_multi_device_cli/**>
  +<examples/common/**>
  +<src/**>
  +<include/**>
"""


def build_arduino(package: Path) -> None:
    (package / "platformio.ini").write_text(arduino_ini(), encoding="utf-8", newline="\n")
    for environment in ("ex_cli_s3", "ex_cli_s2", "ex_multi_s3", "ex_multi_s2"):
        output = run(pio_command("run", "-d", str(package), "-e", environment), package, capture=True)
        checkout = str(ROOT.resolve())
        if checkout in output or checkout.replace("\\", "/") in output:
            raise CheckFailure(f"Arduino build reached into checkout: {environment}")
        for trace in (package / ".pio" / "build" / environment).rglob("*"):
            if not trace.is_file() or trace.suffix.lower() not in {
                ".d",
                ".json",
                ".log",
                ".txt",
            }:
                continue
            try:
                trace_text = trace.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                continue
            if checkout in trace_text or checkout.replace("\\", "/") in trace_text:
                raise CheckFailure(
                    f"Arduino dependency trace reached into checkout: {trace}"
                )
        print(f"clean package build passed: {environment}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--inspect", action="store_true")
    modes.add_argument("--build-platform-neutral", action="store_true")
    modes.add_argument("--build-arduino", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    before = repository_snapshot()
    failure: str | None = None
    try:
        with tempfile.TemporaryDirectory(prefix="at21cs-package-") as temporary:
            temp = Path(temporary).resolve()
            if temp == ROOT.resolve() or ROOT.resolve() in temp.parents:
                raise CheckFailure("temporary package directory is inside the checkout")
            package = create_and_extract(temp)
            if args.build_platform_neutral:
                build_platform_neutral(package, temp)
            elif args.build_arduino:
                build_arduino(package)
    except (CheckFailure, OSError, tarfile.TarError) as error:
        failure = str(error)
    try:
        after = repository_snapshot()
        if after != before:
            failure = describe_repository_change(before, after)
    except CheckFailure as error:
        failure = str(error)
    if failure is not None:
        print(f"Package check FAILED: {failure}", file=sys.stderr)
        return 1
    print("Package check PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
