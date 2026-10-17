"""Generate original geometric theme resources using OFL Noto Sans SC."""
from pathlib import Path
import sys,base64,json,struct,math
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'firmware/tools'))
from theme_codec import save
from build_theme import apply_theme
FONT=ROOT/'assets/fonts/NotoSansSC.ttf'
def font(size):
 f=ImageFont.truetype(str(FONT),size);f.set_variation_by_axes([500]);return f
def b64(data):return base64.b64encode(data).decode()
def rgb565(im):return b64(b''.join(struct.pack('<H',((r&248)<<8)|((g&252)<<3)|(b>>3)) for r,g,b in im.convert('RGB').getdata()))
def mask(im):return b64(im.convert('L').tobytes())
def bitmap(size,chars,cap):
 f=font(size);blob=bytearray();glyphs={}
 for c in chars:
  box=f.getbbox(c);l,t,r,b=box;w=min(120,max(0,r-l));h=min(120,max(0,b-t));im=Image.new('L',(max(1,w),max(1,h)))
  ImageDraw.Draw(im).text((-l,-t),c,font=f,fill=255)
  advance=min(cap,max(1,round(f.getlength(c))))
  if w>advance:
   im=im.resize((advance,max(1,h)));w=advance
  glyphs[c]=[len(blob),0,t,w,h,advance];blob.extend(im.tobytes() if w*h else b'')
 return {'size':size,'glyphs':glyphs,'mask':b64(blob)}
bg=Image.new('RGB',(480,320),'#090f17');d=ImageDraw.Draw(bg)
# Existing renderer owns all changing numbers. Keep those areas clear.
for i in range(5):d.rounded_rectangle((7+i*95,225,94+i*95,315),radius=5,outline='#25384c',width=1)
d.line((10,215,470,215),fill='#39bccc',width=1)
d.text((18,188),'OPEN INSTRUMENT',font=font(12),fill='#7a94a9')
# A geometric gauge replaces all copyrighted character illustrations.
portraits=[]
for color in ['#39bccc','#f5a34c']:
 im=Image.new('RGB',(260,230),'#090f17');dr=ImageDraw.Draw(im)
 for radius in [70,88,100]:dr.arc((130-radius,115-radius,130+radius,115+radius),140,400,fill=color,width=3)
 dr.line((130,115,185,60),fill=color,width=5);dr.ellipse((124,109,136,121),fill='white');portraits.append(rgb565(im))
logo=Image.new('L',(240,84));ImageDraw.Draw(logo).text((20,20),'OPEN OBD',font=font(33),fill=255)
face=Image.new('RGB',(100,49),'#090f17');fd=ImageDraw.Draw(face);fd.line((8,38,32,15,50,33,77,8,94,8),fill='#39bccc',width=3)
face2=Image.new('RGB',(100,49),'#090f17');ImageDraw.Draw(face2).line((8,38,32,8,50,33,77,3,94,3),fill='#f5a34c',width=4)
# Firmware's small debug alphabet is generated from the same licensed font.
debug={}
for code in range(32,127):
 im=Image.new('L',(5,7));ImageDraw.Draw(im).text((0,-2),chr(code),font=font(7),fill=255)
 debug[chr(code)]=[sum((1<<y) for y in range(7) if im.getpixel((x,y))>90) for x in range(5)]
t={'format':'suki-gauge-theme','schemaVersion':1,'id':'open-instrument','name':'Open Instrument','version':'1.0.0','description':'Original geometric dashboard. Noto Sans SC font under OFL-1.1.',
 'target':{'width':480,'height':320,'driver':'ILI9488','orientation':'landscape','engine':'suki-gauge-v1'},
 'palette':{'bg':'#090f17','white':'#eaf3fa','yellow':'#f5a34c','muted':'#7a94a9','cyan':'#39bccc','pink':'#e28fbe'},
 'boot':{'engine':'suki-ignition-v1','durationMs':3800,'trigger':'power-on-reset','repeat':False,'labels':{'top':'OPEN OBD','right':'PLATFORM','tagline':'START YOUR DRIVE','footer':'COMMUNITY INSTRUMENT'}},
 'character':{'engine':'suki-steering-v1','mode':'rpm','durationMs':4800},'readings':['oilTempC','coolantC','oilPressureBar','afr','throttle','rpm'],
 'resources':{'width':480,'height':320,'font':debug,'background':rgb565(bg),'bitmapFonts':[bitmap(72,'0123456789-',38),bitmap(23,'0123456789.-',18),bitmap(16,'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !.-:/%',18),bitmap(12,'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !.-:/%°',14)],'ledMask':b64(bytes([255]*256)),
 'character':{'bounds':[256,64,480,218],'faceRect':[336,117,100,49],'faces':[rgb565(face),rgb565(face2)],'mask':b64(bytes([255]*4900)),'sine':[round(math.sin(i*math.pi/32)*3) for i in range(64)]},
 'boot':{'duration':3800,'portraitSize':[260,230],'logoSize':[240,84],'portraits':portraits,'logo':mask(logo),'glow':mask(logo)}},'integrity':{}}
path=ROOT/'firmware/themes/open-instrument-1.0.0.sukitheme';path.parent.mkdir(exist_ok=True);t=save(t,path);apply_theme(t,ROOT/'firmware')
preview=bg.copy();pd=ImageDraw.Draw(preview);pd.text((18,47),'2400',font=font(64),fill='#eaf3fa');pd.text((25,133),'RPM',font=font(13),fill='#7a94a9');pd.text((310,80),'OPEN',font=font(25),fill='#39bccc')
for i,(label,value) in enumerate(zip(['IAT','ECT','MAF','LOAD','TPS'],['32','89','12.4','24','18'])):
 pd.text((16+i*95,231),label,font=font(11),fill='#7a94a9');pd.text((16+i*95,250),value,font=font(22),fill='#eaf3fa');pd.rectangle((16+i*95,295,72+i*95,299),fill='#39bccc')
for i in range(12):pd.rounded_rectangle((14+i*38,18,36+i*38,25),radius=2,fill='#39bccc' if i<5 else '#25384c')
preview.save(ROOT/'theme-community/static/open-instrument.png');logo.save(ROOT/'theme-community/static/open-logo.png')
(ROOT/'device-tool/主题/open-instrument-1.0.0.sukitheme').write_bytes(path.read_bytes())
print('Generated open-instrument from original geometric drawings and OFL font')
