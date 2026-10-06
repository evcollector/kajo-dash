# Controller backend interface

This is the implemented controller boundary and the contract for adding another
controller or transport. The interface lives in `controller_manager.h`, the
hardware adapters in `controller_backends.cpp`, and the stable registry and
runtime manager in `controller_manager.cpp`.

The document began as a design review. It is retained as the rationale behind
the interface, plus the rules that future backends must preserve.

## What already works

`src/lvgl_app/dashboards.cpp` contains twelve themes without a controller-type
or transport branch. `screens.cpp` copies one `ControllerSnapshot` for a frame,
then supplies its values and field masks to the dashboard. The themes format
missing data from the semantic availability mask rather than knowing why a
backend lacks it.

Before this refactor, the coupling was distributed like this:

| layer            | sites that know the backend |
| ---------------- | --------------------------- |
| `dashboards.cpp` | 0                           |
| `app_logic.cpp`  | 8                           |
| `main_lvgl.cpp`  | 8                           |
| `screens.cpp`    | 49                          |

The former `main_lvgl.cpp` sites were transport selection, two separate
FreeRTOS telemetry tasks chosen by backend, and a ride-mode accessor gated on
FarDriver. They now live behind the manager and backend records; `main_lvgl.cpp`
only starts the manager.

## Four things cross the boundary

They are different in kind and should not be merged. Folding capabilities into
values is the tempting shortcut, and it is how `controllerType ==` finds its way
back into a theme.

- **telemetry** — what the vehicle is doing, in one canonical form, with
  availability;
- **capabilities** — what this controller can do, so the UI can hide rather
  than invent;
- **link control** — device selection and connection state;
- **lifecycle** — start, stop, reset, and yielding the radio to a firmware
  update.

## Telemetry: backend samples, one published snapshot

Themes previously assembled their view from separate accessors, each taking the
lock on its own. The current dashboard path copies one snapshot, preventing
values and validity metadata from different moments appearing in one frame.

The backend does not build that snapshot directly. It publishes one canonical
`ControllerSample`; the manager applies the shared battery model, supplies
staleness/link state, feeds the ride logger, and atomically publishes the final
`ControllerSnapshot`. This keeps battery persistence, logging policy and the
snapshot lock out of every protocol implementation.

The masks describe semantic values, not struct members. `tripKm` and battery
state of charge occur in both current structs but each gets one bit. Fault and
ride-mode bits distinguish a genuine zero (no fault or neutral/unknown mode)
from a backend that cannot report the field at all.

