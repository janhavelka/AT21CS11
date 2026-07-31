# Shared AT21CS v2 contract

This file freezes the target architecture and public vocabulary for every
implementation prompt in this directory. An implementation agent must not
rename these types, reintroduce deleted APIs, or replace the architecture with a
different abstraction merely because another sibling library uses one.

If implementation evidence proves this contract impossible, stop and report the
exact conflict. Do not hide the conflict behind an adapter.

## 1. Target public/source layout

```text
include/AT21CS/
  Types.h
  Status.h
  CommandTable.h
  Transport.h
  Bus.h
  Config.h
  AT21CS.h
  Version.h
  platform/esp32/Esp32Transport.h
src/
  Bus.cpp
  AT21CS.cpp
  platform/esp32/Esp32Transport.cpp
```

Delete `include/AT21CS/Core.h`. `AT21CS.h` is the normal core umbrella. The
explicit platform header may include guarded ESP32 platform types; no other
public header may include or name Arduino, ESP-IDF, FreeRTOS, ESP32, GPIO
registers, or IRAM attributes.

## 2. Ownership model

```text
one physical SI/O wire
    |
    +-- one externally owned Esp32Transport (or fake/custom transport)
    |
    +-- one AT21CS::Bus
            |
            +-- Driver address 0
            +-- Driver address 1
            +-- ... up to address 7
```

Rules:

- `Esp32Transport` owns pins and physical timing.
- `Bus` copies a transport descriptor and owns effects shared by the entire
  wire: binding epoch, Reset generation, write-high deadline, and the fixed
  last-two physical-result diagnostics.
- `Driver` contains a non-owning `Bus*` and one device's A2:A0 address, part,
  desired speed, state, and health.
- Backend, Bus, and Driver objects are fixed-size, externally allocated,
  non-copyable, and non-movable.
- Backend outlives Bus. Bus outlives every bound Driver.
- Destructors perform no callbacks. The owner ends Drivers locally, then must
  obtain `Bus::end()==OK`, and only then calls `Esp32Transport::end()` or
  destroys the objects. Skipping this order after an ambiguous write can
  violate the chip's required high-only interval.
- The library is not thread-safe. One firmware owner serializes all access to a
  Bus. The library creates no mutex/task/queue.
- One Driver on one Bus must never call or reset another Driver. Coordination is
  through Bus generation/deadline state only.

## 3. Exact common enums

Define these in `Types.h`:

```cpp
namespace AT21CS {

enum class PartType : uint8_t {
  UNKNOWN = 0,
  AT21CS01,
  AT21CS11
};

enum class SpeedMode : uint8_t {
  HIGH_SPEED = 0,
  STANDARD_SPEED
};

enum class DriverState : uint8_t {
  UNINIT = 0,
  PROBING,
  INIT_CONFIG,
  READY,
  BUSY,
  DEGRADED,
  OFFLINE,
  RECOVERING,
  SLEEPING,
  FAULT
};

enum class WriteEffect : uint8_t {
  NOT_ATTEMPTED = 0,
  MAY_HAVE_COMMITTED,
  COMMITTED
};

enum class MutationEffect : uint8_t {
  NOT_ATTEMPTED = 0,
  MAY_HAVE_COMMITTED,
  ACCEPTED,
  VERIFIED
};

}  // namespace AT21CS
```

`SLEEPING` remains because `AGENTS.md` mandates the state name. V2 has no
supported sleep operation, so no public transition enters it. Document it as
reserved/unreachable; do not invent a fake sleep feature.

Provide total `const char* toString(...)` overloads for every public enum. An
unknown cast value returns `"UNKNOWN"` and never logs or allocates.

## 4. Exact Status contract

Define in `Status.h`:

```cpp
enum class Err : uint8_t {
  OK = 0,
  NOT_BOUND,
  NOT_INITIALIZED,
  INVALID_STATE,
  BUSY,
  INVALID_CONFIG,
  INVALID_PARAM,

  NOT_PRESENT,

  NACK_DEVICE_ADDRESS,
  NACK_MEMORY_ADDRESS,
  NACK_DATA,

  TRANSPORT_TIMEOUT,
  LINE_STUCK,
  IO_ERROR,
  CLOCK_STALLED,

  UNSUPPORTED_COMMAND,
  CRC_MISMATCH,
  PART_MISMATCH,
  VERIFY_MISMATCH,
  INDETERMINATE
};

enum class ProtocolPhase : uint8_t {
  NONE = 0,
  PRESENCE,
  RESET,
  DISCOVERY_SAMPLE,
  DISCOVERY_RELEASE,
  START,
  DEVICE_ADDRESS_WRITE,
  MEMORY_ADDRESS,
  RESTART,
  DEVICE_ADDRESS_READ,
  DATA_WRITE,
  DATA_READ,
  STOP,
  WRITE_HIGH_HOLD,
  VERIFY
};

struct Status {
  Err code = Err::OK;
  int32_t detail = 0;
  const char* msg = "OK";

  constexpr bool ok() const { return code == Err::OK; }
  constexpr bool is(Err expected) const { return code == expected; }

  static constexpr Status Ok();
  static constexpr Status Error(Err code, int32_t detail = 0);
};
```

Do not add dynamic messages, exceptions, `std::string`, or `inProgress()`.
`msg` always points to the canonical static `toString(code)` result. Callers do
not supply free-form messages, so one error code cannot acquire contradictory
text in different paths.

