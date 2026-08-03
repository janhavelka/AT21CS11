# Stage 05 coverage matrix

This manifest maps each protocol/architecture finding to an independent named
host oracle. Physical qualification remains owned by Stage 08; a host test or
structure-only build is not hardware evidence.

## Public fallible API matrix

| API requirement | Named host matrix | Production source/helper under test |
|---|---|---|
| `Bus::bind` | `test_invalid_bus_rebind_is_transactional_and_silent`; `test_binding_epoch_lifecycle_and_stale_driver_cache` | `Bus::bind` validation and epoch transaction |
| `Bus::end` | `test_bus_presence_and_end_transport_fault_matrix_is_exact`; `test_live_claims_block_bus_end_until_driver_end`; `test_rebind_and_end_preserve_retained_hold` | `Bus::end`, retained hold, claim mask |
| `Bus::readPresenceIndicator` | `test_presence_false_is_not_transport_failure`; `test_bus_presence_and_end_transport_fault_matrix_is_exact`; `test_presence_is_input_only_and_reset_waits_during_retained_hold` | `Bus::readPresenceIndicator`, `_readPresence` |
| `Driver::bind` | `test_standard_startup_requires_explicit_at21cs01`; `test_driver_invalid_rebind_preserves_working_binding`; `test_address_claims_and_transactional_rebind` | `Driver::bind`, `Bus::_claimAddress` |
| `Driver::initialize` | `test_initialize_failures_preserve_exact_status_and_identity_nack_state`; `test_lifecycle_and_speed_transport_fault_matrix_tracks_once`; `test_all_driver_states_have_exact_initialize_and_recover_admission` | initialization lifecycle |
| `Driver::begin` | `test_begin_absence_retains_binding_and_exact_status`; `test_lifecycle_and_speed_transport_fault_matrix_tracks_once` | bind + initialization transaction |
| `Driver::recover` | `test_recover_after_boot_absence_needs_no_config_resupply`; `test_failed_offline_recovery_remains_offline_and_uninitialized`; `test_lifecycle_and_speed_transport_fault_matrix_tracks_once`; `test_all_driver_states_have_exact_initialize_and_recover_admission` | recovery lifecycle |
| `Driver::probe` | `test_probe_is_nondestructive_tracked_and_offline_sticky_on_failure`; `test_read_identity_and_probe_transport_fault_matrix_tracks_once`; `test_read_identity_public_api_nack_matrix_is_exact` | direct Manufacturer-ID probe |
| `Driver::readEeprom` | `test_eeprom_length_and_address_boundary_matrix_is_complete`; `test_read_scratch_commits_only_complete_frames`; `test_read_identity_and_probe_transport_fault_matrix_tracks_once`; `test_read_identity_public_api_nack_matrix_is_exact` | EEPROM random-read/chunk path |
| `Driver::writeEepromPage` | `test_eeprom_page_positions_lengths_and_frames_are_exact`; `test_write_validation_is_complete_transactional_and_callback_free`; `test_eeprom_write_public_api_transport_matrix_tracks_once`; `test_write_nacks_map_every_address_and_data_phase_without_replay` | page write/effect mapping |
| `Driver::writeEeprom` | `test_eeprom_bulk_edges_and_page_splits_are_exact`; `test_eeprom_length_and_address_boundary_matrix_is_complete`; `test_eeprom_write_public_api_transport_matrix_tracks_once`; `test_multi_page_write_stops_on_ambiguous_page_and_keeps_prefix` | bounded page splitter |
| `Driver::readSecurity` | `test_security_length_and_address_boundary_matrix_is_complete`; `test_security_public_api_transport_matrix_tracks_once`; `test_security_read_and_write_nack_phases_are_exact` | Security random-read path |
| `Driver::writeSecurityUserPage` | `test_security_length_and_address_boundary_matrix_is_complete`; `test_security_public_api_transport_matrix_tracks_once`; `test_security_read_and_write_nack_phases_are_exact` | Security page-write path |
| `Driver::writeSecurityUser` | `test_security_length_and_address_boundary_matrix_is_complete`; `test_security_public_api_transport_matrix_tracks_once`; `test_security_write_frames_locked_nack_and_bulk_health_are_exact` | Security user range splitter |
| `Driver::readSecurityLockState` | `test_check_lock_frame_and_memory_nack_semantics_are_exact`; `test_security_public_api_transport_matrix_tracks_once` | Check Lock `2h/W + 0x60` |
| `Driver::permanentlyLockSecurity` | `test_lock_mutation_already_verified_mismatch_and_hold_ambiguity`; `test_lock_postcheck_failure_preserves_accepted_and_exact_status`; `test_security_public_api_transport_matrix_tracks_once` | lock precheck/mutation/postcheck |
| `Driver::readSerialNumber` | `test_serial_crc_product_and_diagnostics_are_independent`; `test_read_identity_and_probe_transport_fault_matrix_tracks_once`; `test_read_identity_public_api_nack_matrix_is_exact` | serial read, product byte, CRC |
| `Driver::readManufacturerId` | `test_scalar_outputs_initialize_before_all_failures`; `test_read_identity_and_probe_transport_fault_matrix_tracks_once`; `test_read_identity_public_api_nack_matrix_is_exact` | direct Manufacturer-ID read |
| `Driver::readRomZoneState` | `test_rom_zone_read_mappings_and_invalid_values_are_exact`; `test_rom_and_freeze_transport_fault_matrix_tracks_once` | ROM-zone register read |
| `Driver::permanentlyEnableRomZone` | `test_rom_zone_mutation_outcomes_and_one_health_update_are_exact`; `test_rom_postcheck_mismatch_and_failure_preserve_accepted`; `test_rom_and_freeze_transport_fault_matrix_tracks_once` | ROM precheck/mutation/postcheck |
| `Driver::permanentlyFreezeRomZones` | `test_freeze_observation_liveness_confirms_only_matching_part`; `test_freeze_complete_frame_and_confirmed_postcheck_are_exact`; `test_freeze_accepted_postcheck_mismatch_and_failures_stay_accepted`; `test_rom_and_freeze_transport_fault_matrix_tracks_once` | Freeze observation/mutation/liveness |
| `Driver::setSpeedMode` | `test_speed_failure_evidence_controls_speed_knowledge`; `test_at21cs11_standard_rejection_is_silent_and_untracked`; `test_lifecycle_and_speed_transport_fault_matrix_tracks_once` | speed validation/command/cache transaction |

