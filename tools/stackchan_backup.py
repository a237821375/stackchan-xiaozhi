"""Read-only, standard-library extraction of StackChan calibration from flash.

Only partition metadata and the two encoder zeros leave this module. It never
opens a device, writes flash, exports other NVS values, or grants calibration
approval. Callers must read their local backup into bytes. Unsafe or unsupported
input raises ValueError with messages that contain no private NVS content.

Independent implementation of the ESP-IDF partition/NVS wire formats, checked
against Espressif's Apache-2.0 nvs_parser.py and nvs_types.hpp/cpp in IDF 6.0.1:
https://github.com/espressif/esp-idf/tree/v6.0.1/components/nvs_flash
No SDK, vendor source, or external Python package is required.

Supported: plain NVS page versions 1/2, committed integers, legacy blobs and
indexed blobs. Recovery states, corruption, encryption, unknown versions, and
conflicting live calibration records fail closed. Erased history is ignored;
identical live copies produced by garbage collection are accepted. Sequence
numbers must be unique; they never justify selecting one conflicting value.
The backup need only cover every declared partition (16 MiB is installer policy).
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import struct
import zlib

_TABLE_OFFSET = 0x8000
_TABLE_SIZE = 0xc00
_PAGE_SIZE = 4096
_INTEGER_TYPES = {1, 2, 4, 8, 0x11, 0x12, 0x14, 0x18}


def _crc(data: bytes) -> int:
    return zlib.crc32(data, 0xffffffff) & 0xffffffff


def _name(raw: bytes, *, full_label: bool = False) -> str:
    # NVS requires a NUL. Partition labels may occupy the complete 16 bytes.
    end = raw.find(b"\0")
    if end == -1 and full_label:
        end = len(raw)
    if end < 1:
        raise ValueError("Invalid partition or NVS name")
    try:
        return raw[:end].decode("utf-8")
    except UnicodeDecodeError:
        raise ValueError("Invalid partition or NVS name") from None


def partitions_from_flash(data: bytes) -> list[dict]:
    """Validate the table at 0x8000 and return bounded, nonoverlapping ranges."""
    if len(data) < _TABLE_OFFSET + _TABLE_SIZE:
        raise ValueError("Backup is truncated before the partition table")
    table = data[_TABLE_OFFSET:_TABLE_OFFSET + _TABLE_SIZE]
    partitions = []
    names = set()
    ended = False
    for index in range(0, _TABLE_SIZE, 32):
        entry = table[index:index + 32]
        if entry == b"\xff" * 32:
            if table[index:] != b"\xff" * (_TABLE_SIZE - index):
                raise ValueError("Invalid partition table termination")
            ended = True
            break
        if entry[:2] == b"\xeb\xeb":
            if entry[2:16] != b"\xff" * 14 or entry[16:] != hashlib.md5(table[:index]).digest():
                raise ValueError("Partition table MD5 mismatch")
            if table[index + 32:] != b"\xff" * (_TABLE_SIZE - index - 32):
                raise ValueError("Invalid data after partition table MD5")
            ended = True
            break
        magic, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", entry)
        if magic != 0x50aa:
            raise ValueError("Invalid partition table magic")
        name = _name(label, full_label=True)
        if name in names:
            raise ValueError("Duplicate partition name")
        if offset < 0x9000 or offset % _PAGE_SIZE or not size or offset + size > len(data):
            raise ValueError("Partition range is invalid or backup is truncated")
        names.add(name)
        partitions.append(dict(name=name, type=kind, subtype=subtype, offset=offset, size=size, flags=flags))
    if not ended or not partitions:
        raise ValueError("Partition table is empty or unterminated")
    ranges = sorted((p["offset"], p["offset"] + p["size"]) for p in partitions)
    if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise ValueError("Partitions overlap")
    return partitions


@dataclass(frozen=True)
class _Entry:
    namespace: int
    kind: int
    key: str
    chunk: int
    value: int | bytes | tuple[int, int, int]


def _nvs_entries(data: bytes) -> list[_Entry]:
    if not data or len(data) % _PAGE_SIZE:
        raise ValueError("NVS partition is truncated or misaligned")
    result = []
    sequences = set()
    for offset in range(0, len(data), _PAGE_SIZE):
        page = data[offset:offset + _PAGE_SIZE]
        if page == b"\xff" * _PAGE_SIZE:
            continue
        state, sequence = struct.unpack_from("<II", page)
        if state not in (0xfffffffe, 0xfffffffc) or page[8] not in (255, 254):
            raise ValueError("NVS page is encrypted, invalid, unsupported, or in recovery")
        if _crc(page[4:28]) != struct.unpack_from("<I", page, 28)[0]:
            raise ValueError("NVS page CRC mismatch")
        if sequence in sequences:
            raise ValueError("Ambiguous NVS page sequences")
        sequences.add(sequence)
        states = [(page[32 + i // 4] >> ((i % 4) * 2)) & 3 for i in range(126)]
        if 1 in states or page[63] & 0xf0 != 0xf0:
            raise ValueError("Invalid NVS entry bitmap")
        index = 0
        while index < 126:
            raw = page[64 + index * 32:96 + index * 32]
            if states[index] == 0:  # Erased history is never a candidate.
                index += 1
                continue
            if states[index] == 3:
                if raw != b"\xff" * 32:
                    raise ValueError("Uncommitted NVS entry data")
                index += 1
                continue
            ns, kind, span, chunk = raw[:4]
            if _crc(raw[:4] + raw[8:]) != struct.unpack_from("<I", raw, 4)[0]:
                raise ValueError("NVS entry CRC mismatch")
            if not span or index + span > 126 or any(s != 2 for s in states[index:index + span]):
                raise ValueError("NVS entry span is truncated or uncommitted")
            key = _name(raw[8:24])
            if ns == 255:
                raise ValueError("Invalid NVS namespace")
            if kind in _INTEGER_TYPES:
                if span != 1 or chunk != 255:
                    raise ValueError("Invalid NVS integer header")
                width = kind & 15
                value = int.from_bytes(raw[24:24 + width], "little", signed=bool(kind & 16))
            elif kind in (0x21, 0x41, 0x42):
                length = struct.unpack_from("<H", raw, 24)[0]
                if span != 1 + (length + 31) // 32 or (kind == 0x42) == (chunk == 255):
                    raise ValueError("Invalid NVS variable entry header")
                value = page[96 + index * 32:96 + index * 32 + length]
                if _crc(value) != struct.unpack_from("<I", raw, 28)[0]:
                    raise ValueError("NVS payload CRC mismatch")
            elif kind == 0x48:
                if span != 1 or chunk != 255:
                    raise ValueError("Invalid NVS blob index header")
                size, count, start = struct.unpack_from("<IBB", raw, 24)
                if start not in (0, 128) or count > 127 or size > 127 * 125 * 32:
                    raise ValueError("Invalid NVS blob index")
                value = (size, count, start)
            else:
                raise ValueError("Unsupported NVS entry type")
            result.append(_Entry(ns, kind, key, chunk, value))
            index += span
    return result


def _one(values: list) -> object:
    if not values:
        raise ValueError("Required calibration record is missing")
    if any(value != values[0] for value in values[1:]):
        raise ValueError("Conflicting live calibration records")
    return values[0]


def _config(records: list[_Entry]) -> bytes:
    candidates = []
    indexes = [r for r in records if r.kind == 0x48]
    chunks = [r for r in records if r.kind == 0x42]
    if any(r.kind not in (0x41, 0x42, 0x48) for r in records):
        raise ValueError("Calibration config has an unsupported type")
    candidates.extend(r.value for r in records if r.kind == 0x41)
    used = set()
    for index in indexes:
        size, count, start = index.value
        if size != 16 or count != 1:
            raise ValueError("Unsupported calibration blob size or chunk count")
        matching = [r.value for r in chunks if r.chunk == start]
        payload = _one(matching)
        if len(payload) != size:
            raise ValueError("Incomplete calibration blob")
        used.add(start)
        candidates.append(payload)
    if any(chunk.chunk not in used for chunk in chunks):
        raise ValueError("Uncommitted calibration blob chunks")
    return _one(candidates)


def _partition_calibration(data: bytes) -> list[tuple[int, int, str]]:
    entries = _nvs_entries(data)
    namespaces = {}
    names = {}
    for entry in entries:
        if entry.namespace != 0:
            continue
        if entry.kind != 1 or not 1 <= entry.value <= 254:
            raise ValueError("Invalid NVS namespace declaration")
        if (entry.value in namespaces and namespaces[entry.value] != entry.key or
                entry.key in names and names[entry.key] != entry.value):
            raise ValueError("Conflicting NVS namespace declarations")
        namespaces[entry.value] = entry.key
        names[entry.key] = entry.value
    if any(e.namespace and e.namespace not in namespaces for e in entries):
        raise ValueError("Undeclared NVS namespace")
    candidates = []
    servo = [e for e in entries if namespaces.get(e.namespace) == "servo" and
             e.key in ("zero_pos_1", "zero_pos_2")]
    if servo:
        if any(e.kind not in _INTEGER_TYPES for e in servo):
            raise ValueError("Original servo calibration must use integers")
        yaw = _one([e.value for e in servo if e.key == "zero_pos_1"])
        pitch = _one([e.value for e in servo if e.key == "zero_pos_2"])
        candidates.append((yaw, pitch, "servo/zero_pos_1,zero_pos_2"))
    config = [e for e in entries if namespaces.get(e.namespace) == "head_cal" and e.key == "config"]
    if config:
        payload = _config(config)
        if len(payload) != 16:
            raise ValueError("Unsupported calibration config length")
        schema, yaw, pitch, verified = struct.unpack("<IiiI", payload)
        if schema != 1 or verified not in (0, 1):
            raise ValueError("Unsupported calibration config schema or approval value")
        candidates.append((yaw, pitch, "head_cal/config"))
    return candidates


def calibration_from_flash(data: bytes) -> dict:
    """Return validated zeros with source and approved=False; never recover trust."""
    candidates = []
    for partition in partitions_from_flash(data):
        if partition["type"] != 1 or partition["subtype"] != 2:
            continue
        if partition["flags"]:
            raise ValueError("Encrypted or unsupported NVS partition flags")
        start = partition["offset"]
        candidates.extend(_partition_calibration(data[start:start + partition["size"]]))
    yaw, pitch = _one([(y, p) for y, p, _ in candidates])
    if not 96 <= yaw <= 927 or not 0 <= pitch <= 831:
        raise ValueError("Calibration encoder zeros are outside safe bounds")
    # Prefer the versioned schema if both formats store exactly the same zeros.
    source = "head_cal/config" if any(s == "head_cal/config" for _, _, s in candidates) else candidates[0][2]
    return dict(yaw_zero=yaw, pitch_zero=pitch, source=source, approved=False)
