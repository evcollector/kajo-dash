from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import make_demo_video as demo  # noqa: E402

HAS_PILLOW = importlib.util.find_spec("PIL") is not None


def caption(frame: int, heading: str = "H", body: str = "b", **extra) -> dict:
    return {"frame": frame, "heading": heading, "body": body, **extra}


class CaptionSpanTests(unittest.TestCase):
    def test_a_caption_lasts_until_the_next_one(self):
        spans = demo.caption_spans([caption(0, "A"), caption(100, "B")], 300)
        self.assertEqual([(s.start, s.end, s.heading) for s in spans], [(0, 100, "A"), (100, 300, "B")])

    def test_the_last_caption_lasts_to_the_end_of_the_scene(self):
        self.assertEqual(demo.caption_spans([caption(40)], 250)[0].end, 250)

    def test_caption_off_ends_the_caption_and_is_not_a_span(self):
        spans = demo.caption_spans([caption(0), {"frame": 50, "off": True}], 300)
        self.assertEqual([(s.start, s.end) for s in spans], [(0, 50)])

    def test_until_frame_ends_a_caption_before_the_next_one_starts(self):
        spans = demo.caption_spans([caption(0, until_frame=40), caption(100)], 300)
        self.assertEqual([(s.start, s.end) for s in spans], [(0, 40), (100, 300)])

    def test_until_frame_cannot_outlast_the_next_caption_or_the_scene(self):
        spans = demo.caption_spans([caption(0, until_frame=900), caption(100, until_frame=900)], 300)
        self.assertEqual([(s.start, s.end) for s in spans], [(0, 100), (100, 300)])

    def test_a_caption_replaced_on_its_own_frame_is_dropped(self):
        spans = demo.caption_spans([caption(30, "gone"), caption(30, "kept")], 300)
        self.assertEqual([s.heading for s in spans], ["kept"])

    def test_a_caption_off_before_any_caption_does_nothing(self):
        self.assertEqual(demo.caption_spans([{"frame": 0, "off": True}], 100), [])

    def test_a_missing_body_is_empty(self):
        spans = demo.caption_spans([{"frame": 0, "heading": "Only a heading"}], 100)
        self.assertEqual((spans[0].heading, spans[0].body), ("Only a heading", ""))


class FadeTests(unittest.TestCase):
    span = demo.Span(0, 300, "H", "b")

    def test_the_caption_reaches_full_opacity_and_holds(self):
        self.assertEqual(demo.fade_alpha(demo.FADE_IN_FRAMES - 1, self.span), 1.0)
        self.assertEqual(demo.fade_alpha(150, self.span), 1.0)
        self.assertEqual(demo.fade_alpha(300 - demo.FADE_OUT_FRAMES, self.span), 1.0)

    def test_it_eases_in_from_the_first_frame_and_out_before_the_end(self):
        self.assertTrue(0.0 < demo.fade_alpha(0, self.span) < 0.5)
        self.assertTrue(0.0 < demo.fade_alpha(299, self.span) < 0.5)

    def test_opacity_stays_within_zero_and_one(self):
        values = [demo.fade_alpha(frame, self.span) for frame in range(300)]
        self.assertTrue(all(0.0 <= value <= 1.0 for value in values))

    def test_a_caption_shorter_than_its_fades_never_reaches_full_opacity(self):
        short = demo.Span(0, 6, "H", "b")
        self.assertLess(max(demo.fade_alpha(frame, short) for frame in range(6)), 1.0)


class RailTimelineTests(unittest.TestCase):
    def test_every_frame_is_covered_exactly_once_and_in_order(self):
        spans = demo.caption_spans([caption(20), caption(120), {"frame": 200, "off": True}], 400)
        runs = demo.rail_timeline(spans, 400)
        cursor = 0
        for first, count, _index, _alpha in runs:
            self.assertEqual(first, cursor)
            self.assertGreater(count, 0)
            cursor += count
        self.assertEqual(cursor, 400)

    def test_no_captions_is_one_empty_run(self):
        self.assertEqual(demo.rail_timeline([], 90), [(0, 90, None, 0.0)])

    def test_held_frames_collapse_into_one_run(self):
        runs = demo.rail_timeline([demo.Span(0, 300, "H", "b")], 300)
        holds = [run for run in runs if run[3] == 1.0]
        self.assertEqual(len(holds), 1)
        self.assertGreater(holds[0][1], 250)
        self.assertLess(len(runs), 20, "only the fading frames should need a run each")

    def test_frames_before_and_after_a_caption_are_empty_runs(self):
        runs = demo.rail_timeline([demo.Span(30, 90, "H", "b")], 120)
        self.assertEqual(runs[0], (0, 30, None, 0.0))
        self.assertEqual(runs[-1], (90, 30, None, 0.0))


