"""Compile one validated theme into C++ Flash resources and browser assets."""
from pathlib import Path
import argparse
import base64
import json
import os
import struct
from theme_codec import load,resource_bytes,canonical,validate

ROOT=Path(__file__).resolve().parents[1]


def generate(theme):
    a=theme['resources'];c=a['character'];b=a['boot']
    lines=['#pragma once','#include <Arduino.h>','namespace obd {',
        'struct Glyph5x7 { char ch; uint8_t col[5]; };',
        'static const Glyph5x7 kFont5x7[] = {']
    for ch,cols in sorted({**a['font'],'Q':[62,65,81,33,94],'Z':[97,81,73,69,67]}.items(),key=lambda item:ord(item[0])):
        lines.append('  {char('+str(ord(ch))+'),{'+','.join(map(str,cols))+'}},')
    lines+=['};','constexpr size_t kFont5x7Count=sizeof(kFont5x7)/sizeof(kFont5x7[0]);',
        'enum class UiFont : uint8_t { Hero, Value, Label, Small };',
        'struct UiGlyph { uint16_t code; uint32_t offset; int16_t left,top; uint16_t width,height,advance; };',
        'struct UiFontDef { const uint8_t* mask; const UiGlyph* glyphs; uint16_t count; };']
    def array(name,values,typ='uint8_t'):
        lines.append(f'static const {typ} {name}[] PROGMEM = {{')
        for i in range(0,len(values),24):lines.append('  '+','.join(str(v) for v in values[i:i+24])+',')
        lines.append('};')
    def words(s):
        data=base64.b64decode(s);return struct.unpack('<'+'H'*(len(data)//2),data)
    for i,f in enumerate(a['bitmapFonts']):
        array('kUiFontMask'+str(i),base64.b64decode(f['mask']))
        lines.append(f'static const UiGlyph kUiGlyph{i}[] = {{')
        for ch,g in sorted(f['glyphs'].items(),key=lambda item:ord(item[0])):
            lines.append('  {'+','.join(map(str,[ord(ch)]+g))+'},')
        lines.append('};')
    lines.append('static const UiFontDef kUiFonts[] = {')
    for i,f in enumerate(a['bitmapFonts']):lines.append(f'  {{kUiFontMask{i},kUiGlyph{i},{len(f["glyphs"])} }},')
    lines.append('};')
    array('kUiLedMask',base64.b64decode(a['ledMask']))
    lines.append('namespace suki_assets {')
    array('kBackground',words(a['background']),'uint16_t')
    lines.append('static constexpr int16_t kFaceX=336,kFaceY=117,kFaceWidth=100,kFaceHeight=49;')
    for name,data in zip(['Calm','Effort'],c['faces']):array('kFace'+name,words(data),'uint16_t')
    array('kFaceMask',base64.b64decode(c['mask']));array('kCharacterSine',c['sine'],'int8_t')
    lines.append('static constexpr uint16_t kBootPortraitW=260,kBootPortraitH=230,kBootLogoW=240,kBootLogoH=84;')
    for name,data in zip(['Calm','Effort'],b['portraits']):array('kBoot'+name,words(data),'uint16_t')
    array('kBootLogo',base64.b64decode(b['logo']));array('kBootGlow',base64.b64decode(b['glow']))
    lines.append('} // suki_assets\nnamespace theme {')
    for name,value in theme['palette'].items():
        r,g,blue=[int(value[i:i+2],16) for i in [1,3,5]]
        color=((r&248)<<8)|((g&252)<<3)|(blue>>3)
        lines.append(f'static constexpr uint16_t k{name.title()}=0x{color:04X};')
    for name,value in [('Id',theme['id']),('Version',theme['version'])]:
        lines.append(f'static const char k{name}[]='+json.dumps(value)+';')
    for name,value in theme['boot']['labels'].items():lines.append(f'static const char kBoot{name.title()}[]='+json.dumps(value)+';')
    lines.append('static constexpr uint16_t kBootDuration=3800;')
    lines.append('static constexpr bool kCharacterRpmLinked='+('true' if theme['character']['mode']=='rpm' else 'false')+';')
    lines.append('}} // obd::theme\n')
    from runtime_theme import dynamic_header
    header=dynamic_header('\n'.join(lines))
    # Imported metadata is data; escape '<' when embedding it in a script tag.
    js='const TFT_BUILTIN_THEME='+json.dumps(theme,ensure_ascii=False,separators=(',',':')).replace('<','\\u003c')+';\n'
    js+='const TFT_ASSETS={...TFT_BUILTIN_THEME.resources,themeConfig:{palette:TFT_BUILTIN_THEME.palette,boot:TFT_BUILTIN_THEME.boot,character:TFT_BUILTIN_THEME.character}};\n'
    return header,js


def write_changed(file,text):
    file.parent.mkdir(parents=True,exist_ok=True)
    data=text.encode('utf-8')
    if file.is_file() and file.read_bytes()==data:return
    temp=file.with_name(file.name+'.tmp');temp.write_bytes(data);os.replace(temp,file)


def apply_theme(theme,output_root=ROOT):
    # Website build jobs can use separate project copies instead of sharing a
    # mutable active_theme.h. Validation finishes before any output is written.
    theme=validate(theme);header,js=generate(theme);output_root=Path(output_root)
    stored=output_root/'themes'/f'{theme["id"]}-{theme["version"]}.sukitheme'
    if stored.is_file() and canonical(load(stored))!=canonical(theme):
        raise ValueError('Theme id/version already exists with different content; use a new version.')
    write_changed(stored,canonical(theme).decode('utf-8')+'\n')
    write_changed(output_root/'src/ui/themes/active_theme.h',header)
    write_changed(output_root/'web/assets.js',js)
    for name in ['suki_background.h','suki_boot_assets.h','suki_character.h','suki_fonts.h','font5x7.h']:
        write_changed(output_root/'src/ui'/name,'#pragma once\n#include "themes/active_theme.h"\n')
    write_changed(output_root/'themes/active.json',json.dumps({'id':theme['id'],'name':theme['name'],'version':theme['version'],'payloadSha256':theme['integrity']['payloadSha256']},ensure_ascii=False,indent=2)+'\n')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('theme',type=Path,nargs='?',default=ROOT/'themes/suki-ignition-1.0.0.sukitheme')
    parser.add_argument('--validate-only',action='store_true')
    parser.add_argument('--output-root',type=Path,default=ROOT,help='Resource output directory; use an isolated complete project copy for website build jobs.')
    args=parser.parse_args();theme=load(args.theme)
    if not args.validate_only:
        apply_theme(theme,args.output_root)
    print(json.dumps({'status':'VALID' if args.validate_only else 'GENERATED','theme':theme['id'],'version':theme['version'],'resource_bytes':resource_bytes(theme),'payloadSha256':theme['integrity']['payloadSha256'],'board_written':False},ensure_ascii=False,indent=2))


if __name__=='__main__':main()
