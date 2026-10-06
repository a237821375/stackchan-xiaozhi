"""Host tests for the portable asset format, preservation, and mouth states."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_stackchan_assets as assets


def fixture_blob(name=b"font.bin", offset=0):
    # Independent, hand-encoded mmap fixture: one 3-byte resource, 320 x 240.
    payload = struct.pack("<32sIIHH", name, 3, offset, 320, 240) + b"ZZabc"
    return struct.pack("<III", 1, sum(payload) & 0xffff, len(payload)) + payload


class AssetApiTestCase(unittest.TestCase):
    def setUp(self):
        for name in ("pack_assets", "unpack_assets", "replace_emojis", "asset_states",
                     "load_renderer", "validate_gif"):
            self.assertTrue(callable(getattr(assets, name, None)),
                            f"Portable asset API is missing {name}")


class AssetFormatTests(AssetApiTestCase):
    def test_unpack_standard_mmap_fixture_and_flash_padding(self):
        self.assertEqual(assets.unpack_assets(fixture_blob() + b"\xff" * 100),
                         {"font.bin": (b"abc", 320, 240)})

    def test_pack_matches_independent_wire_fixture(self):
        self.assertEqual(assets.pack_assets({"font.bin": (b"abc", 320, 240)}),
                         fixture_blob())

    def test_reject_corrupt_and_truncated_assets(self):
        bad_checksum = bytearray(fixture_blob())
        bad_checksum[-1] ^= 1
        for blob in (b"", fixture_blob()[:-1], bytes(bad_checksum),
                     fixture_blob(offset=100), fixture_blob(name=b"../font.bin")):
            with self.subTest(blob=blob[:20]), self.assertRaises(ValueError):
                assets.unpack_assets(blob)

    def test_reject_names_that_cannot_roundtrip_in_wire_format(self):
        for name in ("../x", "a/b", "a\\b", "x\0y", "x" * 32, ""):
            with self.subTest(name=name), self.assertRaises(ValueError):
                assets.pack_assets({name: (b"abc", 0, 0)})


class PreservationTests(AssetApiTestCase):
    def setUp(self):
        super().setUp()
        self.index = {"version": 1, "srmodels": "srmodels.bin",
                      "text_font": "font.bin", "text_font_meta": {"bundle_id": "noto-v1"},
                      "emoji_collection": [{"name": "neutral", "file": "old.gif"}],
                      "skin": {"dark": {"background_color": "#123456"}},
                      "custom_wake_word": "hello"}
        self.base = {"index.json": (json.dumps(self.index).encode(), 0, 0),
                     "srmodels.bin": (b"wake model", 0, 0),
                     "font.bin": (b"font", 0, 0), "other.dat": (b"extra", 7, 9),
                     "old.gif": (b"old emoji", 320, 240)}
        self.gifs = {key + ".gif": (b"gif", 320, 240)
                     for key, _, _ in assets.asset_states()}

    def test_replace_only_emojis_and_theme_preserves_models_fonts_metadata(self):
        updated = assets.replace_emojis(self.base, self.gifs)
        index = json.loads(updated["index.json"][0])
        self.assertNotIn("old.gif", updated)
        for name in ("srmodels.bin", "font.bin", "other.dat"):
            self.assertEqual(updated[name], self.base[name])
        for key in ("version", "srmodels", "text_font", "text_font_meta", "custom_wake_word"):
            self.assertEqual(index[key], self.index[key])
        self.assertTrue(index["hide_subtitle"])
        self.assertEqual(index["skin"]["dark"]["background_color"], "#000000")
        self.assertEqual(len(index["emoji_collection"]), 41)
        names = {entry["name"] for entry in index["emoji_collection"]}
        self.assertIn("neutral_talk", names)
        self.assertNotIn("sleepy_talk", names)
        self.assertEqual(self.base["old.gif"][0], b"old emoji")

    def test_missing_required_base_resource_and_missing_state_fail(self):
        for missing in ("font.bin", "srmodels.bin"):
            incomplete = dict(self.base)
            del incomplete[missing]
            with self.subTest(missing=missing), self.assertRaises(ValueError):
                assets.replace_emojis(incomplete, self.gifs)
        del self.gifs["happy_talk.gif"]
        with self.assertRaises(ValueError):
            assets.replace_emojis(self.base, self.gifs)

    def test_partition_limit_is_enforced(self):
        with self.assertRaises(ValueError):
            assets.pack_assets({"huge.bin": (bytes(0x800000), 0, 0)})


class RendererTests(AssetApiTestCase):
    def test_idle_mouth_still_and_talking_mouth_moves_for_all_emotions(self):
        renderer = assets.load_renderer(assets.DEFAULT_SOURCE)
        for emotion in renderer.NAMES:
            for talking in (False, True):
                shapes = set()
                for frame in range(0, 75, 3):
                    im = renderer.render(emotion, frame / 75, talking=talking)
                    self.assertEqual(im.size, (320, 240))
                    self.assertEqual(im.getpixel((0, 0)), (0, 0, 0))
                    region = im.convert("L").crop((150, 125, 156, 173)).point(
                        lambda value: 255 if value > 160 else 0)
                    box = region.getbbox()
                    shapes.add(box[3] - box[1])
                with self.subTest(emotion=emotion, talking=talking):
                    if talking and emotion != "sleepy":
                        self.assertGreaterEqual(max(shapes) - min(shapes), 8)
                    else:
                        self.assertEqual(len(shapes), 1)

    def test_encoded_gif_is_opaque_and_infinite_and_has_six_second_loop(self):
        renderer = assets.load_renderer(assets.DEFAULT_SOURCE)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "happy_talk.gif"
            renderer.save_gif("happy", path, talking=True)
            stats = assets.validate_gif(path, talking=True)
            self.assertEqual(stats["duration_ms"], 6000)
            # Pillow can merge identical adjacent frames while keeping durations.
            self.assertGreater(stats["frames"], 1)
            self.assertLessEqual(stats["frames"], 75)


if __name__ == "__main__":
    unittest.main()