For protocol NACKs, encode phase and zero-based data index in `detail`:

```cpp
constexpr int32_t makeProtocolDetail(
    ProtocolPhase phase, uint16_t byteIndex = 0);
constexpr ProtocolPhase protocolDetailPhase(int32_t detail);
constexpr uint16_t protocolDetailIndex(int32_t detail);
```

Use the high 8 bits for `ProtocolPhase` and low 16 bits for index; remaining
bits are zero. Transport timeout/line/I/O errors retain backend-native numeric
detail in `Status::detail`; their exact phase remains available in
`BusSnapshot::lastTransfer` or, for a separate write hold,
`BusSnapshot::lastWriteCycle.hold`.

## 5. Exact frame transport contract

Per-byte callbacks are forbidden. Define in `Transport.h`:

```cpp
enum class TransportCode : uint8_t {
  OK = 0,
  NACK,
  TIMEOUT,
  LINE_STUCK,
  IO_ERROR
};

enum class TransferPhase : uint8_t {
  NONE = 0,
  PRESENCE,
  RESET_LOW,
  RESET_RECOVERY,
  DISCOVERY_REQUEST,
  DISCOVERY_SAMPLE,
  DISCOVERY_RELEASE,
  START,
  DEVICE_ADDRESS_WRITE,
  MEMORY_ADDRESS,
  RESTART,
  DEVICE_ADDRESS_READ,
  DATA_WRITE,
  DATA_READ,
  STOP,
  WAIT_HIGH
};

struct TransferResult {
  TransportCode code = TransportCode::IO_ERROR;
  TransferPhase phase = TransferPhase::NONE;
  int32_t detail = 0;
  size_t dataBytesTransferred = 0;
  bool currentWriteByteMayBeAccepted = false;
  bool firstDeviceAddressAcked = false;
  bool memoryAddressAcked = false;
  bool repeatedDeviceAddressAcked = false;
  bool stopCompleted = false;

  constexpr bool ok() const { return code == TransportCode::OK; }
};

struct WriteCycleResult {
  TransferResult frame{};
  TransferResult hold{};
  bool holdRequired = false;
  bool holdCompleted = false;
};

struct SingleWireTransfer {
  SpeedMode speed = SpeedMode::HIGH_SPEED;
  uint8_t deviceAddress = 0;

  bool hasMemoryAddress = false;
  uint8_t memoryAddress = 0;

  const uint8_t* txData = nullptr;
  size_t txLength = 0;

  bool hasRepeatedStart = false;
  uint8_t repeatedDeviceAddress = 0;

  uint8_t* rxData = nullptr;
  size_t rxLength = 0;

  uint32_t minimumPostTransferHighUs = 0;
};

using NowUsFn = uint64_t (*)(void* user);

using TransferFn = TransferResult (*)(
    const SingleWireTransfer& transfer,
    uint64_t deadlineUs,
    void* user);

using ResetDiscoverFn = TransferResult (*)(
    bool& present,
    uint64_t deadlineUs,
    void* user);

using WaitUntilUsFn = TransferResult (*)(
    uint64_t deadlineUs,
    void* user);

using ReadPresenceFn = TransferResult (*)(
    bool& present,
    uint64_t deadlineUs,
    void* user);

struct SingleWireTransport {
  void* user = nullptr;
  NowUsFn nowUs = nullptr;
  TransferFn transfer = nullptr;
  ResetDiscoverFn resetAndDiscover = nullptr;
  WaitUntilUsFn waitUntilUs = nullptr;
  ReadPresenceFn readPresence = nullptr;  // optional
};
```

### Transfer invariants

- Core passes data payload only in `txData`; address bytes are separate fields.
- `txLength <= 8` and `rxLength <= 8`.
- `txLength` and `rxLength` are never both nonzero.
- A write frame uses raw write `deviceAddress`, optional memory address, and
  `txData`.
- A random read uses raw write `deviceAddress`, a memory address, repeated
  Start, raw read `repeatedDeviceAddress`, and `rxData`.
- A direct read such as Manufacturer ID uses raw read `deviceAddress` and
  `rxData`, without memory address or repeated Start.
- An address-only command has no memory address/data/repeated Start.
- `dataBytesTransferred` counts payload bytes ACKed on write or copied on read;
  it excludes device and memory address bytes.
- `currentWriteByteMayBeAccepted` closes the otherwise unsafe gap between
  transmitting all eight bits of one write payload byte and obtaining a
  definite ninth-bit ACK/NACK result. It identifies payload index
  `dataBytesTransferred`; that byte is not included in the proven count.
- A well-formed result may set `currentWriteByteMayBeAccepted=true` only for a
  non-`OK`, non-`NACK` failure at `DATA_WRITE`, after all eight bits of that
  payload byte were delivered and before its ACK outcome became known. It is
  false when the byte was not fully delivered, its ACK/NACK was definite, the
  transfer is not a write, or the failure is at any other phase.
- `firstDeviceAddressAcked` describes the first device-address byte, whether
  that byte is a write address or a direct-read address.
- `memoryAddressAcked` is meaningful only when `hasMemoryAddress=true`.
- `repeatedDeviceAddressAcked` is meaningful only when
  `hasRepeatedStart=true`. These explicit address-ACK fields are required
  because `dataBytesTransferred==0` cannot distinguish an address ACK followed
  by a later failure from an address NACK.
