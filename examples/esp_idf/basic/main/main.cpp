#include <cstdint>
#include <cstdio>
#include <fcntl.h>

#include "esp_idf_version.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "AT21CS/Version.h"
#include "AT21CSBuildIdentity.h"
#include "BringupCli.h"

namespace {

class ConsoleOutput final : public at21cs_example::CliOutput {
 public:
  void write(const char* bytes, size_t length) override {
    std::fwrite(bytes, 1, length, stdout);
  }
};

bool configureConsole() {
  if (std::setvbuf(stdin, nullptr, _IONBF, 0) != 0 ||
      std::setvbuf(stdout, nullptr, _IONBF, 0) != 0) {
    return false;
  }
  const int input = fileno(stdin);
  const int flags = fcntl(input, F_GETFL, 0);
  return flags >= 0 && fcntl(input, F_SETFL, flags | O_NONBLOCK) == 0;
}

uint32_t nowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

int readChar() {
  const int value = std::getchar();
  if (value == EOF) {
    // An empty non-blocking read must not hide later input on this stream.
    std::clearerr(stdin);
    return -1;
  }
  return value;
}

at21cs_example::WireInstance wire;
ConsoleOutput output;
at21cs_example::BringupCli cli(wire, output, nowMs, readChar);

}  // namespace

extern "C" void app_main() {
  if (!configureConsole()) {
    std::fputs("Cannot configure the non-blocking console; CLI stopped.\n", stderr);
    return;
  }
  output.printf("AT21CS %s commit=%s status=%s sdk=%s target=%s\n",
                AT21CS::VERSION, AT21CS_GIT_COMMIT, AT21CS_GIT_STATUS,
                esp_get_idf_version(), CONFIG_IDF_TARGET);
  cli.start();
  while (true) {
    cli.pollInput();
    cli.step();
    vTaskDelay(1);
  }
}
