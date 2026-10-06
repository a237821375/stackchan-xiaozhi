import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import io
from contextlib import redirect_stdout, redirect_stderr
import zlib
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import stackchan_backup
from test_stackchan_backup import flash, servo
from unittest.mock import patch
from test_stackchan_release import image, bundle, STATES
try:
    import stackchan_install as installer
except ImportError:
    installer = None

PARTITIONS = [('nvs',1,2,0x9000,0x4000,0),('otadata',1,0,0xd000,0x2000,0),
              ('phy_init',1,1,0xf000,0x1000,0),('ota_0',0,16,0x20000,0x3f0000,0),
              ('ota_1',0,17,0x410000,0x3f0000,0),('assets',1,130,0x800000,0x800000,0)]

def device_flash():
    data = bytearray(flash(servo(), partitions=[]))
    data.extend(b'\xff' * (0x1000000-len(data)))
    table = b''.join(struct.pack('<HBBII16sI',0x50aa,t,s,o,n,name.encode(),f) for name,t,s,o,n,f in PARTITIONS)
    table += b'\xeb\xeb'+b'\xff'*14+hashlib.md5(table).digest()
    data[0x8000:0x8000+len(table)] = table
    return bytes(data)

class InstallTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(installer, 'Installer not implemented')
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.data = device_flash()
        self.meta = installer.backup_metadata(self.data, 'unit-a')
        self.manifest = dict(schema=1, board='m5stack-core-s3-stackchan', chip='esp32s3',
                             flash_size=0x1000000, version='0.1.0-alpha.2', files={})
        for name, offset in [('bootloader',0),('partition_table',0x8000),('otadata',0xd000),('app',0x20000),('assets',0x800000)]:
            content = {
                'partition_table':self.data[0x8000:0x8c00],
                'bootloader':image(app=False), 'app':image('0.1.0-alpha.2'),
                'otadata':b'\xff'*8192,
                'assets':bundle({**{state+'.gif':(b'GIF89a',320,240) for state in STATES},
                                 'index.json':(json.dumps({'hide_subtitle':True,'emoji_collection':[
                                     {'name':state,'file':state+'.gif'} for state in STATES]}).encode(),0,0)})
            }[name]
            path = self.root / (name+'.bin'); path.write_bytes(content)
            self.manifest['files'][name] = dict(path=path.name, offset=offset, size=len(content), sha256=hashlib.sha256(content).hexdigest())
        self.save()
    def save(self):
        (self.root/'manifest.json').write_text(json.dumps(self.manifest))
    def plan(self, **kw):
        return installer.installation_plan(self.root, self.data, self.meta, kw.get('identity','unit-a'), kw.get('mode','first-install'))
    def test_first_install_exact_regions_and_calibration(self):
        plan = self.plan()
        self.assertEqual([x['offset'] for x in plan['writes']], [0,0x8000,0xd000,0x20000,0x800000])
        self.assertEqual(plan['calibration']['yaw_zero'],460)
        self.assertFalse(plan['calibration']['approved'])
    def test_wrong_unit_corrupt_backup_missing_hash(self):
        with self.assertRaises(ValueError): self.plan(identity='unit-b')
        self.meta['sha256']='bad'
        with self.assertRaises(ValueError): self.plan()
    def test_rehashed_truncated_otadata_and_wrong_chip_rejected(self):
        for name,content in [('otadata',b'\xff'*4096),('app',image('0.1.0-alpha.2',chip=0))]:
            (self.root/(name+'.bin')).write_bytes(content)
            self.manifest['files'][name].update(size=len(content),sha256=hashlib.sha256(content).hexdigest())
            self.save()
            with self.assertRaises(ValueError): self.plan()
    def test_tampered_firmware(self):
        (self.root/'app.bin').write_bytes(b'bad')
        with self.assertRaises(ValueError): self.plan()
    def test_traversal_and_nvs_overwrite(self):
        for path,offset in [('../app.bin',0x20000),('app.bin',0x9000)]:
            self.manifest['files']['app'].update(path=path,offset=offset); self.save()
            with self.assertRaises(ValueError): self.plan()
    def test_erase_sector_boundary_and_image_overflow(self):
        self.manifest['files']['bootloader']['size']=0x9001; self.save()
        with self.assertRaises(ValueError): self.plan()
    def test_incompatible_nvs_or_partition_layout(self):
        self.manifest['files']['partition_table']['offset']=0x9000; self.save()
        with self.assertRaises(ValueError): self.plan()
    def test_upgrade_active_slot_crc_and_no_metadata_writes(self):
        data = bytearray(self.data)
        seq=2
        entry=struct.pack('<I20sII',seq,b'\xff'*20,2,zlib.crc32(struct.pack('<I',seq),0xffffffff)&0xffffffff)
        data[0xd000:0xd020]=entry
        self.data=bytes(data); self.meta=installer.backup_metadata(self.data,'unit-a')
        plan=self.plan(mode='upgrade')
        self.assertEqual([x['name'] for x in plan['writes']],['app','assets'])
        self.assertEqual(plan['writes'][0]['offset'],0x410000)
        data[0xd01c]^=1; self.data=bytes(data); self.meta=installer.backup_metadata(self.data,'unit-a')
        with self.assertRaises(ValueError): self.plan(mode='upgrade')
    def test_upgrade_erased_ota_defaults_slot_zero(self):
        self.assertEqual(self.plan(mode='upgrade')['writes'][0]['offset'],0x20000)
    def test_upgrade_pending_rollback_refused(self):
        data=bytearray(self.data); seq=1
        data[0xd000:0xd020]=struct.pack('<I20sII',seq,b'\xff'*20,1,zlib.crc32(struct.pack('<I',seq),0xffffffff)&0xffffffff)
        self.data=bytes(data); self.meta=installer.backup_metadata(self.data,'unit-a')
        with self.assertRaises(ValueError): self.plan(mode='upgrade')
    def test_manifest_board_and_extra_file_rejected(self):
        self.manifest['files']['nvs']=dict(self.manifest['files']['app']); self.save()
        with self.assertRaises(ValueError): self.plan()
    def test_missing_calibration_install_keeps_head_disabled(self):
        data=bytearray(self.data); data[0x9000:0xd000]=b'\xff'*0x4000
        self.data=bytes(data); self.meta=installer.backup_metadata(self.data,'unit-a')
        self.assertIsNone(self.meta['calibration'])
        self.assertIsNone(self.plan()['calibration'])
        directory=self.root/'missing-calibration'
        self.assertIsNone(installer.save_backup(directory,self.data,'unit-a')['calibration'])
    def test_unknown_calibration_does_not_discard_private_backup(self):
        data = bytearray(self.data)
        data[0x901c] ^= 1  # Corrupted NVS page CRC must still stop installation.
        directory = self.root/'private'
        with self.assertRaises(ValueError): installer.save_backup(directory,bytes(data),'unit-a')
        self.assertEqual((directory/'flash.bin').read_bytes(),bytes(data))
        self.assertEqual((directory/'flash.bin').stat().st_mode&0o777,0o600)
        self.assertTrue((directory/'backup.json').exists())
    def test_install_refuses_stale_device_before_writes(self):
        directory=self.root/'private'; directory.mkdir()
        (directory/'flash.bin').write_bytes(self.data)
        (directory/'backup.json').write_text(json.dumps(self.meta))
        class Fake:
            identity='unit-a'
            written=False
            closed=False
            def read(self,*args): return b'bad metadata'
            def write(self,*args): self.written=True
            def close(self): self.closed=True
        device=Fake()
        with patch.object(installer,'Device',return_value=device), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            result=installer.main(['install','--package',str(self.root),'--backup',str(directory),
                                  '--mode','first-install','--port','fake','--yes'])
        self.assertEqual(result,1); self.assertFalse(device.written); self.assertTrue(device.closed)
    def test_old_backup_with_unchanged_metadata_refused(self):
        directory=self.root/'private'; directory.mkdir()
        (directory/'flash.bin').write_bytes(self.data)
        (directory/'backup.json').write_text(json.dumps(self.meta))
        live=bytearray(self.data); live[0x20040]^=1; live=bytes(live)
        class Fake:
            identity='unit-a'
            written=False
            def read(self,offset,size): return live[offset:offset+size]
            def write(self,*args): self.written=True
            def close(self): pass
        device=Fake()
        with patch.object(installer,'Device',return_value=device), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            result=installer.main(['install','--package',str(self.root),'--backup',str(directory),
                                  '--mode','first-install','--port','fake','--yes'])
        self.assertEqual(result,1); self.assertFalse(device.written)
    def test_device_sets_flash_parameters_before_first_read(self):
        events=[]
        class Chip:
            CHIP_NAME='ESP32-S3'
            _port=SimpleNamespace(close=lambda:events.append('close'))
            def get_security_info(self): return {'parsed_flags':{'SECURE_BOOT_EN':False,'SECURE_DOWNLOAD_ENABLE':False},'flash_crypt_cnt':0}
            def read_mac(self): return bytes(range(6))
            def flash_set_parameters(self,size): events.append(('parameters',size))
            def change_baud(self,baud): events.append(('baud',baud))
            def read_flash(self,*args): events.append('read'); return b'test'
        chip=Chip()
        api=SimpleNamespace(__version__='5.4.0',detect_chip=lambda **kw:chip,
                            run_stub=lambda esp:esp,attach_flash=lambda esp:events.append('attach'),
                            read_flash=lambda esp,offset,size,**kwargs:(events.append(('public-read',offset,size,kwargs.get('flash_size'))),esp.read_flash(offset,size))[1])
        cmds=SimpleNamespace(detect_flash_size=lambda esp:'16MB')
        with patch.dict(sys.modules,{'esptool':api,'esptool.cmds':cmds}):
            dev=installer.Device('fake'); self.assertEqual(dev.read(0,4),b'test'); dev.close()
        self.assertIn(('public-read',0,4,'16MB'),events)
        self.assertIn('attach',events)
        self.assertLess(events.index('attach'),events.index(('parameters',0x1000000)))
        self.assertLess(events.index(('parameters',0x1000000)),events.index('read'))
    def test_verified_flash_reads_are_bounded_and_reassemble_exact_snapshot(self):
        calls=[]
        expected=b'a'*0x10000+b'b'*0x10000+b'last partial region'
        def read(esp,offset,size,**kwargs):
            # ESP32-S3 stub sends 4096-byte data frames. Keep each request to
            # one frame; multi-frame streaming failed on the connected K151.
            self.assertLessEqual(size,0x1000)
            self.assertEqual(kwargs['flash_size'],'16MB')
            calls.append((offset,size))
            return expected[offset-0x9000:offset-0x9000+size]
        device=installer.Device.__new__(installer.Device)
        device.api=SimpleNamespace(read_flash=read); device.esp=object()
        self.assertEqual(device.read(0x9000,len(expected)),expected)
        self.assertEqual(sum(size for _,size in calls),len(expected))
        self.assertEqual(calls[0][0],0x9000)
        self.assertEqual(calls[-1][0]+calls[-1][1],0x9000+len(expected))
        for offset,size in ((-1,4),(0,0),(0xffffff,2)):
            with self.assertRaises(ValueError):device.read(offset,size)
        device.api.read_flash=lambda *args,**kwargs:b'partial'
        with self.assertRaises(ValueError):device.read(0,0x20000)

    def test_short_backup_refused(self):
        with self.assertRaises(ValueError): installer.backup_metadata(self.data[:0x10000],'unit-a')

if __name__=='__main__': unittest.main()