```c
enum TelemetryField : uint32_t {
  TELEMETRY_FIELD_SPEED              = 1UL << 0,
  TELEMETRY_FIELD_POWER              = 1UL << 1,
  TELEMETRY_FIELD_VOLTAGE            = 1UL << 2,
  TELEMETRY_FIELD_CURRENT            = 1UL << 3,
  TELEMETRY_FIELD_MOTOR_TEMP         = 1UL << 4,
  TELEMETRY_FIELD_ESC_TEMP           = 1UL << 5,
  TELEMETRY_FIELD_TRIP_DISTANCE      = 1UL << 6,
  TELEMETRY_FIELD_ODOMETER           = 1UL << 7,
  TELEMETRY_FIELD_AVG_SPEED          = 1UL << 8,
  TELEMETRY_FIELD_UPTIME             = 1UL << 9,
  TELEMETRY_FIELD_BATTERY_SOC        = 1UL << 10,
  TELEMETRY_FIELD_TRIP_ENERGY        = 1UL << 11,
  TELEMETRY_FIELD_REGEN_ENERGY       = 1UL << 12,
  TELEMETRY_FIELD_RIDE_EFFICIENCY    = 1UL << 13,
  TELEMETRY_FIELD_LIFETIME_ENERGY    = 1UL << 14,
  TELEMETRY_FIELD_LIFETIME_DISTANCE  = 1UL << 15,
  TELEMETRY_FIELD_LIFETIME_EFFICIENCY = 1UL << 16,
  TELEMETRY_FIELD_RANGE              = 1UL << 17,
  TELEMETRY_FIELD_EQUIVALENT_CYCLES  = 1UL << 18,
  TELEMETRY_FIELD_PACK_RESISTANCE    = 1UL << 19,
  TELEMETRY_FIELD_LEARNED_CAPACITY   = 1UL << 20,
  TELEMETRY_FIELD_LEARNED_SAMPLES    = 1UL << 21,
  TELEMETRY_FIELD_FAULT              = 1UL << 22,
  TELEMETRY_FIELD_RIDE_MODE          = 1UL << 23,
  TELEMETRY_FIELD_MOTOR_CURRENT      = 1UL << 24,
  TELEMETRY_FIELD_DUTY               = 1UL << 25,
  TELEMETRY_FIELD_PHASE_VOLTAGE      = 1UL << 26,
};

using TelemetryFieldMask = uint32_t;

inline bool telemetryHas(TelemetryFieldMask mask, TelemetryField field) {
  return (mask & static_cast<TelemetryFieldMask>(field)) != 0;
}

// Optional absolute counters from which the shared battery model banks deltas.
// A backend that does not report the complete counter set leaves `available`
// false; it must not invent zero counters.
struct ControllerEnergyCounters {
  bool available;
  bool rebaseDistance;  // conversion source changed; move, do not bank, baseline
  float ampHours;
  float ampHoursCharged;
  float wattHours;
  float wattHoursCharged;
  float odometerKm;
  float voltage;
  float current;
};

struct ControllerSample {
  uint32_t sampledAtMs;
  DashboardValues values;
  ControllerEnergyCounters energy;
  TelemetryFieldMask available;
  TelemetryFieldMask derived;  // always a subset of `available`
  uint8_t faultCode;
  uint8_t rideMode;
  uint8_t firmwareMajor;
  uint8_t firmwareMinor;
  bool firmwareKnown;
};

struct ControllerSnapshot {
  uint32_t sampledAtMs;
  TelemetryLink link;
  DashboardValues values;
  BatteryStats battery;
  TelemetryFieldMask available;
  TelemetryFieldMask derived;  // always a subset of `available`
  uint8_t faultCode;    // 0 when none, or when the link is not live
  uint8_t rideMode;     // 0 when the controller does not report one
  uint8_t firmwareMajor;
  uint8_t firmwareMinor;
  bool firmwareKnown;
};

// Copies the complete published state under the telemetry lock once.
ControllerSnapshot controllerSnapshot();
```

The current semantic set uses twenty-five bits. A 16-bit mask does not fit, and
leaving headroom for fields a new controller brings costs nothing, so the masks
are `uint32_t`. The manager asserts or sanitises `derived & ~available == 0`
before publishing.

### Why availability is not optional

`DashboardValues` by itself has no way to say a field is unavailable. Every
member is a bare number, so a controller that cannot report one has to publish
something, and whatever it publishes is indistinguishable from a real reading.
The FarDriver path already demonstrates the failure: its frames carry no
distance counter, `odoKm` is therefore set to zero, and the odometer reads
`0 KM` — which a rider cannot tell apart from a bike that has covered no ground.

`BatteryStats` solved this in the same codebase, using negative values for
"cannot be known" and zero for "not yet", documented per field. The masks
generalise that rather than replacing it.

### Why `derived` is a separate mask

The FarDriver trip distance is integrated from speed on this display; the VESC's
comes from the controller's own tachometer. Both are legitimate, both land in
`tripKm`, and they are not the same quality of number. The first feature that
exports ride data or reconciles it against the controller will care.

`derived` is also where road-speed provenance belongs. Whether the VESC reports
its own speed is **negotiated at runtime**, not a property of the backend: the
setup-values probe is cross-checked against the ordinary poll and disables
itself for the session on disagreement, falling back to eRPM. A static
capability flag would be wrong. `TELEMETRY_FIELD_SPEED` present in `derived` means
this display converted it; absent means the controller supplied it.

## Capabilities: what the controller can do

These are the differences that must survive rather than be abstracted away.
Everything here is fixed for a given backend; anything negotiated at runtime
belongs in the snapshot instead.

```c
struct ControllerCapabilities {
  bool showsDeviceList;        // a scan and device list make sense
  bool usesWiredLink;          // a baud rate is a meaningful setting
  bool hasCanTarget;           // a CAN id is a meaningful setting
  bool hasLinkDiagnostics;     // a raw frame view is worth offering
  bool readsConfiguration;     // can pull limits from the controller
  bool writesConfiguration;    // reserved; nothing does this yet
  bool reportsRideMode;        // the UI can offer controller ride modes
};
```

