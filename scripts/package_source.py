"""Build public source archive from an explicit, checked boundary."""
from pathlib import Path
import hashlib,json,sys,zipfile
from check_repository import source_files,main as check
ROOT=Path(__file__).resolve().parents[1]
def main():
    check();dest=ROOT/'release';dest.mkdir(exist_ok=True)
    files=source_files();archive=dest/'OBD-Platform-GitHub.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(files):z.write(p,Path('OBD-Platform')/p.relative_to(ROOT))
    with zipfile.ZipFile(archive) as z:
        if z.testzip():raise RuntimeError('Archive CRC failed')
        if any(any(part in name.split('/') for part in ('.pio','data','备份','__pycache__')) for name in z.namelist()):raise RuntimeError('Runtime/private content in source archive')
    (dest/'SHA256SUMS.txt').write_text('\n'.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name for p in sorted(dest.glob('*.zip')))+'\n',encoding='utf-8')
    print(f'Created {archive.name}: {len(files)} files, {archive.stat().st_size} bytes. No remote upload.')
if __name__=='__main__':main()
