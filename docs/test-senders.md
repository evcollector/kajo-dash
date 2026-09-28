# Fake controller test senders

Two minimal lab applications that impersonate a controller over Bluetooth, one
per backend, so the dashboard's link handling can be exercised without a
vehicle. Flash **one spare CYD** with a sender, then use another CYD running the
ordinary dashboard as the receiver. These are separate TFT_eSPI applications,
not LVGL dashboard themes.

The important difference between them follows the real protocols: **FarDriver
streams and a VESC never speaks first.** The FarDriver sender has a packet rate
to set and streaming to start; the VESC sender answers requests, so its controls
are about *what it answers* rather than how often it talks.

Both exercise the protocol assumptions in the current firmware. Neither
validates a real controller's behaviour. Capture replay, malformed frames and
fragmentation are outside both.

## Flashing and restoring

| | FarDriver | VESC |
| --- | --- | --- |
| Upload | `scripts\upload_fardriver_test.bat` | `scripts\upload_vesc_test.bat` |
| Build only | `--build-only` | `--build-only`, or `pio run -e vesc_test` |
| Environment | `fardriver_test` | `vesc_test` |

With one USB serial device the script selects that port; with several it asks
which is the spare CYD. To select explicitly, pass the port:
`scripts\upload_vesc_test.bat COM4`. Use the board's USB data connector and a
data cable. Neither script ever builds the default dashboard environment, and
both leave the result visible until a keypress.

Restore the normal dashboard with `scripts\upload_firmware_usb.bat COM4`. The
senders use the same partition table, do not erase the whole flash, and write
only their own inversion preference.

## Controls common to both

- **Mode** (bottom of screen): switch between **Ride Sequence** (default) and
  **Stationary**. Switching mode restarts the sequence. New connections reset
  its time.
- **Disconnect:** drop the current BLE client and resume advertising; reconnect
  from the receiver.
- **Invert Colors:** toggle panel inversion and save it in the sender's own
  settings namespace (`fd-test` / `vesc-test`). Initial detection matches the
  dashboard's panel readback heuristic. This does not change the dashboard's
  stored panel profile.
- **BOOT:** short press/release toggles transmission; hold for 1.2 seconds and
  release to invert. Hold only after boot; holding during reset enters the
  bootloader.
- **Serial at 115200 baud:** `d` disconnects, `i` toggles inversion, `m`
  switches mode, `s` toggles transmission. FarDriver adds `r` to cycle the
  packet rate; VESC adds `v` to toggle setup values. Connection and
  subscription events are logged.

Touch uses the existing dashboard calibration, read-only, if available;
otherwise the dashboard's factory mapping. BOOT and serial remain usable if
touch needs calibration. Both screens are fixed English 320x240 diagnostic
interfaces.

---

# FarDriver sender

Advertises `FarDriver TEST xxxx`, service `FFE0`, with a notify-only `FFEC`
characteristic and one BLE client at a time.

## Reproduce the discovery/Continue problem

1. Power the sender and scan for FarDriver on the receiver. Select the
   `FarDriver TEST xxxx` entry; do not select a real controller accidentally.
   Choose **MODE: STATIONARY** for setup/Continue testing.
2. Wait for **CONNECTED / SILENT** on the sender. BLE and notification
   subscription are established, but it sends no packets.
3. Try **Continue** on the receiver. Note whether confirmation opens.
4. Return to discovery (or reconnect), tap **START PACKETS** on the sender,
   then try **Continue** while packets arrive. The counter should increase.
5. Tap **PAUSE PACKETS** and try again. Compare 1, 10 and 50 packets/second.
   Repeat with an original and an inverted-panel receiver.

Each connection and new subscription starts silent. Starting before subscription
does not arm automatic transmission. A phone can connect instead of the receiver
to save these synthetic packets using the Android capture option.

## Its own controls

- **Start/Pause packets:** switch between silence and continuous notifications.
- **Rate:** cycle nominal 1, 10 and 50 notifications/second; default 10. Timing
  is best effort — display redraws and Bluetooth scheduling add jitter.

## What the packets mean

One 16-byte frame per notification, rotating IDs `0x80`, `0x81`, `0xB3`, `0xB5`.
The CRC uses the current receiver's reflected `0xA001` polynomial and `0x7F3C`
seed, stored low byte first. Each field group updates once per four
notifications. Sent counts successful notification submissions, not
application-level acknowledgements from the receiver.

**Stationary** represents 52.0 V, 0 A, 0 RPM, first gear, 30 C controller
temperature, 35 C motor temperature, and 75% charge. Zero RPM is intentional.

**Ride Sequence** repeats every 60 seconds, interpolating between these points:

