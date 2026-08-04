#include <unity.h>

#include <cstdint>
#include <cstring>
#include <limits>

#include "WireInstance.h"

namespace {

using namespace at21cs_example;

AT21CS::SerialNumberInfo serialWith(uint8_t value) {
  AT21CS::SerialNumberInfo serial{};
  for (size_t index = 0; index < AT21CS::cmd::SECURITY_SERIAL_SIZE; ++index) {
    serial.bytes[index] = static_cast<uint8_t>(value + index);
  }
  serial.productIdOk = true;
  serial.crcOk = true;
  return serial;
}

void clearStartupSerial(HotPlugPolicy& policy) {
  (void)policy.noteSerial(AT21CS::Status::Ok(), serialWith(0x10));
}

}  // namespace

void test_hotplug_cadence_is_immediate_exact_wrap_safe_and_has_no_catchup() {
  HotPlugPolicy policy;
  policy.start(true);
  clearStartupSerial(policy);

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::PROBE),
      static_cast<int>(policy.nextAction(100, false, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));
  policy.noteProbe(100, AT21CS::Status::Ok(), AT21CS::DriverState::READY);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::NONE),
      static_cast<int>(policy.nextAction(1099, false, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::PROBE),
      static_cast<int>(policy.nextAction(1100, false, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));
  policy.noteProbe(5000, AT21CS::Status::Ok(), AT21CS::DriverState::READY);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::NONE),
      static_cast<int>(policy.nextAction(5001, false, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));

  const uint32_t beforeWrap = std::numeric_limits<uint32_t>::max() - 500U;
  policy.noteProbe(beforeWrap, AT21CS::Status::Ok(),
                   AT21CS::DriverState::READY);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::NONE),
      static_cast<int>(policy.nextAction(498, false, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::PROBE),
      static_cast<int>(policy.nextAction(499, false, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));
  policy.resetTiming();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::PROBE),
      static_cast<int>(policy.nextAction(500, false, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));
}

void test_presence_debounce_handles_bounce_absence_attach_and_sample_errors() {
  HotPlugPolicy policy;
  policy.start(true);
  clearStartupSerial(policy);

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::SAMPLE_PRESENCE),
      static_cast<int>(policy.nextAction(0, true, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));

  TEST_ASSERT_FALSE(
      policy.notePresenceSample(0, AT21CS::Status::Ok(), true, 100));
  TEST_ASSERT_FALSE(
      policy.notePresenceSample(20, AT21CS::Status::Ok(), false, 100));
  TEST_ASSERT_FALSE(
      policy.notePresenceSample(40, AT21CS::Status::Ok(), true, 100));
  TEST_ASSERT_FALSE(
      policy.notePresenceSample(139, AT21CS::Status::Ok(), true, 100));
  TEST_ASSERT_TRUE(
      policy.notePresenceSample(140, AT21CS::Status::Ok(), true, 100));
  TEST_ASSERT_TRUE(policy.debouncedKnown());
  TEST_ASSERT_TRUE(policy.debouncedPresent());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::NONE),
      static_cast<int>(policy.nextAction(140, true, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));

  TEST_ASSERT_FALSE(
      policy.notePresenceSample(160, AT21CS::Status::Ok(), false, 100));
  TEST_ASSERT_TRUE(
      policy.notePresenceSample(260, AT21CS::Status::Ok(), false, 100));
  TEST_ASSERT_FALSE(policy.debouncedPresent());
  TEST_ASSERT_TRUE(policy.recoveryPending());
  TEST_ASSERT_NOT_EQUAL(
      static_cast<int>(AutomaticAction::RECOVER),
      static_cast<int>(policy.nextAction(260, true, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));

  TEST_ASSERT_FALSE(
      policy.notePresenceSample(280, AT21CS::Status::Ok(), true, 100));
  TEST_ASSERT_TRUE(
      policy.notePresenceSample(380, AT21CS::Status::Ok(), true, 100));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::RECOVER),
      static_cast<int>(policy.nextAction(380, true, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));
  policy.noteRecovery(380, AT21CS::Status::Error(AT21CS::Err::NOT_PRESENT),
                      AT21CS::DriverState::OFFLINE, false);
  TEST_ASSERT_NOT_EQUAL(
      static_cast<int>(AutomaticAction::RECOVER),
      static_cast<int>(policy.nextAction(1379, true, true, false,
                                         AT21CS::DriverState::OFFLINE, 20, 1000)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::RECOVER),
      static_cast<int>(policy.nextAction(1380, true, true, false,
                                         AT21CS::DriverState::OFFLINE, 20, 1000)));

  const AT21CS::Status sampleError =
      AT21CS::Status::Error(AT21CS::Err::IO_ERROR, 7);
  TEST_ASSERT_TRUE(policy.notePresenceSample(1400, sampleError, false, 100));
  TEST_ASSERT_FALSE(policy.rawKnown());
  TEST_ASSERT_FALSE(policy.debouncedKnown());
  TEST_ASSERT_FALSE(policy.notePresenceSample(1420, sampleError, false, 100));
  (void)policy.noteSerial(sampleError, serialWith(0x70));
  TEST_ASSERT_FALSE(policy.notePresenceSample(1440, sampleError, false, 100));
  TEST_ASSERT_TRUE(policy.notePresenceSample(
      1460, AT21CS::Status::Error(AT21CS::Err::IO_ERROR, 8), false, 100));
}

