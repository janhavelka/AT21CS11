#pragma once

#include <cstdint>

#include "AT21CS/AT21CS.h"

// The optional presence pins are example firmware choices. Exactly -1 disables
// a pin. An enabled input needs a stable external bias; the Backend deliberately
// enables neither an internal pull nor an interrupt and maps the selected
// active-high/active-low polarity to one logical sample.

#ifndef AT21CS_EXAMPLE_PRIMARY_SIO_PIN
#define AT21CS_EXAMPLE_PRIMARY_SIO_PIN 6
#endif
#ifndef AT21CS_EXAMPLE_PRIMARY_PRESENCE_PIN
#define AT21CS_EXAMPLE_PRIMARY_PRESENCE_PIN -1
#endif
#ifndef AT21CS_EXAMPLE_PRIMARY_PRESENCE_ACTIVE_HIGH
#define AT21CS_EXAMPLE_PRIMARY_PRESENCE_ACTIVE_HIGH 1
#endif
#ifndef AT21CS_EXAMPLE_PRIMARY_ADDRESS_BITS
#define AT21CS_EXAMPLE_PRIMARY_ADDRESS_BITS 0
#endif
#ifndef AT21CS_EXAMPLE_PRIMARY_PART
#define AT21CS_EXAMPLE_PRIMARY_PART 11
#endif

#ifndef AT21CS_EXAMPLE_SECONDARY_SIO_PIN
#define AT21CS_EXAMPLE_SECONDARY_SIO_PIN 10
#endif
#ifndef AT21CS_EXAMPLE_SECONDARY_PRESENCE_PIN
#define AT21CS_EXAMPLE_SECONDARY_PRESENCE_PIN -1
#endif
#ifndef AT21CS_EXAMPLE_SECONDARY_PRESENCE_ACTIVE_HIGH
#define AT21CS_EXAMPLE_SECONDARY_PRESENCE_ACTIVE_HIGH 1
#endif
#ifndef AT21CS_EXAMPLE_SECONDARY_ADDRESS_BITS
#define AT21CS_EXAMPLE_SECONDARY_ADDRESS_BITS 0
#endif
#ifndef AT21CS_EXAMPLE_SECONDARY_PART
#define AT21CS_EXAMPLE_SECONDARY_PART 11
#endif

#if AT21CS_EXAMPLE_PRIMARY_PRESENCE_ACTIVE_HIGH != 0 && \
    AT21CS_EXAMPLE_PRIMARY_PRESENCE_ACTIVE_HIGH != 1
#error "AT21CS_EXAMPLE_PRIMARY_PRESENCE_ACTIVE_HIGH must be 0 or 1"
#endif
#if AT21CS_EXAMPLE_SECONDARY_PRESENCE_ACTIVE_HIGH != 0 && \
    AT21CS_EXAMPLE_SECONDARY_PRESENCE_ACTIVE_HIGH != 1
#error "AT21CS_EXAMPLE_SECONDARY_PRESENCE_ACTIVE_HIGH must be 0 or 1"
#endif

