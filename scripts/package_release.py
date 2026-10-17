"""Package built firmware for the separate, backup-first device tool."""
from pathlib import Path
import hashlib, json, shutil
ROOT=Path(__file__).resolve().parents[1]
def main():
    framework=Path.home()/'.platformio/packages/framework-arduinoespressif32'
    boot_app=framework/'tools/partitions/boot_app0.bin'
    newest=max(p.stat().st_mtime_ns for p in (ROOT/'firmware/src').rglob('*') if p.is_file())
    newest=max(newest,(ROOT/'firmware/platformio.ini').stat().st_mtime_ns,(ROOT/'firmware/partitions_theme.csv').stat().st_mtime_ns)
    plans=[]
    for mode in ('vehicle','simulation'):
        build=ROOT/'firmware/.pio/build'/mode
        if not (build/'firmware.bin').exists() or (build/'firmware.bin').stat().st_mtime_ns<newest:
            raise SystemExit(f'Rebuild first: pio run -d firmware -e {mode}')
        files=[(build/'bootloader.bin',0x1000),(build/'partitions.bin',0x8000),(boot_app,0xe000),(build/'firmware.bin',0x10000)]
        if any(not p.exists() for p,_ in files):raise SystemExit('Missing built image or framework boot_app0.bin')
        plans.append((mode,files))
    for mode,files in plans:
        dest=ROOT/'device-tool/固件'/mode;dest.mkdir(parents=True,exist_ok=True);images=[]
        for source,offset in files:
            shutil.copy2(source,dest/source.name)
            images.append({'file':source.name,'offset':offset,'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
        manifest={'format':'obd-firmware-package','schema':1,'name':f'Open Platform v33 {mode}','chip':'esp32','flash_bytes':4194304,'images':images,'initialize_on_layout_change':['theme','spiffs']}
        (dest/'firmware-package.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print('Packaged vehicle + simulation. No serial port opened; no board written.')
if __name__=='__main__':main()
