# AT21CS11 -- ESP-IDF Migration Prompt

> **Library**: AT21CS11 (Microchip single-wire EEPROM / serial number IC)
> **Current version**: 1.1.1 -> **Target**: 2.0.0
> **Namespace**: `AT21CS`
> **Main class**: `AT21CS::Driver`
> **Include path**: `#include "AT21CS11/AT21CS11.h"`
> **Difficulty**: Medium -- has IDF GPIO already but also Arduino fallback paths

---

## Pre-Migration

```bash
git tag v1.1.1   # freeze Arduino-era version
```

---

## Current State -- Arduino Dependencies (exact)

| API | Count | Locations |
|-----|-------|-----------|
| `#include <Arduino.h>` | 1 | `.cpp` only |
| `delayMicroseconds()` | ~17 | Bit-banging timing |
| `millis()` | 4 | Timeout checks |
| `pinMode()` | via `#ifdef` | Arduino fallback GPIO |
| `digitalWrite()` | via `#ifdef` | Arduino fallback GPIO |
| `digitalRead()` | via `#ifdef` | Arduino fallback GPIO |
| `portENTER_CRITICAL` / `portEXIT_CRITICAL` | present | Already FreeRTOS native |

**Key**: The library already has IDF GPIO code behind `#ifdef ARDUINO_ARCH_ESP32` with direct `gpio_set_direction()`, `gpio_set_level()`, `gpio_get_level()` calls. There's an Arduino fallback for non-ESP32 Arduino boards.

---

## Steps

### 1. Remove `#include <Arduino.h>`

### 2. Remove Arduino GPIO fallback entirely

Delete all `#ifdef ARDUINO` / `#else` branches that use `pinMode()`, `digitalWrite()`, `digitalRead()`. Keep **only** the ESP-IDF GPIO paths that already exist:

```cpp
#include "driver/gpio.h"
// gpio_set_direction(), gpio_set_level(), gpio_get_level()
```

### 3. Replace `delayMicroseconds()` -> `esp_rom_delay_us()`

All ~17 sites. This is the correct replacement for bit-banging timing on ESP32:

```cpp
#include "esp_rom_sys.h"
// delayMicroseconds(us) -> esp_rom_delay_us(us)
```

`esp_rom_delay_us()` is a busy-wait (identical behavior to `delayMicroseconds`), which is correct for single-wire protocol bit-level timing.

### 4. Replace 4x `millis()` -> `nowMs()` helper

```cpp
#include "esp_timer.h"

static inline uint32_t nowMs() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}
```

### 5. Keep `portENTER_CRITICAL` / `portEXIT_CRITICAL`

These are already FreeRTOS native -- no change needed.

### 6. Add `CMakeLists.txt` (library root)

```cmake
idf_component_register(
    SRCS "src/AT21CS11.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer
)
```

### 7. Add `idf_component.yml` (library root)

```yaml
version: "2.0.0"
description: "AT21CS11 single-wire EEPROM/serial-number driver"
targets:
  - esp32s2
  - esp32s3
dependencies:
  idf: ">=5.0"
```

### 8. Version bump

- `library.json` -> `2.0.0`
- `Version.h` (if present) -> `2.0.0`

### 9. Replace Arduino example with ESP-IDF example

Create `examples/espidf_basic/main/main.cpp`:

```cpp
#include <cstdio>
#include "AT21CS11/AT21CS11.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
    AT21CS::Config cfg{};
    cfg.pin = GPIO_NUM_5;  // single-wire data pin

    AT21CS::Driver drv;
    auto st = drv.begin(cfg);
    if (st.err != AT21CS::Err::Ok) {
        printf("begin() failed: %s\n", st.msg);
        return;
    }

    uint8_t serial[8];
    st = drv.readSerialNumber(serial, sizeof(serial));
    if (st.err == AT21CS::Err::Ok) {
        printf("Serial: %02X%02X%02X%02X%02X%02X%02X%02X\n",
               serial[0], serial[1], serial[2], serial[3],
               serial[4], serial[5], serial[6], serial[7]);
    }

    vTaskDelay(portMAX_DELAY);
}
```

Create `examples/espidf_basic/main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." REQUIRES driver esp_timer)
```

Create `examples/espidf_basic/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "../..")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(at21cs11_espidf_basic)
```

---

## Verification

```bash
cd examples/espidf_basic && idf.py set-target esp32s2 && idf.py build
```

- [ ] `idf.py build` succeeds
- [ ] Zero `#include <Arduino.h>` anywhere
- [ ] Zero `delayMicroseconds()`, `millis()`, `pinMode()`, `digitalWrite()`, `digitalRead()`
- [ ] No `#ifdef ARDUINO` remaining
- [ ] Only ESP-IDF GPIO and timing APIs used
- [ ] Version bumped to 2.0.0
- [ ] `git tag v2.0.0`
