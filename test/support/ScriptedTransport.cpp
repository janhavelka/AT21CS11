#include "support/ScriptedTransport.h"

namespace AT21CS::test {

static_assert(ScriptedTransport::TRANSFER_CAPACITY > 0);
static_assert(ScriptedTransport::EVENT_CAPACITY >
              ScriptedTransport::TRANSFER_CAPACITY);

}  // namespace AT21CS::test
