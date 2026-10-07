"""Replace standard build assets' emojis with opaque StackChan GIF pairs.

The base mmap bundle comes from the normal firmware build; no device backup,
serial device, or private face resource is needed. All other resources survive
byte-for-byte. Install tools/requirements-stackchan.txt before running.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

from PIL import Image

PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = PROJECT_ROOT / "tools/stackchan-face-sources/factory-style-talking-20260929"
DEFAULT_BASE_ASSETS = PROJECT_ROOT / "build/generated_assets.bin"
ASSET_LIMIT = 0x800000
EMOTIONS = tuple("neutral happy laughing funny sad angry crying loving embarrassed surprised shocked thinking winking cool relaxed delicious kissy confident sleepy silly confused".split())
HEADER = struct.Struct("<III")
ENTRY = struct.Struct("<32sIIHH")


def _validate_name(name):
    if (not name or name in (".", "..") or "/" in name or "\\" in name
            or "\0" in name or len(name.encode("utf-8")) >= 32):
        raise ValueError(f"Invalid mmap resource name: {name!r}")


def unpack_assets(data):
    """Decode the firmware's 32-byte-name mmap format, allowing flash padding."""
    if len(data) < HEADER.size:
        raise ValueError("Assets header is truncated")
    count, checksum, length = HEADER.unpack_from(data)
    if not 0 < count < 2000 or not ENTRY.size * count <= length <= len(data) - HEADER.size:
        raise ValueError("Invalid assets table or payload length")
    payload = data[HEADER.size:HEADER.size + length]
    if sum(payload) & 0xffff != checksum:
        raise ValueError("Assets checksum mismatch")
    files = {}
    spans = []
    for i in range(count):
        raw, size, offset, width, height = ENTRY.unpack_from(payload, i * ENTRY.size)
        try:
            name = raw.split(b"\0", 1)[0].decode("utf-8")
        except UnicodeDecodeError as error:
            raise ValueError("Invalid resource filename encoding") from error
        _validate_name(name)
        start = ENTRY.size * count + offset
        end = start + 2 + size
        if name in files or end > len(payload) or payload[start:start + 2] != b"ZZ":
            raise ValueError(f"Invalid or duplicate resource entry: {name}")
        spans.append((start, end))
        files[name] = (payload[start + 2:end], width, height)
    previous_end = ENTRY.size * count
    for start, end in sorted(spans):
        if start != previous_end:
            raise ValueError("Assets entries overlap or leave unindexed bytes")
        previous_end = end
    if previous_end != len(payload):
        raise ValueError("Assets payload has unindexed bytes")
    return files


def pack_assets(files):
    """Encode resources as the standard firmware mmap bundle, below 8 MiB."""
    if not 0 < len(files) < 2000:
        raise ValueError("Assets must contain between 1 and 1999 resources")
    if HEADER.size + ENTRY.size * len(files) + sum(2 + len(v[0]) for v in files.values()) >= ASSET_LIMIT:
        raise ValueError("StackChan assets must be smaller than the 8 MiB partition")
    table, body = bytearray(), bytearray()
    for name in sorted(files, key=lambda n: (Path(n).suffix, Path(n).stem)):
        _validate_name(name)
        data, width, height = files[name]
        if not 0 <= width <= 65535 or not 0 <= height <= 65535:
            raise ValueError(f"Invalid dimensions for {name}")
        table.extend(ENTRY.pack(name.encode("utf-8"), len(data), len(body), width, height))
        body.extend(b"ZZ" + data)
    payload = table + body
    return HEADER.pack(len(files), sum(payload) & 0xffff, len(payload)) + payload


def asset_states():
    """Yield (resource key, emotion, talking) for the 41 runtime states."""
    for emotion in EMOTIONS:
        yield emotion, emotion, False
        if emotion != "sleepy":
            yield emotion + "_talk", emotion, True


def load_renderer(source):
    """Load the checked-in renderer's explicit render/save_gif Python API."""
    path = Path(source) / "generate.py"
    spec = importlib.util.spec_from_file_location("stackchan_face_renderer", path)
    renderer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(renderer)
    if tuple(renderer.NAMES) != EMOTIONS:
        raise ValueError("Renderer must provide the expected 21 StackChan emotions")
    return renderer


