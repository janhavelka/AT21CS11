#!/usr/bin/env python3
"""Exercise native example guards on temporary copies of the source."""
from __future__ import annotations

import shutil
import tempfile
import unittest
from pathlib import Path

import check_idf_example_contract as contract


class NativeExampleContractTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.example = self.root / contract.EXAMPLE
        source = contract.ROOT / contract.EXAMPLE
        for path in contract.source_files(source):
            copy = self.example / path.relative_to(source)
            copy.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, copy)
        (self.root / "docs").mkdir()
        shutil.copyfile(contract.ROOT / "docs/ESP_IDF.md", self.root / "docs/ESP_IDF.md")

    def rejected(self, path: Path, before: str, after: str, message: str) -> None:
        original = path.read_text(encoding="utf-8")
        self.assertIn(before, original)
        path.write_text(original.replace(before, after), encoding="utf-8")
        try:
            with self.assertRaises(contract.ContractError) as error:
                contract.check(self.root)
            self.assertIn(message, str(error.exception))
        finally:
            path.write_text(original, encoding="utf-8")

    def test_unmodified_copy(self) -> None:
        contract.check(self.root)

    def test_required_files(self) -> None:
        for name in contract.REQUIRED_FILES:
            with self.subTest(name=name):
                path = self.example / name
                data = path.read_bytes()
                path.unlink()
                try:
                    with self.assertRaises(contract.ContractError) as error:
                        contract.check(self.root)
                    self.assertIn(f"missing {contract.EXAMPLE.as_posix()}/{name}", str(error.exception))
                finally:
                    path.write_bytes(data)

    def test_project_entry(self) -> None:
        cmake = self.example / "CMakeLists.txt"
        self.rejected(cmake, "project(at21cs_basic)", "project(other)", "project(at21cs_basic)")

    def test_checkout_component_override_in_cmake(self) -> None:
        for path in self.example.rglob("*"):
            if not (path.name == "CMakeLists.txt" or path.suffix == ".cmake"):
                continue
            with self.subTest(file=path.relative_to(self.example)):
                text = path.read_text(encoding="utf-8")
                self.rejected(path, text, text + "\nset(EXTRA_COMPONENT_DIRS ../../..)\n", "fixed-name component")

    def test_wrapper_only_includes_root_component(self) -> None:
        wrapper = self.example / contract.WRAPPER
        self.rejected(wrapper, "include(", 'message("extra")\ninclude(', "only an include")
        self.rejected(wrapper, "/CMakeLists.txt", "/other.cmake", "only an include")

    def test_forbidden_native_shell_code(self) -> None:
        main = self.example / "main/main.cpp"
        for injected, error in (
            ("#include <Arduino.h>", "Arduino.h"),
            ("Serial.print(1);", "Serial."),
            ("CommandRegistration local[] = {};", "CommandRegistration"),
            ("void handleHelp() {}", "handleHelp"),
            ("gpio_set_level(6, 1);", "gpio_set_level"),
            ("xTaskCreatePinnedToCore();", "xTaskCreate"),
        ):
            with self.subTest(injected=injected):
                self.rejected(main, '#include "BringupCli.h"', injected + '\n#include "BringupCli.h"', error)

    def test_console_and_yield(self) -> None:
        main = self.example / "main/main.cpp"
        for token in ("O_NONBLOCK", "F_GETFL", "F_SETFL", "getchar()", "_IONBF", "vTaskDelay(1)"):
            with self.subTest(token=token):
                self.rejected(main, token, "removed", token)
        self.rejected(main, "!configureConsole()", "false", "console setup must succeed")
        self.rejected(main, "vTaskDelay(1);", "if (idle) vTaskDelay(1);", "each owner iteration")

    def test_main_registration_inputs(self) -> None:
        cmake = self.example / "main/CMakeLists.txt"
        for token in ("main.cpp", "../../../common"):
            self.rejected(cmake, token, "removed", "expected SRCS main.cpp")

    def test_target_cannot_override_common_defaults(self) -> None:
        target = self.example / "sdkconfig.defaults.esp32s3"
        with target.open("a", encoding="utf-8") as output:
            output.write("\nCONFIG_FREERTOS_HZ=100\n")
        with self.assertRaisesRegex(contract.ContractError, "CONFIG_FREERTOS_HZ=1000"):
            contract.check(self.root)

    def test_missing_configuration(self) -> None:
        for name, settings in (
            ("sdkconfig.defaults", contract.COMMON_DEFAULTS),
            ("sdkconfig.defaults.esp32s2", ("CONFIG_ESP_CONSOLE_USB_CDC=y",)),
            ("sdkconfig.defaults.esp32s3", ("CONFIG_ESP_CONSOLE_SECONDARY_NONE=y",)),
        ):
            for setting in settings:
                with self.subTest(name=name, setting=setting):
                    self.rejected(self.example / name, setting, "", setting)

    def test_missing_dependencies(self) -> None:
        cmake = self.example / "main/CMakeLists.txt"
        for name in ("AT21CS01_AT21CS11", "esp_timer", "freertos", "vfs",
                     "esp_driver_usb_serial_jtag", "esp_usb_cdc_rom_console"):
            with self.subTest(name=name):
                self.rejected(cmake, name, "removed", "REQUIRES " + name)

    def test_no_overrides_or_manifests(self) -> None:
        cmake = self.example / "CMakeLists.txt"
        self.rejected(cmake, "project(", "override_path: ../..\nproject(", "override_path")
        (self.example / "main/idf_component.yml").write_text("version: 1.0.0", encoding="utf-8")
        with self.assertRaisesRegex(contract.ContractError, "unexpected example manifest"):
            contract.check(self.root)

    def test_documented_envelope_and_build_scope(self) -> None:
        for path in (self.example / "README.md", self.root / "docs/ESP_IDF.md"):
            for token in ("0.60", "1.00", "set-target esp32s2", "set-target esp32s3",
                          "not hardware validation"):
                with self.subTest(document=path.name, token=token):
                    self.rejected(path, token, "removed", token)


if __name__ == "__main__":
    unittest.main()
