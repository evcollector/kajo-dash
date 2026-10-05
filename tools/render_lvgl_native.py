from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools" / "lvgl_native_preview"
BUILD = SOURCE / "build_contact_sheet"
OUT = ROOT / "preview_output" / "lvgl"
LVGL = ROOT / ".pio" / "libdeps" / "kajo" / "lvgl"
THUMBNAIL_HEADER = ROOT / "include" / "cyd_dash_thumbnails_4bit.h"
W, H = 320, 240

NAMES = [
    "01_cyber_hud",
    "02_dual_gauge",
    "03_simple",
    "04_bar_graph",
    "05_pixel_mono",
    "06_pixel_gauge",
    "07_large_tiles",
    "08_big_readout",
    "09_redline",
    "10_trace",
    "11_minimal_ride",
    "12_efficiency",
    "13_dashboard_hold",
    "13_dashboard_logging_on",
    "13_dashboard_demo",
    "11_demo_mode",
    "13_battery_two_bars",
    "13_battery_one_bar",
    "13_battery_critical",
    "13_battery_critical_blink_off",
    "13_battery_critical_light",
    "13_dashboard_fardriver_fields",
    "13_menu_auto_return",
    "13_sweep_cyber_hud",
    "13_sweep_dual_gauge",
    "13_sweep_simple",
    "13_sweep_bar_graph",
    "13_sweep_pixel_mono",
    "13_sweep_pixel_gauge",
    "13_sweep_large_tiles",
    "13_sweep_big_readout",
    "13_sweep_redline",
    "13_sweep_trace",
    "13_sweep_minimal_ride",
    "13_sweep_efficiency",
    "02_dual_gauge_range_transition",
    "09_redline_range_transition",
    "11_settings_page_1",
    "11_settings_page_2",
    "11_developer_options",
    "11_changelog",
    "11_developer_options_fardriver",
    "11_developer_disable_prompt",
    "11_recovery_hold",
    "11_recovery_reset_prompt",
    "11_developer_prompt",
    "11_developer_enabled",
    "11_developer_already_enabled",
    "11_logging_saved_notice",
    "11_logging_card_ready_notice",
    "11_logging_card_removed_notice",
    "11_logging_card_removed_active_notice",
    "11_logging_error_notice",
    "11_logging_not_ready_notice",
    "11_controller_saved_notice",
    "11_controller_offline_notice",
    "11_controller_restart_notice",
    "11_controller_setup_saved_notice",
    "11_controller_settings",
    "11_controller_settings_fardriver",
    "11_controller_setup",
    "11_controller_setup_connection",
    "11_controller_setup_discovery",
    "11_controller_setup_confirm",
    "11_controller_setup_fardriver_connected",
    "11_controller_setup_fardriver_confirm",
    "12_gradient_example",
    "13_dash_ui_selector",
    "13_dash_ui_selector_full",
    "13_dash_ui_selector_saved",
    "13_dash_ui_selector_page_2",
    "13_dash_ui_selector_page_3",
    "14_dash_ui_colors",
    "14_dash_ui_data",
    "14_dash_ui_data_choice",
    "14_dash_ui_data_applied",
    "14_gradient_custom",
    "15_language_submenu",
    "16_units_submenu",
    "17_vesc_submenu_page_1",
    "18_vesc_submenu_page_2",
    "19_controller_config_uart",
    "19_controller_config_ble",
    "19_controller_config_fardriver",
    "20_connection_uart",
    "20_connection_ble",
    "20_connection_fardriver",
    "20_connection_bluetooth_off",
    "20_power_vesc",
    "20_power_fardriver",
    "21_calibration",
    "21_gauge_ranges",
    "21_gauge_ranges_manual",
    "22_modes_vesc",
    "22_modes_fardriver",
    "23_mode_labels",
    "23_dashboard_ride_mode",
    "27_pack_setup",
    "24_display_submenu",
    "24_display_submenu_auto",
    "14_auto_brightness_prompt",
    "24_auto_brightness_calibrate_dark",
    "24_auto_brightness_calibrate_bright",
    "24_auto_brightness_calibrated",
    "24_auto_brightness_calibration_failed",
    "24_display_submenu_page_2",
    "24_display_info",
    "24_companion_off",
    "24_companion_ready",
    "24_companion_connected",
    "24_display_panel",
    "24_display_panel_gamma",
    "24_auto_return",
    "24_fardriver_ble",
    "24_firmware_update_locked",
    "24_firmware_update_receiving",
    "24_firmware_update_downgrade_confirm",
    "24_firmware_update_verified",
    "24_firmware_update_user_locked",
    "24_firmware_update_user_ready",
    "24_firmware_update_user_receiving",
    "24_firmware_update_user_success",
    "24_firmware_update_user_cancelled",
    "24_touch_test",
    "25_pin_submenu",
    "25_logging_submenu",
    "25_logging_no_card",
    "25_ride_logs",
    "25_ride_logs_recording",
    "25_ride_logs_clearing",
    "25_ride_logs_cleared",
    "25_ride_logs_clear_failed",
    "25_ride_logs_clear_sd",
    "25_replay",
    "25_replay_playing",
    "25_replay_edge",
    "25_replay_regen",
    "25_replay_gap",
    "25_replay_swap",
    "25_replay_summary",
    "25_replay_charts",
    "25_replay_charts_two",
    "25_replay_fields",
    "25_replay_delete",
    "25_replay_four",
    "25_replay_four_low",
    "25_replay_phase",
    "25_replay_two",
    "25_replay_one",
    "25_replay_zoom",
    "25_replay_zoom_max",
    "25_replay_zoom_four",
    "25_replay_no_card",
    "26_reset_submenu",
    "26_reset_confirm_default",
    "26_reset_confirm_full",
    "26_reset_progress",
    "27_battery_submenu",
    "28_first_boot_config",
    "28_vehicle_name_input",
    "28_battery_capacity_input",
    "29_pin_lock",
    "30_config_language",
    "31_config_units",
    "32_config_theme",
    "33_config_complete",
    "36_link_lost",
    "36_bluetooth_off",
    "36_never_connected",
    "36_no_controller",
    "37_fault",
]

