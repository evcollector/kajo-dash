from __future__ import annotations

import argparse
import asyncio
import sys
import time
from pathlib import Path

from bleak import BleakClient, BleakScanner

from protocol import (
    COMMAND_COMMIT_MANIFEST,
    COMMAND_FINISH,
    COMMAND_REBOOT,
    CONTROL_UUID,
    DATA_UUID,
    MAX_DATA_BYTES,
    PROTOCOL_VERSION,
    SERVICE_UUID,
    STATE_ERROR,
    STATE_CANCELLED,
    STATE_CONFIRM_DOWNGRADE,
    STATE_READY_TO_REBOOT,
    STATE_RECEIVING,
    STATUS_UUID,
    begin_commands,
    commit_sector_command,
    data_packet,
    decode_status,
    load_release_manifest,
    signature_commands,
    transport_sectors,
)

COMPANION_SERVICE_UUID = "7c7d7f00-2aa7-4f62-a497-6a9b02d14d00"
COMPANION_CONTROL_UUID = "7c7d7f01-2aa7-4f62-a497-6a9b02d14d00"
COMPANION_RESPONSE_UUID = "7c7d7f02-2aa7-4f62-a497-6a9b02d14d00"
COMPANION_PROTOCOL_VERSION = 2
COMMAND_ENTER_UPDATE_MODE = 8


async def find_device(address: str | None, timeout: float, service_uuid: str = SERVICE_UUID):
    if address:
        device = await BleakScanner.find_device_by_address(address, timeout=timeout)
    else:
        device = await BleakScanner.find_device_by_filter(
            lambda candidate, advertisement: service_uuid.lower() in
            {uuid.lower() for uuid in (advertisement.service_uuids or [])}, timeout=timeout)
    if not device:
        return None
    return device


async def request_update_mode(device) -> None:
    print(f"Connecting to {device.name or 'KAJO-Dash Bluetooth Link'} [{device.address}]")
    async with BleakClient(device, timeout=15.0) as client:
        if client.services.get_service(SERVICE_UUID):
            return
        if not client.services.get_service(COMPANION_SERVICE_UUID):
            raise RuntimeError("connected display exposes neither Bluetooth Link nor the update service")
        # The controller BLE client must finish shutting down before commands
        # are accepted. Match the phone's identity/readiness handshake.
        ready_deadline = time.monotonic() + 8.0
        while True:
            await client.write_gatt_char(COMPANION_CONTROL_UUID, bytes([1]), response=True)
            identity = bytes(await client.read_gatt_char(COMPANION_RESPONSE_UUID))
            if len(identity) >= 48 and identity[:4] == b"KAJC":
                if identity[4] != COMPANION_PROTOCOL_VERSION:
                    raise RuntimeError(f"display uses unsupported Bluetooth Link protocol {identity[4]}")
                if not (identity[6] & 2) or (identity[6] & 1):
                    break
            if time.monotonic() >= ready_deadline:
                raise RuntimeError("display could not prepare Bluetooth Link for update commands")
            await asyncio.sleep(0.2)
        await client.write_gatt_char(
            COMPANION_CONTROL_UUID, bytes([COMMAND_ENTER_UPDATE_MODE]), response=True)
        deadline = time.monotonic() + 3.0
        last = b""
        while time.monotonic() < deadline:
            last = bytes(await client.read_gatt_char(COMPANION_RESPONSE_UUID))
            if (len(last) >= 8 and last[:4] == b"KAJM" and
                    last[4] == COMPANION_PROTOCOL_VERSION and last[5] == 6):
                if last[6] == 1 and last[7] == 1:
                    print("Display accepted the update-mode request.")
                    return
                raise RuntimeError("display rejected the requested Bluetooth mode")
            if len(last) >= 7 and last[:4] == b"KAJE":
                raise RuntimeError(f"display rejected update mode with error {last[6]}")
            await asyncio.sleep(0.08)
        raise RuntimeError(f"display did not acknowledge update mode (last response {last!r})")