namespace at21cs_example {
namespace board {

static constexpr uint32_t SERIAL_BAUD = 115200;

static constexpr int SIO_PRIMARY = AT21CS_EXAMPLE_PRIMARY_SIO_PIN;
static constexpr int PRESENCE_PRIMARY = AT21CS_EXAMPLE_PRIMARY_PRESENCE_PIN;
static constexpr bool PRESENCE_PRIMARY_ACTIVE_HIGH =
    AT21CS_EXAMPLE_PRIMARY_PRESENCE_ACTIVE_HIGH != 0;
static constexpr uint8_t ADDRESS_BITS_PRIMARY =
    AT21CS_EXAMPLE_PRIMARY_ADDRESS_BITS;
static constexpr uint8_t OFFLINE_THRESHOLD_PRIMARY = 5;

#if AT21CS_EXAMPLE_PRIMARY_PART == 1
static constexpr AT21CS::PartType EXPECTED_PART_PRIMARY =
    AT21CS::PartType::AT21CS01;
#elif AT21CS_EXAMPLE_PRIMARY_PART == 11
static constexpr AT21CS::PartType EXPECTED_PART_PRIMARY =
    AT21CS::PartType::AT21CS11;
#else
#error "AT21CS_EXAMPLE_PRIMARY_PART must be 1 or 11"
#endif

static constexpr int SIO_SECONDARY = AT21CS_EXAMPLE_SECONDARY_SIO_PIN;
static constexpr int PRESENCE_SECONDARY =
    AT21CS_EXAMPLE_SECONDARY_PRESENCE_PIN;
static constexpr bool PRESENCE_SECONDARY_ACTIVE_HIGH =
    AT21CS_EXAMPLE_SECONDARY_PRESENCE_ACTIVE_HIGH != 0;
static constexpr uint8_t ADDRESS_BITS_SECONDARY =
    AT21CS_EXAMPLE_SECONDARY_ADDRESS_BITS;
static constexpr uint8_t OFFLINE_THRESHOLD_SECONDARY = 5;

#if AT21CS_EXAMPLE_SECONDARY_PART == 1
static constexpr AT21CS::PartType EXPECTED_PART_SECONDARY =
    AT21CS::PartType::AT21CS01;
#elif AT21CS_EXAMPLE_SECONDARY_PART == 11
static constexpr AT21CS::PartType EXPECTED_PART_SECONDARY =
    AT21CS::PartType::AT21CS11;
#else
#error "AT21CS_EXAMPLE_SECONDARY_PART must be 1 or 11"
#endif

static constexpr uint32_t DETECT_SAMPLE_MS = 20;
static constexpr uint32_t DETECT_DEBOUNCE_MS = 100;
static constexpr uint32_t HOTPLUG_POLL_MS = 1000;

static_assert(AT21CS_EXAMPLE_PRIMARY_ADDRESS_BITS >= 0 &&
                  AT21CS_EXAMPLE_PRIMARY_ADDRESS_BITS <= 7,
              "primary address bits must be in 0..7");
static_assert(AT21CS_EXAMPLE_SECONDARY_ADDRESS_BITS >= 0 &&
                  AT21CS_EXAMPLE_SECONDARY_ADDRESS_BITS <= 7,
              "secondary address bits must be in 0..7");
static_assert(PRESENCE_PRIMARY >= -1,
              "primary presence pin is -1 or a nonnegative GPIO");
static_assert(PRESENCE_SECONDARY >= -1,
              "secondary presence pin is -1 or a nonnegative GPIO");
static_assert(SIO_PRIMARY != SIO_SECONDARY,
              "the two example wire instances need distinct SI/O pins");
static_assert(PRESENCE_PRIMARY == -1 || PRESENCE_PRIMARY != SIO_PRIMARY,
              "primary detect pin must differ from primary SI/O");
static_assert(PRESENCE_PRIMARY == -1 || PRESENCE_PRIMARY != SIO_SECONDARY,
              "primary detect pin must differ from secondary SI/O");
static_assert(PRESENCE_SECONDARY == -1 || PRESENCE_SECONDARY != SIO_PRIMARY,
              "secondary detect pin must differ from primary SI/O");
static_assert(PRESENCE_SECONDARY == -1 ||
                  PRESENCE_SECONDARY != SIO_SECONDARY,
              "secondary detect pin must differ from secondary SI/O");
static_assert(PRESENCE_PRIMARY == -1 || PRESENCE_SECONDARY == -1 ||
                  PRESENCE_PRIMARY != PRESENCE_SECONDARY,
              "enabled detect pins must be distinct");
static_assert(DETECT_SAMPLE_MS > 0u && DETECT_SAMPLE_MS < 0x80000000u,
              "detect sample period must be wrap-safe");
static_assert(DETECT_DEBOUNCE_MS > 0u &&
                  DETECT_DEBOUNCE_MS < 0x80000000u,
              "detect debounce period must be wrap-safe");
static_assert(HOTPLUG_POLL_MS > 0u && HOTPLUG_POLL_MS < 0x80000000u,
              "hot-plug poll period must be wrap-safe");

}  // namespace board
}  // namespace at21cs_example