Cached getters are covered by `test_cached_getters_are_bus_silent`; `Driver::end`
is covered by `test_driver_end_is_idempotent_silent_and_releases_one_claim`.

## Protocol findings

| Finding | Named host test / deferred evidence | Production source or helper under test |
|---|---|---|
| P-01 | `test_write_hold_trace_has_no_intervening_frame_events`; `test_retained_hold_blocks_a_second_driver_before_its_frame` | `Bus::_executeWrite`, `Bus::_completeWriteHighHold` |
| P-02 | `test_one_callback_owns_complete_frame`; physical waveform is Stage 08 HIL-01 through HIL-06 | `SingleWireTransport::transfer`, `Esp32Transport::_transfer` |
| P-03 | `test_initialize_is_uninit_only_and_ordinary_calls_never_reset` | Driver ordinary-I/O paths and `Driver::_synchronizeBusState` |
| P-04 | `test_v1_surface_is_absent`; `test_read_boundaries_validate_complete_size_t_ranges` | public `Driver` API; `Driver::_readRandomRaw` |
| P-05 | `test_speed_failure_evidence_controls_speed_knowledge`; Stage 08 HIL-02/HIL-03 waveform evidence remains physical | `Driver::_setSpeedModeRaw` |
| P-06 | `test_esp32_reset_discovery_has_one_exact_request_and_release_check`; physical waveform is Stage 08 HIL-01 through HIL-06 | `Esp32Transport::_resetAndDiscover` |
| P-07 | `test_esp32_high_speed_frame_is_msb_first_and_samples_ack_absolutely`; `test_esp32_standard_frame_and_every_address_nack_phase_are_exact`; physical waveform is Stage 08 HIL-01 through HIL-06 | `Esp32Transport::_readBit`, timing profiles |
| P-08 | `test_security_length_and_address_boundary_matrix_is_complete` | `Driver::writeSecurityUser` |
| P-09 | `test_at21cs11_standard_rejection_is_silent_and_untracked` | `Driver::setSpeedMode` |
| P-10 | `test_transport_errors_remain_distinct`; `test_lifecycle_and_speed_transport_fault_matrix_tracks_once`; `test_read_identity_and_probe_transport_fault_matrix_tracks_once`; `test_eeprom_write_public_api_transport_matrix_tracks_once`; `test_security_public_api_transport_matrix_tracks_once`; `test_rom_and_freeze_transport_fault_matrix_tracks_once` | `TransferResult`, `Bus::_mapTransferFailure`, all callback-using public APIs |
| P-11 | `test_initialize_failures_preserve_exact_status_and_identity_nack_state` | `Driver::_runInitializationSequence`, `Bus::_resetAndDiscover` |
| P-12 | `test_check_lock_frame_and_memory_nack_semantics_are_exact`; physical sacrificial verification is Stage 08 HIL-07/HIL-08 | `Driver::_readSecurityLockStateRaw` |
| P-13 | `test_freeze_observation_ack_is_early_stop_and_mutation_address_nack_is_indeterminate`; `test_v1_surface_is_absent` | `Driver::_observeFreezeStateRaw`, public `Driver` API |
| P-14 | `test_status_messages_and_crc_vectors_are_independent_literals` | `Driver::crc8Maxim` |
| P-15 | `test_esp32_bit_slots_are_preflighted_before_falling_edges`; physical DFS/load proof is Stage 08 HIL-01 through HIL-06 | ESP32 timing lock and segment clock |
| P-16 | `test_cached_getters_are_bus_silent`; `test_v1_surface_is_absent` | cached speed getters and public `Driver` API |
| P-17 | `test_esp32_timing_profiles_and_pin_ranges_are_exact`; `test_esp32_reset_discovery_has_one_exact_request_and_release_check`; physical timing matrix is Stage 08 HIL-01 through HIL-06 | ESP32 timing tables, Reset/Discovery, post-frame high |
| P-18 | `test_status_messages_and_crc_vectors_are_independent_literals`; `test_check_lock_frame_and_memory_nack_semantics_are_exact`; `test_freeze_complete_frame_and_confirmed_postcheck_are_exact` prove the behavioral references; Stage 07 owns non-protected document correction | CRC/timing/Lock/Freeze production behavior |
| P-19 | `test_manufacturer_revisions_and_part_mismatch_are_exact` | `Driver::_classifyManufacturerIdRaw` |
| P-20 | `test_discovery_sample_and_release_are_distinct`; `test_esp32_reset_discovery_has_one_exact_request_and_release_check`; held-low waveform is Stage 08 HIL-01 through HIL-06 | `Bus::_resetAndDiscover`, `Esp32Transport::_resetAndDiscover` |
| P-21 | **HIL_ONLY:** Stage 08 HIL-07/HIL-08 temperature write qualification | 10 ms Bus write-high policy and released parts |
| P-22 | **HIL_ONLY:** Stage 08 HIL-09 removable two-channel harness qualification | released harness electrical profile |