def replace_emojis(base_files, gifs):
    """Replace only the emoji collection and black theme/subtitle preferences."""
    if "index.json" not in base_files:
        raise ValueError("Base assets are missing index.json")
    try:
        index = json.loads(base_files["index.json"][0])
    except (ValueError, UnicodeDecodeError) as error:
        raise ValueError("Base index.json is invalid") from error
    if not isinstance(index, dict) or index.get("version", 1) != 1:
        raise ValueError("Unsupported base assets index version")
    for field in ("srmodels", "text_font"):
        if field in index and index[field] not in base_files:
            raise ValueError(f"Base assets are missing the referenced {field} resource")
    expected = {key + ".gif" for key, _, _ in asset_states()}
    if set(gifs) != expected:
        raise ValueError("GIF collection must contain all 41 idle/talking states")
    old_emojis = {entry["file"] for entry in index.get("emoji_collection", [])}
    files = {name: value for name, value in base_files.items() if name not in old_emojis}
    if set(gifs) & set(files):
        raise ValueError("New GIF name conflicts with a preserved base resource")
    files.update(gifs)
    index = copy.deepcopy(index)
    index["emoji_collection"] = [{"name": key, "file": key + ".gif"}
                                 for key, _, _ in asset_states()]
    index["hide_subtitle"] = True
    skin = index.setdefault("skin", {})
    for mode in ("light", "dark"):
        skin.setdefault(mode, {})["background_color"] = "#000000"
        skin[mode]["text_color"] = "#FFFFFF"
    files["index.json"] = (json.dumps(index, separators=(",", ":")).encode(), 0, 0)
    return files


def validate_gif(path, talking):
    """Verify decoded pixels, mouth state, infinite loop, and opaque corners."""
    mouth_shapes = set()
    duration = 0
    with Image.open(path) as im:
        if im.info.get("loop") != 0 or "transparency" in im.info:
            raise ValueError(f"GIF must loop forever with no transparency: {path}")
        frames = im.n_frames
        for frame in range(frames):
            im.seek(frame)
            rgb = im.convert("RGB")
            if im.size != (320, 240) or "transparency" in im.info:
                raise ValueError(f"Invalid GIF dimensions or transparency: {path}")
            if any(rgb.getpixel(p) != (0, 0, 0) for p in ((0, 0), (319, 0), (0, 239), (319, 239))):
                raise ValueError(f"GIF background must be opaque black: {path}")
            duration += im.info.get("duration", 0)
            region = rgb.convert("L").crop((150, 125, 156, 173)).point(lambda v: 255 if v > 160 else 0)
            box = region.getbbox()
            if box:
                mouth_shapes.add(box[3] - box[1])
        if not mouth_shapes or (talking and max(mouth_shapes) - min(mouth_shapes) < 8) or (not talking and len(mouth_shapes) != 1):
            raise ValueError(f"GIF mouth does not match talking={talking}: {path}")
        if duration != 6000:
            raise ValueError(f"GIF must have a six-second loop: {path}")
    return {"frames": frames, "duration_ms": duration}


def build_assets(base_assets, output, source=DEFAULT_SOURCE):
    base_data = Path(base_assets).read_bytes()
    base_files = unpack_assets(base_data)
    renderer = load_renderer(source)
    output = Path(output)
    if (output / "assets.bin").resolve() == Path(base_assets).resolve():
        raise ValueError("Output must not overwrite the base assets")
    (output / "gifs").mkdir(parents=True, exist_ok=True)
    gifs, records = {}, []
    for key, emotion, talking in asset_states():
        path = output / "gifs" / f"{key}.gif"
        renderer.save_gif(emotion, path, talking=talking)
        stats = validate_gif(path, talking)
        data = path.read_bytes()
        gifs[path.name] = (data, 320, 240)
        records.append({"name": key, "sha256": hashlib.sha256(data).hexdigest(),
                        "bytes": len(data), **stats})
    files = replace_emojis(base_files, gifs)
    binary = pack_assets(files)
    if unpack_assets(binary) != files:
        raise ValueError("Generated assets failed the format round trip")
    preserved = [{"name": name, "bytes": len(value[0]), "sha256": hashlib.sha256(value[0]).hexdigest()}
                 for name, value in sorted(base_files.items()) if name != "index.json" and files.get(name) == value]
    manifest = {"gif_count": len(gifs), "asset_bytes": len(binary),
                "sha256": hashlib.sha256(binary).hexdigest(),
                "base_assets_sha256": hashlib.sha256(base_data).hexdigest(),
                "preserved_resources": preserved, "files": records}
    (output / "assets.bin").write_bytes(binary)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-assets", type=Path, default=DEFAULT_BASE_ASSETS,
                        help="Standard build mmap assets (default: build/generated_assets.bin)")
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE,
                        help="Renderer source directory (defaults to the checked-in source)")
    parser.add_argument("--output", type=Path, required=True, help="Directory for assets.bin, gifs/, and manifest.json")
    args = parser.parse_args()
    try:
        manifest = build_assets(args.base_assets, args.output, args.source)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Asset build failed: {error}\n")
    print(f"Validated {manifest['gif_count']} state-aware GIFs; assets {manifest['asset_bytes']} bytes")


if __name__ == "__main__":
    main()