class FfconcatTests(unittest.TestCase):
    def test_each_image_is_held_for_its_frame_count_and_the_last_is_repeated(self):
        text = demo.ffconcat_text([("a.png", 30), ("b.png", 90)], 60)
        self.assertEqual(
            text.splitlines(),
            ["ffconcat version 1.0", "file 'a.png'", "duration 0.500000", "file 'b.png'", "duration 1.500000", "file 'b.png'"],
        )

    def test_the_durations_add_up_to_the_scene(self):
        entries = [("a.png", 7), ("b.png", 288), ("c.png", 5)]
        durations = [float(line.split()[1]) for line in demo.ffconcat_text(entries, 60).splitlines() if line.startswith("duration")]
        self.assertAlmostEqual(sum(durations), 300 / 60, places=5)

    def test_nothing_in_nothing_out(self):
        self.assertEqual(demo.ffconcat_text([], 60), "ffconcat version 1.0\n")


class CutTests(unittest.TestCase):
    def test_every_chapter_in_the_cut_has_a_scene(self):
        chapters = demo.load_cut()
        self.assertTrue(chapters)
        for name in chapters:
            self.assertTrue(demo.scene_path(name).exists(), name)

    def test_every_scene_is_in_the_cut_or_deliberately_not(self):
        scenes = {path.stem for path in demo.SCENES.glob("*.scn")}
        self.assertTrue(set(demo.load_cut()) <= scenes)

    def test_every_chapter_names_itself(self):
        for name in demo.load_cut():
            lines = demo.scene_path(name).read_text(encoding="utf-8").splitlines()
            self.assertTrue(any(line.startswith("title ") for line in lines), f"{name} has no title for its card")

    def test_an_unknown_scene_lists_the_known_ones(self):
        with self.assertRaises(FileNotFoundError) as caught:
            demo.scene_path("no-such-chapter")
        self.assertIn("themes", str(caught.exception))

    def test_a_malformed_cut_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cut.json"
            path.write_text(json.dumps({"chapters": "themes"}), encoding="utf-8")
            with self.assertRaises(ValueError):
                demo.load_cut(path)


class FilterTests(unittest.TestCase):
    def test_the_screen_and_rail_sit_where_the_artwork_expects_them(self):
        graph = demo.compose_graph(40.0, with_card=True)
        self.assertIn(f"overlay={demo.SCREEN_AT[0]}:{demo.SCREEN_AT[1]}", graph)
        self.assertIn(f"overlay={demo.RAIL[0]}:{demo.RAIL[1]}", graph)
        self.assertIn("concat=n=2", graph)

    def test_screen_offsets_are_even_so_chroma_blocks_stay_inside_a_display_pixel(self):
        self.assertEqual((demo.SCREEN_AT[0] % 4, demo.SCREEN_AT[1] % 4), (0, 0))
        self.assertEqual(demo.SCALE % 2, 0)

    def test_the_chapter_dips_to_black_at_both_ends_and_without_a_card_has_no_concat(self):
        graph = demo.compose_graph(10.0, with_card=False)
        self.assertIn("fade=t=in", graph)
        self.assertIn("fade=t=out:st=9.500", graph)
        self.assertNotIn("concat", graph)

    def test_colour_is_converted_and_tagged_as_bt709(self):
        self.assertIn("out_color_matrix=bt709", demo.TO_YUV)
        self.assertIn("color_primaries=bt709", demo.TO_YUV)
        arguments = demo.x264_arguments(14, "slow")
        self.assertEqual(arguments[arguments.index("-colorspace") + 1], "bt709")
        self.assertEqual(arguments[arguments.index("-crf") + 1], "14")

    def test_the_encode_stays_within_level_4_2_so_older_decoders_play_it(self):
        arguments = demo.x264_arguments(14, "slow")
        self.assertEqual(arguments[arguments.index("-level:v") + 1], "4.2")

    def test_the_gif_uses_a_small_undithered_palette(self):
        graph = demo.gif_filter(20, 2)
        self.assertIn("scale=640:480", graph)
        self.assertIn("dither=none", graph)
        self.assertIn("fps=20", graph)