- `NACK` is a completed physical outcome, not `IO_ERROR`.
- The backend always attempts a protocol Stop after NACK when the wire state
  permits, releases SI/O on every exit, and reports `stopCompleted`.
- The backend performs exactly one attempt: no retry, Reset, recovery, logging,
  retained buffer, or recursive Driver call.
- One `transfer()` call owns the complete uninterrupted frame. It must not
  expose scheduler gaps between byte callbacks because byte callbacks do not
  exist.
- `deadlineUs` is an absolute monotonic deadline. The backend checks it before
  starting and guarantees a finite terminal return.
- `minimumPostTransferHighUs` is measured after Stop completion. Speed-change
  commands use a conservative 650 us. Normal High-Speed and Standard frames use
  backend-qualified values of at least 160 us and 650 us respectively.

### Callback result shapes

Bus validates callback results before treating them as evidence. A malformed
result maps to `Err::IO_ERROR`; the original malformed result remains in the
Bus snapshot for diagnosis.

- A successful `transfer()` result has `phase=STOP`,
  `stopCompleted=true`, every address-ACK field required by the request set,
  every inapplicable address-ACK field clear, and
  `dataBytesTransferred` equal to the requested payload length, with
  `currentWriteByteMayBeAccepted=false`.
- A transfer `NACK` names the exact address/data ACK phase. ACK fields and
  `dataBytesTransferred` describe only phases completed before that NACK, and
  `currentWriteByteMayBeAccepted=false` because the NACK is definite.
- Every transfer result has `dataBytesTransferred` no greater than the
  requested payload length. It cannot claim a memory/repeated-address ACK when
  that phase was absent from the request.
- For a write failure at `DATA_WRITE`,
  `currentWriteByteMayBeAccepted=true` additionally requires
  `dataBytesTransferred < txLength` and all address ACKs preceding that payload
  byte. Any other combination is malformed.
- A successful `resetAndDiscover()` result has
  `phase=DISCOVERY_RELEASE`, zero byte/ACK/Stop evidence, and a separately
  initialized `present` output. Discovery response sampling occurs earlier at
  `DISCOVERY_SAMPLE`; the backend then verifies SI/O is released at the
  distinct 25 us release check. A line still low there returns
  `LINE_STUCK` at `DISCOVERY_RELEASE`.
- A successful `waitUntilUs()` result has `phase=WAIT_HIGH`, zero
  byte/ACK/Stop evidence, and is accepted only after a second `nowUs()` proves
  the target deadline was reached.
- A successful `readPresence()` result has `phase=PRESENCE`, zero
  byte/ACK/Stop evidence, and a separately initialized `present` output.
- `NACK` is legal only for `transfer()`. Reset, wait, and presence callbacks
  returning `NACK`, or any callback returning an impossible phase/evidence
  combination, are malformed transport results.
- Bus maps malformed result shapes to `IO_ERROR`, but it must not discard
  plausible programming evidence. For a request with `txLength > 0`, arm the
  retained write-high hold whenever the raw result has
  `dataBytesTransferred > 0`, or has
  `currentWriteByteMayBeAccepted=true`, `phase=DATA_WRITE`, and
  `dataBytesTransferred < txLength`. This fail-closed predicate applies even
  when some other field makes the result malformed.

## 6. Bus contract

Define in `Bus.h`:

```cpp
struct BusConfig {
  SingleWireTransport transport{};
};

struct BusSnapshot {
  bool bound = false;
  bool bindingEpochValid = true;
  uint64_t bindingEpoch = 0;
  uint64_t generation = 0;
  bool resetEstablishedHighSpeed = false;
  uint64_t writeHighUntilUs = 0;
  TransferResult previousTransfer{};
  TransferResult lastTransfer{};
  WriteCycleResult lastWriteCycle{};
};

class Bus {
 public:
  Bus() = default;
  ~Bus() = default;

  Bus(const Bus&) = delete;
  Bus& operator=(const Bus&) = delete;
  Bus(Bus&&) = delete;
  Bus& operator=(Bus&&) = delete;

  Status bind(const BusConfig& config);  // validation/copy only, zero I/O
  Status end();  // waits only when a retained write-high deadline requires it

  bool isBound() const;
  bool hasPresenceIndicator() const;
  Status readPresenceIndicator(bool& present);
  uint64_t generation() const;
  BusSnapshot snapshot() const;

 private:
  friend class Driver;
  // private execute/reset/write-high helpers
};
```

Fixed command/protocol constants:

```cpp
static constexpr size_t MAX_FRAME_DATA_BYTES = 8;
static constexpr uint32_t TRANSFER_TIMEOUT_US = 9000;
static constexpr uint32_t RESET_TIMEOUT_US = 5000;
static constexpr uint32_t WRITE_HIGH_HOLD_US = 10000;
static constexpr uint32_t HIGH_SPEED_HTSS_US = 160;
static constexpr uint32_t STANDARD_SPEED_HTSS_US = 650;
static constexpr uint32_t SPEED_CHANGE_HOLD_US = 650;
```

`WRITE_HIGH_HOLD_US` is not user-configurable. DS20005857I specifies a 5 ms
maximum `tWR` under its stated 25 C test condition; 10 ms is the deliberately
conservative v2 policy, not an unqualified wider-temperature guarantee. Stage
8 must qualify or narrow the released temperature claim.

Bus rules:

- Successful bind validates the complete descriptor before replacing any
  current binding.
