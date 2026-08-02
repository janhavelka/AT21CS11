# Legacy ESP-IDF port note (unsupported)

This document is retained temporarily so Prompt 07 can remove or archive the
old port-document set in its owned documentation cleanup. It is not a current
support or validation claim.

The supported ESP32-S2/S3 firmware framework is Arduino-ESP32 3.3.11 through
the exact PioArduino `platform-espressif32` 55.03.311 pin. The core transport
contract remains framework-independent, but this repository does not implement,
build, test, package, or advertise a native ESP-IDF path.

Do not install/select standalone ESP-IDF, invoke `idf.py`, use
`framework = espidf`, or treat the legacy native-IDF example as supported.
Prompt 06 removes that example; Prompt 07 removes or archives this note and its
companion implementation note.