class ToolCheckTests(unittest.TestCase):
    LISTING = " V....D libx264              libx264 H.264\n V....D libx264rgb           libx264 H.264 RGB\n"

    def test_an_ffmpeg_with_the_encoders_passes(self):
        done = mock.Mock(stdout=self.LISTING)
        with mock.patch.object(demo.subprocess, "run", return_value=done):
            demo.require_encoders("ffmpeg", ("libx264", "libx264rgb"))

    def test_a_missing_encoder_is_named_before_any_render_starts(self):
        done = mock.Mock(stdout=" V....D libx264              libx264 H.264\n")
        with mock.patch.object(demo.subprocess, "run", return_value=done):
            with self.assertRaises(FileNotFoundError) as caught:
                demo.require_encoders("ffmpeg", ("libx264", "libx264rgb"))
        self.assertIn("libx264rgb", str(caught.exception))
        self.assertNotIn("no libx264,", str(caught.exception))

    def test_an_encoder_whose_name_only_prefixes_another_is_not_a_match(self):
        done = mock.Mock(stdout=" V....D libx264rgb           libx264 H.264 RGB\n")
        with mock.patch.object(demo.subprocess, "run", return_value=done):
            with self.assertRaises(FileNotFoundError):
                demo.require_encoders("ffmpeg", ("libx264",))


@unittest.skipUnless(HAS_PILLOW, "Pillow is not installed")
class ArtworkTests(unittest.TestCase):
    def test_the_fonts_and_logos_the_artwork_uses_exist(self):
        for weight in ("Bold", "SemiBold"):
            self.assertTrue((demo.FONTS / f"Rajdhani-{weight}.ttf").exists(), weight)
        for name in ("kajo-lockup-horizontal.png", "kajo-lockup-stacked.png"):
            self.assertTrue((demo.BRAND / name).exists(), name)

    def test_the_background_and_card_fill_the_canvas(self):
        self.assertEqual(demo.render_background().size, demo.CANVAS)
        self.assertEqual(demo.render_card(1, "Twelve themes", "One stream.").size, demo.CANVAS)

    def test_the_rail_layers_match_the_rail_and_the_caption_clears_the_title_block(self):
        static, top, limit = demo.render_rail_static(1, "A long chapter title that wraps", "A subtitle that also wraps over lines")
        self.assertEqual(static.size, (demo.RAIL[2], demo.RAIL[3]))
        self.assertGreater(top, 300)
        self.assertGreater(limit, top + 200, "the caption area is too small to be useful")
        layer = demo.render_caption(demo.Span(0, 100, "Heading", "Body text that wraps onto a second line."), top, limit)
        self.assertEqual(layer.size, static.size)
        self.assertIsNotNone(layer.getchannel("A").getbbox(), "the caption drew nothing")

    def test_a_caption_too_long_for_the_rail_is_an_error_not_an_overlap(self):
        _, top, limit = demo.render_rail_static(1, "Title", "Subtitle")
        with self.assertRaises(ValueError) as caught:
            demo.render_caption(demo.Span(0, 100, "Heading", " ".join(["word"] * 60)), top, limit)
        self.assertIn("too long", str(caught.exception))

    def test_every_caption_in_the_scenes_fits_the_rail(self):
        import re

        for name in demo.load_cut():
            text = demo.scene_path(name).read_text(encoding="utf-8")
            title = re.search(r'^title "(.*)"', text, re.M).group(1)
            subtitle = re.search(r'^subtitle "(.*)"', text, re.M)
            _, top, limit = demo.render_rail_static(1, title, subtitle.group(1) if subtitle else "")
            captions = [line for line in text.splitlines() if line.startswith("caption ")]
            self.assertTrue(captions, f"{name} has no captions")
            for line in captions:
                strings = re.findall(r'"((?:[^"\\]|\\.)*)"', line)
                span = demo.Span(0, 1, strings[0], strings[1] if len(strings) > 1 else "")
                demo.render_caption(span, top, limit)  # raises when it does not fit

    def test_fading_a_layer_scales_its_alpha_without_touching_the_original(self):
        from PIL import Image

        layer = Image.new("RGBA", (4, 4), (255, 255, 255, 200))
        faded = demo.with_alpha(layer, 0.5)
        self.assertEqual(faded.getpixel((0, 0))[3], 100)
        self.assertEqual(layer.getpixel((0, 0))[3], 200)
        self.assertIs(demo.with_alpha(layer, 1.0), layer)

    def test_text_wrapping_respects_the_width(self):
        from PIL import Image, ImageDraw

        draw = ImageDraw.Draw(Image.new("RGB", (10, 10)))
        face = demo.font("SemiBold", 34)
        lines = demo.wrap(draw, "Open the list and pick any ride on the card. Demo rides are marked.", face, 460)
        self.assertGreater(len(lines), 1)
        self.assertTrue(all(draw.textlength(line, font=face) <= 460 for line in lines))


if __name__ == "__main__":
    unittest.main()
