#!/usr/bin/env python3
"""Check the native example's component, console, and firmware-owner contracts."""
from __future__ import annotations

from collections.abc import Iterator
import os
import re
import shlex
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXAMPLE = Path("examples/esp_idf/basic")
WRAPPER = "components/AT21CS01_AT21CS11/CMakeLists.txt"
REQUIRED_FILES = (
    "CMakeLists.txt", "README.md", "sdkconfig.defaults",
    "sdkconfig.defaults.esp32s2", "sdkconfig.defaults.esp32s3", WRAPPER,
    "main/CMakeLists.txt", "main/GenerateBuildIdentity.cmake", "main/main.cpp",
)
COMMON_DEFAULTS = (
    "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y",
    "CONFIG_FREERTOS_HZ=1000",
    "CONFIG_COMPILER_OPTIMIZATION_SIZE=y",
    "# CONFIG_PM_ENABLE is not set",
    "CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192",
    "CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y",
    "CONFIG_LIBC_STDIN_LINE_ENDING_LF=y",
    "CONFIG_LIBC_STDOUT_LINE_ENDING_LF=y",
)
CONSOLES = {
    "esp32s2": "CONFIG_ESP_CONSOLE_USB_CDC=y",
    "esp32s3": "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y",
}
ARDUINO_TOKENS = ("Arduino.h", "Serial.", "millis(", "delay(", "String ")


REQUIRED_COMPONENTS = (
    "AT21CS01_AT21CS11", "esp_timer", "freertos", "vfs",
    "esp_driver_usb_serial_jtag", "esp_usb_cdc_rom_console",
)


class ContractError(ValueError):
    pass


def source_files(project: Path, start: Path | None = None) -> Iterator[Path]:
    # idf.py can leave local build products next to the project's source.
    for directory, dirs, files in os.walk(start or project):
        if Path(directory) == project:
            dirs[:] = [
                name for name in dirs
                if not name.startswith("build") and name != "managed_components"
            ]
            files = [
                name for name in files
                if name not in ("dependencies.lock", "sdkconfig", "sdkconfig.old")
                and not name.startswith("sdkconfig.esp32")
            ]
        for name in files:
            yield Path(directory) / name


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        raise ContractError(f"{label}: missing {token}")


def cmake_source(path: Path) -> str:
    return re.sub(r"#[^\n]*", "", path.read_text(encoding="utf-8"))


def check_config(text: str, required: tuple[str, ...], label: str) -> None:
    for setting in required:
        key = setting.removeprefix("# ").split("=", 1)[0].split()[0]
        values = re.findall(rf"^(?:# )?{key}(?:=.*| is not set)$", text, re.M)
        if values != [setting]:
            raise ContractError(f"{label}: expected {setting}, found {values}")


def check_cmake(project: Path, root: Path) -> None:
    project_cmake = cmake_source(project / "CMakeLists.txt")
    require(project_cmake, "project(at21cs_basic)", "project CMakeLists.txt")

    wrapper = project / WRAPPER
    include = re.fullmatch(
        r'\s*include\(\s*"\${CMAKE_CURRENT_LIST_DIR}/([.\w/]+)"\s*\)\s*',
        cmake_source(wrapper),
    )
    if not include or (wrapper.parent / include[1]).resolve() != (root / "CMakeLists.txt").resolve():
        raise ContractError(f"{WRAPPER}: expected only an include of the root CMakeLists.txt")

    cmake = cmake_source(project / "main/CMakeLists.txt")
    register = re.search(r"\bidf_component_register\(([^()]*)\)", cmake)
    if not register:
        raise ContractError("main/CMakeLists.txt: missing idf_component_register")
    args: dict[str, list[str]] = {}
    field = ""
    for word in shlex.split(register[1]):
        if word in ("SRCS", "INCLUDE_DIRS", "PRIV_INCLUDE_DIRS", "REQUIRES", "PRIV_REQUIRES"):
            field = word
            args[field] = []
        else:
            args.setdefault(field, []).append(word)
    if (args.get("SRCS") != ["main.cpp"]
            or set(args.get("INCLUDE_DIRS", ())) != {".", "../../../common"}):
        raise ContractError("main/CMakeLists.txt: expected SRCS main.cpp and INCLUDE_DIRS . ../../../common")
    for name in REQUIRED_COMPONENTS:
        if name not in args.get("REQUIRES", ()):
            raise ContractError(f"main/CMakeLists.txt: missing REQUIRES {name}")