`showsDeviceList` rather than anything about pairing: the backends save an
address, they do not bond, and what the flag actually decides is whether the
wizard shows a list.

`writesConfiguration` is listed while false everywhere on purpose. The
read-only notices consult it, so that the day a write path exists the UI stops
claiming otherwise by itself.

## Link control: shared types

The two backends are the same interface written twice. `VescBleStatus` has no
field that `FarDriverBleStatus` lacks — it is a strict subset, the extra nine
being diagnostics. The device structs share four of four members with identical
types and differ only in the name of one flag, `likelyVesc` against
`likelyFarDriver`.

Their buffers diverge for no reason (`name[28] address[20]` against
`name[25] address[19]`, both holding the same seventeen-character MAC), and
`screens.cpp` sizes a shared array and bounds a shared button range with
`VESC_BLE_MAX_DEVICES` for *both* backends. Those limits are equal today by
coincidence. Raising FarDriver's to sixteen would make the last four devices
unreachable and overflow `ordered[]`, with nothing to warn anybody.

```c
constexpr uint8_t CONTROLLER_LINK_MAX_DEVICES = 12;
constexpr uint8_t CONTROLLER_ADDRESS_CHARS = 20;
constexpr uint8_t CONTROLLER_NAME_CHARS = 28;

struct ControllerLinkDevice {
  char name[CONTROLLER_NAME_CHARS];
  char address[CONTROLLER_ADDRESS_CHARS];
  int8_t rssi;
  uint8_t addressType;
  bool likelyMatch;
};

enum ControllerLinkState : uint8_t {
  CONTROLLER_LINK_IDLE,
  CONTROLLER_LINK_SCANNING,
  CONTROLLER_LINK_CONNECTING,
  CONTROLLER_LINK_DISCOVERING,
  CONTROLLER_LINK_CONNECTED,
  CONTROLLER_LINK_FAILED,
};

// Replaces the English `message` the UI used to print verbatim. The UI names
// these itself so they can be translated; the free text that remains is
// diagnostic and lives with the diagnostics.
enum ControllerLinkError : uint8_t {
  CONTROLLER_LINK_ERR_NONE,
  CONTROLLER_LINK_ERR_SCAN_FAILED,
  CONTROLLER_LINK_ERR_DEVICE_GONE,
  CONTROLLER_LINK_ERR_CONNECT_FAILED,
  CONTROLLER_LINK_ERR_SERVICE_MISSING,
  CONTROLLER_LINK_ERR_SUBSCRIBE_FAILED,
  CONTROLLER_LINK_ERR_WORKER_FAILED,
  CONTROLLER_LINK_ERR_BUSY,
};

struct ControllerLinkStatus {
  ControllerLinkState state;
  ControllerLinkError error;
  ControllerLinkDevice devices[CONTROLLER_LINK_MAX_DEVICES];
  uint8_t deviceCount;
  bool savedDevice;
  char savedAddress[CONTROLLER_ADDRESS_CHARS];
  char connectedAddress[CONTROLLER_ADDRESS_CHARS];
  uint32_t revision;
};
```

The error enum is not decoration. The two backends currently distinguish five
and eight distinct failure causes respectively, all of them collapsed into one
generic sentence when the status text was localised. Dropping `message` without
this would make that loss permanent.

```c
struct ControllerLinkDiagnostics {
  char serviceUuid[40];
  char characteristicUuid[40];
  char lastPacketHex[64];
  uint32_t frameCount;
  uint32_t crcFailCount;
  uint32_t discardedBytes;
  uint16_t lastPacketLength;
  uint8_t lastFrameId;
  int8_t connectedRssi;
};
```

## The backend record

Function pointers rather than virtual classes, because the codebase is C++
without being object-oriented, and a table of functions reads the way the rest
of it does.

