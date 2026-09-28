# Design decisions

Rationale extracted from the development history that preceded this repository's
first commit. That history was squashed before publication, so what is written
here is all that survives of it.

Every entry answers a question the code cannot: why something is the way it is,
what the obvious alternative was, and — where a defect prompted the decision —
what the failure looked like. Subsystem documentation lives in the other files
in `docs/`; this file is only the reasoning behind decisions that are easy to
undo by accident.

## Correctness invariants that are easy to break again

**Speed and distance carry the same trim, always.** The speed calibration the
rider sets is applied to distance as well. Correcting only speed leaves a trip
reading that disagrees with the speed it was integrated from. This was once
broken on one of two paths: the eRPM fallback fed `batteryStatsUpdate` an
untrimmed odometer while the dashboard showed a trimmed one, so lifetime Wh/km
disagreed with its own trip. It was invisible at the default 100% calibration,
which is why it survived so long.

**A change of distance source must rebase, not bank.** The controller converts
using its own configured wheel and gearing; this board uses its own settings.
When setup values stop answering mid-ride the basis changes underneath
`bankCounterDelta`, which reads a backwards step as a controller reboot and
banks the whole new value — so a source switch could add an entire odometer to
the persisted `lifetimeKm`. `batteryStatsRebaseDistance()` moves the baseline
instead. Reconnects were already safe through `batteryStatsStartRide()`.

**Battery preferences are touched from two tasks.** The telemetry task owns
them, but Reset-with-history reaches `resetBatteryStats()` from the LVGL task.
Two tasks sharing one `Preferences` handle can corrupt NVS, and the totals can
tear. Both are taken under a recursive FreeRTOS mutex — deliberately not a
`portMUX` critical section, because NVS writes block and blocking inside a
critical section panics the core. The preview build compiles it to an empty
struct.

**Only byte `0x02` starts a VESC frame.** `vescReadPacket` once also
resynchronised on `3` as a long-packet start, but `3` is the stop byte, so a
stray tail was read as the start of the next reply — precisely the
desynchronisation the resync loop exists to prevent.

**Setup values are believed only when cross-checked.** `COMM_GET_VALUES_SETUP`
gives speed and distance the controller already derived, which stops this board
describing the same drivetrain a second time. But the field order follows
bldc's `commands.c`, and a layout that shifted between firmware versions would
yield a plausible but wrong speed — the worst kind of wrong on a speedometer.
So the parse is accepted only when the voltage and FET temperature it yields
agree with the ordinary `COMM_GET_VALUES` poll from moments earlier. Those two
sit at opposite ends of the payload, so agreement on both implies the offsets
between them line up too. A failed check disables setup values for the session
and the eRPM path carries on. Ten consecutive bad replies do the same; a single
dropped one keeps the last reading, because switching source per cycle would
make speed jump between two conversions of the same motion.

**A latched fault says nothing after a dropout.** The controller fault byte
travels with the telemetry snapshot under the same lock and reports `0` whenever
the link is not live. It is carried as a plain `uint8_t` rather than an
`mc_fault_code` so the UI layer needs no VescUart header — the host preview
builds `src/lvgl_app` without the library.

## Decisions where the obvious alternative was rejected

**Detected facts sit beside rider-entered ones, never on top.** The controller
firmware version is shown as `<name> - FW 6.02` and is never written into the
stored controller name. Prefilling a user-editable field from a source that can
be absent or wrong is how auto-detection quietly corrupts settings. The version
is requested once per connection — it is identity, not telemetry — retried
every five seconds rather than every poll if unanswered, and dropped on
disconnect, since the next controller to reply need not be the same one.

**A lost link and a fault get different treatments.** Every theme reads zeros
when the controller stops answering, which on a dashboard is indistinguishable
from standing still, so a lost link scrims the screen and puts a chip over the
speed: every figure behind it is a zero standing in for nothing. A fault leaves
the readings alone — they are live and worth watching — and takes a band along
the foot. A dead link outranks a fault, since with nothing arriving the fault
byte is as stale as everything else. Both are one screen-level overlay rather
than a disconnected state in each of the twelve themes.

