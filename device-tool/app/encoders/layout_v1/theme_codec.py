"""SUKI theme v1: data-only resources shared by browser and firmware builds."""
import base64
import copy
import hashlib
import json
import re
from pathlib import Path

FORMAT='suki-gauge-theme'
ENGINE='suki-gauge-v1'
MAX_FILE_BYTES=2*1024*1024
READINGS=['oilTempC','coolantC','oilPressureBar','afr','throttle','rpm']
PALETTE_KEYS=['bg','white','yellow','muted','cyan','pink']


def canonical(value):
    return json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode('utf-8')


def sign(theme):
    result=copy.deepcopy(theme)
    result.pop('integrity',None)
    result['integrity']={'algorithm':'sha256','payloadSha256':hashlib.sha256(canonical(result)).hexdigest()}
    return result


def require(condition,message):
    if not condition:raise ValueError(message)


def keys(value,expected,label):
    require(isinstance(value,dict) and set(value)==set(expected),label+' fields do not match theme v1')


def integer(value,low,high,label):
    require(type(value) is int and low<=value<=high,label+' is outside its supported range')


def blob(value,length,label):
    require(isinstance(value,str) and len(value)<=((length+2)//3)*4,label+' encoded length is invalid')
    try:data=base64.b64decode(value,validate=True)
    except (ValueError,base64.binascii.Error):raise ValueError(label+' is not base64')
    require(len(data)==length,label+' decoded length is invalid')
    require(base64.b64encode(data).decode()==value,label+' base64 is not canonical')
    return data


def validate(theme,verify=True):
    keys(theme,['format','schemaVersion','id','name','version','description','target','palette','boot','character','readings','resources','integrity'],'theme')
    require(theme['format']==FORMAT and type(theme['schemaVersion']) is int and theme['schemaVersion']==1,'Unsupported theme format/version')
    require(isinstance(theme['id'],str) and re.fullmatch(r'[a-z][a-z0-9-]{0,47}',theme['id']),'Invalid theme id')
    require(isinstance(theme['version'],str) and re.fullmatch(r'[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}',theme['version']),'Invalid theme version')
    for field,limit in [('name',80),('description',240)]:
        require(isinstance(theme[field],str) and 0<len(theme[field])<=limit and not any(ord(c)<32 for c in theme[field]),'Invalid '+field)
    require(theme['target']=={'width':480,'height':320,'driver':'ILI9488','orientation':'landscape','engine':ENGINE},'Theme target must be ILI9488 480x320 / '+ENGINE)
    keys(theme['palette'],PALETTE_KEYS,'palette')
    for key,value in theme['palette'].items():require(isinstance(value,str) and re.fullmatch(r'#[0-9a-fA-F]{6}',value),'Invalid palette '+key)
    keys(theme['boot'],['engine','durationMs','trigger','repeat','labels'],'boot')
    require(theme['boot']['engine']=='suki-ignition-v1' and type(theme['boot']['durationMs']) is int and theme['boot']['durationMs']==3800,'Boot v1 uses the supported 3800ms timeline')
    require(theme['boot']['trigger']=='power-on-reset' and theme['boot']['repeat'] is False,'Boot v1 plays once after power-on/reset')
    keys(theme['boot']['labels'],['top','right','tagline','footer'],'boot labels')
    for name,text in theme['boot']['labels'].items():
        require(isinstance(text,str) and 0<len(text)<=40 and re.fullmatch(r'[A-Z0-9 !.\-:/%]+',text),'Unsupported boot text: '+name)
    keys(theme['character'],['engine','mode','durationMs'],'character')
    require(theme['character']=={'engine':'suki-steering-v1','mode':theme['character']['mode'],'durationMs':4800} and theme['character']['mode'] in ['loop','rpm'],'Unsupported character profile')
    require(theme['readings']==READINGS,'Theme cannot change vehicle reading definitions')
    a=theme['resources']
    keys(a,['width','height','font','background','bitmapFonts','ledMask','character','boot'],'resources')
    require(a['width']==480 and a['height']==320,'Invalid resource dimensions')
    blob(a['background'],480*320*2,'background')
    blob(a['ledMask'],256,'ledMask')
    require(isinstance(a['font'],dict) and 20<=len(a['font'])<=128,'Invalid debug font')
    for ch,cols in a['font'].items():
        require(len(ch)==1 and ord(ch)<128 and isinstance(cols,list) and len(cols)==5,'Invalid debug glyph')
        for col in cols:integer(col,0,127,'debug glyph')
    require(isinstance(a['bitmapFonts'],list) and len(a['bitmapFonts'])==4,'Four bitmap fonts are required')
    for index,font in enumerate(a['bitmapFonts']):
        keys(font,['glyphs','mask','size'],'font')
        integer(font['size'],8,100,'font size')
        require(isinstance(font['mask'],str) and len(font['mask'])<=140000,'Font mask exceeds v1 limit')
        try:mask=base64.b64decode(font['mask'],validate=True)
        except (ValueError,base64.binascii.Error):raise ValueError('Invalid font mask')
        require(base64.b64encode(mask).decode()==font['mask'] and len(mask)<=100000,'Invalid font mask')
        require(isinstance(font['glyphs'],dict) and 10<=len(font['glyphs'])<=160,'Invalid font glyph count')
        for ch,g in font['glyphs'].items():
            require(len(ch)==1 and ord(ch)<=65535 and isinstance(g,list) and len(g)==6,'Invalid bitmap glyph')
            for n in g:require(type(n) is int,'Glyph coordinates must be integers')
            offset,left,top,w,h,advance=g
            require(0<=offset<=len(mask) and offset+w*h<=len(mask) and -128<=left<=128 and -128<=top<=128 and 0<=w<=120 and 0<=h<=120 and 0<=advance<=120,'Glyph bounds are invalid')
        needed='0123456789-' if index==0 else '0123456789.-' if index==1 else 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !.-:/%'
        if index==3:needed+='°'
        require(all(ch in font['glyphs'] for ch in needed),'Missing glyphs in font '+str(index))
        if index==0:require(max(font['glyphs'][ch][5] for ch in '0123456789')*4<=165,'RPM font exceeds the reading area')
        if index==1:
            for value in ['10.0','22.0','-40','100']:
                require(sum(font['glyphs'][ch][5] for ch in value)<=74,'Value font exceeds a reading card')
    for label,text in theme['boot']['labels'].items():
        f=a['bitmapFonts'][2 if label=='tagline' else 3]['glyphs']
        require(all(ch in f for ch in text) and sum(f[ch][5] for ch in text)<=420,'Boot label exceeds screen: '+label)
    c=a['character']
    keys(c,['faceRect','bounds','faces','mask','sine'],'character resources')
    require(c['faceRect']==[336,117,100,49] and c['bounds']==[256,64,480,218],'Character v1 geometry differs')
    require(isinstance(c['faces'],list) and len(c['faces'])==2,'Two face poses required')
    for i,face in enumerate(c['faces']):blob(face,9800,'face '+str(i))
    blob(c['mask'],4900,'face mask')
    require(isinstance(c['sine'],list) and len(c['sine'])==64,'Character sine table must have 64 samples')
    for n in c['sine']:integer(n,-127,127,'sine sample')
    b=a['boot']
    keys(b,['duration','portraitSize','logoSize','portraits','logo','glow'],'boot resources')
    require(b['duration']==3800 and b['portraitSize']==[260,230] and b['logoSize']==[240,84],'Boot v1 resource geometry differs')
    require(isinstance(b['portraits'],list) and len(b['portraits'])==2,'Two boot portrait poses required')
    for i,p in enumerate(b['portraits']):blob(p,119600,'portrait '+str(i))
    blob(b['logo'],20160,'logo');blob(b['glow'],20160,'glow')
    keys(theme['integrity'],['algorithm','payloadSha256'],'integrity')
    require(theme['integrity']['algorithm']=='sha256' and isinstance(theme['integrity']['payloadSha256'],str) and re.fullmatch(r'[0-9a-f]{64}',theme['integrity']['payloadSha256']),'Invalid integrity metadata')
    if verify:require(sign(theme)['integrity']==theme['integrity'],'Theme SHA-256 check failed')
    require(len(canonical(theme))<=MAX_FILE_BYTES,'Theme exceeds the 2MB import limit')
    return theme


def load(path):
    path=Path(path)
    require(path.stat().st_size<=MAX_FILE_BYTES,'Theme exceeds the 2MB import limit')
    def whole_number(value):
        n=float(value)
        require(n.is_integer(),'Theme v1 resource numbers must be integers')
        return int(n)
    return validate(json.loads(path.read_text(encoding='utf-8'),parse_float=whole_number))


def save(theme,path):
    theme=validate(sign(theme))
    Path(path).write_bytes(canonical(theme)+b'\n')
    return theme


def resource_bytes(theme):
    a=theme['resources'];c=a['character'];b=a['boot']
    return len(base64.b64decode(a['background']))+len(base64.b64decode(a['ledMask']))+sum(len(base64.b64decode(f['mask'])) for f in a['bitmapFonts'])+sum(len(base64.b64decode(p)) for p in c['faces'])+len(base64.b64decode(c['mask']))+len(c['sine'])+sum(len(base64.b64decode(p)) for p in b['portraits'])+len(base64.b64decode(b['logo']))+len(base64.b64decode(b['glow']))
