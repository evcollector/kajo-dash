# Bluetooth firmware update design

This is a pre-release protocol with no backward-compatibility guarantee. The
display firmware, signed manifest, and uploader must all use the current
protocol version.

## Current implementation

The flash map has `otadata`, `ota_0`, and `ota_1`, with two equal 1,984 KiB
application slots. The current application is written to one slot while the
other remains available as the update target.

`src/lvgl_app/firmware_update.*` owns the transport-independent update
mechanics:

- selecting the inactive OTA partition;
- enforcing a declared image size and strictly sequential offsets;
- streaming SHA-256 while writing;
- rejecting incomplete or mismatched images;
- selecting the new boot partition only after ESP image and digest validation;
- keeping a new image pending until setup and the LVGL loop survive a ten-second
  health window.

`src/lvgl_app/firmware_update_auth.*` verifies a fixed 84-byte manifest signed
with ECDSA P-256/SHA-256. The signed bytes bind the `KAJU` magic, protocol and
target IDs, compression flags, monotonic version code, final image size/SHA-256,
and compressed transport size/SHA-256. The 64-byte wire signature is the raw
big-endian `r || s` pair.

`src/lvgl_app/firmware_update_ble.*` exposes the service only after the rider
opens Information > Bluetooth Link and a connected client requests update mode. The
display acknowledges that request before replacing Bluetooth Link with the signed OTA
receiver and its simpler update screen. The checked-in
public key is a real trust anchor, and the display installs only images signed
by the matching offline private key.

## Update-mode lifecycle

1. The rider explicitly opens Bluetooth Link on the display and the connected updater
   requests update mode. The device never advertises either service during
   ordinary riding.
2. Logging stops cleanly. If the active controller uses BLE, its client link is
   disconnected before update advertising begins.
3. The updater sends a fragmented manifest containing protocol version, target
   family, image version, final and compressed sizes/hashes, and signature.
4. The device validates compatibility and signature before erasing the inactive
   application slot.
5. Data chunks carry an absolute compressed-transport offset. Each independently
   zlib-compressed 4 KiB firmware sector is checked with CRC32, decompressed,
   written once, and then acknowledged. Only committed sector boundaries are
   resumable.
6. After the final chunk, the device checks the streamed digest and ESP image
   structure, then selects the inactive slot for the next boot. Authenticity
   was already established before the first flash erase/write.
7. The display restarts automatically five seconds after verification; the
   updater may request an immediate reboot. An interrupted transfer leaves the
   current slot selected and bootable.
8. The new image remains pending until the UI health window succeeds. A crash or
   reset before confirmation lets the bootloader return to the previous slot.

## BLE service boundary

The custom GATT service UUID is
`7c7d7e00-2aa7-4f62-a497-6a9b02d14d00`, with three characteristics:

- **Control** (`...01...`): write commands (manifest `BEGIN` fragments,
  signature fragments, `COMMIT_MANIFEST`, `COMMIT_SECTOR`, `FINISH`, `ABORT`,
  `REBOOT`, `STATUS`).
- **Data** (`...02...`): write-without-response frames containing a little-endian
  32-bit absolute offset plus up to 240 payload bytes. The update link requests
  a 247-byte ATT MTU, 251-byte data length, and a 7.5-15 ms connection interval.
- **Status** (`...03...`): a 28-byte read/notify value containing protocol,
  state/flags, accepted compressed offset/size, signed error code, version, and
  final firmware size plus an acknowledgement revision counter.

`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1` remains in force. Update mode owns the
single BLE connection, so controller BLE and updater BLE never compete. The
peripheral and broadcaster roles are compiled in but remain dormant outside
explicit update mode.

Protocol 3 requires full-size 244-byte data frames where possible (a four-byte
offset and 240 compressed bytes). A smaller negotiated MTU is rejected rather
than silently reducing throughput. Status/UI updates occur at sector boundaries,
not for every BLE packet.

The uploader sends one compressed sector at a time using write-without-response
packets, commits it with CRC32, and waits for a new status revision before
starting the next sector. This bounds the queue while avoiding per-packet GATT
reads and notifications.

## Authenticity

SHA-256 proves that received bytes match the manifest; ECDSA proves that the
manifest came from the release key. The encrypted release private key stays
outside both the uploader package and repository. Firmware contains only its
public point. BLE pairing is not treated as the authenticity boundary: physical
entry into update mode limits exposure, and every accepted image must match a
signed manifest.

Generate the key with `tools/firmware_update/create_release_key.py`, commit only
the generated `include/firmware_update_public_key.h`, increment
`CYD_FIRMWARE_VERSION_CODE`, and use a wired flash for that initial trust-anchor
and partition-table installation.

The current firmware version code is defined in `include/config.h`. Increment
it for each image that must compare newer than the previous one; use a wired
flash when deliberately rebasing the prototype counter. The OTA protocol
version is independent and remains `3` until the on-air format changes.

## Windows uploader

`tools/firmware_update/upload_firmware.py` is the first Windows uploader. It:

- discovers Bluetooth Link, requests update mode, then discovers the update-service
  UUID (or connects directly if the receiver is already active);
- parse a signed release manifest rather than inventing one locally;
- creates/loads the signed zlib-sector transport and verifies it reconstructs
  the original firmware before connecting;
- requires a 247-byte ATT MTU and sends 240 compressed bytes per full packet;
- rejects displays and release manifests using an older protocol version;
- resumes from the device-reported committed sector boundary after a recoverable
  error or disconnect;
- show transfer rate, byte progress, validation, and reboot state;
- rejects locally inconsistent release/image pairs and refuses to resume a
  different version or image size already in progress.

Target, slot size, and signature enforcement remain authoritative on the
display. A correctly signed older version pauses before transfer and requires
explicit confirmation on the CYD; unsigned, wrong-target, and incompatible-
protocol images remain blocked. See `tools/firmware_update/README.md` for key
generation, signing, and upload commands.

Physical downgrade confirmation originates in the display UI rather than in a
GATT control write. The receiver republishes and notifies the status
characteristic immediately after handling it. Clients may also send the
idempotent `STATUS` command before a read to request a fresh snapshot.

Cancellation is also an acknowledged state transition. Either side requests
the abort, the display publishes `CANCELLED`, and the BLE link remains available
briefly so the uploader can read the terminal state before the display restarts.
Clients must stop producing no-response writes, let the single in-flight GATT
operation settle, and then send `ABORT`; cancelling the BLE library's entire
request queue also cancels the abort that needs to follow it.

Successful image and transport verification schedules an automatic display
restart after five seconds. Clients may still request `REBOOT` during that
window to restart immediately, but the normal user flow requires no final
install action.

KAJO Companion implements the same signed OTA protocol for phone uploads.

## Required physical tests before relying on the release key

- Exact-size valid image and image close to the slot limit.
- Wrong target, zero/oversized image, invalid ESP image, and invalid signature.
- Corrupted chunk, duplicate chunk, skipped/out-of-order offset, and bad final
  digest.
- Disconnect and power loss during erase, write, validation, and before reboot.
- New image crash/reset before health confirmation and successful rollback.
- Successful update while the configured controller normally uses UART, VESC
  BLE, and FarDriver BLE.
- Repeated update attempts while monitoring minimum free heap and watchdogs.