- `bind()` always performs zero callbacks. Quiescent `end()` performs zero
  callbacks; if a write-high deadline is retained, `end()` first calls the same
  bounded `_completeWriteHighHold()` path used before traffic.
- Every successful initial bind or replacement bind advances
  `bindingEpoch` before publishing the new descriptor. `end()` also invalidates
  the current epoch. A Driver whose cached epoch differs must perform no normal
  frame; explicit `recover()` against the current bound Bus is required.
- `bindingEpoch` never wraps. If a replacement bind would advance
  `UINT64_MAX`, it fails with `INVALID_STATE` and preserves the old binding.
  If `end()` encounters `UINT64_MAX`, it marks `bindingEpochValid=false`; that
  Bus object cannot be rebound and must be replaced after all Drivers end.
- A replacement bind with a retained write-high deadline returns `BUSY` with
  zero callbacks and preserves the complete old binding.
- `end()` never clears a retained deadline or descriptor early. If its bounded
  hold completion fails or the clock returns early, it returns that exact error
  and preserves the binding, epoch, and deadline. Only proven completion lets
  `end()` clear the binding and invalidate/advance the epoch.
- `end()` on an already unbound Bus is idempotent `OK`, performs zero callbacks,
  and does not advance the epoch again.
- Backend outlives the bound Bus and cannot be ended until `Bus::end()` returns
  OK. This is required even after a failed write call.
- Presence is a Bus-level/module indicator, not proof that a particular A2:A0
  address responds. `readPresenceIndicator()` never changes Driver health.
- Before every transfer, Bus refuses or completes any retained write-high
  deadline. It never starts a frame early.
- Driver submits mutating frames through one private Bus `_executeWrite()`
  helper, never through normal `_execute()`.
- `_executeWrite()` preserves both the frame result and high-hold result in a
  `WriteCycleResult`.
- Before publishing any new transfer/reset/presence physical result, Bus shifts
  `lastTransfer` into `previousTransfer`. This fixed two-result history is
  required for the private Freeze address-NACK plus Manufacturer-ID liveness
  check; it is not an allocating trace facility.
- If a write frame has at least one proven accepted payload byte, or reports a
  fully delivered current payload byte whose ACK is unknown, Bus
  conservatively records
  `writeHighUntilUs = nowUs + WRITE_HIGH_HOLD_US`, then calls `waitUntilUs`,
  even if the frame later reports malformed evidence, transport failure, or
  Stop uncertainty.
- Zero proven accepted bytes skip the hold only when
  `currentWriteByteMayBeAccepted=false`. A definite first-data-byte NACK skips
  it; an ACK-sampling fault after that byte's eight data bits does not.
- If the frame itself failed, `_executeWrite()` completes any required hold and
  returns the original frame Status. If the frame succeeded and the hold
  failed, it returns the hold Status.
- A wait callback returning success before the deadline maps to
  `CLOCK_STALLED`; the deadline remains active.
- While the deadline is active, no Driver on that Bus can transmit, probe, or
  Reset.
- Every Reset attempt increments generation conservatively because it may have
  affected all devices.
- Only successful Reset+Discovery sets
  `resetEstablishedHighSpeed=true`. Failure leaves physical mode unknown.
- Bus generation is `uint64_t`. If it has reached `UINT64_MAX`, Bus refuses a
  further Reset before line activity rather than wrapping and losing
  invalidation evidence.
- Every `nowUs + interval` operation uses checked addition. Transfer and Reset
  deadline overflow fails with `CLOCK_STALLED` before line activity. A mutating
  frame preflights room for both `TRANSFER_TIMEOUT_US` and
  `WRITE_HIGH_HOLD_US`; if the clock nevertheless jumps so far that the
  post-acceptance hold deadline cannot be represented, Bus records
  `writeHighUntilUs=UINT64_MAX`, returns `CLOCK_STALLED` with ambiguous write
  evidence, and permanently refuses further traffic on that Bus object.

## 7. Driver configuration and snapshots

Define in `Config.h`:

```cpp
struct Config {
  uint8_t addressBits = 0;
  uint8_t offlineThreshold = 5;
  PartType expectedPart = PartType::UNKNOWN;
  SpeedMode startupSpeed = SpeedMode::HIGH_SPEED;
};
```

Delete pin fields, timing callbacks, transport pointer, write timeout, and
discovery retries from core Config.

`startupSpeed=STANDARD_SPEED` is valid only with
`expectedPart=AT21CS01`. `UNKNOWN+STANDARD_SPEED` is rejected at bind time:
the library must not discover AT21CS11 after I/O and then silently ignore the
requested configuration.

Manufacturer classification constants belong in `CommandTable.h`:

```cpp
static constexpr uint32_t MANUFACTURER_ID_PART_MASK = 0x00FFFFF8u;
static constexpr uint32_t MANUFACTURER_ID_AT21CS01_BASE = 0x00D200u;
static constexpr uint32_t MANUFACTURER_ID_AT21CS11_BASE = 0x00D380u;
static constexpr uint32_t MANUFACTURER_ID_REVISION_MASK = 0x00000007u;
```

The low three bits are silicon revision, not part identity. Part detection
compares `(rawId & MANUFACTURER_ID_PART_MASK)` with the two base values and
retains the complete raw ID plus `rawId & MANUFACTURER_ID_REVISION_MASK`.

Define in `AT21CS.h`:

