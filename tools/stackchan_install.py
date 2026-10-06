"""Conservative K151 installer. No SDK needed; esptool 5.4.0 for device I/O.

Backup/plan are read-only. Install touches only validated listed sectors; NVS is
compared before and after, never written. The device stays in download mode and
requires a physical RST after completion. Private backups must remain local.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import zlib
from stackchan_backup import MissingCalibration, calibration_from_flash, partitions_from_flash
from build_stackchan_release import validate_image, _resources, _STATES

FLASH_SIZE = 0x1000000
BOARD = 'm5stack-core-s3-stackchan'
OFFSETS = dict(bootloader=0, partition_table=0x8000, otadata=0xd000, app=0x20000, assets=0x800000)
LIMITS = dict(bootloader=0x8000, partition_table=0x1000, otadata=0x2000, app=0x3f0000, assets=0x800000)
EXPECTED = [dict(name=n,type=t,subtype=s,offset=o,size=z,flags=0) for n,t,s,o,z in [
    ('nvs',1,2,0x9000,0x4000), ('otadata',1,0,0xd000,0x2000),
    ('phy_init',1,1,0xf000,0x1000), ('ota_0',0,16,0x20000,0x3f0000),
    ('ota_1',0,17,0x410000,0x3f0000), ('assets',1,130,0x800000,0x800000)]]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def backup_metadata(data: bytes, identity: str) -> dict:
    if len(data) != FLASH_SIZE or not identity:
        raise ValueError('A complete 16 MiB backup and device identity are required')
    partitions = partitions_from_flash(data)
    if any(p['flags'] for p in partitions):
        raise ValueError('Encrypted/flagged partitions are unsupported')
    try:
        calibration = calibration_from_flash(data)
    except MissingCalibration:
        calibration = None  # Firmware starts with motion disabled until local calibration.
    return dict(schema=1, device_identity=identity, sha256=digest(data),
                size=len(data), partitions=partitions, calibration=calibration)


def _package(root: Path):
    root = root.resolve()
    manifest = json.loads((root / 'manifest.json').read_text())
    if (manifest.get('schema') != 1 or manifest.get('board') != BOARD or
            manifest.get('chip') != 'esp32s3' or manifest.get('flash_size') != FLASH_SIZE or
            not isinstance(manifest.get('version'), str) or not manifest['version']):
        raise ValueError('Unsupported package schema, chip or hardware')
    entries = manifest.get('files')
    if not isinstance(entries, dict) or set(entries) != set(OFFSETS):
        raise ValueError('Package must contain exactly the five firmware regions')
    blobs = {}
    for name, entry in entries.items():
        path = root / entry['path']
        if (path.is_symlink() or not path.resolve().is_relative_to(root) or
                Path(entry['path']).is_absolute()):
            raise ValueError('Unsafe package path')
        content = path.read_bytes()
        if (type(entry.get('offset')) is not int or entry['offset'] != OFFSETS[name] or
                type(entry.get('size')) is not int or not 0 < len(content) <= LIMITS[name] or
                entry['size'] != len(content) or digest(content) != entry.get('sha256')):
            raise ValueError('Firmware hash, length or region is invalid')
        blobs[name] = content
    validate_image(blobs['bootloader'])
    validate_image(blobs['app'],manifest['version'])
    if blobs['otadata'] != b'\xff'*8192:
        raise ValueError('First-install OTA initialization must clear both selection sectors')
    resources = _resources(blobs['assets'])
    index = json.loads(resources['index.json'][0])
    if (index.get('hide_subtitle') is not True or
            index.get('emoji_collection') != [{'name':s,'file':s+'.gif'} for s in _STATES] or
            {name for name in resources if name.endswith('.gif')} != {s+'.gif' for s in _STATES}):
        raise ValueError('Package must contain the 41 matching StackChan expression states')
    image = bytearray(b'\xff' * FLASH_SIZE)
    image[0x8000:0x8000 + len(blobs['partition_table'])] = blobs['partition_table']
    target = partitions_from_flash(bytes(image))
    if target != EXPECTED:
        raise ValueError('Unsupported target partition layout')
    return manifest, blobs


def _active_slot(data: bytes) -> int:
    region = data[0xd000:0xf000]
    if region == b'\xff' * 0x2000:
        return 0
    valid = []
    for offset in (0,0x1000):
        raw = region[offset:offset+32]
        if raw == b'\xff'*32:
            continue
        seq, _, state, crc = struct.unpack('<I20sII',raw)
        if seq in (0,0xffffffff) or crc != zlib.crc32(raw[:4],0xffffffff)&0xffffffff:
            raise ValueError('OTA metadata is damaged; upgrade refused')
        if state in (0,1):
            raise ValueError('OTA image has not completed boot validation; boot normally first')
        if state not in (2,3,4,0xffffffff):
            raise ValueError('Unsupported OTA state')
        if state in (2,0xffffffff):
            valid.append(seq)
    if not valid:
        raise ValueError('No verified active OTA slot')
    return (max(valid)-1)%2


def installation_plan(root: Path, data: bytes, metadata: dict, identity: str, mode: str) -> dict:
    current = backup_metadata(data, identity)
    if (metadata.get('schema') != 1 or metadata.get('device_identity') != identity or
            metadata.get('sha256') != current['sha256'] or metadata.get('size') != FLASH_SIZE or
            metadata.get('partitions') != current['partitions']):
        raise ValueError('Backup is incomplete, altered or belongs to another device')
    manifest, blobs = _package(root)
    partitions = current['partitions']
    nvs = [p for p in partitions if p['name']=='nvs' and p['type']==1 and p['subtype']==2]
    if nvs != EXPECTED[:1] or len([p for p in partitions if p['type']==1 and p['subtype']==2]) != 1:
        raise ValueError('NVS mapping is incompatible; no automatic erase or migration')
    if mode == 'first-install':
        names = list(OFFSETS)
        slot = 0
    elif mode == 'upgrade':
        if partitions != EXPECTED:
            raise ValueError('Upgrade requires the matching StackChan partition layout')
        names = ['app','assets']
        slot = _active_slot(data)
    else:
        raise ValueError('Select first-install or upgrade explicitly')
    writes = []
    for name in names:
        entry = manifest['files'][name]
        offset = (0x410000 if slot else 0x20000) if name=='app' else entry['offset']
        start, end = offset & ~4095, (offset+entry['size']+4095)&~4095
        if start < 0 or end > FLASH_SIZE or (start < 0xd000 and end > 0x9000):
            raise ValueError('Write or erased sector overlaps NVS or flash boundary')
        if any(start < w['erase_end'] and end > w['erase_start'] for w in writes):
            raise ValueError('Write sectors overlap')
        writes.append(dict(name=name,offset=offset,size=entry['size'],sha256=entry['sha256'],
                           path=entry['path'],erase_start=start,erase_end=end))
    return dict(version=manifest['version'],mode=mode,writes=writes,
                calibration=current['calibration'],nvs_sha256=digest(data[0x9000:0xd000]))


class Device:
    """Single esptool session; no reset between identity, snapshot and write."""
    def __init__(self, port):
        import esptool
        from esptool.cmds import detect_flash_size
        if esptool.__version__ != '5.4.0':
            raise ValueError('Install esptool==5.4.0 in a Python virtual environment')
        self.api = esptool
        self.esp = esptool.detect_chip(port=port,baud=115200,connect_attempts=3)
        try:
            if self.esp.CHIP_NAME != 'ESP32-S3':
                raise ValueError('Only ESP32-S3 is supported')
            info = self.esp.get_security_info()
            if (info['parsed_flags']['SECURE_BOOT_EN'] or
                    info['parsed_flags']['SECURE_DOWNLOAD_ENABLE'] or
                    bin(info['flash_crypt_cnt']).count('1') % 2):
                raise ValueError('Secure boot/encrypted/restricted devices are unsupported')
            self.identity = digest(bytes(self.esp.read_mac()))
            self.esp = esptool.run_stub(self.esp)
            esptool.attach_flash(self.esp)
            if detect_flash_size(self.esp) != '16MB':
                raise ValueError('Only 16 MiB flash is supported')
            self.esp.flash_set_parameters(FLASH_SIZE)
            self.esp.change_baud(460800)
        except BaseException:
            self.close()
            raise
    def read(self, offset, size):
        if type(offset) is not int or type(size) is not int or offset < 0 or size < 1 or offset + size > FLASH_SIZE:
            raise ValueError('Flash read range is invalid')
        # Bound each verified transfer. Long streaming reads can lose USB data;
        # never return a partial snapshot or retry a corrupted protocol session.
        chunks = []
        for start in range(offset, offset + size, 0x10000):
            length = min(0x10000, offset + size - start)
            data = self.api.read_flash(self.esp,start,length,flash_size='16MB')
            if data is None or len(data) != length:
                raise ValueError('Incomplete verified flash read; no snapshot returned')
            chunks.append(data)
        return b''.join(chunks)
    def close(self):
        self.esp._port.close()
    def write(self, pairs):
        self.api.write_flash(self.esp,pairs,flash_size='keep',flash_mode='keep',flash_freq='keep',
                             compress=True,erase_all=False,force=False)
        self.api.verify_flash(self.esp,pairs,flash_size='keep',flash_mode='keep',flash_freq='keep')


def save_backup(directory: Path, data: bytes, identity: str):
    # Parent must not expose credentials before permissions are applied.
    directory.mkdir(parents=True,exist_ok=True,mode=0o700)
    directory.chmod(0o700)
    target = directory/'flash.bin'
    record = directory/'backup.json'
    if target.exists() or record.exists():
        raise ValueError('Backup destination already exists; select a fresh directory')
    if len(data) != FLASH_SIZE or not identity:
        raise ValueError('A complete 16 MiB read and identity are required')
    error = None
    try:
        metadata = backup_metadata(data,identity)
    except ValueError as exc:
        # A failed extractor must not destroy the only pre-install snapshot.
        error = exc
        metadata = dict(schema=1,device_identity=identity,sha256=digest(data),
                        size=len(data),validation_error=str(exc))
    for path, content in [(target,data),(record,(json.dumps(metadata,indent=2)+'\n').encode())]:
        fd = os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
        with os.fdopen(fd,'wb') as stream:
            stream.write(content)
    if error is not None:
        raise ValueError("Backup saved locally, but installation refused: " + str(error))
    return metadata


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command',required=True)
    backup = sub.add_parser('backup',help='Read full 16 MiB flash; never write firmware')
    backup.add_argument('--port',required=True); backup.add_argument('--output',type=Path,required=True)
    for command in ('plan','install'):
        p=sub.add_parser(command)
        p.add_argument('--package',type=Path,required=True); p.add_argument('--backup',type=Path,required=True)
        p.add_argument('--mode',choices=['first-install','upgrade'],required=True)
        if command=='install':
            p.add_argument('--port',required=True)
            p.add_argument('--yes',action='store_true',help='Explicitly accept the printed write plan')
    args=parser.parse_args(argv)
    device=None
    try:
        if args.command=='backup':
            device=Device(args.port)
            data=device.read(0,FLASH_SIZE)
            metadata=save_backup(args.output,data,device.identity)
            print(json.dumps(dict(calibration=metadata['calibration'],backup_sha256=metadata['sha256']),indent=2))
            print('Backup saved locally. Contains private NVS; never upload. Short-press RST to boot normally.')
            return 0
        data=(args.backup/'flash.bin').read_bytes()
        metadata=json.loads((args.backup/'backup.json').read_text())
        plan=installation_plan(args.package,data,metadata,metadata.get('device_identity'),args.mode)
        print(json.dumps(plan,indent=2))
        if args.command=='plan':
            return 0
        if not args.yes:
            raise ValueError('Review the plan, then repeat with --yes; no device was opened')
        device=Device(args.port)
        if device.identity != metadata['device_identity']:
            raise ValueError('Connected device differs from the backup; no writes performed')
        # A same-unit backup must describe the whole current flash, not just NVS.
        if digest(device.read(0,FLASH_SIZE)) != metadata['sha256']:
            raise ValueError('Device flash changed since backup; make a fresh backup')
        # Read already-validated bytes once, then pass immutable bytes into esptool.
        manifest,blobs=_package(args.package)
        if manifest['version'] != plan['version'] or any(
                digest(blobs[entry['name']]) != entry['sha256'] or
                manifest['files'][entry['name']]['path'] != entry['path']
                for entry in plan['writes']):
            raise ValueError('Package changed after plan validation; no writes performed')
        pairs=[(entry['offset'],blobs[entry['name']]) for entry in plan['writes']]
        device.write(pairs)
        if digest(device.read(0x9000,0x4000)) != plan['nvs_sha256']:
            raise ValueError('Post-install NVS verification failed; do not boot or discard backup')
        print('Firmware verified; NVS is byte-for-byte unchanged. Short-press RST to boot.')
        c=plan['calibration']
        if c is not None:
            print(f"Local calibration command if required: head calibrate {c['yaw_zero']} {c['pitch_zero']}")
        else:
            print('No stored calibration. Head stays disabled; obtain reliable zeros for THIS unit before local calibration.')
        print('Calibration is NOT approval. Follow the local two-axis validation guide before head approve.')
        return 0
    except (ValueError,OSError,KeyError,TypeError) as error:
        print('Stopped: '+str(error),file=sys.stderr)
        return 1
    finally:
        if device is not None: device.close()

if __name__=='__main__':
    raise SystemExit(main())