async def find_or_prepare_update_device(address: str | None, timeout: float):
    if address:
        entry = await find_device(address, timeout)
    else:
        entry = await find_device(None, 2.0)
        if entry is None:
            entry = await find_device(None, timeout, COMPANION_SERVICE_UUID)
    if entry is None:
        raise RuntimeError("KAJO-Dash display not found; open Settings > Information > Bluetooth Link")

    await request_update_mode(entry)
    # The display acknowledges first, then replaces Bluetooth Link with the signed OTA
    # service. Give Windows time to observe the changed advertisement.
    await asyncio.sleep(0.8)
    update_device = await find_device(None, timeout)
    if update_device is None:
        raise RuntimeError("display accepted update mode but its update service was not found")
    return update_device


async def read_status(client: BleakClient):
    last_error = None
    for _ in range(8):
        try:
            value = bytes(await client.read_gatt_char(STATUS_UUID))
            if len(value) == 28:
                status = decode_status(value)
                if status.protocol_version != PROTOCOL_VERSION:
                    raise RuntimeError(f"device uses unsupported update protocol {status.protocol_version}")
                return status
            last_error = ValueError(f"status must be 28 bytes, received {len(value)}")
        except Exception as exc:
            last_error = exc
        await asyncio.sleep(0.06)
    raise RuntimeError(f"could not read a complete device status: {last_error}")


async def wait_for_status(client: BleakClient, predicate, timeout: float = 8.0):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = await read_status(client)
        if last.state == STATE_CANCELLED:
            raise RuntimeError("firmware upload was cancelled on the display")
        if predicate(last):
            return last
        await asyncio.sleep(0.08)
    raise RuntimeError(f"timed out waiting for device status; last status was {last}")


