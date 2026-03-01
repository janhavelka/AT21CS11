# AT21CS11 ESP-IDF Portability Status

Last audited: 2026-03-01

## Current Reality
- Primary runtime remains PlatformIO + Arduino.
- Bus access is implemented internally with GPIO bit-banging in the driver.
- Timing now supports optional hooks in `AT21CS::Config`:
  - `nowMs`, `sleepUs`, `timeUser`
- Core protocol logic uses internal wrappers (`_nowMs`, `_sleepUs`).
- Arduino APIs are only fallback paths:
  - `_nowMs()` -> `millis()` when `Config.nowMs == nullptr`
  - `_sleepUs()` -> `delayMicroseconds()` when `Config.sleepUs == nullptr`

## ESP-IDF Adapter Requirements
For pure ESP-IDF integration, provide timing hooks:
1. `nowMs(user)` using `esp_timer_get_time()`.
2. `sleepUs(us, user)` using `esp_rom_delay_us()`.

No public I2C callback is required for AT21CS11 because the one-wire transport is implemented by the driver via GPIO.

## Minimal Adapter Pattern
```cpp
static uint32_t idfNowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

static void idfSleepUs(uint32_t us, void*) {
  esp_rom_delay_us(us);
}

AT21CS::Config cfg{};
cfg.sioPin = 5;
cfg.nowMs = idfNowMs;
cfg.sleepUs = idfSleepUs;
```

## Porting Notes
- Keep calling `tick(nowMs)` from the app loop/task.
- Driver uses unsigned delta arithmetic for timeout checks and remains rollover-safe.
- Preserve single-threaded access model (or guard externally with a mutex).

## Verification Checklist
- `python tools/check_core_timing_guard.py` passes.
- Native tests pass where available.
- Bring-up example builds and runs on target board.
- No direct Arduino timing calls are introduced outside fallback wrappers.