void test_hotplug_action_rules_and_terminal_blocking_are_simple() {
  HotPlugPolicy policy;
  policy.start(false);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::RECOVER),
      static_cast<int>(policy.nextAction(0, false, true, false,
                                         AT21CS::DriverState::UNINIT, 20, 1000)));
  policy.noteRecovery(0, AT21CS::Status::Error(AT21CS::Err::NOT_PRESENT),
                      AT21CS::DriverState::OFFLINE, false);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::NONE),
      static_cast<int>(policy.nextAction(999, false, true, false,
                                         AT21CS::DriverState::OFFLINE, 20, 1000)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::RECOVER),
      static_cast<int>(policy.nextAction(1000, false, true, false,
                                         AT21CS::DriverState::OFFLINE, 20, 1000)));

  policy.noteRecovery(1000,
                      AT21CS::Status::Error(AT21CS::Err::INVALID_STATE),
                      AT21CS::DriverState::OFFLINE, false);
  TEST_ASSERT_TRUE(policy.automaticBlocked());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::NONE),
      static_cast<int>(policy.nextAction(5000, false, true, false,
                                         AT21CS::DriverState::OFFLINE, 20, 1000)));

  HotPlugPolicy probeBlocked;
  probeBlocked.start(true);
  clearStartupSerial(probeBlocked);
  probeBlocked.noteProbe(0, AT21CS::Status::Error(AT21CS::Err::NOT_BOUND),
                         AT21CS::DriverState::READY);
  TEST_ASSERT_TRUE(probeBlocked.automaticBlocked());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::NONE),
      static_cast<int>(probeBlocked.nextAction(
          5000, false, true, true, AT21CS::DriverState::READY, 20, 1000)));

  policy.noteRecovery(5000, AT21CS::Status::Ok(),
                      AT21CS::DriverState::READY, true);
  TEST_ASSERT_FALSE(policy.automaticBlocked());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::READ_SERIAL),
      static_cast<int>(policy.nextAction(5000, false, true, true,
                                         AT21CS::DriverState::READY, 20, 1000)));
  clearStartupSerial(policy);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::NONE),
      static_cast<int>(policy.nextAction(5999, false, true, true,
                                         AT21CS::DriverState::DEGRADED, 20, 1000)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::PROBE),
      static_cast<int>(policy.nextAction(6000, false, true, true,
                                         AT21CS::DriverState::DEGRADED, 20, 1000)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(AutomaticAction::NONE),
      static_cast<int>(policy.nextAction(6000, false, true, false,
                                         AT21CS::DriverState::FAULT, 20, 1000)));
}

void test_serial_identity_comparison_preserves_last_good_value_on_error() {
  HotPlugPolicy policy;
  policy.start(true);
  const AT21CS::SerialNumberInfo first = serialWith(0x20);
  IdentityObservation observation =
      policy.noteSerial(AT21CS::Status::Ok(), first);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(IdentityChange::FIRST_SEEN),
                        static_cast<int>(observation.change));
  TEST_ASSERT_TRUE(policy.identityKnown());
  TEST_ASSERT_TRUE(policy.identityCurrent());

  observation = policy.noteSerial(AT21CS::Status::Ok(), first);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(IdentityChange::SAME_DEVICE),
                        static_cast<int>(observation.change));

  const AT21CS::SerialNumberInfo replacement = serialWith(0x40);
  observation = policy.noteSerial(AT21CS::Status::Ok(), replacement);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(IdentityChange::DIFFERENT_DEVICE),
                        static_cast<int>(observation.change));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first.bytes, observation.previous,
                               AT21CS::cmd::SECURITY_SERIAL_SIZE);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(replacement.bytes, observation.current,
                               AT21CS::cmd::SECURITY_SERIAL_SIZE);

  const uint8_t saved[AT21CS::cmd::SECURITY_SERIAL_SIZE] = {
      replacement.bytes[0], replacement.bytes[1], replacement.bytes[2],
      replacement.bytes[3], replacement.bytes[4], replacement.bytes[5],
      replacement.bytes[6], replacement.bytes[7]};
  observation = policy.noteSerial(
      AT21CS::Status::Error(AT21CS::Err::CRC_MISMATCH), serialWith(0x60));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(IdentityChange::NONE),
                        static_cast<int>(observation.change));
  TEST_ASSERT_TRUE(policy.identityKnown());
  TEST_ASSERT_FALSE(policy.identityCurrent());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(saved, policy.identity(), sizeof(saved));
}

void test_example_arbiters_prevent_command_and_wire_starvation() {
  WorkArbiter work;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkChoice::COMMAND),
                        static_cast<int>(work.choose(true, true)));
  work.completed(WorkChoice::COMMAND);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkChoice::AUTOMATIC),
                        static_cast<int>(work.choose(true, true)));
  work.completed(WorkChoice::AUTOMATIC);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WorkChoice::COMMAND),
                        static_cast<int>(work.choose(true, true)));

  WireArbiter wires;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WireChoice::A),
                        static_cast<int>(wires.choose(true, true)));
  wires.completed(WireChoice::A);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WireChoice::B),
                        static_cast<int>(wires.choose(true, true)));
  wires.completed(WireChoice::B);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WireChoice::A),
                        static_cast<int>(wires.choose(true, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WireChoice::B),
                        static_cast<int>(wires.choose(false, true)));
}