DASHBOARD_NAMES = NAMES[:12]


def run(command: list[str]) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def find_executable() -> Path:
    candidates = [
        BUILD / "Release" / "cyd_lvgl_renderer.exe",
        BUILD / "cyd_lvgl_renderer.exe",
    ]
    for path in candidates:
        if path.exists():
            return path
    raise FileNotFoundError("Native LVGL preview executable was not produced")


def build_renderer(reconfigure: bool = False) -> Path:
    if not LVGL.joinpath("lvgl.h").exists():
        raise FileNotFoundError(
            "The PlatformIO LVGL library is missing. Build the kajo environment once, then retry: "
            "pio run -e kajo"
        )
    if shutil.which("cmake") is None:
        raise FileNotFoundError("CMake was not found in PATH")

    if reconfigure and BUILD.exists():
        shutil.rmtree(BUILD)
    BUILD.mkdir(parents=True, exist_ok=True)
    run([
        "cmake",
        "-Wno-dev",
        "-S",
        str(SOURCE),
        "-B",
        str(BUILD),
        "-A",
        "x64",
        f"-DLVGL_DIR={LVGL}",
    ])
    run(["cmake", "--build", str(BUILD), "--config", "Release", "--target", "cyd_lvgl_preview"])
    return find_executable()


def label_font() -> ImageFont.ImageFont:
    candidates = [
        ROOT / "tools" / "fonts" / "rajdhani" / "Rajdhani-SemiBold.ttf",
        Path("C:/Windows/Fonts/consola.ttf"),
    ]
    for path in candidates:
        if path.exists():
            return ImageFont.truetype(str(path), 10)
    return ImageFont.load_default()


