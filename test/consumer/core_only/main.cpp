#include <cstdint>
#include <type_traits>

#include "AT21CS/AT21CS.h"
#include "AT21CS/Bus.h"
#include "AT21CS/Config.h"
#include "AT21CS/Status.h"
#include "AT21CS/Transport.h"
#include "AT21CS/Types.h"
#include "AT21CS/Version.h"

static_assert(!std::is_copy_constructible<AT21CS::Bus>::value,
              "Bus ownership must remain unique");
static_assert(!std::is_copy_constructible<AT21CS::Driver>::value,
              "Driver ownership must remain unique");

int main() {
  AT21CS::Bus bus;
  AT21CS::Driver driver;
  AT21CS::Config config{};
  AT21CS::BusConfig busConfig{};
  AT21CS::SettingsSnapshot snapshot{};
  AT21CS::WriteResult writeResult{};
  AT21CS::MutationResult mutationResult{};
  AT21CS::Status status{};

  (void)bus;
  (void)driver;
  (void)config;
  (void)busConfig;
  (void)snapshot;
  (void)writeResult;
  (void)mutationResult;
  (void)status;
  return AT21CS::VERSION_MAJOR == static_cast<uint16_t>(2) ? 0 : 1;
}