```cpp
struct SerialNumberInfo {
  uint8_t bytes[cmd::SECURITY_SERIAL_SIZE] = {};
  bool productIdOk = false;
  bool crcOk = false;
};

struct WriteResult {
  size_t bytesCommitted = 0;
  size_t lastPageBytesAccepted = 0;
  WriteEffect lastPageEffect = WriteEffect::NOT_ATTEMPTED;
};

struct MutationResult {
  MutationEffect effect = MutationEffect::NOT_ATTEMPTED;
  bool alreadyApplied = false;
};

struct SettingsSnapshot {
  bool bound = false;
  bool initialized = false;
  DriverState state = DriverState::UNINIT;

  uint8_t addressBits = 0;
  uint8_t offlineThreshold = 0;
  PartType expectedPart = PartType::UNKNOWN;
  PartType detectedPart = PartType::UNKNOWN;
  uint32_t manufacturerId = 0;
  uint8_t siliconRevision = 0;
  SpeedMode configuredSpeed = SpeedMode::HIGH_SPEED;
  SpeedMode activeSpeed = SpeedMode::HIGH_SPEED;
  bool speedKnown = false;
  bool seenBusBindingEpochValid = false;
  uint64_t seenBusBindingEpoch = 0;
  uint64_t seenBusGeneration = 0;

  Err lastStatusCode = Err::OK;
  int32_t lastStatusDetail = 0;
  Err lastErrorCode = Err::OK;
  int32_t lastErrorDetail = 0;
  uint64_t lastOkUs = 0;
  uint64_t lastErrorUs = 0;
  uint8_t consecutiveFailures = 0;
  uint32_t totalSuccess = 0;
  uint32_t totalFailures = 0;
};
```

Snapshots contain scalars only: no Config copy, function pointer, context
pointer (including `Status::msg`), Bus pointer, register address, framework
type, or mutable reference.
`lastStatus()`/`lastError()` reconstruct their canonical static `msg`; snapshot
consumers copy only code/detail.

## 8. Exact Driver API

```cpp
class Driver {
 public:
  Driver() = default;
  ~Driver() = default;  // bus-silent

  Driver(const Driver&) = delete;
  Driver& operator=(const Driver&) = delete;
  Driver(Driver&&) = delete;
  Driver& operator=(Driver&&) = delete;

  // Lifecycle
  Status bind(Bus& bus, const Config& config);
  Status initialize();
  Status begin(Bus& bus, const Config& config);
  Status recover();
  void end();

  // Non-destructive reachability/identity
  Status probe();
  // Cached diagnostics
  bool isBound() const;
  bool isInitialized() const;
  bool isOnline() const;
  DriverState state() const;
  PartType detectedPart() const;
  uint32_t manufacturerId() const;
  uint8_t siliconRevision() const;
  bool isSpeedKnown() const;
  SpeedMode speedMode() const;
  Status lastStatus() const;
  Status lastError() const;
  SettingsSnapshot snapshot() const;

  // EEPROM
  Status readEeprom(uint8_t address, uint8_t* data, size_t length);
  Status writeEepromPage(
      uint8_t address, const uint8_t* data, size_t length,
      WriteResult& result);
  Status writeEeprom(
      uint8_t address, const uint8_t* data, size_t length,
      WriteResult& result);

  // Security
  Status readSecurity(uint8_t address, uint8_t* data, size_t length);
  Status writeSecurityUserPage(
      uint8_t address, const uint8_t* data, size_t length,
      WriteResult& result);
  Status writeSecurityUser(
      uint8_t address, const uint8_t* data, size_t length,
      WriteResult& result);
  Status readSecurityLockState(bool& locked);
  Status permanentlyLockSecurity(MutationResult& result);

  // Identity
  Status readSerialNumber(SerialNumberInfo& serial);
  Status readManufacturerId(uint32_t& manufacturerId);

  // ROM zones
  Status readRomZoneState(uint8_t zoneIndex, bool& enabled);
  Status permanentlyEnableRomZone(
      uint8_t zoneIndex, MutationResult& result);
  Status permanentlyFreezeRomZones(MutationResult& result);

  // Speed
  Status setSpeedMode(SpeedMode mode);

  static uint8_t crc8Maxim(const uint8_t* data, size_t length);
};
```

Delete these v1 APIs and do not provide aliases:

```text
tick
waitReady
readCurrentAddress
writeEepromByte
writeSecurityUserByte
lockSecurityRegister
isSecurityLocked
readRomZoneRegister
isZoneRom
setZoneRom
freezeRomZones
areRomZonesFrozen
detectPart
resetAndDiscover
isPresent
driverState
getConfig
getSettings overloads
setHighSpeed
isHighSpeed
setStandardSpeed
isStandardSpeed
Status::inProgress
```

## 9. State and health rules

Centralize state changes in named helpers. Do not assign `_state` throughout
unrelated methods.

### Lifecycle

- Constructed/end: unbound, uninitialized, `UNINIT`.
- Successful bind: bound, uninitialized, `UNINIT`, zero I/O.
- `initialize()` is admitted only from bound, uninitialized `UNINIT`.
  Reinitialization and return from `DEGRADED`/`OFFLINE` use `recover()`.
- Initialize: `PROBING` during one Reset+Discovery, then `INIT_CONFIG` during
  identity and speed setup.
- Initialize success: `READY`, initialized.
- Definite absence: `OFFLINE`, not initialized, binding retained.
- Expected-part mismatch: `FAULT`, not initialized, binding retained.
- Recover: `RECOVERING` for one Reset+Discovery+identity+speed sequence.
- Recover success: `READY`.
- Recover clears `initialized` on entry and restores it only after the complete
  Reset/identity/configuration sequence succeeds.
