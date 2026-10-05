# Physical-device smoke test

Run this matrix before tagging a firmware checkpoint or beginning a large
subsystem. Record the firmware commit, panel revision, controller, transport,
and any failed step. Test both original and alternate CYD panels.

## Build record

- [ ] Commit under test:
- [ ] PlatformIO build succeeds with RAM and flash usage recorded.
- [ ] Native LVGL renderer builds and all checked-in English previews exist.
- [ ] Finnish, German, French, Spanish, and Italian preview sheets were checked
      for clipping on newly changed screens.
- [ ] `git diff --check` reports no whitespace errors.

## Boot and panel

- [ ] Cold boot reaches first-boot setup on erased settings and the dashboard
      on configured settings.
- [ ] Power-up holds a blank white screen through panel bring-up with no garbage
      frame or visible step when the display is enabled.
- [ ] White fades through darkness into the black KAJO artwork, then the artwork
      fades through darkness into the UI without tearing or partially drawn frames.
- [ ] Both light and dark dashboards show the same black/orange KAJO artwork;
      the first-boot setup screen uses the same final fade.
- [ ] Calibration and recovery status text still renders normally after the
      splash has drawn.
- [ ] Both panel profiles behave: check the alternate/inverted panel renders the
      initial blank screen white and the KAJO splash black.
- [ ] Automatic panel detection selects the correct profile.
- [ ] Standard and alternate profile buttons produce the expected reference
      colors and persist through reboot.
- [ ] Display Information reports plausible ESP32 revision, flash size,
      controller IDs, and firmware build time.
- [ ] Holding the recovery gesture during boot restores usable display/touch
      settings.

## Touch and navigation

- [ ] Touch calibration completes and survives reboot.
- [ ] Holding the dashboard shows nothing for two seconds, then the progress
      ring; releasing mid-ring cancels without toggling the controls overlay.
- [ ] A completed hold opens calibration, and the settings-reset offer appears
      only after calibration succeeds.
- [ ] The hold does not arm while the vehicle is moving.
- [ ] The BOOT button drives the same gesture from the dashboard.
- [ ] Touch Test follows the finger across corners and edges without swapped or
      mirrored axes.
- [ ] Settings always opens on its display page (1/2), in normal and developer
      mode alike, and Back returns to Dashboard.
- [ ] Next reaches page 2/2 with Vehicle Config; Developer Options appears there
      only while developer mode is on. Back steps 2/2 → 1/2 → Dashboard.
- [ ] PIN unlock opens the display page.
- [ ] Holding Display opens the developer activation prompt; cancel and enable
      routes both work, and enabling lands on page 2/2 with the new card.
- [ ] With a Bluetooth controller selected and none paired, the dashboard shows
      NO CONTROLLER PAIRED at once; tapping it opens controller setup (after the
      PIN, when one is set).
- [ ] After a settings reset, with the default wired VESC and no controller
      answering, the dashboard shows NO CONTROLLER CONNECTED YET at once
      (tappable, like the pairing alert). Once any controller has sent data,
      later dropouts show the ordinary link-lost alert instead.
- [ ] Developer disable confirmation cancels and disables correctly.
- [ ] Every submenu Back button returns to its owning category.
- [ ] Tiles, arrows, and small menu triangles have reliable touch targets.
- [ ] Menu inactivity return can be cancelled by touch and otherwise returns
      cleanly to the dashboard.

## Controller links

### VESC UART

- [ ] Live telemetry clears the waiting overlay.
- [ ] Disconnect produces a stale/lost state without freezing the UI.
- [ ] Reconnect restores telemetry without reboot.
- [ ] Baud/CAN/profile changes show the correct save or restart notice.

### VESC BLE

- [ ] Scan prioritizes likely VESC adapters and paginates all candidates.
- [ ] Pairing, saved-device reconnect, disconnect, and forget work.
- [ ] Switching UART/BLE applies after the controlled restart.
- [ ] Information reports transport connection separately from telemetry
      freshness.

### FarDriver BLE

- [ ] Scan prioritizes likely FarDriver devices.
- [ ] Pairing and saved-device reconnect work.
- [ ] Link Diagnostics displays service/characteristic UUIDs, RSSI, raw packet
      data, packet count, and CRC count.
- [ ] Information reports the BLE transport as connected even while decoded
      FarDriver telemetry is unavailable.

## Signed Bluetooth firmware update

- [ ] Settings > Information is clearly itemized and Bluetooth Link is the only phone
      and update entry.
- [ ] With the checked-in release public key, the update service is not
      advertised until Bluetooth Link is opened and a client requests update mode.
