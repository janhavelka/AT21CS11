# Legacy ESP-IDF implementation note (unsupported)

This document is retained temporarily for Prompt 07's owned documentation
cleanup. It does not describe a supported build.

Stage 04 provides one externally owned `AT21CS::Esp32Transport` for
ESP32-S2/S3 under Arduino-ESP32 only. It uses the PioArduino-supplied guarded
SoC and FreeRTOS facilities while `Bus` and `Driver` remain framework-neutral.
The former root IDF component, component manifest, native-IDF smoke fixture,
metadata entries, and `ESP_PLATFORM` implementation branch have been removed.

Physical waveform, rise-time, CPU/DFS/load, interrupt-mask, page-duration, and
fault-isolation qualification is `HIL_ONLY` and belongs to Prompt 08. It does
not block the Stage 04 software implementation.
