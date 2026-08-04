#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "AT21CS/AT21CS.h"
#include "WireInstance.h"

namespace at21cs_example {

inline const char* toString(AutomaticAction action) {
  switch (action) {
    case AutomaticAction::NONE: return "none";
    case AutomaticAction::SAMPLE_PRESENCE: return "presence";
    case AutomaticAction::PROBE: return "probe";
    case AutomaticAction::RECOVER: return "recover";
    case AutomaticAction::READ_SERIAL: return "serial";
  }
  return "unknown";
}

inline const char* toString(IdentityChange change) {
  switch (change) {
    case IdentityChange::NONE: return "not current";
    case IdentityChange::FIRST_SEEN: return "first seen";
    case IdentityChange::SAME_DEVICE: return "same device";
    case IdentityChange::DIFFERENT_DEVICE: return "different device";
  }
  return "unknown";
}

inline void printStatus(const char* label, const AT21CS::Status& status) {
  Serial.printf("%s: %s detail=%ld phase=%s index=%u msg=%s\n", label,
                AT21CS::toString(status.code),
                static_cast<long>(status.detail),
                AT21CS::toString(AT21CS::protocolDetailPhase(status.detail)),
                static_cast<unsigned>(
                    AT21CS::protocolDetailIndex(status.detail)),
                status.msg == nullptr ? "" : status.msg);
}

inline void printBytes(const uint8_t* bytes, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    if (index != 0) {
      Serial.print(' ');
    }
    Serial.printf("%02X", static_cast<unsigned>(bytes[index]));
  }
  Serial.println();
}

inline void printIdentity(const IdentityObservation& observation) {
  Serial.printf("identity: %s\n", toString(observation.change));
  if (observation.change == IdentityChange::DIFFERENT_DEVICE) {
    Serial.print("old: ");
    printBytes(observation.previous, AT21CS::cmd::SECURITY_SERIAL_SIZE);
  }
  if (observation.change != IdentityChange::NONE) {
    Serial.print("serial: ");
    printBytes(observation.current, AT21CS::cmd::SECURITY_SERIAL_SIZE);
  }
}

inline void printServiceResult(const char* wire, const ServiceResult& result) {
  if (!result.performed || !result.report) {
    return;
  }
  Serial.printf("[%s automatic %s] ", wire, toString(result.action));
  Serial.printf("%s detail=%ld\n", AT21CS::toString(result.status.code),
                static_cast<long>(result.status.detail));
  if (result.action == AutomaticAction::SAMPLE_PRESENCE &&
      result.presenceKnown) {
    Serial.printf("[%s presence] %s\n", wire,
                  result.presence ? "present" : "absent");
  }
  if (result.action == AutomaticAction::READ_SERIAL && result.status.ok()) {
    printIdentity(result.identity);
  }
}

inline void printInstanceStatus(const char* wire, const WireInstance& instance) {
  const AT21CS::SettingsSnapshot driver = instance.driver.snapshot();
  const AT21CS::BusSnapshot bus = instance.bus.snapshot();
  const HotPlugPolicy& policy = instance.policy();
  Serial.printf(
      "[%s] active=%s bus=%s driver=%s initialized=%s state=%s "
      "address=%u failures=%u automatic-blocked=%s recovery-pending=%s\n",
      wire, instance.active() ? "yes" : "no", bus.bound ? "bound" : "unbound",
      driver.bound ? "bound" : "unbound",
      driver.initialized ? "yes" : "no", AT21CS::toString(driver.state),
      static_cast<unsigned>(driver.addressBits),
      static_cast<unsigned>(driver.consecutiveFailures),
      policy.automaticBlocked() ? "yes" : "no",
      policy.recoveryPending() ? "yes" : "no");
  Serial.printf(
      "[%s] expected=%s detected=%s manufacturer=0x%06lX revision=%u "
      "speed=%s known=%s generation=%llu\n",
      wire, AT21CS::toString(driver.expectedPart),
      AT21CS::toString(driver.detectedPart),
      static_cast<unsigned long>(driver.manufacturerId),
      static_cast<unsigned>(driver.siliconRevision),
      AT21CS::toString(driver.activeSpeed), driver.speedKnown ? "yes" : "no",
      static_cast<unsigned long long>(bus.generation));
  Serial.printf("[%s] identity-known=%s current=%s presence=", wire,
                policy.identityKnown() ? "yes" : "no",
                policy.identityCurrent() ? "yes" : "no");
  if (!instance.bus.hasPresenceIndicator()) {
    Serial.println("disabled");
  } else if (!policy.debouncedKnown()) {
    Serial.println("unknown");
  } else {
    Serial.println(policy.debouncedPresent() ? "present" : "absent");
  }
  Serial.printf("[%s] last-automatic=%s status=%s detail=%ld\n", wire,
                toString(policy.lastAction()),
                AT21CS::toString(policy.lastStatus().code),
                static_cast<long>(policy.lastStatus().detail));
}

inline void printWriteResult(const AT21CS::WriteResult& result) {
  Serial.printf("write: committed=%u accepted-last-page=%u effect=%s\n",
                static_cast<unsigned>(result.bytesCommitted),
                static_cast<unsigned>(result.lastPageBytesAccepted),
                AT21CS::toString(result.lastPageEffect));
}

}  // namespace at21cs_example