def convert_outputs(out: Path, light_out: Path) -> None:
    images: list[tuple[str, Image.Image]] = []
    for name in NAMES:
        ppm = out / f"{name}.ppm"
        if not ppm.exists():
            raise FileNotFoundError(f"Native renderer did not produce {ppm.name}")
        with Image.open(ppm) as source:
            image = source.convert("RGB")
        if image.size != (W, H):
            raise ValueError(f"{ppm.name} has unexpected size {image.size}")
        image.save(out / f"{name}.png")
        images.append((name, image))
        ppm.unlink()

    light_images: list[tuple[str, Image.Image]] = []
    for name in DASHBOARD_NAMES:
        ppm = light_out / f"{name}.ppm"
        if not ppm.exists():
            raise FileNotFoundError(f"Native renderer did not produce light-mode {ppm.name}")
        with Image.open(ppm) as source:
            image = source.convert("RGB")
        if image.size != (W, H):
            raise ValueError(f"{ppm.name} has unexpected size {image.size}")
        light_name = f"{name}_light"
        image.save(out / f"{light_name}.png")
        light_images.append((light_name, image))
        ppm.unlink()

    columns = 6
    label_height = 18
    dashboard_rows = (len(DASHBOARD_NAMES) + 2) // 3
    menu_images = images[len(DASHBOARD_NAMES):]
    menu_rows = (len(menu_images) + columns - 1) // columns
    rows = dashboard_rows + menu_rows
    sheet = Image.new("RGB", (W * columns, (H + label_height) * rows), (25, 25, 25))
    draw = ImageDraw.Draw(sheet)
    font = label_font()

    def paste_cell(column: int, row: int, name: str, image: Image.Image) -> None:
        x = column * W
        y = row * (H + label_height)
        draw.rectangle((x, y, x + W, y + label_height), fill=(15, 18, 20))
        draw.text((x + 5, y + 3), name, fill=(189, 190, 189), font=font)
        sheet.paste(image, (x, y + label_height))

    for index, (name, image) in enumerate(images[:len(DASHBOARD_NAMES)]):
        paste_cell(index % 3, index // 3, f"{name} - DEFAULT", image)
    for index, (name, image) in enumerate(light_images):
        paste_cell(3 + index % 3, index // 3, f"{name.removesuffix('_light')} - LIGHT", image)
    for index, (name, image) in enumerate(menu_images):
        paste_cell(index % columns, dashboard_rows + index // columns, name, image)
    sheet.save(out / "contact_sheet.png")


# The DASH UI selector renders thumbnails baked into
# include/cyd_dash_thumbnails_4bit.h, which this script generates from the
# dashboard captures it has just taken. The renderer that produced those
# captures was compiled against the *previous* header, so its selector shots
# still show the old artwork. Regenerating the header therefore has to be
# followed by a rebuild and a re-render (of the whole set, see below) -- otherwise
# every look change needs the whole script run twice before the selector
# catches up, which is easy to mistake for the change not taking effect.
def refresh_selector_thumbnails(out: Path, light_out: Path) -> None:
    before = THUMBNAIL_HEADER.read_bytes() if THUMBNAIL_HEADER.exists() else b""
    run([sys.executable, str(ROOT / "tools" / "generate_dash_thumbnails.py")])
    if THUMBNAIL_HEADER.read_bytes() == before:
        return
    print("Dashboard thumbnails changed: rebuilding to re-render the selector", flush=True)
    executable = build_renderer()
    # convert_outputs() consumed the first pass's captures and rebuilds the
    # contact sheet from a complete set, so the whole set is rendered again
    # rather than only the selector screens.
    render_all(executable, out, light_out, "en")
    convert_outputs(out, light_out)


def render_all(executable: Path, out: Path, light_out: Path, lang: str) -> None:
    for name in NAMES:
        run([str(executable), str(out), name, f"--lang={lang}"])
    for name in DASHBOARD_NAMES:
        run([str(executable), str(light_out), name, f"--lang={lang}", "--appearance=light"])


def main() -> int:
    parser = argparse.ArgumentParser(description="Render firmware screens through native LVGL")
    parser.add_argument("--reconfigure", action="store_true", help="discard the native CMake build first")
    parser.add_argument("--lang", choices=["en", "fi", "de", "fr", "es", "it"], default="en",
                        help="UI language to render in (checks translated captions for overflow)")
    args = parser.parse_args()

    # English stays the checked-in preview set; other languages render beside it
    out = OUT if args.lang == "en" else OUT.parent / f"lvgl_{args.lang}"

    try:
        executable = build_renderer(args.reconfigure)
        out.mkdir(parents=True, exist_ok=True)
        light_out = out / "light"
        light_out.mkdir(parents=True, exist_ok=True)
        render_all(executable, out, light_out, args.lang)
        convert_outputs(out, light_out)
        if args.lang == "en":
            refresh_selector_thumbnails(out, light_out)
    except (FileNotFoundError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Native LVGL preview failed: {error}", file=sys.stderr)
        return 1

    print(f"Wrote native LVGL previews to: {out}")
    print(f"Open: {out / 'contact_sheet.png'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
