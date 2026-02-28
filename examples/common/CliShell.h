#pragma once

#include "At21Example.h"

namespace cli_shell {
inline bool readLine(String& outLine) {
  return ex::readLine(outLine);
}
}
