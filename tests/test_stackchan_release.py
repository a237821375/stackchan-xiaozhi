"""Public synthetic IDF/asset inputs for distributable release packages."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

PROJECT_SOURCE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_SOURCE / "tools"))
try:
    import build_stackchan_release as release
except ModuleNotFoundError:
    release = None

EMOTIONS = "neutral happy laughing funny sad angry crying loving embarrassed surprised shocked thinking winking cool relaxed delicious kissy confident sleepy silly confused".split()
STATES = [name for emotion in EMOTIONS for name in (
    (emotion,) if emotion == "sleepy" else (emotion, emotion + "_talk"))]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def image(version="1.2.3", chip=9, app=True):
    header = bytearray(24)
    header[:4] = bytes((0xe9, 1, 2, 0x4f))
    struct.pack_into("<H", header, 12, chip)
    header[23] = 1
    payload = bytearray(256 if app else 4)
    if app:
        struct.pack_into("<I", payload, 0, 0xabcd5432)
        payload[16:48] = version.encode().ljust(32, b"\0")
        payload[48:80] = b"xiaozhi".ljust(32, b"\0")
    raw = header + struct.pack("<II", 0x3c000020, len(payload)) + payload
    checksum = 0xef
    for value in payload:
        checksum ^= value
    raw.extend(b"\0" * (15 - len(raw) % 16))
    raw.append(checksum)
    return bytes(raw) + hashlib.sha256(raw).digest()


def partition_table(asset_offset=0x800000, app_size=0x3f0000):
    entries = [("nvs", 1, 2, 0x9000, 0x4000), ("otadata", 1, 0, 0xd000, 0x2000),
               ("phy_init", 1, 1, 0xf000, 0x1000), ("ota_0", 0, 16, 0x20000, app_size),
               ("ota_1", 0, 17, 0x410000, 0x3f0000), ("assets", 1, 130, asset_offset, 0x800000)]
    raw = b"".join(struct.pack("<HBBII16sI", 0x50aa, kind, subtype, offset, size,
                               name.encode(), 0) for name, kind, subtype, offset, size in entries)
    raw += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(raw).digest()
    return raw.ljust(0xc00, b"\xff")


def bundle(files):
    table = bytearray()
    payload = bytearray()
    for name, (data, width, height) in files.items():
        table.extend(struct.pack("<32sIIHH", name.encode(), len(data), len(payload), width, height))
        payload.extend(b"ZZ" + data)
    data = table + payload
    return struct.pack("<III", len(files), sum(data) & 0xffff, len(data)) + data


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data))


def public_source_fixture(root, *, components=True):
    """A clean checkout's public sources plus synthetic dependency notices.

    Host CI has no downloaded components. These tests must never depend on a
    developer's SDK or managed_components directory.
    """
    source = root / "public-source"
    originals = ["LICENSE", "docs/stackchan-assets-provenance.md", "tools/stackchan_backup.py",
                 "tools/stackchan_install.py", "tools/requirements-install.txt", "docs/stackchan-install.md",
                 "docs/stackchan-calibration.md", "docs/stackchan-hardware-validation.md",
                 "main/boards/m5stack/stackchan-k151/README.md"]
    factory = "main/boards/m5stack/stackchan-k151/factory_upstream/"
    originals += [factory + name for name in ("StackChan_LICENSE", "ftservo/LICENSE",
                                               "smooth_ui_toolkit/LICENSE", "provenance.json")]
    originals += [path.relative_to(PROJECT_SOURCE).as_posix()
                  for path in (PROJECT_SOURCE / "docs/licenses").iterdir() if path.is_file()]
    for name in originals:
        destination = source / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes((PROJECT_SOURCE / name).read_bytes())
    if components:
        notices = source / "managed_components"
        license_path = notices / "espressif__esp-sr/LICENSE"
        license_path.parent.mkdir(parents=True)
        license_path.write_text("Synthetic public ESP-SR notice fixture\n")
        fonts = notices / "78__xiaozhi-fonts"
        fonts.mkdir()
        (fonts / "idf_component.yml").write_text("version: 2.0.0\n")
        write_json(fonts / "manifest.json", {"fonts": ["synthetic-public-font"]})
        write_json(fonts / "CHECKSUMS.json", {})
    return source


def inputs(root):
    build = root / "build"
    assets = root / "custom-assets"
    (build / "bootloader").mkdir(parents=True)
    (build / "partition_table").mkdir()
    assets.mkdir()
    (build / "bootloader/bootloader.bin").write_bytes(image(app=False))
    (build / "xiaozhi.bin").write_bytes(image())
    (build / "partition_table/partition-table.bin").write_bytes(partition_table())
    (build / "ota_data_initial.bin").write_bytes(b"\xff" * 8192)
    config = {"IDF_TARGET": "esp32s3", "BOARD_TYPE_M5STACK_STACKCHAN_K151": True,
              "ESPTOOLPY_FLASHSIZE": "16MB", "ESPTOOLPY_FLASHSIZE_16MB": True,
              "SPIRAM": True, "SPIRAM_MODE_QUAD": True, "SPIRAM_MODE_OCT": False,
              "DISABLE_CLOUD_FIRMWARE_UPGRADE": True,
              "ESPTOOLPY_OCT_FLASH": False, "PARTITION_TABLE_OFFSET": 0x8000}
    write_json(build / "config/sdkconfig.json", config)
    sdk = root / "sdk"
    sdk.mkdir()
    (sdk / "LICENSE").write_text("Synthetic public SDK license fixture\n")
    write_json(build / "project_description.json", {"project_name": "xiaozhi", "project_version": "1.2.3",
               "app_bin": "xiaozhi.bin", "target": "esp32s3", "idf_path": str(sdk)})
    write_json(build / "flasher_args.json", {
        "flash_files": {"0x0": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
                        "0xd000": "ota_data_initial.bin", "0x20000": "xiaozhi.bin",
                        "0x800000": "generated_assets.bin"},
        "flash_settings": {"flash_size": "16MB"}, "extra_esptool_args": {"chip": "esp32s3"}})
    resources = {"font.bin": (b"public-font", 0, 0), "models.bin": (b"public-model", 0, 0)}
    base_index = {"version": 1, "text_font": "font.bin", "srmodels": "models.bin", "emoji_collection": []}
    base = bundle({**resources, "index.json": (json.dumps(base_index).encode(), 0, 0)})
    (build / "generated_assets.bin").write_bytes(base)
    records = []
    for state in STATES:
        gif = b"GIF89a" + struct.pack("<HH", 320, 240) + state.encode()
        resources[state + ".gif"] = (gif, 320, 240)
        records.append({"name": state, "bytes": len(gif), "sha256": digest(gif), "frames": 60, "duration_ms": 6000})
    index = {**base_index, "hide_subtitle": True, "skin": {
        mode: {"background_color": "#000000"} for mode in ("dark", "light")},
        "emoji_collection": [{"name": state, "file": state + ".gif"} for state in STATES]}
    resources["index.json"] = (json.dumps(index).encode(), 0, 0)
    binary = bundle(resources)
    (assets / "assets.bin").write_bytes(binary)
    write_json(assets / "manifest.json", {"gif_count": 41, "asset_bytes": len(binary), "sha256": digest(binary),
               "base_assets_sha256": digest(base), "files": records,
               "preserved_resources": [{"name": name, "bytes": len(value[0]), "sha256": digest(value[0])}
                    for name, value in resources.items() if name in ("font.bin", "models.bin")]})
    return build, assets


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(release, "Portable release package builder is missing")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build, self.assets = inputs(self.root)
        self.output = self.root / "release"
        source = public_source_fixture(self.root)
        root_patch = patch.object(release, "PROJECT_ROOT", source)
        root_patch.start()
        self.addCleanup(root_patch.stop)

    def test_package_manifest_exact_offsets_hashes_and_public_files_only(self):
        (self.build / "private-backup.bin").write_bytes(b"do-not-distribute")
        (self.build / "sdkconfig").write_text("PRIVATE_WIFI_PASSWORD=secret")
        (self.build / "merged-binary.bin").write_bytes(b"do-not-distribute")
        manifest = release.build_package(self.build, self.assets, self.output, "1.2.3")
        self.assertEqual(manifest["schema"], 1)
        self.assertEqual(manifest["board"], "m5stack-core-s3-stackchan")
        self.assertEqual(manifest["hardware"], "K151 ESP32-S3 16MB Quad PSRAM ILI9342C")
        self.assertEqual((manifest["chip"], manifest["flash_size"], manifest["version"]), ("esp32s3", 16777216, "1.2.3"))
        self.assertEqual({key: value["offset"] for key, value in manifest["files"].items()}, {
            "bootloader": 0, "partition_table": 0x8000, "otadata": 0xd000, "app": 0x20000, "assets": 0x800000})
        for info in manifest["files"].values():
            binary = (self.output / info["path"]).read_bytes()
            self.assertEqual((len(binary), digest(binary)), (info["size"], info["sha256"]))
        self.assertEqual(len(manifest["partitions"]), 6)
        self.assertEqual(json.loads((self.output / "manifest.json").read_text()), manifest)
        self.assertTrue((self.output / "tools/stackchan_backup.py").is_file())
        self.assertTrue((self.output / "docs/licenses/NotoSans-OFL.txt").is_file())
        self.assertTrue((self.output / "licenses/factory/provenance.json").is_file())
        self.assertTrue((self.output / "licenses/esp-idf/Apache-2.0.txt").is_file())
        self.assertTrue((self.output / "requirements-install.txt").is_file())
        self.assertTrue((self.output / "README.md").is_file())
        self.assertIn("docs/stackchan-calibration.md", (self.output / "README.md").read_text())
        for name in ("stackchan-install.md", "stackchan-calibration.md", "stackchan-hardware-validation.md"):
            self.assertTrue((self.output / "docs" / name).is_file())
        for path in self.output.rglob("*"):
            self.assertNotIn(path.name, ("sdkconfig", "sdkconfig.json", "private-backup.bin", "merged-binary.bin"))

    def test_deterministic_package_and_checksums_cover_every_public_file(self):
        release.build_package(self.build, self.assets, self.output, "1.2.3")
        second = self.root / "second"
        release.build_package(self.build, self.assets, second, "1.2.3")
        contents = lambda root: {p.relative_to(root).as_posix(): p.read_bytes() for p in root.rglob("*") if p.is_file()}
        self.assertEqual(contents(self.output), contents(second))
        sums = (self.output / "SHA256SUMS").read_text().splitlines()
        listed = {}
        for line in sums:
            sha, name = line.split("  ", 1)
            self.assertEqual(sha, digest((self.output / name).read_bytes()))
            listed[name] = sha
        self.assertEqual(set(listed), set(contents(self.output)) - {"SHA256SUMS"})
        self.assertEqual(list(listed), sorted(listed))

    def test_missing_input_and_nonempty_output_fail_before_copying(self):
        (self.build / "bootloader/bootloader.bin").unlink()
        with self.assertRaises((ValueError, FileNotFoundError)):
            release.build_package(self.build, self.assets, self.output, "1.2.3")
        self.assertFalse(self.output.exists())
        self.output.mkdir()
        sentinel = self.output / "keep.txt"
        sentinel.write_text("keep")
        with self.assertRaises(ValueError):
            release.build_package(self.build, self.assets, self.output, "1.2.3")
        self.assertEqual(sentinel.read_text(), "keep")

    def test_wrong_board_psram_flash_and_chip_fail_closed(self):
        config_path = self.build / "config/sdkconfig.json"
        config = json.loads(config_path.read_text())
        for key, value in (("BOARD_TYPE_M5STACK_STACKCHAN_K151", False), ("SPIRAM_MODE_QUAD", False),
                           ("SPIRAM_MODE_OCT", True), ("ESPTOOLPY_FLASHSIZE", "8MB"),
                           ("IDF_TARGET", "esp32"), ("BOARD_TYPE_M5STACK_CORE_S3", True)):
            write_json(config_path, {**config, key: value})
            with self.subTest(key=key), self.assertRaises(ValueError):
                release.build_package(self.build, self.assets, self.output, "1.2.3")
            self.assertFalse(self.output.exists())
        write_json(config_path, config)
        (self.build / "xiaozhi.bin").write_bytes(image(chip=0))
        with self.assertRaises(ValueError):
            release.build_package(self.build, self.assets, self.output, "1.2.3")

    def test_cloud_overwrite_protection_is_required_before_package_creation(self):
        path = self.build / "config/sdkconfig.json"
        original = json.loads(path.read_text())
        for value in (False, None):
            config = dict(original)
            if value is None:
                config.pop("DISABLE_CLOUD_FIRMWARE_UPGRADE")
            else:
                config["DISABLE_CLOUD_FIRMWARE_UPGRADE"] = value
            write_json(path, config)
            with self.subTest(value=value), self.assertRaises(ValueError):
                release.build_package(self.build, self.assets, self.output, "1.2.3")
            self.assertFalse(self.output.exists())

    def test_flash_encryption_builds_are_rejected_before_package_creation(self):
        path = self.build / "config/sdkconfig.json"
        original = json.loads(path.read_text())
        flags = ("SECURE_FLASH_ENC_ENABLED", "SECURE_FLASH_ENCRYPTION_MODE_RELEASE",
                 "SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT", "SECURE_FLASH_REQUIRE_ALREADY_ENABLED")
        configurations = [{flag: True} for flag in flags]
        configurations += [{"SECURE_FLASH_ENC_ENABLED": True, "SECURE_FLASH_ENCRYPTION_MODE_RELEASE": True},
                           {"SECURE_FLASH_ENC_ENABLED": True, "SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT": True}]
        for index, settings in enumerate(configurations):
            output = self.root / f"encryption-rejected-{index}"
            write_json(path, {**original, **settings})
            with self.subTest(settings=settings), self.assertRaises(ValueError):
                release.build_package(self.build, self.assets, output, "1.2.3")
            with self.subTest(settings=settings):
                self.assertFalse(output.exists())
        write_json(path, {**original, **{flag: False for flag in flags}})
        self.assertEqual(release.build_package(self.build, self.assets, self.output, "1.2.3")["version"], "1.2.3")

    def test_app_version_integrity_and_partition_bounds_are_enforced(self):
        app = self.build / "xiaozhi.bin"
        for data in (image("other"), image()[:-1], b"bad", image()[:-1] + b"!"):
            app.write_bytes(data)
            with self.assertRaises(ValueError):
                release.build_package(self.build, self.assets, self.output, "1.2.3")
        app.write_bytes(image())
        for data in (partition_table(asset_offset=0x700000), partition_table(app_size=16)):
            (self.build / "partition_table/partition-table.bin").write_bytes(data)
            with self.assertRaises(ValueError):
                release.build_package(self.build, self.assets, self.output, "1.2.3")
        with self.assertRaises(ValueError):
            release.build_package(self.build, self.assets, self.output, "../bad")

    def test_custom_assets_metadata_binary_and_base_must_agree(self):
        path = self.assets / "manifest.json"
        original = json.loads(path.read_text())
        changed = json.loads(json.dumps(original))
        changed["files"][0]["sha256"] = "0" * 64
        variants = [{**original, "gif_count": 40}, {**original, "files": original["files"][:-1]},
                    {**original, "sha256": "0" * 64}, {**original, "base_assets_sha256": "0" * 64}, changed]
        for variant in variants:
            write_json(path, variant)
            with self.assertRaises(ValueError):
                release.build_package(self.build, self.assets, self.output, "1.2.3")
        write_json(path, original)
        (self.assets / "assets.bin").write_bytes((self.build / "generated_assets.bin").read_bytes())
        with self.assertRaises(ValueError):
            release.build_package(self.build, self.assets, self.output, "1.2.3")

    def test_input_paths_must_stay_inside_build_directory(self):
        path = self.build / "project_description.json"
        project = json.loads(path.read_text())
        for app in ("../outside.bin", "/tmp/outside.bin", "bootloader/bootloader.bin"):
            write_json(path, {**project, "app_bin": app})
            with self.assertRaises(ValueError):
                release.build_package(self.build, self.assets, self.output, "1.2.3")

    def test_malformed_json_types_are_rejected_with_a_controlled_error(self):
        cases = [(self.assets / "manifest.json", "files", [{"name": []}] * 41),
                 (self.build / "flasher_args.json", "flash_settings", []),
                 (self.build / "flasher_args.json", "extra_esptool_args", None)]
        for path, key, value in cases:
            original = json.loads(path.read_text())
            write_json(path, {**original, key: value})
            with self.subTest(key=key), self.assertRaises(ValueError):
                release.build_package(self.build, self.assets, self.output, "1.2.3")
            self.assertFalse(self.output.exists())
            write_json(path, original)

    def test_optional_public_build_metadata_is_filtered_and_checksummed(self):
        info = {"source_sha": "a" * 40, "run_url": "https://github.com/example/stackchan/actions/runs/123",
                "board": "m5stack/stackchan-k151", "variant": "m5stack-stackchan-k151", "target": "esp32s3",
                "language": "zh-CN", "wake_word": "nihaoxiaozhi", "idf_version": "6.0.1",
                "idf_image": "espressif/idf:v6.0.1@sha256:" + "b" * 64,
                "sha256": {"dependencies.lock": "c" * 64, "build/stackchan-licenses/fonts/manifest.json": "d" * 64},
                "license_notices": "stackchan-licenses/", "font_source_limit": "Public font source disclosure.",
                "artifact_scope": "Local preparation for review; hardware validation is pending.",
                "private_password": "this must never be copied"}
        write_json(self.build / "stackchan-build-info.json", info)
        release.build_package(self.build, self.assets, self.output, "1.2.3")
        public = (self.output / "build-info.json").read_bytes()
        self.assertNotIn(b"private_password", public)
        self.assertNotIn(b"this must never", public)
        self.assertEqual(json.loads(public), {key: value for key, value in info.items() if key != "private_password"})
        self.assertIn(digest(public) + "  build-info.json\n", (self.output / "SHA256SUMS").read_text())

    def test_build_metadata_rejects_local_paths_and_invalid_provenance_values(self):
        path = self.build / "stackchan-build-info.json"
        for info in ({"run_url": "/Users/someone/private/build"}, {"source_sha": "private-token"},
                     {"sha256": {"/Users/private/file": "a" * 64}}, {"target": "esp32"}):
            write_json(path, info)
            with self.assertRaises(ValueError):
                release.build_package(self.build, self.assets, self.output, "1.2.3")
            self.assertFalse(self.output.exists())

    def test_missing_downloaded_component_notices_stop_packaging(self):
        # Isolate only the public source-root input; no mocked reader or result.
        source_root = public_source_fixture(self.root / "missing-notices", components=False)
        with patch.object(release, "PROJECT_ROOT", source_root):
            with self.assertRaises((ValueError, FileNotFoundError)):
                release.build_package(self.build, self.assets, self.output, "1.2.3")
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