- Failed recovery from OFFLINE remains `OFFLINE`; it must not become DEGRADED.
- Normal I/O is allowed only while initialized in `READY` or `DEGRADED`.
- `probe()` is additionally allowed from initialized `OFFLINE`; a successful
  identity probe restores `READY`, while a failure retains/updates health.
- `FAULT` is cleared only by a new successful `bind()` followed by
  `initialize()`; `recover()` is rejected.
- `BUSY` is a Driver-local synchronous mutation transient. It spans the
  mutating frame and any required Bus high-only wait. It may begin immediately
  before the frame because Driver cannot observe the address/data ACK while the
  whole-frame callback is executing. A retained Bus deadline after a failed
  wait is represented by `BusSnapshot::writeHighUntilUs`, not by leaving one
  Driver in `BUSY`; no other Driver's state is changed.
- `SLEEPING` has no entry in v2.

State ownership is exact. `OperationKind` is a private implementation enum, not
public API. Production code changes `_state` and `_initialized` only through:

```cpp
enum class OperationKind : uint8_t {
  INITIALIZE = 0,
  RECOVER,
  PROBE,
  NORMAL_IO,
  MUTATION
};

void _setState(DriverState state, bool initialized);
void _enterOperation(DriverState transient);
void _finishOperation(
    const Status& status,
    OperationKind kind,
    DriverState entryState);
void _resetLocalState();
```

Validation/precondition exits occur before `_enterOperation()` and never call
`_finishOperation()`. Every operation that performs a device-facing callback
or protocol frame calls
`_finishOperation()` exactly once. `entryState` is captured before the
transient state is entered; it is never inferred from `BUSY`, `PROBING`, or
`RECOVERING`.

The mapping is exact:

| Kind/result | initialized after finish | stable state |
|---|---:|---|
| `INITIALIZE` success | true | READY |
| `RECOVER` success | true | READY |
| `PROBE`, `NORMAL_IO`, `MUTATION` success | unchanged | READY |
| `INITIALIZE` failure | false | FAULT for identity `PART_MISMATCH`; otherwise health classification |
| `RECOVER` failure | false | FAULT for identity `PART_MISMATCH`; otherwise OFFLINE when `entryState==OFFLINE`, else health classification |
| `PROBE` failure | false only for identity `PART_MISMATCH`, otherwise unchanged | FAULT for identity `PART_MISMATCH`; otherwise OFFLINE when `entryState==OFFLINE`, else health classification |
| `NORMAL_IO`, `MUTATION` failure | unchanged | health classification |

Health classification means `NOT_PRESENT -> OFFLINE`; otherwise OFFLINE when
the updated consecutive count reaches the nonzero threshold, and DEGRADED
below it. `MUTATION` uses the same failure classification as `NORMAL_IO`;
`BUSY` is only its transient entry state.

One lifecycle/identity exception is explicit: during `INITIALIZE`, `RECOVER`,
or `PROBE`, a Manufacturer-ID first-device-address NACK is definite absence of
that addressed target. Preserve the exact `NACK_DEVICE_ADDRESS` Status/detail,
but choose stable state OFFLINE immediately rather than waiting for the health
threshold. Do not rewrite it to `NOT_PRESENT`.

### Health

- Validation/precondition errors update neither state nor health.
- Raw frame helpers never update health.
- Each public logical operation that reaches a device-facing callback/frame
  calls the one final `_finishOperation()` helper.
- Success sets `lastStatus=OK`, resets consecutive failures, increments
  `totalSuccess` saturating, and returns to READY.
- Failure sets both `lastStatus` and persistent `lastError`, records time,
  increments `totalFailures` and consecutive failures saturating.
- `offlineThreshold==0` disables threshold-based OFFLINE classification.
- `NOT_PRESENT` moves directly to OFFLINE.
- Transport/protocol failures below threshold move to DEGRADED.
- CRC and verify mismatches are tracked logical failures.
- Unsupported/invalid requests rejected before I/O are not health failures.
- A success does not erase persistent `lastError`; rebind/end resets history.
- `isOnline()` is true only when bound, initialized, and state is READY or
  DEGRADED. It is false for every transient state, OFFLINE, SLEEPING, FAULT,
  and UNINIT.
- `Bus::readPresenceIndicator()` is a bus-silent-with-respect-to-SI/O diagnostic
  callback and does not update Driver health/state. Callback success with
  `present=false` returns OK.

### Output commit policy

- Scalar output parameters are initialized before validation or state checks:
  `present=false`, `locked=false`, `enabled=false`, and manufacturer ID zero.
- `SerialNumberInfo`, `WriteResult`, and `MutationResult` are reset before any
  validation or state check.
- Every physical read frame receives into a fixed local
  `uint8_t scratch[MAX_FRAME_DATA_BYTES]`. Caller memory is updated only after
  that complete frame validates successfully.
- Therefore a multi-frame EEPROM/Security read may commit complete earlier
  chunks, but the failed chunk and every later chunk remain untouched. No
  dynamic buffer or caller-sized stack allocation is allowed.

### Write and mutation evidence

- `WriteResult::bytesCommitted` is the fully accepted-plus-held contiguous page
  prefix. The current page is `COMMITTED` only after its complete frame and
  required high hold succeed.