- [ ] With a release signed by the key currently installed on the display, a
      connected phone or Windows uploader can request update mode. The display
      acknowledges the handoff, stops ride logging, releases any controller BLE
      link, and advertises OTA only while the update screen remains active.
- [ ] UART, VESC BLE, and FarDriver BLE configurations all enter and leave
      update mode cleanly; Cancel & Restart restores the normal controller link.
- [ ] Wrong-target, unconfirmed downgrade, modified-manifest, modified-image,
      and invalid-signature releases are rejected without selecting a new boot
      image; a confirmed signed downgrade proceeds.
- [ ] Disconnecting the uploader preserves the expected offset, and rerunning
      the same release resumes. A different version/size is refused until the
      staged transfer is cancelled.
- [ ] Duplicate, skipped, and out-of-order chunks are rejected without corrupting
      the accepted offset.
- [ ] A complete valid release verifies, restarts automatically after five
      seconds (or immediately on an uploader reboot request), and boots the new slot.
- [ ] Reset/power loss during transfer continues to boot the previous slot.
- [ ] A deliberately unhealthy new image rolls back; a healthy image remains
      installed after the ten-second UI health window and another cold boot.
- [ ] Repeated transfers do not trigger watchdog resets or unsafe heap loss.

## Ride logging and notices

- [ ] Card insertion/removal updates Logging without blocking touch.
- [ ] A recorded dashboard demo ride appears as a demo ride in the list and
      replays; switching demo mode does not mix demo and controller samples in one file.
- [ ] Logging is off on a fresh install. Once enabled, a ride starts after movement, pauses after a long stop without
      closing the ride, resumes into the same ride when movement returns, and
      closes it after the 5-minute stop window.
- [ ] Cutting bike power mid-ride still leaves a ride that opens in replay and
      downloads to the phone.
- [ ] Removing the card while recording stops safely and reports the failure.
- [ ] Saved ride catalog is newest-first and invalid/truncated files do not
      crash browsing.
- [ ] A ride of about 30 minutes (a demo ride will do) opens within a few
      seconds, and replay keeps up with it: 100x playback, dragging the cursor
      and the skip buttons follow the touch without the cursor trailing. Time
      the open and note the rate it was recorded at.
- [ ] Replay repaints only the cursor, dots and bubbles between ticks. Playing
      at 1x and 100x, dragging, tapping and skipping leave no extra cursor
      line, dot or bubble text behind; the `LVGL perf` serial reports show far
      fewer `pixels_s` at 1x than before (about 55,000 per tick became about
      1,100 natively).
- [ ] Replay zoom: the corner buttons zoom the chart in and out about the
      cursor with a short stretch animation, the track under the charts shows
      the view's place in the ride, and the buttons dim at the whole ride and at
      the narrowest view. Zoom in two or three steps on a 30 minute ride and note
      how long the stretched overview stays before the crisp trace replaces it;
      play zoomed at 10x and 100x through several page turns without a stall or a
      stale trace, and skip across pages both ways. Compare the `LVGL perf` serial
      reports during the animation with the figures in
      `dashboard-rendering-performance.md`.
- [ ] Replay holds its ride file open between reads. Deleting that ride from the
      summary, leaving replay, and opening another ride all leave the card and
      the ride list intact; so does a new ride starting in the background while
      one is open in replay.
- [ ] Ride logs > Clear SD > Clear keeps the dialog open with a filling bar and
      a removed-file count, says SD CARD CLEARED only after the wipe finishes,
      then returns to an empty list on its own. With a card holding a few
      hundred files the display stays responsive and does not reboot.
- [ ] Every transient notice starts hidden, slides fully into the shared bottom
      housing, remains readable, and slides out before deletion.

## Reset behavior

- [ ] Default Reset restores display/vehicle/controller settings, removes saved
      VESC and FarDriver BLE pairings, and restores logging to Automatic / 5 Hz.
- [ ] Default Reset keeps existing SD-card ride files, their next ride number,
      and battery history.
- [ ] Full Reset performs the same settings/pairing reset and also removes all
      SD-card ride files, resets the ride sequence, and clears battery history.
- [ ] Full Reset requested without an SD card remains pending across reboot and
      removes the old ride files when that card is inserted again.
- [ ] Neither reset changes the controller-owned odometer.

## Soak

- [ ] Repeat dashboard → settings → controller scan → dashboard at least ten
      times without a reset or visible heap degradation.
- [ ] Leave telemetry, dashboard redraw, and logging active for at
      least 30 minutes.
- [ ] Verify touch responsiveness while BLE scans, SD writes, and dashboard
      redraws occur.
- [ ] Cold reboot after the soak and confirm all saved settings and the latest
      ride catalog remain usable.
