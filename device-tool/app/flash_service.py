"""Firmware and theme writes are independent, bounded and backed up.
The UI is neutral; theme encoders are selected by device engine capability.
"""
from pathlib import Path
import hashlib,json,struct,subprocess,sys,zlib
from datetime import datetime
BASE=Path(__file__).resolve().parent
FLASH_SIZE=0x400000
def digest(data):return hashlib.sha256(data).hexdigest()
def partitions(data):
 result=[]
 for i in range(0,0xc00,32):
  if i+32>len(data):break
  magic,typ,sub,offset,size,label,flags=struct.unpack_from('<HBBII16sI',data,i)
  if magic==0xebeb:
   if data[i+16:i+32]!=hashlib.md5(data[:i]).digest():raise ValueError('分区表 MD5 校验失败')
   break
  if magic==0xffff:break
  if magic!=0x50aa:raise ValueError('分区表包含无法识别的记录')
  if not size or offset>FLASH_SIZE or size>FLASH_SIZE-offset:raise ValueError('分区表包含越界区域')
  result.append({'type':typ,'subtype':sub,'offset':offset,'size':size,'name':label.split(b'\0')[0].decode('ascii','strict')})
 if not result:raise ValueError('没有找到有效 ESP32 分区表，不能猜测写入位置')
 if len({p['name'] for p in result})!=len(result):raise ValueError('分区表包含重复名称')
 for i,p in enumerate(result):
  for q in result[i+1:]:
   if p['offset']<q['offset']+q['size'] and q['offset']<p['offset']+p['size']:raise ValueError('分区表存在重叠')
 return result
def app_partition(parts):
 apps=[p for p in parts if p['type']==0]
 if len(apps)!=1:raise ValueError('单文件更新仅支持单应用分区；多应用/OTA 固件请使用完整固件包')
 return apps[0]
def check_image(data):
 if len(data)<32 or len(data)>FLASH_SIZE or data[0]!=0xe9 or not 1<=data[1]<=16:raise ValueError('不是有效的 ESP32 固件镜像，或超过当前硬件容量')
 if struct.unpack_from('<H',data,12)[0]!=0:raise ValueError('当前工具硬件配置仅支持经典 ESP32，不接受 S3/C3 等镜像')
 # Parse the complete ESP image, including XOR checksum and optional SHA256.
 import site
 tool=Path.home()/'.platformio/packages/tool-esptoolpy'
 if str(tool) not in sys.path:sys.path.insert(0,str(tool))
 site.addsitedir(str(tool/'_contrib'))
 try:
  from esptool.bin_image import LoadFirmwareImage
  import tempfile
  with tempfile.NamedTemporaryFile(suffix='.bin',delete=False) as f:f.write(data);name=f.name
  try:
   image=LoadFirmwareImage('esp32',name)
   if image.checksum!=image.calculate_checksum():raise ValueError('固件校验和错误')
   if image.append_digest and not image.stored_digest==image.calc_digest:raise ValueError('固件 SHA256 错误')
  finally:Path(name).unlink(missing_ok=True)
 except ImportError:raise ValueError('缺少 esptool，请运行 python -m pip install esptool')

def firmware_plan(path):
 path=Path(path)
 if path.suffix.lower()=='.bin':
  data=path.read_bytes();check_image(data)
  return {'kind':'application','name':path.name,'chip':'esp32','flash_bytes':FLASH_SIZE,'images':[{'path':str(path.resolve()),'offset':None,'sha256':digest(data),'size':len(data)}],'note':'只更新已识别的单应用分区；不写主题，不修改分区。'}
 if path.is_dir():path=path/'firmware-package.json'
 obj=json.loads(path.read_text(encoding='utf-8'))
 if obj.get('format')!='obd-firmware-package' or type(obj.get('schema')) is not int or obj['schema']!=1 or obj.get('chip')!='esp32' or obj.get('flash_bytes')!=FLASH_SIZE:raise ValueError('固件包格式或芯片不匹配；当前支持经典 ESP32 4 MB')
 if not isinstance(obj.get('name'),str) or not 1<=len(obj['name'])<=80:raise ValueError('固件包名称无效')
 images=[];parent=path.parent.resolve()
 for item in obj.get('images',[]):
  file=(parent/item['file']).resolve()
  if not file.is_relative_to(parent):raise ValueError('固件包文件路径越界')
  data=file.read_bytes();offset=item['offset']
  if type(offset) is not int or offset<0 or offset%0x1000 or offset>=FLASH_SIZE or len(data)>FLASH_SIZE-offset or not data:raise ValueError('镜像地址或长度无效')
  if digest(data)!=item['sha256']:raise ValueError('固件包文件校验失败：'+file.name)
  if offset in [0x1000,0x10000]:check_image(data)
  images.append({'path':str(file),'offset':offset,'size':len(data),'sha256':digest(data)})
 if not images or len(images)>8:raise ValueError('固件包镜像数量无效')
 for i,p in enumerate(images):
  for q in images[i+1:]:
   if p['offset']<q['offset']+q['size'] and q['offset']<p['offset']+p['size']:raise ValueError('固件包镜像存在重叠')
 table=next((i for i in images if i['offset']==0x8000),None)
 if not table:raise ValueError('完整固件包缺少分区表；应用单文件请直接选择 .bin')
 parts=partitions(Path(table['path']).read_bytes());app=app_partition(parts)
 if any(i['offset'] not in {0x1000,0x8000,0xe000,app['offset']} for i in images):raise ValueError('固件包包含非固件写入区域；主题必须单独烧录')
 if not any(i['offset']==0x1000 for i in images):raise ValueError('完整固件包缺少启动程序')
 image=next((i for i in images if i['offset']==app['offset']),None)
 if not image or image['size']>app['size']:raise ValueError('应用缺失或超过目标分区')
 if app['offset']!=0x10000:check_image(Path(image['path']).read_bytes())
 erasable=[]
 for item in obj.get('initialize_on_layout_change',[]):
  if not isinstance(item,str):raise ValueError('初始化分区名称无效')
  part=next((p for p in parts if p['name']==item and p['type']==1),None)
  if not part or part['offset']%0x1000 or part['size']%0x1000:raise ValueError('初始化区域必须是完整数据分区')
  if any(part['offset']<i['offset']+i['size'] and i['offset']<part['offset']+part['size'] for i in images):raise ValueError('初始化区域与镜像重叠')
  erasable.append(part)
 return {'kind':'package','name':obj['name'],'chip':'esp32','flash_bytes':FLASH_SIZE,'images':images,'initialize_on_layout_change':erasable,'note':'写入包内声明的固件镜像；主题需单独选择烧录。分区改变时仅初始化包内声明的数据分区。'}

