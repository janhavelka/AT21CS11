# The smoke project's fixed-name wrapper includes this file from its own directory.
# Resolve the sources and public headers relative to the library itself.
idf_component_register(
  SRCS
    "${CMAKE_CURRENT_LIST_DIR}/src/AT21CS.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/Bus.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/platform/esp32/Esp32Transport.cpp"
  INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include"
  PRIV_REQUIRES esp_driver_gpio esp_hal_gpio esp_pm esp_timer
  LDFRAGMENTS "${CMAKE_CURRENT_BINARY_DIR}/at21cs_pm.lf"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

# At -Os, IRAM code calls these PM lock wrappers out of line. Place them in
# IRAM too, using the archive name assigned by the consuming project.
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/at21cs_pm.lf" CONTENT [=[
[mapping:at21cs_pm_locks]
archive: lib@COMPONENT_NAME@.a
entries:
    if PM_ENABLE = y:
        Esp32Transport:_ZN6AT21CS14Esp32Transport18_acquireTimingLockEv (noflash)
        Esp32Transport:_ZN6AT21CS14Esp32Transport18_releaseTimingLockEv (noflash)
]=] @ONLY)