```c
// Stable and persisted. Never renumber; never store a registry array index,
// which changes whenever a backend is added or reordered.
enum ControllerBackendId : uint8_t {
  CONTROLLER_ID_VESC_UART = 1,
  CONTROLLER_ID_VESC_BLE = 2,
  CONTROLLER_ID_FARDRIVER_BLE = 3,
};

// The UI maps this enum to translated text. Only controller/brand names remain
// program strings because they are proper names rather than interface copy.
enum ControllerTransport : uint8_t {
  CONTROLLER_TRANSPORT_UART,
  CONTROLLER_TRANSPORT_BLE,
};

enum ControllerPollResult : uint8_t {
  CONTROLLER_POLL_NO_SAMPLE,  // normal while disconnected or between frames
  CONTROLLER_POLL_UPDATED,    // `out` contains a complete coherent sample
  CONTROLLER_POLL_FAILED,     // backend cannot continue without restart
};

struct ControllerBackend {
  ControllerBackendId id;
  const char *name;            // "VESC", "FarDriver"
  ControllerTransport transport;
  ControllerCapabilities caps;

  // lifecycle
  void (*begin)();
  void (*stop)();
  void (*resetSettings)();             // clear this backend's saved identity
  // Idempotent: may be called on every updater tick. It requests shutdown on
  // the first call and returns true once the backend has released the radio.
  bool (*quiesceForFirmwareUpdate)();

  // device selection; no-ops when caps.showsDeviceList is false
  void (*startScan)();
  void (*connectDevice)(uint8_t index);
  void (*disconnect)();
  // Dial the saved device by its stored identity, not a list slot. Null for
  // links without pairing. The manager calls it after a radio handover and
  // every few seconds while the link is down, except while a setup screen
  // (controllerManagerSetSetupOpen) or a handover owns the link.
  void (*reconnectSaved)();
  ControllerLinkStatus (*linkStatus)();
  bool (*diagnostics)(ControllerLinkDiagnostics &out);

  // Telemetry is called from the manager's single task, never a per-backend
  // telemetry task. UPDATED is the only result for which `out` is consumed.
  ControllerPollResult (*poll)(ControllerSample &out);
  uint16_t pollIntervalMs;
  uint16_t taskStackBytes;  // ESP32 FreeRTOS stack depth is specified in bytes
};

const ControllerBackend *controllerBackends();
uint8_t controllerBackendCount();
const ControllerBackend *activeControllerBackend();
const ControllerBackend *controllerBackendById(ControllerBackendId id);
// Reset iterates every registry entry so an inactive BLE identity cannot
// survive a display reset. Quiesce delegates to the active backend.
void controllerResetAllSettings();
bool controllerQuiesceForFirmwareUpdate();
```

`poll` is the part the first draft missed, and it is what makes the rest worth
doing. `controller_manager` owns **one** telemetry task, sized from the active
backend's `taskStackBytes`, and that task calls `poll`. `NO_SAMPLE` is expected
while a controller is absent and does not itself make the backend unhealthy;
the manager derives `LINK_WAITING` or `LINK_LOST` from the last successfully
published sample. `FAILED` means the backend worker or transport has entered a
terminal state and requires a restart. The manager now owns the sole controller
telemetry task, so adding a backend adds no task and touches no scheduler code.

After an `UPDATED` result, the manager consumes `energy` when available,
updates the shared battery model, merges its `BatteryStats` and availability
into the snapshot, calls `rideLoggerSample()`, and publishes the complete state
under one lock. Demo mode publishes a complete synthetic snapshot to the UI and
feeds the logger with the same availability mask without pretending to be a
controller backend.

`resetSettings` and `quiesceForFirmwareUpdate` already exist in spirit and are
done by naming both backends explicitly — `app_logic.cpp` calls
`vescBleStream.resetSettings()` and `farDriverBleResetSettings()` side by side,
and the firmware updater suspends ride logging and waits on
`controllerBleDisconnected()` before claiming the radio. Neither is a bug; both
are the coupling this record removes.

Registry entries are flat: VESC over UART, VESC over Bluetooth, and FarDriver
over Bluetooth are three entries rather than a type crossed with a transport. A
wired backend then needs no special case — `showsDeviceList` is false, its scan
and connect functions do nothing, and the UI skips discovery because the
capability says to.

## Where the VESC wire protocol lives

