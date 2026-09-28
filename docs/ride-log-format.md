# Ride-log binary format

Ride files are stored on the SD card as `/rides/R<id>.cyd`. All integer fields
are little-endian. Structures are packed, and both the header and every record
end with CRC-16/CCITT (`0x1021`, initial value `0xFFFF`) over all preceding
bytes in that structure.

This is a pre-release format. A reader must reject versions or record sizes it
does not understand; the firmware does not carry compatibility readers for
superseded prototype layouts.

## File header — version 3, 32 bytes

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `KAJL` |
| 4 | 2 | Format version (`3`) |
| 6 | 2 | Header size (`32`) |
| 8 | 2 | Record size (`44`) |
| 10 | 1 | Samples per second |
| 11 | 1 | Flags: bit 0 set when the ride was recorded from demo mode |
| 12 | 4 | Ride ID |
| 16 | 4 | Creation time in boot milliseconds |
| 20 | 10 | Reserved |
| 30 | 2 | Header CRC |

## Telemetry record — version 3, 44 bytes

| Offset | Size | Field | Scale |
| ---: | ---: | --- | --- |
| 0 | 4 | Elapsed ride time | ms |
| 4 | 4 | Controller-session uptime | s |
| 8 | 4 | Power | W, signed |
| 12 | 4 | Trip distance | m |
| 16 | 4 | Net ride energy | 0.1 Wh, signed |
| 20 | 4 | Regenerated energy | 0.1 Wh |
| 24 | 2 | Speed | 0.1 km/h, signed |
| 26 | 2 | Pack voltage | 0.01 V |
| 28 | 2 | Pack current | 0.1 A, signed |
| 30 | 2 | Phase current | 0.1 A, signed |
| 32 | 2 | Motor temperature | 0.1 °C, signed |
| 34 | 2 | Controller temperature | 0.1 °C, signed |
| 36 | 1 | Battery state of charge | percent |
| 37 | 1 | Controller fault code | backend-defined raw code |
| 38 | 4 | Available-field mask | `TelemetryField` bits |
| 42 | 2 | Record CRC | — |

The availability mask is authoritative. A numeric zero in a field whose bit is
clear is only padding and must not be interpreted as a measured zero. The bit
assignments live in `src/lvgl_app/controller_manager.h`; changing them requires
a new log-format version.

Phase current is the motor side, several times the pack figure away from full
duty, and is signed through regeneration. Only backends that report it
separately set its bit: VESC does, FarDriver does not yet (see
`farDriverBackendPoll`), and a ride recorded without it charts as "Not
recorded" rather than as zero.

The flags byte at offset 11 was reserved and written as zero, so a file from
before it existed reads as an ordinary ride and needs no format bump. The ride
list shows a flagged file as "DEMO RIDE n": demo recordings are written to the
card like any other, and nothing else distinguishes them.

Version 3 added phase current and moved everything after pack current along by
two bytes. Version 2 files are rejected outright. Nothing migrates them, and
nothing needs to: no build that wrote them was ever released. `kLogVersion`,
`kHeaderBytes` and `kRecordBytes` in `ride_replay_core.h` are the single source
of truth for both the writer and the reader.

## How a ride is recorded

Recording is split across two tasks so SD latency never reaches the UI. The
telemetry task builds and CRCs each record after a controller poll and pushes
it onto a 64-slot queue; the `ride-logger` task owns the card exclusively and
is the only thing that opens it. Replay reads, phone downloads, catalog scans
and deletions all travel the same queue, so a read cannot collide with a write.

The writer buffers records in 1 KB of RAM and flushes when the next record
would not fit, when 1.5 s has passed, or when the ride stops. Ride IDs come
from the `logger`/`nextRide` preference and are never reused. Before each new
ride, the oldest files are deleted while free space is under 512 KB. With the
card idle and logging off, the task performs no SD transactions at all.

Sample rates are 1, 2, 5 (default) or 10 Hz. At 5 Hz a ride costs about
790 KB per hour. Both backends poll at 100 ms, so 10 Hz sits at that ceiling
and will drift slightly below it.

### Ride policy

Logging is a single on/off setting, off by default. While it is on, every ride
is started, paused and closed by this policy; there is no manual start or stop.
Settings saved by older firmware with the retired manual mode load as on.

Movement means at least 2 km/h **or** 75 W in either direction, so loading the
motor starts a ride before the wheel reports speed. The three windows live in
`ride_log_policy.h` and are exercised by `cyd_ride_log_policy`:

| Window | Default | Effect |
| --- | ---: | --- |
| Start | 2 s moving | Opens the ride. Nudging the bike creates no file. |
| Pause | 30 s still | Stops appending samples; the ride stays open. |
| Stop | 5 min still | Closes the ride. |

Pausing and stopping are deliberately separate. Appending stationary records
until the ride closed would inflate its duration and drag its average speed
down; ending the ride as soon as it paused would split one journey into several
at every longer stop. Moving again before the stop window resumes into the same
file, and the paused stretch is simply absent, which the replay parser already
reads as a gap. Either timer resets the moment the condition flips, so a
traffic light neither pauses the ride nor breaks its trace.

Losing the controller link counts as standing still. Samples are what drive the
policy, so the controller task keeps applying it while the link is down: a short
dropout resumes into the same ride, a lasting one closes it after the stop
window, and switching logging off takes effect without waiting for telemetry. Until a
ride is closed its file belongs to the writer and cannot be replayed.

Dashboard demo mode follows the same logging policy as
controller telemetry. Its samples come from the dashboard timer; the
controller telemetry task does not submit samples while demo mode is enabled.
Changing between demo and controller telemetry closes the current ride so a
file never mixes the two sources. Theme previews do not feed the logger.

### Losing power mid-ride

There is no safe-shutdown signal: the bike's power can go away between flushes.
At most one flush interval of records is lost, and the file can be left with a
partial trailing record because SD appends are not atomic. Every reader rounds
the record count down and ignores that tail — the ride list, the ride-series
reader, the replay parser and the phone's `RideFileValidator` alike — so the
ride still opens with every complete record intact and CRC-checked. Do not
reintroduce a whole-number-of-records check in any one of them: it would leave
a ride that lists correctly and then refuses to open.
