"""Synthetic, public fixtures for read-only flash calibration extraction.

No device backups, credentials, or user NVS values belong in these tests.
"""
import hashlib
from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
try:
    import stackchan_backup as backup
except ModuleNotFoundError:
    backup = None


def crc(data):
    return zlib.crc32(data, 0xffffffff) & 0xffffffff


def item(ns, kind, key, value, *, span=1, chunk=255):
    raw = bytearray(struct.pack("<BBBBI16s8s", ns, kind, span, chunk, 0,
                                key.encode() + b"\0", value.ljust(8, b"\xff")))
    struct.pack_into("<I", raw, 4, crc(raw[:4] + raw[8:]))
    return bytes(raw)


def integer(ns, key, value, kind=0x14):
    return item(ns, kind, key, value.to_bytes(kind & 15, "little", signed=bool(kind & 16)))


def variable(ns, key, payload, kind=0x42, chunk=0):
    slots = (len(payload) + 31) // 32
    return [item(ns, kind, key, struct.pack("<HHI", len(payload), 65535, crc(payload)),
                 span=slots + 1, chunk=chunk)] + [
        payload.ljust(slots * 32, b"\xff")[i * 32:(i + 1) * 32] for i in range(slots)]


def page(entries, sequence=0, *, state=0xfffffffe, version=254, erased=()):
    result = bytearray(b"\xff" * 4096)
    struct.pack_into("<IIB", result, 0, state, sequence, version)
    struct.pack_into("<I", result, 28, crc(result[4:28]))
    for index, entry in enumerate(entries):
        result[64 + index * 32:96 + index * 32] = entry
        shift = (index % 4) * 2
        result[32 + index // 4] &= ~(3 << shift)
        result[32 + index // 4] |= (0 if index in erased else 2) << shift
    return bytes(result)


def servo(yaw=460, pitch=620, **kwargs):
    return page([integer(0, "servo", 1, 1), integer(1, "zero_pos_1", yaw),
                 integer(1, "zero_pos_2", pitch)], **kwargs)


def modern(yaw=460, pitch=620, approved=1, schema=1, legacy=False, sequence=0):
    data = struct.pack("<IiiI", schema, yaw, pitch, approved)
    entries = [integer(0, "head_cal", 1, 1)]
    entries += variable(1, "config", data, kind=0x41 if legacy else 0x42,
                        chunk=255 if legacy else 0)
    if not legacy:
        entries += [item(1, 0x48, "config", struct.pack("<IBBH", 16, 1, 0, 65535))]
    return page(entries, sequence=sequence)


def flash(nvs=None, *, partitions=None, md5=True):
    if partitions is None:
        partitions = [("nvs", 1, 2, 0x9000, 0x3000, 0),
                      ("app", 0, 0, 0x10000, 0x10000, 0)]
    result = bytearray(b"\xff" * 0x20000)
    table = b"".join(struct.pack("<HBBII16sI", 0x50aa, kind, subtype, offset, size,
                                  name.encode(), flags)
                     for name, kind, subtype, offset, size, flags in partitions)
    if md5:
        table += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(table).digest()
    result[0x8000:0x8000 + len(table)] = table
    if nvs is not None:
        result[0x9000:0x9000 + len(nvs)] = nvs
    return bytes(result)


class BackupTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(backup, "Portable read-only backup API is missing")

    def test_empty_calibration_has_distinct_error_from_partial_record(self):
        self.assertTrue(hasattr(backup,'MissingCalibration'))
        with self.assertRaises(backup.MissingCalibration): backup.calibration_from_flash(flash())
        incomplete = page([integer(0,'servo',1,1),integer(1,'zero_pos_1',460)])
        try:
            backup.calibration_from_flash(flash(incomplete))
        except ValueError as error:
            self.assertNotIsInstance(error,backup.MissingCalibration)
        else:
            self.fail('Partial calibration must fail validation')
    def test_partition_table_with_and_without_md5(self):
        for md5 in (True, False):
            self.assertEqual(backup.partitions_from_flash(flash(md5=md5)), [
                {"name": "nvs", "type": 1, "subtype": 2, "offset": 0x9000,
                 "size": 0x3000, "flags": 0},
                {"name": "app", "type": 0, "subtype": 0, "offset": 0x10000,
                 "size": 0x10000, "flags": 0}])

    def test_reject_partition_damage_truncation_overlap_and_duplicates(self):
        broken = bytearray(flash())
        broken[0x8010] ^= 1
        overlap = [("a", 1, 2, 0x9000, 0x3000, 0), ("b", 0, 0, 0xa000, 0x1000, 0)]
        for data in (bytes(broken), b"", flash()[:0x1ffff], flash(partitions=overlap),
                     flash(partitions=[("a", 1, 2, 0x8000, 0x1000, 0)]),
                     flash(partitions=[("a", 1, 2, 0x9000, 0, 0)]),
                     flash(partitions=[("a", 1, 2, 0x9000, 0x1000, 0),
                                       ("a", 1, 2, 0xa000, 0x1000, 0)])):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                backup.partitions_from_flash(data)

    def test_original_committed_integers_are_extracted_without_approval(self):
        self.assertEqual(backup.calibration_from_flash(flash(servo())), {
            "yaw_zero": 460, "pitch_zero": 620,
            "source": "servo/zero_pos_1,zero_pos_2", "approved": False})

    def test_new_and_legacy_blob_configuration_never_inherits_approval(self):
        for legacy in (True, False):
            self.assertEqual(backup.calibration_from_flash(flash(modern(legacy=legacy))), {
                "yaw_zero": 460, "pitch_zero": 620, "source": "head_cal/config",
                "approved": False})

    def test_zero_boundaries_and_supported_integer_widths(self):
        for yaw, pitch in ((96, 0), (927, 831)):
            result = backup.calibration_from_flash(flash(servo(yaw, pitch)))
            self.assertEqual((result["yaw_zero"], result["pitch_zero"]), (yaw, pitch))
        for kind in (2, 4, 8, 0x12, 0x14, 0x18):
            data = page([integer(0, "servo", 1, 1), integer(1, "zero_pos_1", 460, kind),
                         integer(1, "zero_pos_2", 620, kind)])
            self.assertEqual(backup.calibration_from_flash(flash(data))["yaw_zero"], 460)

    def test_reject_missing_wrong_namespace_wrong_type_and_unsafe_zeros(self):
        wrong_type = page([integer(0, "servo", 1, 1),
                           *variable(1, "zero_pos_1", b"460", 0x21, 255),
                           integer(1, "zero_pos_2", 620)])
        for data in (flash(), flash(page([integer(0, "servo", 1, 1)])),
                     flash(page([integer(0, "other", 1, 1), integer(1, "zero_pos_1", 460),
                                 integer(1, "zero_pos_2", 620)])), flash(wrong_type),
                     *(flash(servo(y, p)) for y, p in ((95, 620), (928, 620), (460, -1), (460, 832))),
                     flash(modern(schema=2)), flash(modern(approved=2))):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                backup.calibration_from_flash(data)

    def test_reject_encrypted_unsupported_and_corrupted_pages(self):
        bad_header = bytearray(servo())
        bad_header[4] ^= 1
        bad_entry = bytearray(servo())
        bad_entry[64 + 32 + 24] ^= 1
        bad_blob = bytearray(modern())
        bad_blob[64 + 64] ^= 1
        illegal_bitmap = bytearray(servo())
        illegal_bitmap[32] = (illegal_bitmap[32] & ~12) | 4
        unfinished = bytearray(servo())
        unfinished[32] |= 12
        for data in (flash(bytes(bad_header)), flash(bytes(bad_entry)), flash(bytes(bad_blob)),
                     flash(bytes(illegal_bitmap)), flash(bytes(unfinished)),
                     flash(servo(version=253)), flash(servo(state=0xfffffff8)),
                     flash(servo(), partitions=[("nvs", 1, 2, 0x9000, 0x3000, 1)])):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                backup.calibration_from_flash(data)

    def test_erased_history_is_ignored_but_live_conflicts_are_rejected(self):
        entries = [integer(0, "servo", 1, 1), integer(1, "zero_pos_1", 400),
                   integer(1, "zero_pos_1", 460), integer(1, "zero_pos_2", 620)]
        self.assertEqual(backup.calibration_from_flash(flash(page(entries, erased=(1,))))["yaw_zero"], 460)
        for data in (page(entries), servo() + servo(470, sequence=1),
                     servo() + modern(470, sequence=1), servo() + servo(sequence=0)):
            with self.assertRaises(ValueError):
                backup.calibration_from_flash(flash(data))
        self.assertEqual(backup.calibration_from_flash(flash(servo() + servo(sequence=1)))["yaw_zero"], 460)

    def test_namespace_ids_are_partition_local_and_conflicts_fail(self):
        entries = [integer(0, "servo", 1, 1), integer(0, "other", 1, 1),
                   integer(1, "zero_pos_1", 460), integer(1, "zero_pos_2", 620)]
        with self.assertRaises(ValueError):
            backup.calibration_from_flash(flash(page(entries)))

    def test_full_length_partition_label_is_valid_but_nvs_keys_need_a_nul(self):
        image = flash(partitions=[("sixteen-byte-key", 1, 2, 0x9000, 0x3000, 0)])
        self.assertEqual(backup.partitions_from_flash(image)[0]["name"], "sixteen-byte-key")
        image = flash(partitions=[("0123456789abcdef", 1, 2, 0x9000, 0x3000, 0)])
        self.assertEqual(backup.partitions_from_flash(image)[0]["name"], "0123456789abcdef")
        broken = bytearray(integer(1, "zero_pos_1", 460))
        broken[8:24] = b"0123456789abcdef"
        struct.pack_into("<I", broken, 4, crc(broken[:4] + broken[8:]))
        with self.assertRaises(ValueError):
            backup.calibration_from_flash(flash(page([integer(0, "servo", 1, 1), bytes(broken)])))

    def test_version_one_full_page_and_namespace_across_pages(self):
        records = page([integer(1, "zero_pos_1", 460), integer(1, "zero_pos_2", 620)],
                       sequence=8, state=0xfffffffc, version=255)
        declaration = page([integer(0, "servo", 1, 1)], sequence=7, version=255)
        self.assertEqual(backup.calibration_from_flash(flash(records + declaration))["pitch_zero"], 620)

    def test_blob_version_128_and_chunks_on_another_page(self):
        chunks = page([integer(0, "head_cal", 1, 1)] + variable(
            1, "config", struct.pack("<IiiI", 1, 460, 620, 0), chunk=128), sequence=1)
        indexes = page([item(1, 0x48, "config", struct.pack("<IBBH", 16, 1, 128, 65535))], sequence=2)
        self.assertEqual(backup.calibration_from_flash(flash(indexes + chunks))["pitch_zero"], 620)

    def test_independent_partitions_cannot_supply_half_a_calibration(self):
        first = page([integer(0, "servo", 1, 1), integer(1, "zero_pos_1", 460)])
        second = page([integer(0, "servo", 1, 1), integer(1, "zero_pos_2", 620)])
        image = flash(first + second, partitions=[("nvs", 1, 2, 0x9000, 0x1000, 0),
                                                  ("nvs2", 1, 2, 0xa000, 0x1000, 0)])
        with self.assertRaises(ValueError):
            backup.calibration_from_flash(image)

    def test_blob_requires_committed_matching_chunk_and_index(self):
        data = struct.pack("<IiiI", 1, 460, 620, 1)
        ns = [integer(0, "head_cal", 1, 1)]
        chunks = variable(1, "config", data)
        index = item(1, 0x48, "config", struct.pack("<IBBH", 16, 1, 0, 65535))
        for entries in (ns + chunks, ns + [index], ns + chunks + [
            item(1, 0x48, "config", struct.pack("<IBBH", 17, 1, 0, 65535))],
                        ns + variable(1, "config", data, chunk=128) + [index]):
            with self.assertRaises(ValueError):
                backup.calibration_from_flash(flash(page(entries)))
        with self.assertRaises(ValueError):
            backup.calibration_from_flash(flash(page(ns + chunks + [index], erased=(2,))))


if __name__ == "__main__":
    unittest.main()
