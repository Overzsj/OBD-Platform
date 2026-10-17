"""Check public-source boundary. Does not claim a complete license audit."""
from pathlib import Path
import ast,hashlib,json,re,sys
ROOT=Path(__file__).resolve().parents[1]
SKIP={'.git','.pio','__pycache__','artifacts','data','release','备份'}
def source_files():
    return [p for p in ROOT.rglob('*') if p.is_file() and not any(x in SKIP for x in p.relative_to(ROOT).parts)]
def main():
    errors=[];files=source_files()
    for p in files:
        rel=p.relative_to(ROOT).as_posix()
        if p.suffix=='.py':
            try:ast.parse(p.read_text(encoding='utf-8-sig'))
            except SyntaxError as e:errors.append(f'{rel}: {e}')
        if p.suffix in ('.py','.md','.json','.h','.cpp','.ini','.yml','.js','.html','.css','.cmd'):
            text=p.read_text(encoding='utf-8-sig')
            if re.search(r'[CD]:\\(?:Users\\Administrator|GPT\\)',text) or ('wxid'+'_' in text and p.name!='check_repository.py'):errors.append(rel+': local private path')
            if re.search(r'\b(?:94:08:53|10:52:1[Cc]):(?:[0-9A-Fa-f]{2}:){2}[0-9A-Fa-f]{2}\b',text):errors.append(rel+': private device address')
        if p.name in ('profiles7.json','profiles7.bson','backup.json','flash-backup.bin'):errors.append(rel+': private database/backup')
        if p.stat().st_size>95*1024*1024:errors.append(rel+': too large for normal GitHub source')
    for p in (ROOT/'configs').rglob('*.json'):json.loads(p.read_text(encoding='utf-8'))
    for p in [ROOT/'firmware/themes/open-instrument-1.0.0.sukitheme',ROOT/'device-tool/主题/open-instrument-1.0.0.sukitheme']:
        sys.path.insert(0,str(ROOT/'firmware/tools'));from theme_codec import load;load(p)
    if (ROOT/'firmware/themes/open-instrument-1.0.0.sukitheme').read_bytes()!=(ROOT/'device-tool/主题/open-instrument-1.0.0.sukitheme').read_bytes():errors.append('Theme copies differ')
    if errors:raise SystemExit('\n'.join(errors))
    print(f'PASS {len(files)} public files: Python syntax, JSON, theme integrity, private path/address/database boundary')
if __name__=='__main__':main()
