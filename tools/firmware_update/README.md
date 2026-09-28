# Signed firmware uploader: Bluetooth updates and USB installs

The display accepts firmware only while its Firmware Update screen is active
and only when the fixed manifest is signed by the offline ECDSA P-256 release
key whose public half is compiled into the firmware. The uploader and the
display firmware must use the same OTA protocol version; older variants are not
supported.

For a test package or an ordinary release, none of the individual commands
below need to be run by hand -- choose option 5 in `kajo.bat` (or double-click
`scripts\make_release.bat`), pick **T** or **R**, check the plan it prints and
type the key password. It packages the version in `include/config.h`: a test
package goes to `releases\test\` and shows as `0.01-test` on the display; a
release goes to `releases\` and `dist\`, is recorded in `RELEASES.md`, committed
and tagged, and then moves `include/config.h` on to the next version. A failed
step changes nothing, so it can simply be run again. "Test packages and
releases" in `docs/development.md` and the docstring of `make_release.py` have
the details, including the unattended `--yes` mode. The rest of this file
documents what it does, and is what you want when something needs doing
differently.

`scripts\make_release.bat` creates the project's virtual environment on first
use. To set it up by hand instead:

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python -m pip install -r tools\firmware_update\requirements.txt
```

Create the release key once. The private path must be outside this repository:

```powershell
.\.venv\Scripts\python tools\firmware_update\create_release_key.py `
  --private-key C:\Users\you\Secure\kajo-release-key.pem
```

The tool encrypts the PEM and prompts twice for its password. For unattended
release automation, provide the password through the
`CYD_RELEASE_KEY_PASSWORD` environment variable or choose another variable with
`--password-env`; do not put the password in a command line or repository file.

Commit the generated public-key header, increment
`CYD_FIRMWARE_VERSION_CODE`, build, and perform a wired flash. Never commit or
copy the private key into the uploader directory.

`CYD_FIRMWARE_VERSION_CODE` in `include/config.h` is the number the display
reports about itself and the number OTA compares for rollback. The release
builder moves it on after every release; test packages share the code of the
release they lead up to. Do not reset it through OTA:
going backwards is intentionally treated as a downgrade and needs confirmation
on the panel, so rebase with a wired flash instead. Protocol version `3` is a
separate wire-format identifier and must not be reset with the firmware
counter.

Sign a built image into a portable release set:

```powershell
.\.venv\Scripts\python tools\firmware_update\sign_firmware.py `
  --firmware .pio\build\kajo\firmware.bin `
  --private-key C:\Users\you\Secure\kajo-release-key.pem
```

The version code and the output path come from `include/config.h`, so they
cannot drift from the firmware being signed; `--version-code` and `--output`
override them and the signer refuses a version code that disagrees with the
header. Signing also checks the private key against the public key compiled into
the current build and stops if they are not a pair -- which is the cheapest way
to confirm a newly rotated key before flashing anything.

This produces three public files with matching stems: `.json`, `.bin`, and
`.zlib`. The JSON uses relative sibling paths, so the set can be moved to
another computer. The private key is never copied into `releases`.

Straight after signing, while `.pio\build\kajo` still holds the signed build,
add the USB install layout:

```powershell
.\.venv\Scripts\python tools\firmware_update\package_usb.py releases\firmware-v5.json
```

It copies that build's bootloader and partition table, and the Arduino core's
`boot_app0.bin`, beside the release as `firmware-v5-bootloader.bin`,
`-partitions.bin` and `-boot_app0.bin`, and records where each image goes in
`firmware-v5-usb.json` (the format is in `usb_flash.py`). It refuses if the
build directory no longer holds the signed image. None of these files is
signed: a USB install needs physical access, and the display's signature check
only guards the Bluetooth path.

## Maintainer: build the public Windows package

Build the standalone uploader and package it with a signed release:

```bat
tools\firmware_update\build_windows_uploader.bat
```

```powershell
.\.venv\Scripts\python tools\firmware_update\package_windows_release.py `
  --manifest releases\firmware-v5.json
```

