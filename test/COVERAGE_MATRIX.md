# Test and validation coverage

This file maps the current public behavior to independent host oracles and
build checks. Host tests and structure-only builds are not physical waveform
evidence; the exact physical scope is recorded in
[`docs/HARDWARE_VALIDATION.md`](../docs/HARDWARE_VALIDATION.md).

## Public API coverage

| Surface | Principal host coverage |
|---|---|
| `Bus::bind`, `Bus::end` | `test_invalid_bus_rebind_is_transactional_and_silent`; `test_binding_epoch_lifecycle_and_stale_driver_cache`; `test_live_claims_block_bus_end_until_driver_end`; `test_rebind_and_end_preserve_retained_hold` |
| Presence indicator | `test_presence_false_is_not_transport_failure`; `test_bus_presence_and_end_transport_fault_matrix_is_exact`; `test_presence_is_input_only_and_reset_waits_during_retained_hold` |
| Driver bind/start/end | `test_driver_invalid_rebind_preserves_working_binding`; `test_address_claims_and_transactional_rebind`; `test_begin_absence_retains_binding_and_exact_status`; `test_driver_end_is_idempotent_silent_and_releases_one_claim` |
| Initialize/recover/probe | `test_initialize_failures_preserve_exact_status_and_identity_nack_state`; `test_recover_after_boot_absence_needs_no_config_resupply`; `test_failed_offline_recovery_remains_offline_and_uninitialized`; `test_probe_is_nondestructive_tracked_and_offline_sticky_on_failure` |
| EEPROM reads | `test_eeprom_length_and_address_boundary_matrix_is_complete`; `test_multi_frame_reads_are_whole_call_transactional`; `test_read_identity_public_api_nack_matrix_is_exact` |
| EEPROM writes | `test_eeprom_page_positions_lengths_and_frames_are_exact`; `test_eeprom_bulk_edges_and_page_splits_are_exact`; `test_write_validation_is_complete_transactional_and_callback_free`; `test_write_nacks_map_every_address_and_data_phase_without_replay`; `test_multi_page_write_stops_on_ambiguous_page_and_keeps_prefix` |
| Security reads/writes | `test_security_length_and_address_boundary_matrix_is_complete`; `test_multi_frame_reads_are_whole_call_transactional`; `test_security_read_and_write_nack_phases_are_exact`; `test_security_write_frames_locked_nack_and_bulk_health_are_exact` |
| Security Lock | `test_check_lock_frame_and_memory_nack_semantics_are_exact`; `test_lock_mutation_already_verified_mismatch_and_hold_ambiguity`; `test_lock_postcheck_failure_preserves_accepted_and_exact_status` |
| Identity and CRC | `test_serial_crc_product_and_diagnostics_are_independent`; `test_manufacturer_revisions_and_part_mismatch_are_exact`; `test_status_messages_and_crc_vectors_are_independent_literals` |
| ROM-zone state/enable | `test_rom_zone_read_mappings_and_invalid_values_are_exact`; `test_rom_zone_mutation_outcomes_and_one_health_update_are_exact`; `test_rom_postcheck_mismatch_and_failure_preserve_accepted` |
| ROM-zone Freeze | `test_freeze_observation_liveness_confirms_only_matching_part`; `test_freeze_complete_frame_and_confirmed_postcheck_are_exact`; `test_freeze_accepted_postcheck_mismatch_and_failures_stay_accepted`; `test_freeze_first_data_nack_has_no_hold_or_effect`; `test_freeze_uncertain_payload_is_held_ambiguous_and_not_replayed` |
| Speed | `test_speed_failure_evidence_controls_speed_knowledge`; `test_at21cs11_standard_rejection_is_silent_and_untracked`; `test_lifecycle_and_speed_transport_fault_matrix_tracks_once` |

Every transport-using public group also has NACK/timeout/line-stuck/I/O fault
matrix coverage. Cached getters are covered by
`test_cached_getters_are_bus_silent`.

## Cross-cutting invariants

| Requirement | Principal host coverage |
|---|---|
| Complete validation before I/O, including `SIZE_MAX` | EEPROM/Security boundary matrices; `test_read_boundaries_validate_complete_size_t_ranges`; `test_write_validation_is_complete_transactional_and_callback_free` |
| Whole-frame callback and exact protocol phases | `test_one_callback_owns_complete_frame`; `test_transport_errors_remain_distinct`; public NACK matrices |
| MSb-first timing, final host NACK and Reset/Discovery | `test_esp32_high_speed_frame_is_msb_first_and_samples_ack_absolutely`; `test_esp32_standard_frame_and_every_address_nack_phase_are_exact`; `test_esp32_reset_discovery_has_one_exact_request_and_release_check` |
| Checked deadlines and stalled clocks | `test_checked_deadline_boundaries_are_exact`; `test_post_acceptance_hold_addition_handles_below_at_and_above_max`; `test_esp32_transfer_validation_and_wait_guards_are_bounded` |
| Bus-wide released-high write hold | `test_write_hold_trace_has_no_intervening_frame_events`; `test_retained_hold_blocks_a_second_driver_before_its_frame` |
| Conservative mutation evidence/no replay | `test_every_uncertain_data_ack_is_held_reported_and_never_replayed`; Security/ROM/Freeze ambiguity tests |
| Address ownership and independent Buses | `test_address_claims_and_transactional_rebind`; `test_separate_buses_reuse_addresses_and_isolate_hold_and_lifecycle`; `test_esp32_instances_keep_descriptors_pins_and_line_state_independent` |
| Shared Reset generation and lazy resynchronization | `test_reset_generation_is_shared`; `test_shared_reset_resynchronizes_each_device_without_cross_health`; `test_standard_speed_is_restored_lazily_after_shared_reset` |
| State admission and saturating health | table-driven lifecycle/state tests; `test_health_saturates_and_last_error_persists_across_success` |
| No v1/dead public surface | `test_v1_surface_is_absent`; static production-placeholder checker |
| Example parser, hot-plug policy and fairness | `test_example_cli.cpp`; `test_example_policy.cpp`; `tools/check_cli_contract.py` |

## Build and documentation gates

- `native` and `native_sanitize` execute all registered host tests through
  production core paths and the fixed-capacity scripted Backend.
- `ex_cli_s2`, `ex_cli_s3`, `ex_multi_s2`, and `ex_multi_s3` build the two
  shipped examples against the pinned Arduino platform.
- `phy_smoke_s2` and `phy_smoke_s3` compile the production ESP32 Backend; IRAM
  checks verify placement of timing-critical methods and emitted lambdas.
- `tools/check_docs.py` validates public links, API/default records, the
  authoritative datasheet hash, irreversible-operation guidance, and Doxygen.
- `tools/check_package.py` verifies the exact export allowlist and builds clean
  platform-neutral and Arduino consumers outside the checkout.

## Physical result boundary

The recorded ESP32-S2/AT21CS11 run passed functional and destructive behavior,
including Security Lock, three ROM zones and configuration Freeze. No
oscilloscope/electrical qualification, ESP32-S3/AT21CS01 run, shared-wire
multi-address fixture, physical hot-plug, or optional detect-pin fixture was
performed or claimed.