**A setting that affects nothing is not shown.** While the controller supplies
speed, a wheel figure on this board changes nothing, so the field asks for speed
calibration instead. Without setup values the wheel size is what converts motor
revolutions to distance, so it swaps back. The VESC page and the setup wizard
follow the same rule, and both ask for the wheel before any controller has
answered.

**Controller selection is a backend table, not branches at call sites.**
Switching backends does not restart the display: the telemetry task waits for
the old backend to hand back the single NimBLE client slot, then begins the new
one. The setup wizard borrows the radio the same way, so scanning for a
controller other than the running one works without a restart.

**The test senders' encoders are independent of the firmware's decoders.** Two
spare CYDs can impersonate a VESC or a FarDriver. The encoders were written
separately on purpose, so the host tests that decode them are evidence rather
than tautology. For the same reason the fixture CRCs in those tests are
hand-computed constants: a CRC read back from the encoder under test proves
nothing. Note that this makes the constants sensitive to fixture content — the
VESC firmware-version CRC covers the device name, so renaming the fixture
changes it even though the frame length does not.

## Naming and wire formats

The project was CYD, then TEHO-Dash, then KAJO. Renames of this kind hide from
plain grep, and two rounds of debris have already had to be swept up.

**Identifiers that cross the wire moved while nothing was published**, because
they cannot move afterwards: the signed-manifest magic, the release bundle
extension, the advertised BLE names, and the Android `applicationId`.

**Two things deliberately did not move.** The Finnish translations, where
*teho* is the ordinary noun for power — `TEHO`, `Tehokkuus`, `Tehoasteikko` and
the rest are UI strings, not the product name, so the rename was a list of
explicit tokens rather than a word substitution. And the changelog entry
recording the earlier rename: it was true when written, and the display shows it
as history.

**A test that pins a magic value as hex will not catch a rename.** The manifest
test pinned `"43594455"`, which is why it kept passing review while failing.
Wire constants in tests now carry their ASCII in a comment.

## Tooling hazards

**Source can be compiled without being tracked.** `build_src_filter`'s
`+<lvgl_app/>` glob compiles whatever is in the directory, so `ride_logger.cpp`
and six controller files were each built into the firmware while invisible to
git — one stray clean away from gone. This has happened twice.

**`tools/serve_editor.py` validates `Origin` and `Host`.** One of its POST
endpoints launches a build-and-flash, and a cross-origin POST needs no
preflight, so any page open in a browser while the editor ran could have
triggered a flash or overwritten `layout.json`; a hostname pointed at
`127.0.0.1` defeated the loopback bind. Dotfiles are not served, because the
document root is the whole project.

**The preview renderer regenerates its own inputs.** It once wrote the selector
thumbnail header *after* building the renderer that embeds it, so selector
captures always showed the previous artwork and every look change needed two
full passes. It now re-renders those screens when the header actually changes.

**LVGL font aliases can change size silently.** Removing the
`lv_font_montserrat_*` aliases and renaming their 169 uses to the Rajdhani faces
they resolved to revealed three that differed: `montserrat_10` gave a 12 px
font, `28` gave 24, `48` gave 36.

**Button id bases are `constexpr int`, not enumerators.** `BTN_COLOR_BASE +
accent` and its kin add two different enumeration types, which GCC in C++20 mode
warns about at 19 sites while MSVC accepts quietly — enough noise to hide a real
warning. Nothing declares a `MenuButtonId` anywhere and every handler takes an
`int id`, so the bases became what they always were in practice.

## Hardware

Wiring is documented in [wiring.md](wiring.md), including which of the CYD's two
similar-looking 4-pin connectors carries the controller link — picking the wrong
one puts the controller on UART0 alongside the USB-serial bridge and the boot
ROM's reset chatter — and which pins of the MKSESC 84200HP COMM connector to
leave alone. `PowerSW` in particular shuts the controller down if it is grounded
by accident.