The resulting `dist\KAJO-Dash-Firmware-v5-Windows.zip` contains the uploader EXE,
double-click launcher, signed JSON/bin/zlib set, its USB install layout, short
instructions, and copies of `LICENSE`, `NOTICE`, `THIRD-PARTY-NOTICES.md` and
the `licenses/` texts. The packager refuses a release without a USB layout. The
ZIP contains no signing key and is suitable for a GitHub Release attachment.

The uploader EXE bundles esptool for the USB install.
`build_windows_uploader.bat` passes `--collect-data esptool` so that esptool's
flasher stubs travel with it.

After a successful build, `build_windows_uploader.bat` records what the EXE was
built from in `KAJO Firmware Uploader.inputs` beside it: a fingerprint of the
uploader's modules, its requirements and build script, and the installed package
versions, plus the EXE's own hash (see `uploader_inputs.py`). The release
builder rebuilds the EXE whenever that record is missing or no longer matches,
so an uploader from before a change is never packaged by mistake.

## End user: first install over USB

The user extracts the complete Windows updater ZIP, connects the display with a
USB data cable, double-clicks `Install or Update KAJO-Dash.bat` and chooses
**1. Install over USB cable**. The uploader picks the one USB serial adapter
(CH340/CH9102, CP210x, FTDI), ignoring Bluetooth serial ports, and asks which
one if there are several. It checks the release against its signed manifest
and the USB layout, then writes the bootloader, partition table, OTA-data reset
and firmware with esptool, which verifies each image and resets the board. NVS
is never written, so a reinstall keeps the display's settings; `--erase-all`
starts clean.

If the board does not enter its download mode by itself, the uploader explains
the BOOT/RST sequence and offers another attempt. A transfer that fails after
connecting is retried at 115200 baud. When no adapter is found, it says so and
links the CH340 and CP210x drivers, which Windows usually installs by itself.

In a source checkout, run `kajo.bat --usb`, or call the uploader directly:

```powershell
.\.venv\Scripts\python tools\firmware_update\upload_firmware.py `
  releases\firmware-v5.json --usb --port COM4
```

## End user: install an update

The user extracts the complete Windows updater ZIP, opens Settings > Information
> Bluetooth Link on the display, double-clicks `Install or Update KAJO-Dash.bat`
and chooses **2. Update over Bluetooth**.

In a source checkout, use menu option 6 in `kajo.bat` or `kajo.bat --ble`.
The release packager copies that same script under the name above; without the
development scripts directory it opens the two-choice installer menu.

The public launcher selects the bundled protocol 3 manifest, discovers Bluetooth Link,
asks the display to switch to its signed OTA service, transfers and verifies the
firmware, and requests restart. It
never asks for a private key. Interrupted transfers resume at the last committed
sector when the launcher is run again. Older manifests are ignored.

In a source checkout, the same launcher falls back to the Python uploader when
the standalone EXE is absent. That fallback can create a virtual environment;
public ZIP users do not need Python or pip.

## Contributor: build and install your own firmware

The display trusts exactly one signing key, compiled into the image from
`include/firmware_update_public_key.h`. Official releases are signed with the
matching private key, which is held offline and is not in this repository. A
build made from your own checkout is therefore **not** installable over
Bluetooth by default: the display will reject it as unsigned, which is the
intended behaviour and not a fault.

There are two ways to run your own build.

### Flash over USB

The simplest route, and the right one for occasional changes. Nothing needs
signing, no key is involved, and the trust anchor stays as it is, so the display
still accepts official releases afterwards:

```bat
scripts\upload_firmware_usb.bat
```

### Install your own trust anchor

Worth doing if you are iterating often enough that walking a cable to the bike
becomes the bottleneck. You replace the compiled public key with your own, which
lets you sign and upload your builds exactly as the maintainer does.

Create a key of your own. The private half must live outside the repository, and
`--replace-public-header` is required because a configured key already exists:

```powershell
.\.venv\Scripts\python tools\firmware_update\create_release_key.py `
  --private-key D:\Secure\my-cyd-key.pem `
  --replace-public-header
