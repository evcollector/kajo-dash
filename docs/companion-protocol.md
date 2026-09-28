# Companion BLE protocol

The user-controlled Bluetooth Link mode exposes protocol version 2 through the
service and characteristics declared in `src/lvgl_app/companion_ble.h`.
Commands are written to the control characteristic; the phone polls the
response characteristic after each acknowledged command. All multibyte values
are little-endian.

## Commands

- `1`: device identity.
- `2`: end the companion session.
- `3, page`: completed-ride catalog, eight entries per page.
- `4, rideId, field, startSeconds, endSeconds, maximumPoints`: downsampled ride
  series. Field 1 is speed in 0.1 km/h and field 2 is power in watts. The first
  implementation returns at most 36 averaged points.
- `5, rideId, offset, requestedBytes`: raw ride-file range, with a maximum
  220-byte payload.
- `6, dashboardMode`: read the complete customization profile for one dashboard.
- `7, dashboardMode, accent, backgroundAccent, gradientPosition, flags,
  data[12]`: validate, save, and select a complete dashboard profile. The
  18-byte command fits inside the 20-byte control characteristic without
  relying on a negotiated MTU.
- `8`: request the signed firmware-update mode. The display replies with a
  `KAJM` acknowledgement, closes Bluetooth Link after a short acknowledgement window,
  and starts the separate OTA service. A missing release key is rejected.

Responses use `KAJC` for identity, `KAJR` for catalog pages, `KAJS` for series,
`KAJF` for file ranges, `KAJT` for theme profiles, `KAJM` for a Bluetooth-mode
handoff, and `KAJE` for errors. The
identity response includes the selected dashboard mode at byte 11. A `KAJT`
response carries the mode, whether it is currently selected, accent and
background colors, gradient position and flags, active data-slot count, and
all twelve data-field ordinals. Android reads a profile before editing it so
applying visual changes cannot silently replace existing field assignments.

Completed rides are browsable
without copying the complete file. A full download advances through explicit
offsets and Android validates the `KAJL` header and every record CRC before
keeping the file.

SD operations are serialized through the ride logger task. The active ride is
not listed or exported, so companion reads cannot race its append buffer.
