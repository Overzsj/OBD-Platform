import copy,json,struct,tempfile,unittest,zlib,shutil
from pathlib import Path
from types import SimpleNamespace
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'app'))
import flash_service as f
BASE=Path(__file__).resolve().parent
PACKAGE=BASE.parent/'固件/simulation/firmware-package.json'
SOURCE=BASE.parent/'主题/open-instrument-1.0.0.sukitheme'
CAPS={'HARDWARE GET':{'driver':'ILI9488','width':480,'height':320},'THEME GET':{'engine':'suki-layout-v1','slots':['iat','coolant','maf','load','throttle']}}
class Fake:
 def __init__(self,blank=False,short=False,fail=False):
  self.calls=[];self.short=short;self.fail=fail;self.data=bytearray(b'\xff'*f.FLASH_SIZE)
  if not blank:
   for item in f.firmware_plan(PACKAGE)['images']:
    data=Path(item['path']).read_bytes();self.data[item['offset']:item['offset']+len(data)]=data
 def __call__(self,args,**kwargs):
  self.calls.append(args)
  if 'read_flash' in args:Path(args[-1]).write_bytes(self.data[:100] if self.short else self.data)
  def check():
   if self.fail and 'verify_flash' in args:raise RuntimeError('verification failed')
  return SimpleNamespace(stdout='Detected flash size: 4MB\n' if 'flash_id' in args else 'OK\n',stderr='',check_returncode=check)
