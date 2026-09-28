# Releases

Every published KAJO-Dash firmware release, oldest first.

The version being worked on is the one in `include/config.h`. Option 5 in
`kajo.bat` packages it: **T** makes a test package for your own boards, which
shows as `0.01-test` on the display and changes nothing here; **R** releases it.
A release adds its row to this table in the commit it tags (`v0.01`), then moves
`include/config.h` on to the next version. "Building a release" in
[docs/development.md](docs/development.md) has the details.

The SHA-256 is that of the firmware image, `firmware-v<code>.bin`, the same
value as the `sha256` in its signed manifest. It identifies a firmware file
later: `certutil -hashfile firmware-v1.bin SHA256` on Windows.

Keep the table at the end of this file: the release builder appends to it.

| Version | Code | Date | Firmware SHA-256 |
| --- | --- | --- | --- |