| Time | Phase | Raw RPM | Voltage | Current | Derived power | Motor / ESC | SOC |
| --- | --- | ---: | ---: | ---: | ---: | --- | ---: |
| 0–5 s | Idle | 0 | 52 V | 0 A | 0 W | 35 / 30 C | 75% |
| 15 s | Accelerating | 300 | 50 V | 25 A | 1,250 W | 42 / 34 C | 74% |
| 25 s | Peak | 600 | 49 V | 40 A | 1,960 W | 55 / 42 C | 72% |
| 35 s | Cruise | 450 | 51 V | 18 A | 918 W | 60 / 45 C | 71% |
| 40 s | Coast | 400 | 52 V | 0 A | 0 W | 57 / 42 C | 71% |
| 50 s | Regen | 100 | 53 V | -10 A | -530 W | 48 / 37 C | 72% |
| 55–60 s | Stopped | 0 | 52 V | 0 A | 0 W | 35 / 30 C | 75% |

Gear also varies through 1, 2, and 3. The accelerated temperature/SOC changes
are display-test fixtures, not a physical battery model. The receiver
calculates power from voltage/current and speed from raw RPM and its
wheel/pole/gearing settings. With 700 mm wheels, 7 pole pairs and 1:1 gearing,
peak speed is about 45 km/h. Packet rate does not change the cycle duration.
One sample is held across each four-frame group, so low rates show coarser
steps. The sender shows cycle time, voltage, watts and raw RPM.

---

# VESC sender

Advertises `VESC TEST xxxx` with the Nordic UART Service `6e400001-...`, a write
characteristic `...0002` and a notify characteristic `...0003`, one BLE client
at a time.

## Testing a VESC Bluetooth link

1. Power the sender. On the receiver choose VESC and Bluetooth in the controller
   setup, then scan. Select the `VESC TEST xxxx` entry.
2. The sender shows **CONNECTED / ANSWERING** once the receiver subscribes. The
   request counter should climb immediately: the dashboard polls continuously.
3. Tap **STOP ANSWERING** to make the controller go silent while the BLE link
   stays up. This is the "connected but not talking" case, and the dashboard
   should report the link as lost rather than freezing on stale values.
4. Tap **SETUP VALS: OFF** to test the eRPM fallback.

## Its own controls

- **Start/Stop answering:** answer or ignore every request. The BLE link stays
  connected either way, so this isolates a silent controller from a dropped
  link.
- **Setup values ON/OFF:** answer or ignore `COMM_GET_VALUES_SETUP`. Real VESC
  firmwares vary in whether they provide it, and the dashboard falls back to
  eRPM and the configured wheel size when it goes unanswered. Turning it off is
  how to test that fallback without finding an old VESC. The dashboard probes
  once and needs ten consecutive failures before it gives up, so allow a few
  seconds for the switch to take effect.

Switching mode also resets the totals.

## What the replies contain

Requests and replies use the VESC UART framing the dashboard already speaks:
`0x02 | length | payload | crc_hi | crc_lo | 0x03`, with CRC-16/XMODEM over the
payload. Replies are chunked into 20-byte notifications, so they arrive intact
at the smallest usable MTU; the receiver reassembles the byte stream exactly as
it would from a UART. `COMM_FORWARD_CAN` requests are unwrapped and answered as
the inner command.

Three commands are answered — `COMM_GET_VALUES` (4), `COMM_GET_VALUES_SETUP`
(47) and `COMM_FW_VERSION` (0, reported as 6.2). Anything else is counted as
ignored and left unanswered, which is also what a real VESC does for commands it
does not implement.

**Stationary** represents 52.0 V, 0 A, 0 eRPM, 30 C controller and 35 C motor
temperature, no fault. Zero eRPM is intentional.

**Ride Sequence** repeats every 60 seconds, interpolating between these points:

| Time | Phase | eRPM | Voltage | Input current | Speed | FET / motor |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 0–5 s | Idle | 0 | 52.0 V | 0 A | 0 km/h | 30 / 35 C |
| 15 s | Accelerating | 4,200 | 50.0 V | 18 A | 25 km/h | 34 / 42 C |
| 25 s | Peak | 8,400 | 49.0 V | 44 A | 50 km/h | 42 / 55 C |
| 35 s | Cruise | 6,300 | 51.0 V | 13 A | 37 km/h | 45 / 60 C |
| 40 s | Coast | 5,600 | 52.0 V | 0 A | 33 km/h | 42 / 57 C |
| 50 s | Regen | 1,400 | 53.0 V | -7 A | 8 km/h | 37 / 48 C |
| 55–60 s | Stopped | 0 | 52.0 V | 0 A | 0 km/h | 30 / 35 C |

Charge, energy, distance and the tachometer are **not** interpolated: they are
integrated from the current sample and count up the way a real controller's do,
so the receiver's trip and battery arithmetic sees a plausible series instead of
a value that jumps backwards every cycle. They reset on a new connection and on
a mode change.

The two replies are kept consistent with each other on purpose. The dashboard
cross-checks voltage and FET temperature between `GET_VALUES` and
`GET_VALUES_SETUP` before it will trust setup values, and abandons them for the
rest of the session if they disagree — so a sender that got this wrong would
silently test only the eRPM fallback.
`tools/lvgl_native_preview/vesc_sender_test.cpp` asserts the agreement, along
with decoding every frame of two full ride cycles with the firmware's own
parsers.