## Architecture findings

| Finding | Named host test / deferred evidence | Production source or helper under test |
|---|---|---|
| A-01 | `test_retained_hold_blocks_a_second_driver_before_its_frame`; `test_one_bus_routes_unique_addresses_and_isolates_driver_health` | `Bus`-owned generation, claims, diagnostics, and hold deadline |
| A-02 | `test_recover_after_boot_absence_needs_no_config_resupply` | `Driver::begin`, `Driver::recover` |
| A-03 | `test_driver_bind_is_callback_free`; `test_driver_end_is_idempotent_silent_and_releases_one_claim` | external Backend/Bus ownership boundary |
| A-04 | `test_driver_invalid_rebind_preserves_working_binding`; `test_v1_surface_is_absent` | `Driver::bind`, public `Driver` API |
| A-05 | `test_write_nacks_map_every_address_and_data_phase_without_replay`; `test_rom_zone_mutation_outcomes_and_one_health_update_are_exact` | `WriteResult`, `MutationResult`, mutation paths |
| A-06 | `test_hardware_objects_are_noncopyable_nonmovable` | `Bus`, `Driver`, `Esp32Transport` type declarations |
| A-07 | `test_public_defaults_are_deterministic`; `test_v1_surface_is_absent` | `Status` defaults and public API |
| A-08 | `test_all_driver_states_have_exact_online_and_normal_io_admission`; `test_all_driver_states_have_exact_probe_admission`; `test_all_driver_states_have_exact_initialize_and_recover_admission` | Driver admission and transition helpers |
| A-09 | `test_failed_offline_recovery_remains_offline_and_uninitialized` | `Driver::_finishOperation` recovery mapping |
| A-10 | `test_v1_surface_is_absent` | public `Driver` API |
| A-11 | `test_composite_operations_update_health_once` | raw composite helpers and `Driver::_finishOperation` |
| A-12 | `test_public_defaults_are_deterministic` | `SettingsSnapshot` |
| A-13 | `test_initialize_failures_preserve_exact_status_and_identity_nack_state`; `test_initialize_is_uninit_only_and_ordinary_calls_never_reset` | initialization/reset attempt count |
| A-14 | `test_reset_generation_is_shared`; `test_shared_reset_resynchronizes_each_device_without_cross_health` | `Bus::generation`, successful-Reset HS knowledge |
| A-15 | `test_health_saturates_and_last_error_persists_across_success`; `test_health_threshold_and_success_recovery_are_exact` | Driver health counters/status history |
| A-16 | `test_standard_speed_is_restored_lazily_after_shared_reset`; `test_shared_reset_resynchronizes_each_device_without_cross_health` | `Driver::_synchronizeBusState` |
| A-17 | `test_malformed_success_and_evidence_are_rejected`; `test_malformed_current_write_evidence_matrix_is_rejected`; `test_write_cycle_keeps_frame_and_hold_results_and_blocks_bus` | Bus callback-result validation and diagnostics |
| A-18 | `test_binding_epoch_lifecycle_and_stale_driver_cache`; `test_rebind_and_end_preserve_retained_hold`; `test_live_claims_block_bus_end_until_driver_end` | Bus binding epoch, `Bus::bind`, `Bus::end` |
| A-19 | `test_checked_deadline_boundaries_are_exact`; `test_post_acceptance_hold_addition_handles_below_at_and_above_max`; `test_esp32_transfer_validation_and_wait_guards_are_bounded` | checked deadline arithmetic and bounded waits |
| A-20 | `test_every_uncertain_data_ack_is_held_reported_and_never_replayed`; `test_esp32_write_ack_boundaries_preserve_ambiguous_and_definite_evidence` | current-byte acceptance evidence and write effect mapping |
| A-21 | `test_separate_buses_reuse_addresses_and_isolate_hold_and_lifecycle`; `test_failed_shared_reset_invalidates_both_device_speed_views`; `test_esp32_instances_keep_descriptors_pins_and_line_state_independent`; physical proof is Stage 08 HIL-09 | independent Transport -> Bus -> Driver tuples |
| A-22 | `test_address_claims_and_transactional_rebind`; `test_separate_buses_reuse_addresses_and_isolate_hold_and_lifecycle` | per-Bus address claim mask |

