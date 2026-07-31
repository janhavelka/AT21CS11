#include <unity.h>

void test_public_defaults_are_deterministic();
void test_hardware_objects_are_noncopyable_nonmovable();
void test_invalid_bus_rebind_is_transactional_and_silent();
void test_binding_epoch_lifecycle_and_stale_driver_cache();
void test_rebind_and_end_preserve_retained_hold();
void test_one_callback_owns_complete_frame();
void test_every_nack_phase_maps_exactly();
void test_malformed_success_and_evidence_are_rejected();
void test_unknown_data_ack_arms_hold_without_replay();
void test_transport_errors_remain_distinct();
void test_physical_diagnostics_shift_without_allocation();
void test_checked_deadlines_and_post_acceptance_overflow_fail_closed();
void test_presence_false_is_not_transport_failure();
void test_write_cycle_keeps_frame_and_hold_results_and_blocks_bus();
void test_reset_generation_is_shared();
void test_failed_reset_invalidates_speed_knowledge();
void test_discovery_sample_and_release_are_distinct();
void test_core_headers_are_framework_neutral();
void test_v1_surface_is_absent();
void test_independent_buses_are_fully_isolated();
void test_esp32_descriptor_lifecycle_and_stale_callbacks();
void test_address_claims_and_transactional_rebind();
void test_live_claims_block_bus_end_until_driver_end();

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_public_defaults_are_deterministic);
  RUN_TEST(test_hardware_objects_are_noncopyable_nonmovable);
  RUN_TEST(test_invalid_bus_rebind_is_transactional_and_silent);
  RUN_TEST(test_binding_epoch_lifecycle_and_stale_driver_cache);
  RUN_TEST(test_rebind_and_end_preserve_retained_hold);
  RUN_TEST(test_one_callback_owns_complete_frame);
  RUN_TEST(test_every_nack_phase_maps_exactly);
  RUN_TEST(test_malformed_success_and_evidence_are_rejected);
  RUN_TEST(test_unknown_data_ack_arms_hold_without_replay);
  RUN_TEST(test_transport_errors_remain_distinct);
  RUN_TEST(test_physical_diagnostics_shift_without_allocation);
  RUN_TEST(test_checked_deadlines_and_post_acceptance_overflow_fail_closed);
  RUN_TEST(test_presence_false_is_not_transport_failure);
  RUN_TEST(test_write_cycle_keeps_frame_and_hold_results_and_blocks_bus);
  RUN_TEST(test_reset_generation_is_shared);
  RUN_TEST(test_failed_reset_invalidates_speed_knowledge);
  RUN_TEST(test_discovery_sample_and_release_are_distinct);
  RUN_TEST(test_core_headers_are_framework_neutral);
  RUN_TEST(test_v1_surface_is_absent);
  RUN_TEST(test_independent_buses_are_fully_isolated);
  RUN_TEST(test_esp32_descriptor_lifecycle_and_stale_callbacks);
  RUN_TEST(test_address_claims_and_transactional_rebind);
  RUN_TEST(test_live_claims_block_bus_end_until_driver_end);
  return UNITY_END();
}
