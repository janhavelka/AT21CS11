#!/usr/bin/env python3
"""Inspect and build the curated PlatformIO package outside the checkout."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import posixpath
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

from check_idf_example_contract import EXAMPLE as IDF_EXAMPLE, REQUIRED_FILES as IDF_EXAMPLE_FILES

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE_NAME = "AT21CS01_AT21CS11-2.0.0.tar.gz"
CORE_CONSUMER = ROOT / "test" / "consumer" / "core_only" / "main.cpp"
PLATFORM_URL = (
    "https://github.com/pioarduino/platform-espressif32/releases/download/"
    "55.03.311/platform-espressif32.zip"
)

ROOT_FILES = {
    "library.json",
    "CMakeLists.txt",
    "idf_component.yml",
    *((IDF_EXAMPLE / name).as_posix() for name in IDF_EXAMPLE_FILES),
    "LICENSE",
    "README.md",
    "CHANGELOG.md",
    "docs/ESP_IDF.md",
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
    includes = json.loads((ROOT / "library.json").read_text(encoding="utf-8"))["export"]["include"]
    required = ROOT_FILES | {prefix + "**" for prefix in ALLOWED_PREFIXES}
    if sorted(includes) != sorted(required):
        raise CheckFailure("library.json export.include does not match the exact package rules")
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


def package_path_allowed(name: str) -> bool:
    path = PurePosixPath(name)
    if any(part.lower() in DENIED_PARTS for part in path.parts):
        return False
    if path.suffix.lower() in DENIED_SUFFIXES:
        return False
    if "esp-idf" in name.lower() or "framework=espidf" in name.lower():
        return False
    if name in ROOT_FILES:
        return True
    return name.startswith(ALLOWED_PREFIXES) and path.name.lower() not in DENIED_NAMES


def validate_archive(archive: Path) -> tuple[tarfile.TarFile, dict[str, tarfile.TarInfo]]:
    handle = tarfile.open(archive, mode="r:gz")
    members: dict[str, tarfile.TarInfo] = {}
    directories: set[str] = set()
    try:
        for member in handle.getmembers():
            name = normalized_member(member)
            if member.isdir():
                directories.add(name)
                continue
            if not member.isfile():
                raise CheckFailure(f"archive contains a non-regular file: {name}")
            if name in members:
                raise CheckFailure(f"archive contains a duplicate member: {name}")
            members[name] = member

        actual = set(members)
        expected = expected_files()
        allowed_directories = {
            parent.as_posix() for name in expected for parent in PurePosixPath(name).parents
        }
        missing = sorted(expected - actual)
        extra = sorted((actual - expected) | {f"{name}/" for name in directories - allowed_directories})
        if missing or extra:
            lines = ["package contents do not match the explicit allowlist"]
            lines.extend(f"missing: {name}" for name in missing)
            lines.extend(f"extra: {name}" for name in extra)
            raise CheckFailure("\n".join(lines))

        for name in sorted(actual):
            if not package_path_allowed(name):
                raise CheckFailure(f"denied package file: {name}")
        with handle.extractfile(members["idf_component.yml"]) as manifest:
            if manifest.read() != git_output("show", "HEAD:idf_component.yml"):
                raise CheckFailure("packaged idf_component.yml differs from the committed manifest")
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
            # normpath collapses "docs/../README.md" so a valid parent-relative
            # link resolves to a real member instead of failing on its "..".
            resolved = posixpath.normpath(
                (relative_doc.parent / target).as_posix()
            )
            if (
                resolved == ".."
                or resolved.startswith("../")
                or resolved not in members
            ):
                raise CheckFailure(f"broken package link in {relative_doc}: {target}")


def validate_framework_text(relative: str, text: str) -> None:
    # Permanent exception: these two guides state the exact scope once.
    approved = (
        "`library.json` lists `espidf` because the package ships the ESP-IDF component "
        "and native example; PlatformIO `framework = espidf` builds are not tested (only "
        "`idf.py` 6.0.1 for esp32s3/esp32s2)."
    )
    if relative in ("README.md", "docs/ESP_IDF.md"):
        if text.count(approved) != 1:
            raise CheckFailure(f"expected the approved ESP-IDF scope sentence exactly once: {relative}")
        text = text.replace(approved, "", 1)
    if re.search(r"framework\s*=\s*espidf", text, flags=re.IGNORECASE):
        raise CheckFailure(f"unsupported PlatformIO ESP-IDF claim: {relative}")


def reject_checkout_text(package: Path) -> None:
    for source in package.rglob("*"):
        if not source.is_file() or source.suffix.lower() in {".pdf"}:
            continue
        try:
            text = source.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if contains_checkout_path(text):
            raise CheckFailure(f"package embeds the checkout path: {source}")
        validate_framework_text(source.relative_to(package).as_posix(), text)


def create_and_extract(archive: Path, package: Path) -> None:
    run(pio_command("pkg", "pack", str(ROOT), "--output", str(archive)), ROOT)
    if not archive.is_file():
        raise CheckFailure("PlatformIO did not create the requested archive")
    package.mkdir()
    handle, members = validate_archive(archive)
    try:
        safe_extract(handle, members, package)
    finally:
        handle.close()
    validate_package_links(package, set(members))
    reject_checkout_text(package)


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
    # JSON escapes Windows separators, Ninja escapes drive colons and spaces.
    normalized = value.replace("\\\\", "/").replace("\\", "/").replace("$:", ":").replace("$ ", " ").casefold()
    # A sibling like AT21CS11-package is also a valid external build root.
    path = checkout.replace("\\", "/").casefold()
    return re.search(re.escape(path) + r"""(?=$|[/\s"';,)\]])""", normalized) is not None


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


def idf_workspace(out: Path, target: str) -> Path:
    out = out.resolve()
    if out.is_relative_to(ROOT) or out.name.casefold() == "at21cs11":
        raise CheckFailure("choose a short output directory outside the checkout with a different name")
    if out.is_relative_to(Path(tempfile.gettempdir()).resolve()):
        raise CheckFailure("the ESP-IDF output directory must be outside the system temp directory")
    out.mkdir(parents=True, exist_ok=True)
    # Keep each extracted consumer and its evidence, including on retries.
    return Path(tempfile.mkdtemp(prefix=f"{target[-2:]}-", dir=out))


def build_idf(package: Path, build: Path, target: str, idf_command: str) -> None:
    command = shlex.split(idf_command, posix=os.name != "nt")
    if os.name == "nt":
        command = [arg.strip('"') for arg in command]
    if not command:
        raise CheckFailure("empty ESP-IDF command")
    command += [
        "-C", str(package / IDF_EXAMPLE), "-B", str(build),
        "-D", f"SDKCONFIG={build}.sdkconfig", "set-target", target, "build",
    ]
    print(shlex.join(command), flush=True)
    output = run(command, package, capture=True, env=clean_compiler_environment())
    if contains_checkout_path(output):
        raise CheckFailure("ESP-IDF build output contains the checkout path")
    if not (build / "at21cs_basic.bin").is_file() or not (build / "at21cs_basic.elf").is_file():
        raise CheckFailure("ESP-IDF did not link the packaged example")
    config = Path(f"{build}.sdkconfig").read_text(encoding="utf-8").splitlines()
    if f'CONFIG_IDF_TARGET="{target}"' not in config:
        raise CheckFailure(f"packaged build did not select {target}")
    identity = (build / "esp-idf/main/AT21CSBuildIdentity.h").read_text(encoding="utf-8").splitlines()
    for macro in ("AT21CS_GIT_COMMIT", "AT21CS_GIT_STATUS"):
        if f'#define {macro} "unknown"' not in identity:
            raise CheckFailure(f"packaged example identity must report unknown for {macro}")

    # Ninja consumes compiler .d files. Read its actual header dependencies
    # as well as CMake's configure, compile and link paths.
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    ninja = re.search(r"^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$", cache, flags=re.MULTILINE)
    if not ninja:
        raise CheckFailure("cannot locate the ESP-IDF build's Ninja executable")
    deps = run([ninja[1], "-t", "deps"], build, capture=True)
    # Ninja can record relative header paths. Resolve each distinct input from
    # the build directory, including symlinks, before checking its origin.
    inputs = {line.strip() for line in deps.splitlines() if line.startswith("    ")}
    resolved = {(build / name).resolve() for name in inputs}
    if (package / "src/platform/esp32/Esp32Transport.cpp" not in resolved
            or any(path.is_relative_to(ROOT) for path in resolved)):
        raise CheckFailure("missing or checkout-dependent Backend compiler dependencies")
    for trace in build.rglob("*"):
        if trace.is_file() and trace.suffix.lower() in (".json", ".txt", ".d", ".log", ".ninja", ".rsp"):
            if contains_checkout_path(trace.read_text(encoding="utf-8", errors="replace")):
                raise CheckFailure(f"ESP-IDF dependency trace reached into checkout: {trace}")
    print(f"clean ESP-IDF package build passed: {target}, unknown identity, artifacts in {build}")


def archive_checksum(archive: Path) -> str:
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return f"{digest}  {ARCHIVE_NAME}\n"


def export_archive(archive: Path, destination: Path) -> None:
    destination = destination.resolve()
    paths = (destination / ARCHIVE_NAME, destination / "SHA256SUMS")
    if destination.is_relative_to(ROOT):
        if any(destination.is_relative_to(ROOT / prefix) for prefix in ALLOWED_PREFIXES):
            raise CheckFailure("package export directory must be outside the packaged source directories")
        relative = [path.relative_to(ROOT).as_posix() for path in paths]
        if destination.is_relative_to(ROOT / ".git") or git_output("ls-files", "--", *relative):
            raise CheckFailure("refusing to replace repository files with a package export")
    destination.mkdir(parents=True, exist_ok=True)
    target, checksum = paths
    # A fixed staging directory also reserves the destination between the two renames.
    stage = destination / f".{ARCHIVE_NAME}.staging"
    stage.mkdir()
    try:
        if any(path.is_symlink() for path in paths):
            raise CheckFailure("package export files must not be symlinks")
        previous = stage / "previous.tar.gz"
        if target.exists() or checksum.exists():
            if (not target.is_file() or not checksum.is_file()
                    or checksum.read_text(encoding="ascii") != archive_checksum(target)):
                raise CheckFailure(f"refusing to overwrite unrelated files in {destination}")
            shutil.copy2(target, previous)
        staged = stage / ARCHIVE_NAME
        shutil.copyfile(archive, staged)
        sums = stage / "SHA256SUMS"
        sums.write_text(archive_checksum(staged), encoding="ascii", newline="\n")
        staged.replace(target)
        try:
            sums.replace(checksum)
        except OSError:
            if previous.exists():
                previous.replace(target)
            else:
                target.unlink()
            raise
    finally:
        shutil.rmtree(stage)
    print(f"exported validated {target} and {checksum.name}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--inspect", action="store_true")
    modes.add_argument("--build-platform-neutral", action="store_true")
    modes.add_argument("--build-arduino", action="store_true")
    modes.add_argument("--export-archive", type=Path, metavar="DIR")
    modes.add_argument("--build-idf", action="store_true")
    parser.add_argument("--out", type=Path, help="short external directory for ESP-IDF builds")
    parser.add_argument("--target", choices=("esp32s2", "esp32s3"))
    parser.add_argument("--idf-command", help="ESP-IDF 6.0.1 launcher and any prefix arguments")
    args = parser.parse_args()
    options = (args.out, args.target, args.idf_command)
    if args.build_idf and not all(options):
        parser.error("--build-idf requires --out, --target and --idf-command")
    if not args.build_idf and any(options):
        parser.error("--out, --target and --idf-command are for --build-idf")
    return args


def main() -> int:
    args = parse_args()
    before = repository_snapshot()
    failure: str | None = None
    with tempfile.TemporaryDirectory(prefix="at21cs-package-") as temporary:
        temp = Path(temporary).resolve()
        archive = temp / ARCHIVE_NAME
        try:
            if temp.is_relative_to(ROOT):
                raise CheckFailure("temporary package directory is inside the checkout")
            workspace = idf_workspace(args.out, args.target) if args.build_idf else temp
            package = workspace / "consumer"
            create_and_extract(archive, package)
            if args.build_platform_neutral:
                build_platform_neutral(package, temp)
            elif args.build_arduino:
                build_arduino(package)
            elif args.build_idf:
                build_idf(package, workspace / "b", args.target, args.idf_command)
        except (CheckFailure, OSError, tarfile.TarError, ValueError) as error:
            failure = str(error)
        try:
            after = repository_snapshot()
            if after != before:
                failure = describe_repository_change(before, after)
            # CI requests its two output files inside the checkout. Publish them
            # only once packing and the complete before/after comparison pass.
            if failure is None and args.export_archive:
                export_archive(archive, args.export_archive)
        except (CheckFailure, OSError, ValueError) as error:
            failure = str(error)
    if failure is not None:
        print(f"Package check FAILED: {failure}", file=sys.stderr)
        return 1
    print("Package check PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
