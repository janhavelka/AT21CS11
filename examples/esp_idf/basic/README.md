# Single-device CLI for ESP-IDF

This ESP-IDF 6.0.1 project runs the same 14 commands as Arduino example 01
on ESP32-S2 and ESP32-S3.

## Wiring

| Connection or setting | Example default | Main component override |
|---|---|---|
| EEPROM SI/O | GPIO 6, open drain | `AT21CS_EXAMPLE_PRIMARY_SIO_PIN` |
| Optional detect input | Disabled (`-1`), active high when enabled | `AT21CS_EXAMPLE_PRIMARY_PRESENCE_PIN`, `AT21CS_EXAMPLE_PRIMARY_PRESENCE_ACTIVE_HIGH` |
| Three address bits | 0 | `AT21CS_EXAMPLE_PRIMARY_ADDRESS_BITS` |
| Expected part | AT21CS11 (11), High Speed only | `AT21CS_EXAMPLE_PRIMARY_PART` (1 selects AT21CS01) |

Connect the board and EEPROM grounds. An external SI/O pull-up is required
because the Backend disables internal pulls. An enabled detect input also
needs a defined level when the device is absent. To change the example
wiring, add the relevant definitions after `idf_component_register` in
`main/CMakeLists.txt`. For example, a board using GPIO 4 for SI/O can use
the following setting.

```cmake
target_compile_definitions(${COMPONENT_LIB} PRIVATE AT21CS_EXAMPLE_PRIMARY_SIO_PIN=4)
```

SI/O must rise within 0.60 microseconds in High Speed or 1.00 microsecond in
Standard Speed after the host releases it. At a 3.3 V pull-up supply,
`tPUP ~= 1.04 * R_PUP * C_BUS` estimates the time to rise from 0.5 V to
0.7 times the pull-up voltage. Include the connector, cable and probe in
the total capacitance.

| Total `C_BUS` | Approximate maximum High-Speed `R_PUP` at 3.3 V |
|---:|---:|
| 100 pF | 5.8 kOhm |
| 330 pF | 1.7 kOhm |
| 1000 pF | 0.58 kOhm |

## Build and connect

From the checkout root in an ESP-IDF 6.0.1 environment, select your target
and build its image.

```sh
cd examples/esp_idf/basic
idf.py -B ../../../build-example-s2 -D SDKCONFIG=../../../sdkconfig.esp32s2 set-target esp32s2 build
idf.py -B ../../../build-example-s3 -D SDKCONFIG=../../../sdkconfig.esp32s3 set-target esp32s3 build
```

Connect the board's native USB port. ESP32-S2 uses USB CDC and ESP32-S3
uses USB Serial/JTAG. The console is unbuffered, with non-blocking input.
LF passthrough preserves the engine's CRLF and LF output. The input parser
accepts CR, LF and CRLF. To flash and open the monitor, substitute the board's
port for `PORT` and run the line for your target.

```sh
idf.py -B ../../../build-example-s2 -p PORT flash monitor
idf.py -B ../../../build-example-s3 -p PORT flash monitor
```

The banner reports the library version, 12-character Git commit, working-tree
status, ESP-IDF version and target. Commit and status both read `unknown`
when the sources lack their own Git checkout or Git is unavailable.

## First session

```text
help
status
presence
probe
manufacturer
read-eeprom 0 8
```

`presence` reports the optional detect input. With detection disabled, use
`probe` to check whether the device responds. `write-page` changes EEPROM
data and requires the exact confirmation token `CONFIRM_EEPROM_OVERWRITE`.
Irreversible provisioning is outside the CLI. See the
[provisioning guide](../../../docs/IRREVERSIBLE_OPERATIONS.md).

The main task on CPU0 is the sole owner of the Backend, Bus and Driver.
It yields one tick on every pass. Call the library only from that task.
An AT21CS01 Standard-Speed segment can mask interrupts for up to about
5.8 ms. The example uses an 8192-byte main-task stack. Its high-water mark
has not been measured on a board. A native build is not hardware validation.