- Any proven accepted payload byte, or a fully delivered current payload byte
  whose ACK is unknown, followed by frame/Stop/hold uncertainty is
  `MAY_HAVE_COMMITTED`; it is never replayed automatically.
- `MutationEffect::NOT_ATTEMPTED` means there is neither a proven accepted
  mutation payload byte nor a fully delivered current byte with unknown ACK.
  It therefore requires `currentWriteByteMayBeAccepted=false`; an unknown ACK
  is `MAY_HAVE_COMMITTED` even when the proven count is zero.
- Proven or possible accepted payload plus incomplete frame/Stop/hold is
  `MAY_HAVE_COMMITTED`.
- A complete mutation frame plus proven hold is `ACCEPTED`. If a required
  verification read then fails or observes the old state, retain `ACCEPTED`
  and return the exact read error or `VERIFY_MISMATCH`; do not downgrade the
  evidence or replay the command.
- Only positive documented readback is `VERIFIED`. A precheck that observes the
  target state also returns `VERIFIED` with `alreadyApplied=true`.

## 10. Bus binding epoch, Reset generation, and speed knowledge

Every Driver caches `seenBusBindingEpoch`, its validity, and
`seenBusGeneration`.

- A binding-epoch mismatch means the Bus transport may now describe a different
  physical wire. On observation set `speedKnown=false`; normal I/O fails
  `INVALID_STATE` with zero frames and `isOnline()` is false. Explicit
  `recover()` is the only operation that may adopt the current valid bound
  epoch.
- A successful Bus Reset establishes all devices in High-Speed mode.
- On generation mismatch with `resetEstablishedHighSpeed=true`, Driver:
  - sets cached active speed to High-Speed;
  - sets `speedKnown=true`;
  - adopts the generation;
  - restores its configured Standard Speed, if needed, before its next normal
    command.
- On generation mismatch with `resetEstablishedHighSpeed=false`, normal I/O
  sets `speedKnown=false` and returns `INVALID_STATE` without a frame; explicit
  `recover()` is required.
- A Driver must not Reset merely because another Driver advanced the generation.
- This lazy synchronization prevents initializing/recovering one addressed
  device from causing a reset loop across the other devices.
- A successful initialize/recover adopts the current binding epoch and Reset
  generation, sets active speed High-Speed, and sets `speedKnown=true` before
  applying the configured Standard mode.
- `isOnline()` additionally requires a current valid Bus binding epoch and
  `speedKnown=true`. `speedMode()` returns the cached active value, which is
  meaningful only while `isSpeedKnown()` is true.

## 11. Exact protocol frames

Let:

```text
D(op, rw) = (op << 4) | (addressBits << 1) | rw
```

All bytes are MSb-first. Host ACKs every received byte except the final byte,
which it NACKs.

### Manufacturer ID / probe

```text
Start -> D(C,1) -> read 3 bytes -> host NACK final byte -> Stop
```

Revision-zero base values:

- AT21CS01: `0x00D200`
- AT21CS11: `0x00D380`

Bits D2:D0 are silicon revision. Classification masks those bits; a nonzero
documented/future revision is not rejected merely because the complete 24-bit
value differs from the revision-zero base. Values with an unknown masked part
code return `PART_MISMATCH` with the complete raw ID in `Status::detail`.

### EEPROM random read

```text
Start -> D(A,0) -> address
Repeated Start -> D(A,1) -> 1..8 data bytes -> Stop
```

Core chunks longer reads into frames of at most 8 bytes.

### EEPROM page write

```text
Start -> D(A,0) -> address -> 1..8 data bytes -> Stop -> high-only hold
```

One physical frame never crosses an 8-byte page.

### Security read/write

Same shapes with opcode `B`. Read addresses are `0x00..0x1F`; writes are
`0x10..0x1F`, page-bounded.

### Check Security Lock

```text
Start -> D(2,0) ACK -> 0x60
  memory-address ACK  = unlocked
  memory-address NACK = locked
Stop
```

The device-address NACK is not lock state.

### Permanently lock Security

```text
precheck lock state
Start -> D(2,0) -> 0x60 -> 0x00 -> Stop -> high-only hold
postcheck lock state
```

`0x00` is a stable chosen don't-care data value.

### ROM zone read

Zone/register/range mapping:

| Zone | Register | EEPROM range |
|---:|---:|---:|
| 0 | `0x01` | `0x00..0x1F` |
| 1 | `0x02` | `0x20..0x3F` |
| 2 | `0x04` | `0x40..0x5F` |
| 3 | `0x08` | `0x60..0x7F` |

```text
Start -> D(7,0) -> register
Repeated Start -> D(7,1) -> one byte -> Stop
```

Only `0x00` and `0xFF` are accepted. Other values return `VERIFY_MISMATCH` with
the observed byte in `Status::detail`; they must never be silently interpreted
as false.

### Permanently enable ROM zone

```text
pre-read zone
Start -> D(7,0) -> register -> 0xFF -> Stop -> high-only hold
post-read zone and require 0xFF
```

### Permanently Freeze ROM configuration

Private observation uses no invented read command:

```text
Start -> D(1,0)
  address ACK  -> Stop early -> not frozen
  address NACK -> Stop -> same-address Manufacturer-ID liveness check
```

- `_observeFreezeStateRaw(bool& frozen)` is private and untracked.
- Address ACK means not frozen. The immediate Stop aborts the incomplete
  Freeze sequence without sending `0x55`/`0xAA`.