def check_console(main: str) -> None:
    setup = main.partition("bool configureConsole()")[2].partition("\n}")[0]
    common, _, targets = setup.partition("#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)")
    jtag, _, cdc = targets.partition("#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)")
    if not re.search(r"\|\s*O_NONBLOCK\b", common):
        raise ContractError("main.cpp: enable O_NONBLOCK before preparing USB input")
    install = jtag.find("usb_serial_jtag_driver_install(")
    if install < 0 or jtag.find("usb_serial_jtag_vfs_use_driver(") < install:
        raise ContractError("main.cpp: install and select the USB Serial/JTAG driver for non-blocking RX")
    prime = cdc.find("readChar()")
    if prime < 0 or cdc.find("ungetc(") < prime:
        raise ContractError("main.cpp: prime the USB CDC input lock and preserve its first byte")


def check_shell(project: Path) -> None:
    main = (project / "main/main.cpp").read_text(encoding="utf-8")
    for token in ARDUINO_TOKENS:
        if token in main:
            raise ContractError(f"main.cpp: forbidden Arduino token {token}")
    main = re.sub(r"//[^\n]*|/\*.*?\*/", "", main, flags=re.S)
    for token in (
        'extern "C" void app_main()', '#include "BringupCli.h"', "getchar()",
        "fcntl(", "O_NONBLOCK", "F_GETFL", "F_SETFL", "setvbuf(stdin", "setvbuf(stdout", "clearerr(stdin)",
        "_IONBF", "vTaskDelay(1)", "esp_timer_get_time()", "esp_get_idf_version()",
        "AT21CS::VERSION", "CONFIG_IDF_TARGET", "AT21CS_GIT_COMMIT", "AT21CS_GIT_STATUS",
    ):
        require(main, token, "main.cpp")
    check_console(main)
    forbidden = re.search(
        r"\b(?:CommandRegistration|REGISTRATIONS|dispatch|handle\w+|gpio_\w+|"
        r"xTaskCreate\w*|pthread_create|std::thread)\b", main,
    )
    if forbidden:
        raise ContractError(f"main.cpp: forbidden shell logic {forbidden[0]}")
    guard = re.search(r"if\s*\(\s*!configureConsole\(\)\s*\)\s*\{[^}]*\breturn;", main)
    start = re.search(r"\b\w+\.start\(\);", main)
    if not guard or not start or guard.end() > start.start():
        raise ContractError("main.cpp: console setup must succeed before starting the CLI")
    owner_loop = (
        r"(?:while\s*\(true\)|for\s*\(\s*;\s*;\s*\))\s*\{\s*"
        r"(\w+)\.pollInput\(\);\s*\1\.step\(\);\s*vTaskDelay\(1\);\s*}"
    )
    if not re.search(owner_loop, main[start.end():]):
        raise ContractError("main.cpp: each owner iteration must poll, step and yield one tick")


def check_defaults(project: Path) -> None:
    common = (project / "sdkconfig.defaults").read_text(encoding="utf-8")
    check_config(common, COMMON_DEFAULTS, "sdkconfig.defaults")
    for target, console in CONSOLES.items():
        name = f"sdkconfig.defaults.{target}"
        overrides = (project / name).read_text(encoding="utf-8")
        check_config(overrides, (console, "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y"), name)
        check_config(common + "\n" + overrides, COMMON_DEFAULTS, name)


def check_docs(project: Path, root: Path) -> None:
    for document in (project / "README.md", root / "docs/ESP_IDF.md"):
        text = document.read_text(encoding="utf-8")
        if document.name == "ESP_IDF.md":
            section = re.search(r"^## [^\n]*example[^\n]*\n(.*?)(?=^## |\Z)", text, re.M | re.S | re.I)
            if not section:
                raise ContractError("docs/ESP_IDF.md: missing native example section")
            text = section[1]
        doc = " ".join(text.split())
        for token in ("0.60", "1.00", "set-target esp32s2", "set-target esp32s3",
                      "idf.py", "not hardware validation"):
            require(doc, token, str(document.relative_to(root)))
    readme = " ".join((project / "README.md").read_text(encoding="utf-8").split())
    for token in ("write-page", "CONFIRM_EEPROM_OVERWRITE", "USB Serial/JTAG", "5.8 ms", "3.3 V"):
        require(readme, token, "example README.md")