def theme_plan(path,capabilities):
 from theme_adapters import encode
 return encode(path,capabilities)

class FlashSession:
 def __init__(self,port,backup_root=None,runner=None,progress=None):
  self.port=port;self.root=Path(backup_root or BASE/'backups');self.runner=runner or subprocess.run;self.progress=progress or (lambda text:None);self.folder=None
 def run(self,*args):
  tool=Path.home()/'.platformio/packages/tool-esptoolpy/esptool.py'
  command=[sys.executable,str(tool)] if tool.is_file() else [sys.executable,'-m','esptool']
  result=self.runner(command+['--chip','esp32','--port',self.port,'--baud','460800',*map(str,args)],capture_output=True,text=True,timeout=300)
  with (self.folder/'operations.log').open('a',encoding='utf-8') as f:f.write(result.stdout+result.stderr)
  result.check_returncode()
  return result
 def backup(self):
  self.folder=self.root/datetime.now().strftime('%Y%m%d-%H%M%S-%f');self.folder.mkdir(parents=True)
  self.progress('正在核对芯片与 Flash 容量…');probe=self.run('flash_id')
  import re
  if not re.search(r'Detected flash size:\s*4MB\b',probe.stdout):raise ValueError('设备未确认是 4 MB Flash；停止烧录，避免产生不完整备份')
  self.progress('正在读取完整备份，请保持 USB 连接…');self.run('read_flash','0',hex(FLASH_SIZE),self.folder/'flash-4MB.bin')
  data=(self.folder/'flash-4MB.bin').read_bytes()
  if len(data)!=FLASH_SIZE:raise ValueError('完整备份失败，尚未写入设备')
  (self.folder/'backup.json').write_text(json.dumps({'port':self.port,'bytes':len(data),'sha256':digest(data)},indent=2))
  return data
 def write(self,images):
  args=[]
  for item in images:args.extend([hex(item['offset']),item['path']])
  self.progress('备份完成，正在写入…');self.run('write_flash',*args)
  self.progress('写入完成，正在校验…');self.run('verify_flash',*args)
 def freeze(self,images):
  for index,item in enumerate(images):
   data=Path(item['path']).read_bytes()
   if digest(data)!=item['sha256']:raise ValueError('所选文件在备份期间发生变化；尚未写入')
   target=self.folder/f'image-{index}.bin';target.write_bytes(data);item['path']=str(target)
 def firmware(self,path):
  plan=firmware_plan(path);before=self.backup();self.freeze(plan['images']);parts=partitions(before[0x8000:0x9000]) if plan['kind']=='application' else None
  if parts:
   app=app_partition(parts);item=plan['images'][0]
   if item['size']>app['size']:raise ValueError('应用镜像超过板上应用分区；尚未写入')
   item['offset']=app['offset']
  if plan['kind']=='package':
   table=next(i for i in plan['images'] if i['offset']==0x8000);new=Path(table['path']).read_bytes()
   if before[0x8000:0x8000+len(new)]!=new:
    for p in plan['initialize_on_layout_change']:
     self.progress('正在初始化新数据分区：'+p['name']);self.run('erase_region',hex(p['offset']),hex(p['size']))
  self.write(plan['images']);return self.finish(plan)
 def theme(self,path,capabilities):
  plan=theme_plan(path,capabilities);before=self.backup();parts=partitions(before[0x8000:0x9000]);part=next((p for p in parts if p['name']=='theme' and p['type']==1 and p['subtype']==0x40),None)
  if not part or len(plan['data'])>part['size']:raise ValueError('没有兼容的独立主题分区或空间不足；尚未写入')
  file=self.folder/'theme.bin';file.write_bytes(plan.pop('data'));self.write([{'path':str(file),'offset':part['offset']}]);return self.finish(plan)
 def restore(self,path):
  file=Path(path);data=file.read_bytes()
  if len(data)!=FLASH_SIZE:raise ValueError('恢复文件必须是完整 4 MB 备份')
  sidecar=file.parent/'backup.json'
  if sidecar.is_file() and json.loads(sidecar.read_text(encoding='utf-8')).get('sha256')!=digest(data):raise ValueError('备份 SHA256 不匹配，不能恢复')
  parts=partitions(data[0x8000:0x9000]);app=app_partition(parts)
  check_image(data[app['offset']:app['offset']+app['size']])
  self.backup();target=self.folder/'restore-image.bin';target.write_bytes(data);self.write([{'path':str(target),'offset':0}]);return self.finish({'kind':'restore','name':file.name})
 def finish(self,plan):
  result={'ok':True,'verified':True,'backup':str(self.folder),'kind':plan['kind'],'name':plan['name']}
  (self.folder/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');return result