- Address NACK nominally indicates frozen, but is indistinguishable from an
  absent target. Perform exactly one raw same-address Manufacturer-ID read.
  Only a successful ID matching the already detected part confirms
  `frozen=true`; otherwise return `INDETERMINATE` and preserve both physical
  results in Bus diagnostics.
- Never transmit `D(1,1)`.

`permanentlyFreezeRomZones()`:

1. prechecks through `_observeFreezeStateRaw()`;
2. confirmed frozen returns OK/VERIFIED/`alreadyApplied=true` without a
   mutation frame;
3. confirmed not frozen proceeds; indeterminate/error returns
   NOT_ATTEMPTED;
4. sends exactly one mutation frame:

```text
Start -> D(1,0) -> 0x55 -> 0xAA -> Stop -> high-only hold
```

5. a mutation address NACK after the confirmed-not-frozen precheck remains
   INDETERMINATE;
6. proven accepted payload, or a fully delivered payload byte with unknown
   ACK, plus incomplete frame/hold is MAY_HAVE_COMMITTED;
7. full frame plus proven hold sets ACCEPTED before one postcheck;
8. confirmed frozen promotes to VERIFIED; confirmed not frozen returns
   VERIFY_MISMATCH preserving ACCEPTED; indeterminate/read failure preserves
   ACCEPTED and returns that exact failure.

There is no public Freeze-state query or session cache, and no automatic retry.

### Speed change

AT21CS11 Standard request is rejected before I/O.

```text
set Standard: transmit D(D,0) using current speed
set High:     transmit D(E,0) using current speed
```

After address ACK, use `SPEED_CHANGE_HOLD_US=650` before another frame. Commit
the new cached speed and `Config::startupSpeed` only after the transfer
succeeds, so later recovery restores the last explicitly selected mode.

The explicit address-ACK evidence is safety-critical:

- only a validated failure shape that proves failure before address ACK leaves
  the previously known active/configured speeds unchanged. This means a typed
  NACK at `DEVICE_ADDRESS_WRITE`, or a transport failure at `START` before the
  address byte; a transport error during the address phase does not prove
  rejection;
- malformed or contradictory ACK evidence is fail-closed and makes
  `speedKnown=false`;
- address ACK followed by Stop/post-high/transport failure makes physical speed
  indeterminate: set `speedKnown=false`, do not commit
  `Config::startupSpeed`, and reject every normal command until explicit
  `recover()` establishes High-Speed again;
- a fully successful command commits both active speed and
  `Config::startupSpeed`;
- `setSpeedMode()` synchronizes a changed Bus epoch/generation without first
  restoring the old configured Standard mode. If the requested mode is already
  the newly established known active mode, it performs zero frames and commits
  that mode as the new configured speed.

Do not expose destructive speed-query APIs. Initialization/Recovery establishes
known High-Speed via Reset, then applies configured speed.

## 12. CRC contract

`crc8Maxim()` implements:

- polynomial `0x31`, reflected implementation polynomial `0x8C`;
- init `0x00`;
- refin/refout true;
- xorout `0x00`;
- `"123456789"` -> `0xA1`;
- null with nonzero length returns `0` only as a pure helper convention; public
  APIs validate pointers before calling it.

Serial byte 0 must be `0xA0`; byte 7 is CRC of bytes 0..6. A product-byte
mismatch returns `PART_MISMATCH` with the observed byte in `Status::detail`.
A CRC mismatch returns `CRC_MISMATCH` with
`detail = (computedCrc << 8) | storedCrc`. These encodings are stable public
diagnostics; do not substitute strings or platform error codes.

## 13. ESP32 application construction

The final construction shape is:

```cpp
AT21CS::Esp32Transport backend;
AT21CS::Bus bus;
AT21CS::Driver device0;
AT21CS::Driver device1;

AT21CS::Esp32TransportConfig backendConfig;
backendConfig.sioPin = BOARD_SIO_PIN;

AT21CS::Status st = backend.begin(backendConfig);

AT21CS::BusConfig busConfig;
busConfig.transport = backend.descriptor();
st = bus.bind(busConfig);

AT21CS::Config device0Config;
device0Config.addressBits = 0;
device0Config.expectedPart = AT21CS::PartType::AT21CS11;
st = device0.begin(bus, device0Config);

AT21CS::Config device1Config;
device1Config.addressBits = 1;
device1Config.expectedPart = AT21CS::PartType::AT21CS11;
st = device1.begin(bus, device1Config);
```

Shutdown order is Driver(s), `Bus::end()`, Backend. Driver end is local-only.
Bus end is callback-free when quiescent but may finish one retained bounded
high-only deadline; the owner must not end the backend unless Bus end returned
OK. `Esp32Transport::end()` then releases/configures its owned line safely.

## 14. TunnelMonitor consumption contract

A firmware module statically owns Backend, Bus, and Driver. Only its owner task
calls them. Other tasks receive copied fixed-size status/results.

One page write is the bounded scheduling unit. The library may keep
`writeEeprom()` as a convenience, but TunnelMonitor-style firmware calls
`writeEepromPage()` so one owner operation blocks for at most one frame plus the
10 ms write hold: the fixed 9 ms frame deadline plus hold is a 19 ms library
wait budget. After a retained-hold failure, the owner defers the next Driver
call until its monotonic clock reaches `BusSnapshot::writeHighUntilUs`, so a
later page call cannot combine an old wait with a new page. Application
retry/backoff and reconciliation stay outside the library.
