"""Preserve device assets and replace only emoji/theme using the v2.5.0 format."""
from pathlib import Path
import hashlib
import io
import json
import struct
from PIL import Image

ROOT = Path(__file__).resolve().parent
BACKUP = ROOT.parents[1] / 'backups/xiaozhi-before-otto-20260929.bin'
EXPECTED = set('neutral happy laughing funny sad angry crying loving embarrassed surprised shocked thinking winking cool relaxed delicious kissy confident sleepy silly confused'.split())

def unpack(data):
    count, checksum, length = struct.unpack_from('<III', data)
    assert 0 < count < 2000 and 44 * count <= length <= len(data) - 12
    payload = data[12:12 + length]
    assert sum(payload) & 0xffff == checksum
    result = {}
    for i in range(count):
        raw, size, offset, width, height = struct.unpack_from('<32sIIHH', payload, i * 44)
        name = raw.split(b'\0')[0].decode()
        assert name and name not in result and Path(name).name == name
        start = 44 * count + offset
        assert start + 2 + size <= len(payload) and payload[start:start + 2] == b'ZZ'
        result[name] = (payload[start + 2:start + 2 + size], width, height)
    return result

def pack(files):
    table, body = bytearray(), bytearray()
    for name in sorted(files, key=lambda n: (Path(n).suffix, Path(n).stem)):
        data, width, height = files[name]
        assert len(name.encode()) < 32
        table.extend(struct.pack('<32sIIHH', name.encode(), len(data), len(body), width, height))
        body.extend(b'ZZ' + data)
    payload = table + body
    return struct.pack('<III', len(files), sum(payload) & 0xffff, len(payload)) + payload

def main():
    flash = BACKUP.read_bytes()
    assert len(flash) == 0x1000000
    partitions = {}
    for p in range(0x8000, 0x9000, 32):
        magic, typ, sub, offset, size, label, flags = struct.unpack_from('<HBBII16sI', flash, p)
        if magic != 0x50aa:
            break
        partitions[label.split(b'\0')[0].decode()] = {'offset': offset, 'size': size, 'type': typ, 'subtype': sub}
    assert partitions['assets']['offset'] == 0x800000 and partitions['assets']['size'] == 0x800000
    assert all(v['offset'] + v['size'] <= 0x800000 for k, v in partitions.items() if k != 'assets')
    old = unpack(flash[0x800000:])
    for name, (data, _, _) in old.items():
        (ROOT / 'original-files' / name).write_bytes(data)
    index = json.loads(old['index.json'][0])
    assert index.get('version', 1) == 1
    original_index = json.loads(old['index.json'][0])
    old_emoji = {e['file'] for e in index.get('emoji_collection', []) if 'file' in e}
    files = {k: v for k, v in old.items() if k not in old_emoji}
    gifs = sorted((ROOT / 'source/gifs').glob('*.gif'))
    assert {p.stem for p in gifs} == EXPECTED
    metadata = []
    for path in gifs:
        data = path.read_bytes()
        with Image.open(io.BytesIO(data)) as image:
            assert image.format == 'GIF' and image.size == (240, 240) and image.info.get('loop') == 0
            frames = image.n_frames
            for frame in range(frames):
                image.seek(frame)
                image.load()
            assert frames > 1
        files[path.name] = (data, 240, 240)
        metadata.append({'name': path.stem, 'frames': frames, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    index['emoji_collection'] = [{'name': p.stem, 'file': p.name} for p in gifs]
    for theme in ('light', 'dark'):
        skin = index.setdefault('skin', {}).setdefault(theme, {})
        skin['background_color'] = '#000000'
        skin['text_color'] = '#FFFFFF'
        skin.pop('background_image', None)
    files['index.json'] = (json.dumps(index, ensure_ascii=False, separators=(',', ':')).encode(), 0, 0)
    binary = pack(files)
    assert len(binary) < 0x800000
    assert unpack(binary) == files
    preserved = [k for k in old if k not in old_emoji and k != 'index.json']
    assert all(files[k] == old[k] for k in preserved)
    assert all(index.get(k) == v for k, v in original_index.items() if k not in ('emoji_collection', 'skin'))
    assert 'srmodels' in index and files[index['srmodels']] == old[index['srmodels']]
    (ROOT / 'assets.bin').write_bytes(binary)
    (ROOT / 'unchanged-lower-8MB.bin').write_bytes(flash[:0x800000])
    (ROOT / 'original-assets-partition.bin').write_bytes(flash[0x800000:])
    report = {'status': 'prepared_not_written', 'source_commit': '970cf66906d7c30059faa2704e7002f06b8c3619', 'partitions': partitions, 'backup': str(BACKUP), 'backup_sha256': hashlib.sha256(flash).hexdigest(), 'asset_bytes': len(binary), 'asset_sha256': hashlib.sha256(binary).hexdigest(), 'preserved_files': preserved, 'emojis': metadata}
    (ROOT / 'record.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'partitions': partitions, 'asset_bytes': len(binary), 'emoji_count': len(gifs), 'preserved_files': preserved, 'backup_sha256': report['backup_sha256']}, indent=2))

if __name__ == '__main__':
    main()
