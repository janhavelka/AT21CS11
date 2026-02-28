/// @file test_basic.cpp
/// @brief Native contract tests for AT21CS lifecycle and validation behavior.

#include <unity.h>

#include "Arduino.h"
#include "Wire.h"

SerialClass Serial;
TwoWire Wire;

#include "AT21CS/AT21CS.h"
#include "AT21CS/Config.h"
#include "AT21CS/Status.h"

using namespace AT21CS;

void setUp() {}
void tearDown() {}

void test_status_ok() {
  Status st = Status::Ok();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK), static_cast<uint8_t>(st.code));
}

void test_status_error() {
  Status st = Status::Error(Err::INVALID_PARAM, "bad", 7);
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(7, st.detail);
}

void test_config_defaults() {
  Config cfg;
  TEST_ASSERT_EQUAL_INT16(-1, cfg.sioPin);
  TEST_ASSERT_EQUAL_INT16(-1, cfg.presencePin);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.addressBits);
  TEST_ASSERT_EQUAL_UINT8(5, cfg.offlineThreshold);
}

void test_begin_rejects_missing_sio_pin() {
  Driver dev;
  Config cfg;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_rejects_same_presence_and_sio_pin() {
  Driver dev;
  Config cfg;
  cfg.sioPin = 5;
  cfg.presencePin = 5;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_rejects_zero_offline_threshold() {
  Driver dev;
  Config cfg;
  cfg.sioPin = 6;
  cfg.offlineThreshold = 0;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_rejects_write_timeout_over_limit() {
  Driver dev;
  Config cfg;
  cfg.sioPin = 6;
  cfg.writeTimeoutMs = 251;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));
}

void test_probe_requires_begin() {
  Driver dev;
  Status st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(st.code));
}

void test_recover_requires_begin() {
  Driver dev;
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(st.code));
}

void test_end_without_begin_keeps_uninit() {
  Driver dev;
  dev.end();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_status_ok);
  RUN_TEST(test_status_error);
  RUN_TEST(test_config_defaults);
  RUN_TEST(test_begin_rejects_missing_sio_pin);
  RUN_TEST(test_begin_rejects_same_presence_and_sio_pin);
  RUN_TEST(test_begin_rejects_zero_offline_threshold);
  RUN_TEST(test_begin_rejects_write_timeout_over_limit);
  RUN_TEST(test_probe_requires_begin);
  RUN_TEST(test_recover_requires_begin);
  RUN_TEST(test_end_without_begin_keeps_uninit);
  return UNITY_END();
}
