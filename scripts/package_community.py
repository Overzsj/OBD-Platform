"""Create an independently deployable community ZIP with its schema validator."""
from pathlib import Path
import zipfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    dest=ROOT/'release';dest.mkdir(exist_ok=True)
    files=[p for p in (ROOT/'theme-community').rglob('*') if p.is_file() and not any(x in p.parts for x in ('data','__pycache__','tests'))]
    files += [ROOT/'LICENSE',ROOT/'THIRD_PARTY_NOTICES.md',ROOT/'SECURITY.md',ROOT/'assets/fonts/OFL.txt']
    with zipfile.ZipFile(dest/'OBD-Themes-Standalone.zip','w',zipfile.ZIP_DEFLATED) as z:
        for p in files:z.write(p,p.relative_to(ROOT))
    print('Created release/OBD-Themes-Standalone.zip; launch theme-community/server.py after extraction.')
if __name__=='__main__':main()
