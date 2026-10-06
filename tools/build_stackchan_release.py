"""Build an allowlisted, verifiable StackChan installer directory from IDF output.

This tool uses only Python's standard library. It never reads a device backup or
copies a build directory wholesale. Input/configuration checks finish before any
package files are written. Generated configuration, NVS, logs, credentials and
merged flash images are not package inputs. Existing licensing disclosures are
retained; packaging does not imply permission to publish embedded resources.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from stackchan_backup import partitions_from_flash

PROJECT_ROOT = Path(__file__).resolve().parents[1]
FLASH_SIZE = 16777216
_EMOTIONS = "neutral happy laughing funny sad angry crying loving embarrassed surprised shocked thinking winking cool relaxed delicious kissy confident sleepy silly confused".split()
_STATES = [name for emotion in _EMOTIONS for name in (
    (emotion,) if emotion == "sleepy" else (emotion, emotion + "_talk"))]
_OFFSETS = dict(bootloader=0, partition_table=0x8000, otadata=0xd000, app=0x20000, assets=0x800000)
_DESTINATIONS = dict(bootloader="bootloader.bin", partition_table="partition-table.bin",
                     otadata="ota_data_initial.bin", app="app.bin", assets="assets.bin")


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (ValueError, UnicodeDecodeError):
        raise ValueError("Invalid package input JSON") from None
    if not isinstance(value, dict):
        raise ValueError("Package input JSON must be an object")
    return value


def _read(base: Path, relative: str) -> bytes:
    path = base / relative
    if Path(relative).is_absolute() or not path.resolve().is_relative_to(base.resolve()) or path.is_symlink():
        raise ValueError("Package input path escapes its source directory")
    return path.read_bytes()


def _c_string(raw: bytes) -> str:
    if b"\0" not in raw:
        raise ValueError("ESP application description is not terminated")
    try:
        return raw.split(b"\0", 1)[0].decode("utf-8")
    except UnicodeDecodeError:
        raise ValueError("Invalid ESP application description") from None


def validate_image(data: bytes, version: str | None = None) -> dict:
    """Validate a complete unsigned S3/16MB ESP image, optionally its app version.

    Standard IDF images with a SHA256 digest are supported. Secure boot signature
    trailers, padding outside the image, and images lacking the digest are
    rejected. The application descriptor begins in the first segment at byte 32.
    """
    if (len(data) < 32 or data[0] != 0xe9 or not 1 <= data[1] <= 16 or
            data[2] not in (0, 1, 2, 3) or data[3] >> 4 != 4 or
            struct.unpack_from("<H", data, 12)[0] != 9 or data[23] != 1):
        raise ValueError("Image is not a supported ESP32-S3 16MB firmware image")
    cursor = 24
    checksum = 0xef
    first_segment = b""
    for segment in range(data[1]):
        if cursor + 8 > len(data):
            raise ValueError("ESP image segment header is truncated")
        _, size = struct.unpack_from("<II", data, cursor)
        cursor += 8
        if not size or size % 4 or cursor + size > len(data):
            raise ValueError("ESP image segment is invalid or truncated")
        payload = data[cursor:cursor + size]
        if segment == 0:
            first_segment = payload
        for byte in payload:
            checksum ^= byte
        cursor += size
    end = (cursor // 16 + 1) * 16
    if end + 32 != len(data):
        raise ValueError("ESP image is truncated or has unsupported trailing data")
    if any(data[cursor:end - 1]) or data[end - 1] != checksum:
        raise ValueError("ESP image checksum or padding mismatch")
    if hashlib.sha256(data[:end]).digest() != data[end:]:
        raise ValueError("ESP image SHA256 mismatch")
    app_version = None
    if len(first_segment) >= 256 and struct.unpack_from("<I", first_segment)[0] == 0xabcd5432:
        app_version = _c_string(first_segment[16:48])
    if version is not None and (app_version is None or app_version != version):
        raise ValueError("ESP application version does not match the release version")
    return dict(chip="esp32s3", flash_size=FLASH_SIZE, version=app_version)


def _configuration(build: Path, version: str) -> str:
    config = _json(build / "config/sdkconfig.json")
    required = {"IDF_TARGET": "esp32s3", "BOARD_TYPE_M5STACK_STACKCHAN_K151": True,
                "ESPTOOLPY_FLASHSIZE": "16MB", "ESPTOOLPY_FLASHSIZE_16MB": True,
                "SPIRAM": True, "SPIRAM_MODE_QUAD": True, "PARTITION_TABLE_OFFSET": 0x8000}
    if any(config.get(key) != value for key, value in required.items()):
        raise ValueError("Build configuration does not target K151 S3/16MB/Quad PSRAM")
    if config.get("DISABLE_CLOUD_FIRMWARE_UPGRADE") is not True:
        raise ValueError("K151 release must protect custom firmware from cloud overwrite")
    if any(config.get(key) for key in ("SECURE_FLASH_ENC_ENABLED", "SECURE_FLASH_ENCRYPTION_MODE_RELEASE",
                                       "SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT", "SECURE_FLASH_REQUIRE_ALREADY_ENABLED")):
        raise ValueError("Flash encryption builds are unsupported by the portable installer")
    if any(config.get(key) for key in ("SPIRAM_MODE_OCT", "ESPTOOLPY_OCT_FLASH", "SECURE_BOOT", "SECURE_BOOT_V2_ENABLED")):
        raise ValueError("Build uses unsupported flash, PSRAM, or secure boot configuration")
    if any(value is True and key.startswith("BOARD_TYPE_") and key != "BOARD_TYPE_M5STACK_STACKCHAN_K151"
           for key, value in config.items()):
        raise ValueError("Build has conflicting board selections")
    project = _json(build / "project_description.json")
    if project.get("target") != "esp32s3" or project.get("project_version") != version:
        raise ValueError("Build target or version does not match the release")
    app_name = project.get("app_bin")
    if (not isinstance(app_name, str) or not re.fullmatch(r"[A-Za-z0-9_-]+\.bin", app_name) or
            app_name in ("bootloader.bin", "partition-table.bin", "assets.bin", "merged-binary.bin", "ota_data_initial.bin")):
        raise ValueError("Invalid application input filename")
    flasher = _json(build / "flasher_args.json")
    arguments, settings = flasher.get("extra_esptool_args"), flasher.get("flash_settings")
    if (not isinstance(arguments, dict) or not isinstance(settings, dict) or
            arguments.get("chip") != "esp32s3" or settings.get("flash_size") != "16MB"):
        raise ValueError("Flasher settings do not target ESP32-S3 16MB")
    expected = {0: "bootloader/bootloader.bin", 0x8000: "partition_table/partition-table.bin",
                0xd000: "ota_data_initial.bin", 0x20000: app_name, 0x800000: "generated_assets.bin"}
    try:
        actual = {int(offset, 0): name for offset, name in flasher["flash_files"].items()}
    except (KeyError, TypeError, ValueError, AttributeError):
        raise ValueError("Invalid flasher file mapping") from None
    if actual != expected or len(flasher["flash_files"]) != len(expected):
        raise ValueError("Flasher files or offsets differ from the release layout")
    return app_name


def _resources(data: bytes) -> dict[str, tuple[bytes, int, int]]:
    # The small read-only subset of the firmware mmap format needed here.
    if len(data) < 12:
        raise ValueError("Assets bundle is truncated")
    count, checksum, length = struct.unpack_from("<III", data)
    if not 0 < count < 2000 or not 44 * count <= length == len(data) - 12:
        raise ValueError("Invalid assets bundle length")
    payload = data[12:]
    if sum(payload) & 65535 != checksum:
        raise ValueError("Assets bundle checksum mismatch")
    resources, ranges = {}, []
    for index in range(count):
        raw, size, offset, width, height = struct.unpack_from("<32sIIHH", payload, index * 44)
        name = _c_string(raw)
        if not name or "/" in name or "\\" in name or name in (".", "..") or name in resources:
            raise ValueError("Invalid assets resource name")
        start = 44 * count + offset
        end = start + size + 2
        if end > len(payload) or payload[start:start + 2] != b"ZZ":
            raise ValueError("Invalid assets resource range")
        ranges.append((start, end))
        resources[name] = (payload[start + 2:end], width, height)
    cursor = 44 * count
    for start, end in sorted(ranges):
        if start != cursor:
            raise ValueError("Assets resources overlap or leave gaps")
        cursor = end
    if cursor != len(payload):
        raise ValueError("Assets payload has unindexed data")
    return resources


def _asset_metadata(build: Path, assets: Path, data: bytes) -> dict:
    metadata = _json(assets / "manifest.json")
    base = _read(build, "generated_assets.bin")
    if (len(data) >= 0x800000 or data == base or metadata.get("gif_count") != 41 or
            metadata.get("asset_bytes") != len(data) or metadata.get("sha256") != _sha(data) or
            metadata.get("base_assets_sha256") != _sha(base)):
        raise ValueError("Custom assets or source build hashes do not match")
    records = metadata.get("files")
    if not isinstance(records, list) or len(records) != 41 or any(not isinstance(r, dict) for r in records):
        raise ValueError("Custom assets need 41 animation metadata records")
    if any(not isinstance(r.get("name"), str) for r in records) or {r["name"] for r in records} != set(_STATES):
        raise ValueError("Custom assets animation states are incomplete")
    resources = _resources(data)
    base_resources = _resources(base)
    try:
        index = json.loads(resources["index.json"][0])
        base_index = json.loads(base_resources["index.json"][0])
    except (KeyError, ValueError, UnicodeDecodeError):
        raise ValueError("Assets index is missing or invalid") from None
    expected_emojis = [{"name": state, "file": state + ".gif"} for state in _STATES]
    if (not isinstance(index, dict) or not isinstance(base_index, dict) or index.get("version", 1) != 1 or
            index.get("emoji_collection") != expected_emojis or index.get("hide_subtitle") is not True):
        raise ValueError("Assets index does not select all custom idle/talking states")
    if any(index.get("skin", {}).get(mode, {}).get("background_color") != "#000000" for mode in ("dark", "light")):
        raise ValueError("Custom assets theme must use opaque black backgrounds")
    for field in ("text_font", "srmodels"):
        name = base_index.get(field)
        if name is not None and (index.get(field) != name or name not in resources or
                                 resources[name] != base_resources.get(name)):
            raise ValueError("Custom assets did not preserve build fonts or wake models")
    clean_records = []
    for record in sorted(records, key=lambda r: r["name"]):
        resource = resources.get(record["name"] + ".gif")
        if resource is None:
            raise ValueError("Custom animation resource is missing")
        gif, width, height = resource
        if (len(gif) < 10 or gif[:6] not in (b"GIF89a", b"GIF87a") or
                struct.unpack_from("<HH", gif, 6) != (320, 240) or (width, height) != (320, 240) or
                record.get("sha256") != _sha(gif) or record.get("bytes") != len(gif) or
                record.get("duration_ms") != 6000 or type(record.get("frames")) is not int or record["frames"] < 1):
            raise ValueError("Custom animation metadata does not match its resource")
        clean_records.append({key: record[key] for key in ("name", "bytes", "sha256", "frames", "duration_ms")})
    preserved = []
    old_emojis = {entry["file"] for entry in base_index.get("emoji_collection", [])}
    for name, resource in sorted(base_resources.items()):
        if name == "index.json" or name in old_emojis:
            continue
        if resources.get(name) != resource:
            raise ValueError("Custom assets changed a preserved build resource")
        preserved.append(dict(name=name, bytes=len(resource[0]), sha256=_sha(resource[0])))
    return dict(gif_count=41, asset_bytes=len(data), sha256=_sha(data), base_assets_sha256=_sha(base),
                files=clean_records, preserved_resources=preserved)


def _notices(build: Path) -> dict[str, bytes]:
    root = PROJECT_ROOT
    factory = "main/boards/m5stack/stackchan-k151/factory_upstream/"
    sources = {
        "licenses/project-MIT.txt": root / "LICENSE",
        "licenses/factory/StackChan-MIT.txt": root / (factory + "StackChan_LICENSE"),
        "licenses/factory/FTServo-MIT.txt": root / (factory + "ftservo/LICENSE"),
        "licenses/factory/smooth-ui-toolkit-MIT.txt": root / (factory + "smooth_ui_toolkit/LICENSE"),
        "licenses/factory/provenance.json": root / (factory + "provenance.json"),
        "docs/stackchan-assets-provenance.md": root / "docs/stackchan-assets-provenance.md",
        "tools/stackchan_backup.py": root / "tools/stackchan_backup.py",
        "tools/build_stackchan_release.py": Path(__file__),
        "tools/stackchan_install.py": root / "tools/stackchan_install.py",
        "requirements-install.txt": root / "tools/requirements-install.txt",
        "docs/stackchan-install.md": root / "docs/stackchan-install.md",
        "docs/stackchan-calibration.md": root / "docs/stackchan-calibration.md",
        "docs/stackchan-hardware-validation.md": root / "docs/stackchan-hardware-validation.md",
        "main/boards/m5stack/stackchan-k151/README.md": root / "main/boards/m5stack/stackchan-k151/README.md",
    }
    for optional in ("docs/stackchan-history/MOTION-20261001.md",):
        if (root / optional).is_file():
            sources[optional] = root / optional
    for path in sorted((root / "docs/licenses").glob("*")):
        if path.is_file() and not path.is_symlink():
            sources["docs/licenses/" + path.name] = path
    notices = _json(root / "docs/licenses/upstream-license-sources.json")
    for notice in notices["files"]:
        if _sha(_read(root / "docs/licenses", notice["file"])) != notice["sha256"]:
            raise ValueError("Checked-in upstream license hash mismatch")
    project = _json(build / "project_description.json")
    idf_path = project.get("idf_path")
    if not isinstance(idf_path, str) or not idf_path:
        raise ValueError("Build does not record its SDK license source")
    sources["licenses/esp-idf/Apache-2.0.txt"] = Path(idf_path) / "LICENSE"
    components = root / "managed_components"
    for relative in ("espressif__esp-sr/LICENSE", "78__xiaozhi-fonts/idf_component.yml",
                     "78__xiaozhi-fonts/manifest.json", "78__xiaozhi-fonts/CHECKSUMS.json"):
        sources["licenses/components/" + relative] = components / relative
    for path in sorted(components.rglob("*")):
        if path.is_file() and not path.is_symlink() and (
                path.name.lower().startswith(("license", "copying", "notice")) or path.name == "idf_component.yml"):
            sources["licenses/components/" + path.relative_to(components).as_posix()] = path
    result = {}
    for destination, source in sorted(sources.items()):
        if source.is_symlink():
            raise ValueError("License/tool source must not be a symlink")
        result[destination] = source.read_bytes()
    # The entry point is the same installation guide, with links adjusted for
    # its location at the package root. The source guide remains intact in docs.
    readme = result["docs/stackchan-install.md"].decode("utf-8")
    for name in ("stackchan-calibration.md", "stackchan-hardware-validation.md"):
        readme = readme.replace("(" + name + ")", "(docs/" + name + ")")
    result["README.md"] = readme.replace("(../README.md)", "(README.md)").encode("utf-8")
    return result


def _build_info(build: Path) -> dict | None:
    """Keep only bounded public CI provenance; never export arbitrary fields."""
    path = build / "stackchan-build-info.json"
    if not path.exists():
        return None
    if path.is_symlink() or path.stat().st_size > 1024 * 1024:
        raise ValueError("Build provenance source is invalid or too large")
    info = _json(path)
    patterns = {
        "source_sha": r"[0-9a-f]{40}(?:[0-9a-f]{24})?",
        "run_url": r"https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/actions/runs/[0-9]+",
        "board": r"m5stack(?:/stackchan-k151|-stackchan-k151|-core-s3-stackchan)",
        "variant": r"m5stack-stackchan-k151",
        "target": r"esp32s3",
        "language": r"[a-z]{2}-[A-Z]{2}",
        "wake_word": r"[a-z0-9_]{1,64}",
        "idf_version": r"v?6\.[0-9]+\.[0-9]+(?:[-+.a-zA-Z0-9]+)?",
        "idf_image": r"espressif/idf:v6\.[0-9]+\.[0-9]+@sha256:[0-9a-f]{64}",
        "license_notices": r"stackchan-licenses/",
    }
    public = {}
    for key, pattern in patterns.items():
        if key not in info:
            continue
        value = info[key]
        if not isinstance(value, str) or len(value) > 256 or not re.fullmatch(pattern, value):
            raise ValueError("Build provenance has an invalid public identifier")
        public[key] = value
    for key in ("font_source_limit", "artifact_scope"):
        if key not in info:
            continue
        value = info[key]
        if (not isinstance(value, str) or not 1 <= len(value) <= 1024 or
                any(ord(char) < 32 or ord(char) > 126 for char in value) or
                re.search(r"/Users/|/home/|/private/|/tmp/|[A-Za-z]:\\|file://|(?:password|token|secret)\s*[=:]", value, re.I)):
            raise ValueError("Build provenance disclosure is invalid or contains private data")
        public[key] = value
    if "sha256" in info:
        hashes = info["sha256"]
        known = {"dependencies.lock", "tools/requirements-stackchan.txt", "tools/build_stackchan_assets.py",
                 "tools/stackchan-face-sources/factory-style-talking-20260929/generate.py",
                 "scripts/build_default_assets.py", "sdkconfig", "build/xiaozhi.bin",
                 "build/generated_assets.bin", "build/stackchan-assets/assets.bin", "build/stackchan-assets/manifest.json"}
        if not isinstance(hashes, dict) or len(hashes) > 2000:
            raise ValueError("Build provenance hashes are invalid")
        public_hashes = {}
        for name, sha in hashes.items():
            notice = (isinstance(name, str) and name.startswith("build/stackchan-licenses/") and
                      all(re.fullmatch(r"[A-Za-z0-9_.+-]+", part) and part not in (".", "..")
                          for part in name.split("/")))
            if (name not in known and not notice or not isinstance(sha, str) or
                    not re.fullmatch(r"[0-9a-f]{64}", sha)):
                raise ValueError("Build provenance contains an unknown source path or invalid hash")
            public_hashes[name] = sha
        public["sha256"] = public_hashes
    return public


def build_package(build_dir: Path, assets_dir: Path, output_dir: Path, version: str) -> dict:
    """Validate inputs, create a deterministic public directory, return manifest."""
    build, assets, output = Path(build_dir).resolve(), Path(assets_dir).resolve(), Path(output_dir)
    if not isinstance(version, str) or not re.fullmatch(r"[0-9][A-Za-z0-9._+-]{0,30}", version):
        raise ValueError("Release version must be a safe, nonempty application version")
    if output.is_symlink() or output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError("Package output directory must be new or empty")
    if output.resolve() in (build, assets):
        raise ValueError("Package output cannot replace its input directory")
    app_name = _configuration(build, version)
    binaries = dict(bootloader=_read(build, "bootloader/bootloader.bin"),
                    partition_table=_read(build, "partition_table/partition-table.bin"),
                    otadata=_read(build, "ota_data_initial.bin"), app=_read(build, app_name),
                    assets=_read(assets, "assets.bin"))
    validate_image(binaries["bootloader"])
    validate_image(binaries["app"], version)
    table = binaries["partition_table"]
    if not 0xc00 <= len(table) <= 0x1000 or len(binaries["bootloader"]) > 0x8000:
        raise ValueError("Bootloader or partition table exceeds its flash region")
    fake_flash = bytearray(b"\xff" * FLASH_SIZE)
    fake_flash[0x8000:0x8000 + len(table)] = table
    partitions = partitions_from_flash(bytes(fake_flash))
    expected = [("nvs", 1, 2, 0x9000, 0x4000), ("otadata", 1, 0, 0xd000, 0x2000),
                ("phy_init", 1, 1, 0xf000, 0x1000), ("ota_0", 0, 16, 0x20000, 0x3f0000),
                ("ota_1", 0, 17, 0x410000, 0x3f0000), ("assets", 1, 130, 0x800000, 0x800000)]
    if [(p["name"], p["type"], p["subtype"], p["offset"], p["size"]) for p in partitions] != expected or any(p["flags"] for p in partitions):
        raise ValueError("Target partition table differs from the StackChan release layout")
    if len(binaries["app"]) > 0x3f0000 or binaries["otadata"] != b"\xff" * 8192:
        raise ValueError("Application exceeds its partition or OTA initialization is unsafe")
    asset_metadata = _asset_metadata(build, assets, binaries["assets"])
    files = {key: dict(path="firmware/" + _DESTINATIONS[key], offset=_OFFSETS[key], size=len(data), sha256=_sha(data))
             for key, data in binaries.items()}
    manifest = dict(schema=1, board="m5stack-core-s3-stackchan",
                    hardware="K151 ESP32-S3 16MB Quad PSRAM ILI9342C", chip="esp32s3", flash_size=FLASH_SIZE,
                    version=version, partitions=partitions, files=files, assets_manifest="docs/assets-manifest.json")
    payloads = _notices(build)
    public_info = _build_info(build)
    if public_info is not None:
        payloads["build-info.json"] = (json.dumps(public_info, indent=2, sort_keys=True) + "\n").encode()
    for key, info in files.items():
        payloads[info["path"]] = binaries[key]
    payloads["docs/assets-manifest.json"] = (json.dumps(asset_metadata, indent=2, sort_keys=True) + "\n").encode()
    payloads["manifest.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    checksums = "".join(f"{_sha(data)}  {name}\n" for name, data in sorted(payloads.items()))
    payloads["SHA256SUMS"] = checksums.encode("ascii")
    output.mkdir(parents=True, exist_ok=True)
    for name, data in sorted(payloads.items()):
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--assets-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        manifest = build_package(args.build_dir, args.assets_dir, args.output, args.version)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Release package rejected: {error}\n")
    print(f"Packaged StackChan {manifest['version']} for {manifest['hardware']}")


if __name__ == "__main__":
    main()
