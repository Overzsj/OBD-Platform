"""Independent SUKI-layout theme image. No PID, scripts or vehicle commands."""
import argparse,base64,json,struct,zlib
from pathlib import Path
from theme_codec import load

SIGNALS=['iat','coolant','maf','load','throttle','rpm','speed','voltage','oil_temp','oil_pressure','afr']
DEFAULT=SIGNALS[:5]
HEADER=408
def slots(value):
    if not isinstance(value,list) or len(value)!=5 or any(x not in SIGNALS for x in value):
        raise ValueError('Five supported semantic signal names required')
    return bytes(SIGNALS.index(x) for x in value)
def image(theme,bindings=None):
    a=theme['resources'];c=a['character'];b=a['boot'];decode=base64.b64decode
    chunks=[decode(a['background']),decode(a['ledMask']),decode(c['faces'][0]),decode(c['faces'][1]),decode(c['mask']),bytes(x&255 for x in c['sine']),decode(b['portraits'][0]),decode(b['portraits'][1]),decode(b['logo']),decode(b['glow'])]
    debug=sorted({**a['font'],'Q':[62,65,81,33,94],'Z':[97,81,73,69,67]}.items(),key=lambda x:ord(x[0]));chunks.append(b''.join(bytes([ord(k)]+v) for k,v in debug))
    chunks.extend(decode(f['mask']) for f in a['bitmapFonts'])
    for f in a['bitmapFonts']:
        chunks.append(b''.join(struct.pack('<H2xIhhHHH2x',ord(k),*v) for k,v in sorted(f['glyphs'].items(),key=lambda x:ord(x[0]))))
    colors=[]
    for k in ['bg','white','yellow','muted','cyan','pink']:
        v=theme['palette'][k];r,g,bv=(int(v[i:i+2],16) for i in (1,3,5));colors.append(((r&248)<<8)|((g&252)<<3)|(bv>>3))
    chunks.append(struct.pack('<6H',*colors))
    chunks.append(b''.join(theme['boot']['labels'][k].encode().ljust(41,b'\0') for k in ['top','right','tagline','footer']))
    chunks.append(slots(DEFAULT if bindings is None else bindings))
    chunks.append((theme['id']+'@'+theme['version']).encode().ljust(80,b'\0'))
    chunks.append(bytes([theme['character']['mode']=='rpm']))
    data=bytearray(HEADER);entries=[]
    for i,chunk in enumerate(chunks,1):
        data.extend(b'\0'*((-len(data))%4));entries.append((i,len(data),len(chunk)));data.extend(chunk)
    struct.pack_into('<8sIIII',data,0,b'SUKITH29',1,len(data),0,len(entries))
    for i,e in enumerate(entries):struct.pack_into('<III',data,24+i*12,*e)
    struct.pack_into('<I',data,16,zlib.crc32(data[20:]))
    if len(data)>0xb0000:raise ValueError('Theme exceeds dedicated partition')
    return bytes(data)

def dynamic_header(header):
    """Keep built-in fallback, expose same symbols as mapped immutable pointers."""
    import re
    names=['kFont5x7','kUiLedMask']+['kUiFontMask'+str(i) for i in range(4)]+['kUiGlyph'+str(i) for i in range(4)]+['kBackground','kFaceCalm','kFaceEffort','kFaceMask','kCharacterSine','kBootCalm','kBootEffort','kBootLogo','kBootGlow']
    for name in names:
        pattern=r'static const (\w+) '+name+r'\[\]( PROGMEM)? = \{(.*?)\n\};'
        def replace(m):return 'static const '+m[1]+' builtin_'+name+'[]'+(m[2] or '')+' = {'+m[3]+'\n};\nstatic const '+m[1]+'* '+name+'=builtin_'+name+';'
        header,n=re.subn(pattern,replace,header,flags=re.S)
        if n!=1:raise ValueError('Missing resource '+name)
    header=header.replace('constexpr size_t kFont5x7Count=sizeof(kFont5x7)/sizeof(kFont5x7[0]);','static size_t kFont5x7Count=sizeof(builtin_kFont5x7)/sizeof(builtin_kFont5x7[0]);')
    header=header.replace('static const UiFontDef kUiFonts[]','static UiFontDef kUiFonts[]')
    for name in ['Bg','White','Yellow','Muted','Cyan','Pink']:
        header=header.replace('static constexpr uint16_t k'+name+'=','static uint16_t k'+name+'=')
    for name in ['Top','Right','Tagline','Footer']:
        header=header.replace('static const char kBoot'+name+'[]=','static const char* kBoot'+name+'=')
    header=header.replace('static constexpr bool kCharacterRpmLinked=','static bool kCharacterRpmLinked=')
    return header

def main():
    p=argparse.ArgumentParser();p.add_argument('theme',type=Path);p.add_argument('--bindings',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    bindings=json.loads(a.bindings.read_text(encoding='utf-8')) if a.bindings else DEFAULT
    data=image(load(a.theme),bindings);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(data)
    print(json.dumps({'bytes':len(data),'slots':bindings,'offset':'0x290000','firmware_recompile_required':False}))
if __name__=='__main__':main()