```

That rewrites `include/firmware_update_public_key.h`. Build and flash **once
over USB** — the display is still trusting the previous key at this point, so
this first install cannot go over Bluetooth. From then on, sign and upload your
own builds with the ordinary `sign_firmware.py` and uploader commands.

Two consequences to be aware of:

- your display no longer accepts official releases, because it now trusts only
  your key. Returning to official builds means flashing an official image over
  USB once, which restores the original trust anchor;
- the regenerated `include/firmware_update_public_key.h` is a tracked file.
  Never include it in a pull request. Restore it before committing:

```bat
git checkout -- include/firmware_update_public_key.h
```

Losing the private key is not recoverable from the firmware, and a display
carrying your trust anchor can then only be updated over USB.

Pass a specific release or uploader options when needed:

```bat
kajo.bat --ble releases\firmware-v5.json --reboot
kajo.bat --ble --address AA:BB:CC:DD:EE:FF
```

The Python uploader can also be called directly:

```powershell
.\.venv\Scripts\python tools\firmware_update\upload_firmware.py `
  releases\firmware-v5.json
```

For the phone companion app, which is distributed separately, combine those same
signed artifacts into a single document-picker-friendly bundle:

```powershell
.\.venv\Scripts\python tools\firmware_update\package_kajofw.py `
  releases\firmware-v5.json
```

The resulting `firmware-v5.kajofw` is an uncompressed ZIP container holding
the signed JSON, firmware image, zlib transport, and the same licence and notice
files. The phone importer ignores those extra entries and still checks the
manifest and image. Packaging does not have or need the private release key.
The release builder (option 5 in `kajo.bat`) does this step for you.

### `kajo-update.json`

Packaging also refreshes `releases/kajo-update.json`, a small index that is
attached to each GitHub release under that fixed name. An app finds the newest
firmware without the GitHub API, and so without its rate limit, at:

```
https://github.com/<owner>/<repo>/releases/latest/download/kajo-update.json
```

```json
{
  "format": 1,
  "target": "cyd-esp32",
  "protocol": 3,
  "version_code": 5,
  "version_name": "0.05",
  "tag": "v0.05",
  "bundle": "firmware-v5.kajofw",
  "bundle_size": 1024000,
  "bundle_sha256": "…"
}
```

- `format` changes only if an existing field changes meaning; new fields may be
  added without bumping it, so readers ignore fields they do not know.
- `target` and `protocol` must match what the display reports before anything
  is offered to it.
- `version_code` is compared with the code the display reports. Only a higher one
  is an update.
- The bundle is at `releases/download/<tag>/<bundle>`. Using the tag rather than
  `latest` keeps the index and bundle from the same release even if a newer one
  is published between the two downloads.
- `bundle_size` and `bundle_sha256` let the app reject a truncated or wrong
  download before unpacking it.

None of this is trusted for safety. The display still verifies the ECDSA
signature and refuses lower version codes without physical confirmation, so a
tampered index or bundle can at worst fail to install.

A release's public assets are therefore the Windows updater ZIP, the `.kajofw`
bundle and `kajo-update.json`. Packaging never rewrites the index to describe an
older version than it already does, and `--version-name` supplies the display
name when `include/config.h` has already moved past the release being packaged.
If you also attach a raw `.bin` or the JSON/bin/zlib set, attach `LICENSE`,
`NOTICE`, `THIRD-PARTY-NOTICES.md`, and the `licenses/` texts with it; the raw
files do not contain those notices.

The display restarts automatically five seconds after verification. Passing
`--reboot` requests that restart immediately instead. The public launcher
supplies `--reboot` automatically. The transport
contains independently compressed 4 KiB firmware sectors, allowing a failed
sector or disconnected transfer to resume at the last committed boundary.

Protocol 3 signs the compressed transport size/SHA-256 and the final firmware
size/SHA-256. It requires a 247-byte ATT MTU, sends 240 transport bytes in each
full data packet, checks CRC32 before decompressing each sector, and acknowledges
only after the decompressed sector has been written to the inactive OTA slot.
Signed firmware with an older version code pauses before any image bytes are
accepted. The uploader waits for up to five minutes while the user explicitly
confirms or cancels the downgrade on the CYD display.