## Stage 04 smoke consumers and physical rows

Both builds use `test/consumer/phy_smoke/arduino/src/main.cpp`,
`framework = arduino`, and the exact platform pin
`https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip`.
They compile with disabled smoke pins (`SIO=-1`, `presence=-1`), so these are
structure/build checks only.

| Environment | Board | Stage 08 physical rows |
|---|---|---|
| `phy_smoke_s2` | `esp32-s2-saola-1` | **HIL_ONLY:** HIL-01, HIL-03, HIL-05 |
| `phy_smoke_s3` | `esp32-s3-devkitc-1` | **HIL_ONLY:** HIL-02, HIL-04, HIL-06 |

HIL-07/HIL-08 are separate mutable S3 qualification runs. HIL-09 uses the
Stage 06 firmware-owner fixture; the Stage 04 smoke source does not exercise
those rows.

## Stage 05 owned quality finding

| Finding | Evidence | Source/helper under test |
|---|---|---|
| Q-02 | all named host tests registered by `test_main.cpp`; `native` and `native_sanitize` gates | fixed-capacity scripted transport and production paths |

`native_sanitize` retains the strict native warning set. Its pre-build selector
uses ASan+UBSan on supported non-Windows hosts and GCC trap-mode UBSan on the
local MinGW host, whose compiler installation provides neither sanitizer
runtime library.