def check(root: Path) -> None:
    project = root / EXAMPLE
    for rel in REQUIRED_FILES:
        if not (project / rel).is_file():
            raise ContractError(f"missing {EXAMPLE.as_posix()}/{rel}")
    for path in source_files(project):
        if path.name == "idf_component.yml":
            raise ContractError(f"{path.relative_to(root)}: unexpected example manifest")
        if path.suffix == ".cmake" or path.name == "CMakeLists.txt":
            if "EXTRA_COMPONENT_DIRS" in cmake_source(path):
                raise ContractError(f"{path.relative_to(root)}: use the fixed-name component wrapper")
        if "override_path" in path.read_text(encoding="utf-8"):
            raise ContractError(f"{path.relative_to(root)}: override_path is forbidden")

    check_cmake(project, root)
    check_shell(project)
    check_defaults(project)
    check_docs(project, root)


def check_workflow(path: Path) -> None:
    # Check each contract in its own job. An action pin or target mentioned in
    # another job must not satisfy a missing native build or package gate.
    workflow = path.read_text(encoding="utf-8")
    jobs = dict(re.findall(r"^  ([\w-]+):\n((?:    [^\n]*\n|\n)*)", workflow, re.M))
    idf = "espressif/esp-idf-ci-action@e6f5c74232b1ccd4c97ed641f1e48553853f1fd5"
    upload = "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02"
    download = "actions/download-artifact@d3f86a106a0bac45b974a628896c90dbdf5c8093"
    native_build = (
        f"uses: {idf}", "esp_idf_version: v6.0.1", "target: ${{ matrix.target }}",
        "target: [esp32s3, esp32s2]", "fail-fast: false", "if: always()",
        f"uses: {upload}", "idf.py ", 'set-target "$IDF_TARGET" build',
    )
    iram = ("check_esp32_iram_sections.py", "xtensa-$IDF_TARGET-elf-objdump", "--require-undefined")
    contracts = {
        "esp-idf-example": (*native_build, *iram, "path: examples/esp_idf/basic"),
        "esp-idf-smoke": (
            *native_build, *iram, "path: test/consumer/phy_smoke/idf", "profile: [default, pm]",
            "sdkconfig.defaults;sdkconfig.pm", "esp_pm_lock_acquire esp_pm_lock_release",
            "--elf build/at21cs_phy_smoke.elf",
        ),
        "package-payload": (
            "python tools/check_package.py --export-archive payload", f"uses: {upload}",
            "name: package-payload", "path: payload/", "if-no-files-found: error",
        ),
        "package-idf": (
            *native_build, "needs: package-payload", f"uses: {download}", "name: package-payload",
            "path: payload", "sha256sum --check --strict SHA256SUMS", "tar -xzf", "-C consumer",
            "path: consumer/examples/esp_idf/basic", 'test -f "$file"', "exit 1", *REQUIRED_FILES,
        ),
        "docs-contract": ("python tools/check_idf_example_contract.py", "python tools/test_check_idf_example_contract.py"),
    }
    for name, tokens in contracts.items():
        if name not in jobs:
            raise ContractError(f"CI: missing {name} job")
        body = re.sub(r"(?:^|[ \t]+)#[^\n]*", "", jobs[name], flags=re.M)
        for token in tokens:
            if ": " in token:
                if not re.search(rf"^\s*(?:- )?{re.escape(token)}\s*$", body, re.M):
                    raise ContractError(f"CI {name}: missing {token}")
            elif token in REQUIRED_FILES:
                if not re.search(rf"(?<![\w/.-]){re.escape(token)}(?![\w/.-])", body):
                    raise ContractError(f"CI {name}: missing {token}")
            else:
                require(body, token, f"CI {name}")
        if name == "package-idf" and "actions/checkout@" in body:
            raise ContractError("CI package-idf must build the downloaded archive without checkout")


if __name__ == "__main__":
    try:
        check(ROOT)
        check_workflow(ROOT / ".github/workflows/ci.yml")
    except (ValueError, OSError) as exc:
        sys.exit(f"IDF example contract FAILED: {exc}")
    print("IDF example contract PASSED")
