"""Theme engine adapters. Layout-specific rules do not belong to the UI/flasher."""
from pathlib import Path
import sys,struct,zlib,hashlib
BASE=Path(__file__).resolve().parent
def digest(data):return hashlib.sha256(data).hexdigest()
def encode_layout_v1(path,capabilities):
 path=Path(path);h=capabilities['HARDWARE GET'];t=capabilities['THEME GET']
 if t.get('engine')!='suki-layout-v1':raise ValueError('尚未安装该设备主题引擎的编码器，不能猜测主题格式')
 if (h.get('driver'),h.get('width'),h.get('height'))!=('ILI9488',480,320):raise ValueError('当前主题编码器与设备屏幕不匹配')
 if path.suffix.lower()=='.sukitheme':
  tools=BASE/'encoders/layout_v1'
  if str(tools) not in sys.path:sys.path.insert(0,str(tools))
  from theme_codec import load
  from runtime_theme import image
  data=image(load(path),t.get('slots'))
 elif path.suffix.lower()=='.bin':data=path.read_bytes()
 else:raise ValueError('请选择受支持的主题文件：.sukitheme 或独立主题 .bin')
 if len(data)<408 or data[:8]!=b'SUKITH29' or struct.unpack_from('<I',data,8)[0]!=1 or struct.unpack_from('<I',data,12)[0]!=len(data) or zlib.crc32(data[20:])!=struct.unpack_from('<I',data,16)[0]:raise ValueError('独立主题格式或校验错误；不能把普通固件当作主题烧录')
 count=struct.unpack_from('<I',data,20)[0]
 if count!=24:raise ValueError('主题资源表无效')
 end=408
 for i in range(count):
  ident,offset,size=struct.unpack_from('<III',data,24+i*12)
  if ident!=i+1 or offset%4 or offset<end or offset>len(data) or size>len(data)-offset:raise ValueError('主题资源越界或重叠')
  end=offset+size
 lengths=[struct.unpack_from('<I',data,24+i*12+8)[0] for i in range(24)]
 offsets=[struct.unpack_from('<I',data,24+i*12+4)[0] for i in range(24)]
 if lengths[:10]!=[307200,256,9800,9800,4900,64,119600,119600,20160,20160]:raise ValueError('主题素材尺寸不符合设备引擎')
 if lengths[10]%6 or not 120<=lengths[10]<=768:raise ValueError('主题调试字体无效')
 for f in range(4):
  if not 0<lengths[11+f]<=100000 or lengths[15+f]%20 or not 200<=lengths[15+f]<=3200:raise ValueError('主题字形表无效')
  previous=-1
  for pos in range(offsets[15+f],offsets[15+f]+lengths[15+f],20):
   code,index,left,top,width,height,advance=struct.unpack_from('<H2xIhhHHH',data,pos)
   if code<=previous or width>120 or height>120 or advance>120 or index>lengths[11+f] or width*height>lengths[11+f]-index:raise ValueError('主题字形越界')
   previous=code
 if lengths[19:]!=[12,164,5,80,1]:raise ValueError('主题元数据长度错误')
 if any(v>=11 for v in data[offsets[21]:offsets[21]+5]) or data[offsets[23]]>1:raise ValueError('主题数据绑定或动画设置无效')
 labels=data[offsets[20]:offsets[20]+164];identity=data[offsets[22]:offsets[22]+80]
 if any(0 not in labels[i*41:(i+1)*41] for i in range(4)) or 0 not in identity:raise ValueError('主题文字没有结束标记')
 if any(c and (c<32 or c>126 or c in [34,92]) for c in labels):raise ValueError('主题标签字符无效')
 if any(c and c not in b'abcdefghijklmnopqrstuvwxyz0123456789-@.' for c in identity):raise ValueError('主题标识字符无效')
 return {'kind':'theme','name':path.name,'engine':t['engine'],'width':h['width'],'height':h['height'],'size':len(data),'sha256':digest(data),'data':data,'note':'只写设备分区表中名为 theme 的数据区，不更新固件。'}

ADAPTERS={'suki-layout-v1':encode_layout_v1}
def encode(path,capabilities):
 engine=capabilities.get('THEME GET',{}).get('engine')
 if engine not in ADAPTERS:raise ValueError('尚未安装该设备主题引擎的编码器，不能猜测主题格式')
 return ADAPTERS[engine](path,capabilities)