`src/lvgl_app/vesc_protocol.*` holds the framing, CRC-16/XMODEM and payload
parsers for the VESC UART protocol. It has no Arduino, Stream or LVGL
dependency, so the same code compiles for the device and for the host test at
`tools/lvgl_native_preview/vesc_protocol_test.cpp`.

The split is deliberate and worth preserving in any new backend: pure byte
handling on one side, transport and session state on the other.
`controller_backends.cpp` owns the serial or BLE stream, the reconnect timing
and the sample assembly; it hands byte buffers to the protocol module and gets
structs back. A parser bug is then reproducible on a desktop rather than only
on a moving bike.

This replaced the SolidGeek VescUart library, which had been the only GPLv3
dependency in the tree. The reader kept here resynchronises on a stray byte,
survives `millis()` rollover and yields to the scheduler while waiting, none of
which the library's reader did.

## Ride logs carry the same masks

The first record layout stored plain values, so a FarDriver ride logged a zero
trip and zero energy exactly as the dashboard displayed them, and an exported
log inherited the ambiguity the masks were added to remove. Fixing that was a
format break rather than a field addition.

The record has carried the availability mask since V2, and the log format is
now at **V3**, which added phase current after pack current;
`docs/ride-log-format.md` is the authority on the layout. `kLogVersion`,
`kHeaderBytes` and `kRecordBytes` in `ride_replay_core.h` are shared by the
writer and the reader so the two cannot drift.

The record follows the snapshot rather than leading it: a field is logged
because a backend reports it, and its bit is the same `TelemetryField` bit the
dashboard reads. Phase current is the newest example — VESC reports it
separately from pack current and sets `TELEMETRY_FIELD_MOTOR_CURRENT`, while
FarDriver does not yet, so a FarDriver ride charts it as "Not recorded" instead
of as zero. A new field costs a bit, a record slot and a format version, and
nothing else.

Superseded layouts are rejected outright rather than read by a compatibility
path, per the pre-release policy in `AGENTS.md`. The Android companion's
`RideFileValidator` checks the same three header numbers, so firmware and app
have to move together.

## What this is worth

Adding a controller becomes: write one file implementing the record, declare one
capability set, add one registry entry. No scheduler code, no transport wiring,
no edits to the twelve themes.

Before this boundary, the same job meant finding and extending around forty
paired conditionals across three files, plus a new FreeRTOS task. Missing one
did not fail to compile — it silently took the VESC branch.

## Implemented migration

1. Shared `ControllerLinkDevice` / `ControllerLinkStatus`, filled by thin
   adapters, remove buffer and list-limit coupling.
2. `ControllerSample` and manager-owned `ControllerSnapshot` carry the 32-bit
   availability and derived masks; FarDriver no longer claims a zero odometer.
3. The ride record stores availability; superseded layouts are deliberately
   unsupported. The format has since moved to V3 for phase current.
4. Telemetry task ownership plus firmware-update and reset lifecycle now live
   behind `controller_manager`.
5. Stable backend ids and the capability registry drive persistence and UI.
6. The setup UI uses the registry while retaining the present two-stage flow.
7. Collapsing the wizard to a single backend list remains an optional UI change,
   not part of this implementation.

Steps 1, 4, 5 and 6 are intended not to change a single pixel, which is worth
using as verification:
rebuild, re-render, and diff against the tracked PNGs in
`preview_output/lvgl/`. A correct refactor produces no differences at all, which
is a stronger check than reading the diff.

Steps 2, 3 and 7 change behaviour on purpose — an unavailable field becoming a
dash, a log record gaining validity, a wizard losing a page — and their diffs
have to be read.

Step 4 is the risky one. It moves FreeRTOS task creation and touches the shared
snapshot under its lock. It deserves its own commit, with a clean render diff
either side of it.

### Motor Effort telemetry

VESC supplies signed `dutyCycle` from GET_VALUES (int16 / 1000).
`phaseVoltage` is derived as `Vbus * abs(dutyCycle) / sqrt(3)` under the
sinusoidal FOC/default overmodulation assumption; it is a fundamental
phase-neutral peak (amplitude) estimate, not a measured phase voltage. VESC advertises
both availability bits and marks phase voltage derived. FarDriver leaves
both unavailable. These values are live dashboard fields; the ride record
does not store new duty or phase-voltage series.
