/// @file test_basic.cpp
/// @brief Basic unit tests for AT21CS support types.

#include <cassert>
#include <cstdio>

// Include stubs first
#include "Arduino.h"
#include "Wire.h"

// Stub implementations
SerialClass Serial;
TwoWire Wire;

#include "AT21CS/Config.h"
#include "AT21CS/Status.h"

using namespace AT21CS;

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name)                                \
  do {                                                \
    printf("Running %s... ", #name);                \
    test_##name();                                    \
    printf("PASSED\n");                             \
    testsPassed++;                                    \
  } while (0)

#define ASSERT_TRUE(x) assert(x)
#define ASSERT_FALSE(x) assert(!(x))
#define ASSERT_EQ(a, b) assert((a) == (b))

TEST(status_ok) {
  Status st = Status::Ok();
  ASSERT_TRUE(st.ok());
  ASSERT_EQ(st.code, Err::OK);
}

TEST(status_error) {
  Status st = Status::Error(Err::INVALID_PARAM, "bad", 7);
  ASSERT_FALSE(st.ok());
  ASSERT_EQ(st.code, Err::INVALID_PARAM);
  ASSERT_EQ(st.detail, 7);
}

TEST(config_defaults) {
  Config cfg;
  ASSERT_EQ(cfg.sioPin, -1);
  ASSERT_EQ(cfg.presencePin, -1);
  ASSERT_EQ(cfg.addressBits, 0);
  ASSERT_EQ(cfg.offlineThreshold, 5);
}

int main() {
  printf("\n=== AT21CS Unit Tests ===\n\n");

  RUN_TEST(status_ok);
  RUN_TEST(status_error);
  RUN_TEST(config_defaults);

  printf("\n=== Results: %d passed, %d failed ===\n\n", testsPassed, testsFailed);
  return testsFailed > 0 ? 1 : 0;
}