class FlashTests(unittest.TestCase):
 def test_real_images_and_package(self):
  p=f.firmware_plan(PACKAGE);self.assertEqual(p['kind'],'package');self.assertEqual(len(p['images']),4)
  self.assertEqual(f.firmware_plan(PACKAGE.parent/'firmware.bin')['kind'],'application')
 def test_bad_image(self):
  data=bytearray((PACKAGE.parent/'firmware.bin').read_bytes());data[100]^=1
  with self.assertRaises(Exception):f.check_image(data)
  data=bytearray((PACKAGE.parent/'firmware.bin').read_bytes());data[12]=9
  with self.assertRaises(ValueError):f.check_image(data)
 def test_backup_before_app_write_and_only_app(self):
  fake=Fake()
  with tempfile.TemporaryDirectory() as folder:
   result=f.FlashSession('TEST',folder,fake).firmware(PACKAGE.parent/'firmware.bin');self.assertTrue(result['verified'])
  self.assertIn('flash_id',fake.calls[0]);self.assertIn('read_flash',fake.calls[1]);writes=next(c for c in fake.calls if 'write_flash' in c);self.assertEqual(writes[-2],'0x10000');self.assertNotIn('0x290000',writes)
 def test_theme_does_not_write_firmware(self):
  fake=Fake()
  with tempfile.TemporaryDirectory() as folder:f.FlashSession('TEST',folder,fake).theme(SOURCE,CAPS)
  write=next(c for c in fake.calls if 'write_flash' in c);self.assertEqual(write[-2],'0x290000');self.assertNotIn('0x10000',write)
 def test_blank_board_full_package_no_theme_required(self):
  fake=Fake(blank=True)
  with tempfile.TemporaryDirectory() as folder:f.FlashSession('TEST',folder,fake).firmware(PACKAGE)
  self.assertIn('flash_id',fake.calls[0]);self.assertIn('read_flash',fake.calls[1]);erases=[c for c in fake.calls if 'erase_region' in c];self.assertEqual(len(erases),2)
  writes=next(c for c in fake.calls if 'write_flash' in c);self.assertIn('0x10000',writes);self.assertNotIn('0x290000',writes)
 def test_same_layout_preserves_theme_and_logs(self):
  fake=Fake()
  with tempfile.TemporaryDirectory() as folder:f.FlashSession('TEST',folder,fake).firmware(PACKAGE)
  self.assertFalse(any('erase_region' in c for c in fake.calls))
 def test_failed_backup_blocks_all_writes(self):
  fake=Fake(short=True)
  with tempfile.TemporaryDirectory() as folder,self.assertRaises(ValueError):f.FlashSession('TEST',folder,fake).firmware(PACKAGE)
  self.assertEqual(len(fake.calls),2)
 def test_raw_app_blank_board_rejected(self):
  fake=Fake(blank=True)
  with tempfile.TemporaryDirectory() as folder,self.assertRaises(ValueError):f.FlashSession('TEST',folder,fake).firmware(PACKAGE.parent/'firmware.bin')
  self.assertEqual(len(fake.calls),2)
 def test_theme_blank_board_rejected(self):
  fake=Fake(blank=True)
  with tempfile.TemporaryDirectory() as folder,self.assertRaises(ValueError):f.FlashSession('TEST',folder,fake).theme(SOURCE,CAPS)
  self.assertEqual(len(fake.calls),2)
 def test_wrong_engine_no_device_operation(self):
  caps=copy.deepcopy(CAPS);caps['THEME GET']['engine']='unknown';fake=Fake()
  with tempfile.TemporaryDirectory() as folder,self.assertRaises(ValueError):f.FlashSession('TEST',folder,fake).theme(SOURCE,caps)
  self.assertEqual(fake.calls,[])
 def test_wrong_flash_capacity_blocks_backup_and_write(self):
  fake=Fake()
  def wrong_capacity(args,**kwargs):
   result=fake(args,**kwargs)
   if 'flash_id' in args:result.stdout='Detected flash size: 8MB\n'
   return result
  with tempfile.TemporaryDirectory() as folder,self.assertRaises(ValueError):f.FlashSession('TEST',folder,wrong_capacity).firmware(PACKAGE)
  self.assertEqual(len(fake.calls),1);self.assertIn('flash_id',fake.calls[0])
 def test_bad_resource_even_with_updated_crc(self):
  data=bytearray(f.theme_plan(SOURCE,CAPS)['data']);struct.pack_into('<I',data,32,1);struct.pack_into('<I',data,16,zlib.crc32(data[20:]))
  with tempfile.TemporaryDirectory() as folder:
   p=Path(folder)/'theme.bin';p.write_bytes(data)
   with self.assertRaises(ValueError):f.theme_plan(p,CAPS)
 def test_partitions_md5(self):
  data=bytearray((PACKAGE.parent/'partitions.bin').read_bytes());data[12]^=1
  with self.assertRaises(ValueError):f.partitions(data)
 def test_firmware_package_cannot_include_theme(self):
  with tempfile.TemporaryDirectory() as folder:
   target=Path(folder)/'package';shutil.copytree(PACKAGE.parent,target);p=target/'firmware-package.json';obj=json.loads(p.read_text(encoding='utf-8'))
   (target/'theme.bin').write_bytes(b'example');obj['images'].append({'file':'theme.bin','offset':0x290000,'sha256':f.digest(b'example')});p.write_text(json.dumps(obj))
   with self.assertRaises(ValueError):f.firmware_plan(p)
 def test_changed_file_during_backup_blocks_write(self):
  fake=Fake()
  with tempfile.TemporaryDirectory() as folder:
   p=Path(folder)/'app.bin';shutil.copy2(PACKAGE.parent/'firmware.bin',p)
   def mutate(args,**kwargs):
    result=fake(args,**kwargs)
    if 'read_flash' in args:
     data=bytearray(p.read_bytes());data[-1]^=1;p.write_bytes(data)
    return result
   with self.assertRaises(ValueError):f.FlashSession('TEST',Path(folder)/'backups',mutate).firmware(p)
   self.assertFalse(any('write_flash' in c or 'erase_region' in c for c in fake.calls))
 def test_verify_failure_not_reported_as_success(self):
  fake=Fake(fail=True)
  with tempfile.TemporaryDirectory() as folder:
   with self.assertRaises(RuntimeError):f.FlashSession('TEST',folder,fake).firmware(PACKAGE)
   self.assertFalse(list(Path(folder).rglob('result.json')));self.assertTrue(list(Path(folder).rglob('flash-4MB.bin')))
 def test_restore_backs_up_before_writing(self):
  fake=Fake()
  with tempfile.TemporaryDirectory() as folder:
   p=Path(folder)/'backup.bin';p.write_bytes(fake.data);f.FlashSession('TEST',Path(folder)/'new',fake).restore(p)
  self.assertIn('flash_id',fake.calls[0]);self.assertIn('read_flash',fake.calls[1]);writes=next(c for c in fake.calls if 'write_flash' in c);self.assertEqual(writes[-2],'0x0')
if __name__=='__main__':unittest.main()