async def upload(args) -> None:
    release = load_release_manifest(args.manifest, args.firmware)
    transport = release.transport_path.read_bytes()
    sectors = list(transport_sectors(transport))
    device = await find_or_prepare_update_device(args.address, args.scan_timeout)
    print(f"Connecting to {device.name or 'KAJO-Dash Firmware Update'} [{device.address}]")

    async with BleakClient(device, timeout=15.0) as client:
        data_characteristic = client.services.get_characteristic(DATA_UUID)
        if data_characteristic is None:
            raise RuntimeError("connected device does not expose the firmware data characteristic")
        required_write = MAX_DATA_BYTES + 4
        negotiated_write = getattr(data_characteristic, "max_write_without_response_size", 0)
        mtu_wait_deadline = time.monotonic() + 2.0
        while negotiated_write < required_write and time.monotonic() < mtu_wait_deadline:
            await asyncio.sleep(0.05)
            negotiated_write = getattr(data_characteristic, "max_write_without_response_size", negotiated_write)
        if negotiated_write < required_write:
            raise RuntimeError(
                f"display negotiated only a {negotiated_write}-byte BLE write; "
                f"update protocol {PROTOCOL_VERSION} requires {required_write} bytes"
            )
        chunk_size = MAX_DATA_BYTES
        print(
            f"BLE payload {chunk_size} bytes, 4 KiB zlib sectors, "
            f"{args.packet_delay * 1000:g} ms packet pacing"
        )

        status = await read_status(client)
        if status.state == STATE_CANCELLED:
            raise RuntimeError("firmware upload was cancelled on the display")
        if not status.key_configured:
            raise RuntimeError("device has no release public key configured")
        if args.reboot_staged:
            await client.write_gatt_char(CONTROL_UUID, bytes([COMMAND_REBOOT]), response=True)
            print("Requested restart into the already verified staged firmware.")
            await asyncio.sleep(1.0)
            return
        if status.state == STATE_RECEIVING:
            if (status.total_bytes != release.transport_size or status.image_size != release.image_size or
                    status.version_code != release.version_code):
                raise RuntimeError(
                    "device is already receiving a different release "
                    f"(version {status.version_code}, {status.image_size} firmware bytes); "
                    "cancel it on the display before starting this upload"
                )
            print(f"Resuming device transfer at byte {status.expected_offset}")
        else:
            for fragment in begin_commands(release):
                await client.write_gatt_char(CONTROL_UUID, fragment, response=True)
            for fragment in signature_commands(release.signature):
                await client.write_gatt_char(CONTROL_UUID, fragment, response=True)
            await client.write_gatt_char(CONTROL_UUID, bytes([COMMAND_COMMIT_MANIFEST]), response=True)
            status = await wait_for_status(
                client, lambda value: value.state in (STATE_CONFIRM_DOWNGRADE, STATE_RECEIVING, STATE_ERROR))
            if status.state == STATE_CONFIRM_DOWNGRADE:
                print(
                    f"Display confirmation required: signed version {status.version_code} is older than the "
                    "installed firmware. Confirm or cancel the downgrade on the display.",
                    flush=True,
                )
                status = await wait_for_status(
                    client, lambda value: value.state in (STATE_RECEIVING, STATE_ERROR), timeout=300.0)
            if status.state == STATE_ERROR or not status.manifest_accepted:
                raise RuntimeError(f"device rejected signed manifest with error {status.error}")

        offset = status.expected_offset
        if offset > len(transport):
            raise RuntimeError(f"device reported impossible transport offset {offset}")
        sector_by_offset = {sector_offset: sector for sector_offset, sector in sectors}
        if offset != len(transport) and offset not in sector_by_offset:
            raise RuntimeError(f"device resume offset {offset} is not a compressed-sector boundary")
        started = time.monotonic()
        transfer_started_offset = offset
        retry_count = 0
        while offset < len(transport):
            sector = sector_by_offset[offset]
            sector_start = offset
            previous_revision = status.revision
            packet_offset = sector_start
            for position in range(0, len(sector), chunk_size):
                chunk = sector[position : position + chunk_size]
                await client.write_gatt_char(DATA_UUID, data_packet(packet_offset, chunk), response=False)
                packet_offset += len(chunk)
                if args.packet_delay:
                    await asyncio.sleep(args.packet_delay)
            sent_offset = sector_start + len(sector)
            await client.write_gatt_char(
                CONTROL_UUID, commit_sector_command(sector_start, sector), response=True)
            status = await wait_for_status(
                client,
                lambda value: value.revision != previous_revision,
                timeout=5.0,
            )
            if status.state == STATE_ERROR:
                raise RuntimeError(f"device stopped the transfer with error {status.error}")
            if status.expected_offset > len(transport):
                raise RuntimeError(f"device reported impossible offset {status.expected_offset}")
            if status.expected_offset < sent_offset:
                retry_count += 1
                if retry_count > 5:
                    raise RuntimeError(
                        f"device repeatedly stopped at offset {status.expected_offset} with error {status.error}"
                    )
                print(
                    f"\nRetrying from device offset {status.expected_offset} after transport error {status.error}",
                    flush=True,
                )
                offset = status.expected_offset
                continue
            retry_count = 0
            offset = status.expected_offset
            elapsed = max(0.001, time.monotonic() - started)
            percent = offset * 100.0 / len(transport)
            rate = (offset - transfer_started_offset) / elapsed / 1024.0
            print(f"\r{offset:>8} / {len(transport)} compressed bytes  {percent:5.1f}%  {rate:6.1f} KiB/s",
                  end="", flush=True)
        print()

        await client.write_gatt_char(CONTROL_UUID, bytes([COMMAND_FINISH]), response=True)
        status = await wait_for_status(client, lambda value: value.state in (STATE_READY_TO_REBOOT, STATE_ERROR), 20.0)
        if status.state != STATE_READY_TO_REBOOT:
            raise RuntimeError(f"device rejected the completed image with error {status.error}")
        print("Firmware hash, image structure, and signature verified by the display.")
        if args.reboot:
            await client.write_gatt_char(CONTROL_UUID, bytes([COMMAND_REBOOT]), response=True)
            print("Restart requested. The new image must pass its on-device health window.")
        else:
            print("The display will restart automatically five seconds after verification.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Upload a signed firmware release to a KAJO-Dash display")
    parser.add_argument("manifest", type=Path, help="signed release JSON produced by sign_firmware.py")
    parser.add_argument("--firmware", type=Path, help="override the image path recorded in the release JSON")
    parser.add_argument("--address", help="specific Bluetooth address instead of service discovery")
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--packet-delay", type=float, default=0.0, help="seconds between no-response packets")
    parser.add_argument("--reboot", action="store_true", help="restart immediately after device verification")
    parser.add_argument("--reboot-staged", action="store_true",
                        help="restart an image already verified by the display without starting another transfer")
    args = parser.parse_args()
    try:
        asyncio.run(upload(args))
    except KeyboardInterrupt:
        print("\nUpload interrupted; reconnect to resume from the device-reported offset.", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"Upload failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
